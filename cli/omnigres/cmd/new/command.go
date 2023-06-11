package new

import (
	"context"
	"fmt"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/kirsle/configdir"
	"github.com/mholt/archiver/v4"
	"github.com/omnigres/omnigres/cli/download"
	"github.com/omnigres/omnigres/cli/omnigres"
	"github.com/omnigres/omnigres/cli/project"
	"github.com/spf13/cobra"
	"gopkg.in/yaml.v3"
	"io"
	"log"
	"os"
	"path"
	"path/filepath"
	"strings"
)

type (
	errMsg error
)

func isSubpath(base, target string) (bool, error) {
	// get the relative path from base to target
	relative, err := filepath.Rel(base, target)
	if err != nil {
		return false, err
	}
	// check if the relative path starts with '..', which means target is not a subpath of base
	return !strings.HasPrefix(relative, ".."), nil
}

type info struct {
	pgDir       string
	downloadDir string
	revision    string
}

// Command represents the `new` command
var Command = &cobra.Command{
	Use:   "new <directory>",
	Short: "Creates and initializes a new Omnigres application",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {

		// Get current directory
		dir, err := os.Getwd()
		if err != nil {
			fmt.Println(err)
			return
		}
		// Get name
		var name = cmd.Flag("name").Value.String()
		if len(args) == 1 {
			// Get the specified directory
			arg_dir := args[0]
			// If path is not absolute, join it with the current directory
			if !path.IsAbs(arg_dir) {
				dir = path.Join(dir, args[0])
			} else {
				dir = arg_dir
			}
		}

		projectFile := path.Join(dir, "omnigres.yml")
		_, err = os.Stat(projectFile)
		if err == nil {
			fmt.Printf("Project file %s already exists, aborting.\n", projectFile)
			os.Exit(1)
		}

		config, err := omnigres.LoadConfig()
		if err != nil {
			fmt.Printf("Error loading config %s: %s", omnigres.ConfigFilename(), err.Error())
			os.Exit(1)
		}

		ctx, _ := context.WithCancel(cmd.Context())

		// Prepare Omnigres
		localCache := configdir.LocalCache("omnigres")
		omnigresPath := path.Join(localCache, "sources")

		info := info{
			pgDir: path.Join(localCache, "pg"),
		}

		extractorHandler := func(ctx context.Context, f archiver.File) error {
			targetPath := path.Join(omnigresPath, f.NameInArchive)
			if f.IsDir() {
				if len(info.downloadDir) == 0 {
					info.downloadDir = path.Join(omnigresPath, strings.Split(f.NameInArchive, string(os.PathSeparator))[0])
				}
			}
			_, err := os.Stat(targetPath)
			if !os.IsNotExist(err) {
				// file exists
				return nil
			}
			if is, _ := isSubpath(omnigresPath, targetPath); !is {
				return fmt.Errorf("%s is a dangerous path", f.NameInArchive)
			}

			if f.IsDir() {
				err := os.MkdirAll(targetPath, os.ModeDir|0o755)
				if err != nil {
					return err
				}
			} else {
				reader, err := f.Open()
				if err != nil {
					return err
				}
				defer reader.Close()

				out, err := os.Create(targetPath)
				if err != nil {
					return err
				}
				defer out.Close()

				_, err = io.Copy(out, reader)
				if err != nil {
					return err
				}
			}

			return nil
		}
		extractor, errors := download.Extractor(ctx, extractorHandler)
		downloader, err := download.Download("https://github.com/omnigres/omnigres/tarball/master", "Omnigres", extractor)

		if err != nil {
			println(err.Error())
		}

		info.revision = omnigres.InferRevisionFromTarballName(downloader.Filename)

		go func() {
			for msg := range errors {
				println(msg.Error())
			}
		}()

		screen := makeScreen(config, name, dir, &info, downloader)
		p := tea.NewProgram(screen)
		screenOut_, err := p.Run()
		if err != nil {
			log.Fatal(err)
		}

		screenOut := screenOut_.(newScreen)

		if screenOut.builder.IsDone() {
			config.Revisions = append(config.Revisions, info.revision)
			config.Save()
		}

		if screenOut.IsDone() {
			name = screenOut.projectName()
			_ = os.MkdirAll(dir, os.ModeDir|0o755)

			bytes, err := yaml.Marshal(&project.Config{Name: name, Omnigres: project.Omnigres{Revision: info.revision}})
			if err != nil {
				println(err.Error())
				os.Exit(1)
			}
			err = os.WriteFile(projectFile, bytes, 0o644)
			if err != nil {
				println(err.Error())
				os.Exit(1)
			}

			err = os.WriteFile(path.Join(dir, ".gitignore"), []byte(`.omnigres\n`), 0o644)
			if err != nil {
				println(err.Error())
				os.Exit(1)
			}

			fmt.Printf("Created project %s in %s", name, dir)
		}
	},
}

func init() {

	Command.Flags().StringP("name", "n", "", "Application name")
}
