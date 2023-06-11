package cmd

import (
	"os"

	"github.com/spf13/cobra"
)

import (
	"github.com/omnigres/omnigres/cli/cmd/new"
)

// rootCmd represents the base command when called without any subcommands
var rootCmd = &cobra.Command{
	Use:   "omnigres",
	Short: "Omnigres CLI",
	Long:  `This command tool helps managing Omnigres-based application development and deployment workflows'`,
}

// Execute starts the tool
func Execute() {
	err := rootCmd.Execute()
	if err != nil {
		os.Exit(1)
	}
}

func init() {
	rootCmd.AddCommand(new.Command)
}
