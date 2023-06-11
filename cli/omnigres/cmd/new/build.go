package new

import (
	"bufio"
	"fmt"
	"github.com/charmbracelet/bubbles/spinner"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/omnigres/omnigres/cli/style"
	"io"
	"os"
	"os/exec"
	"path"
	"sync"
	"syscall"
	"time"
)

// Internal ID management. Used during animating to ensure that frame messages
// are received only by spinner components that sent them.
var (
	lastID int
	idMtx  sync.Mutex
)

// Return the next ID we should use on the Model.
func nextID() int {
	idMtx.Lock()
	defer idMtx.Unlock()
	lastID++
	return lastID
}

type omnigresBuild struct {
	spinner    spinner.Model
	id         int
	stdout     []string
	stdoutPipe io.ReadCloser
	stdoutChan chan string
	info       *info
	proc       *exec.Cmd
	configured bool
	built      bool
	errored    bool
	width      int
	tail       []string
}

func newOmnigresBuild(info *info) *omnigresBuild {
	return &omnigresBuild{
		spinner:    spinner.New(spinner.WithSpinner(spinner.Dot)),
		id:         nextID(),
		stdout:     make([]string, 10000),
		stdoutChan: make(chan string, 1000),
		info:       info,
		configured: false,
		built:      false,
		errored:    false,
		width:      80,
		tail:       []string{""},
	}
}

func (m *omnigresBuild) Init() tea.Cmd {
	return nil
}

type startMsg struct {
	id int
}

type lineMsg struct {
	id    int
	lines []string
	ok    bool
}

func (m omnigresBuild) line() tea.Msg {
	lines := make([]string, 0, 8)
	ok := true
	var msg string
loop:
	for {
		select {
		case msg, ok = <-m.stdoutChan:
			if !ok {
				// Done
				break loop
			}
			lines = append(lines, msg)
		default:
			// Nothing new
			if len(lines) > 0 {
				break loop
			}
		}
	}
	return lineMsg{id: m.id, lines: lines, ok: ok}
}

func (m *omnigresBuild) Start() tea.Msg {
	return startMsg{id: m.id}
}

func (m *omnigresBuild) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width - 4
		return m, nil
	case lineMsg:
		if msg.ok {
			m.stdout = append(m.stdout, msg.lines...)
			tailLen := 6
			if tailLen > len(m.stdout) {
				tailLen = len(m.stdout)
			}
			m.tail = m.stdout[len(m.stdout)-tailLen:]
		} else {
			m.proc.Wait()
			if m.proc.ProcessState.ExitCode() != 0 {
				m.errored = true
			} else {
				m.errored = false
			}
			if !m.errored {
				if !m.configured {
					m.configured = true
					buildDir := path.Join(m.info.downloadDir, "build")
					m.proc = exec.Command("cmake", "--build", buildDir, "--parallel")

					// Ensure the process would die with the cli
					m.proc.SysProcAttr = &syscall.SysProcAttr{
						Setpgid: true,
					}
					var err error
					m.stdoutPipe, err = m.proc.StdoutPipe()
					if err != nil {
						panic(err)
					}
					m.stdoutChan = make(chan string)
					if err := m.proc.Start(); err != nil {
						panic(err)
					}
					go m.readPipe()
				} else {
					m.built = true
					m.stdout = m.stdout[:0]
					return m, nil
				}
			}
		}
		return m, tea.Tick(time.Millisecond*10, func(time.Time) tea.Msg {
			return m.line()
		})
	case startMsg:
		buildDir := path.Join(m.info.downloadDir, "build")
		os.MkdirAll(buildDir, os.ModeDir|0o755)
		m.proc = exec.Command("cmake", m.info.downloadDir, "-B", buildDir, "-DCMAKE_BUILD_TYPE=Release", fmt.Sprintf("-DPGDIR=%s", m.info.pgDir))
		// Ensure the process would die with the cli
		m.proc.SysProcAttr = &syscall.SysProcAttr{
			Setpgid: true,
		}

		var err error
		m.stdoutPipe, err = m.proc.StdoutPipe()
		if err != nil {
			panic(err)
		}
		//stderr, err := proc.StderrPipe()
		//if err != nil {
		//	panic(err)
		//}
		if err := m.proc.Start(); err != nil {
			panic(err)
		}
		go m.readPipe()

		var cmd tea.Cmd
		m.spinner, cmd = m.spinner.Update(m.spinner.Tick())
		return m, tea.Batch(cmd, m.line)
	default:
	}
	var cmd tea.Cmd
	m.spinner, cmd = m.spinner.Update(msg)
	return m, cmd
}

func (m omnigresBuild) readPipe() {
	stdoutScanner := bufio.NewScanner(m.stdoutPipe)
	for stdoutScanner.Scan() {
		// Send the line over the channel
		line := stdoutScanner.Text()
		m.stdoutChan <- line
	}
	close(m.stdoutChan)
}

var checkMark = lipgloss.NewStyle().Foreground(lipgloss.Color("42")).SetString("✓")

func (m *omnigresBuild) View() string {
	if m.errored {
		return style.Style.Foreground(lipgloss.Color("#ff0000")).Render(lipgloss.JoinVertical(lipgloss.Left, m.stdout...))
	} else if !m.built {
		info := style.Style.Foreground(lipgloss.Color(style.LogoColor)).Render(fmt.Sprintf(" %s Building Omnigres, may take a while", m.spinner.View()))
		m.tail[0] = info
		return style.Style.Foreground(lipgloss.Color("#818589")).MaxWidth(m.width).Render(lipgloss.JoinVertical(lipgloss.Left, m.tail...))
	} else {
		return fmt.Sprintf(" %s %s", checkMark, "Built Omnigres")
	}
}

func (m omnigresBuild) IsDone() bool {
	return m.built
}
