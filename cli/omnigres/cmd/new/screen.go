package new

import (
	"github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/omnigres/omnigres/cli/download"
	"github.com/omnigres/omnigres/cli/omnigres"
	"github.com/omnigres/omnigres/cli/tui"
)

type buildPhase int

const (
	buildNotStarted buildPhase = iota
	buildStarted
)

type newScreen struct {
	omnigresDownload download.Downloader
	buildPhase
	builder             *omnigresBuild
	name                string
	newProjectNameModel newProjectNameModel
	revisionReady       bool
}

func makeScreen(config omnigres.Config, name string, dir string, info *info, omnigresDownload download.Downloader) newScreen {
	newProjectName := newProjectNameModel{}
	if len(name) == 0 {
		newProjectName = makeProjectNameModel(dir)
	}
	revisionReady := false
	for _, rev := range config.Revisions {
		if rev == info.revision {
			revisionReady = true
		}
	}
	return newScreen{
		name:                name,
		buildPhase:          buildNotStarted,
		builder:             newOmnigresBuild(info),
		omnigresDownload:    omnigresDownload,
		newProjectNameModel: newProjectName,
		revisionReady:       revisionReady,
	}
}

func (m newScreen) projectName() string {
	if len(m.name) == 0 {
		return m.newProjectNameModel.textInput.Value()
	} else {
		return m.name
	}
}

func (m newScreen) Init() tea.Cmd {
	if !m.revisionReady {
		return tea.Batch(m.newProjectNameModel.Init(), m.omnigresDownload.Init())
	} else {
		return tea.Batch(m.newProjectNameModel.Init())
	}
}

func (m newScreen) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	cmds := make([]tea.Cmd, 0)

	switch msg := msg.(type) {
	case tea.KeyMsg:
		if msg.Type == tea.KeyCtrlC {
			return m, tea.Quit
		}
	}

	m.newProjectNameModel, cmds = tui.Update(m.newProjectNameModel, msg, cmds)
	m.omnigresDownload, cmds = tui.Update(m.omnigresDownload, msg, cmds)

	if m.buildPhase == buildNotStarted && m.omnigresDownload.IsDone() {
		m.buildPhase = buildStarted
		cmds = append(cmds, m.builder.Start)
	}

	m.builder, cmds = tui.Update(m.builder, msg, cmds)

	if m.IsDone() {
		cmds = append(cmds, tea.Quit)
	}

	return m, tea.Sequence(cmds...)
}

func (m newScreen) View() string {
	components := make([]string, 0)
	if !m.revisionReady {
		if m.buildPhase == buildStarted {
			components = append(components, m.builder.View())
		}
		if !m.omnigresDownload.IsDone() {
			components = append(components, m.omnigresDownload.View())
		}
	}

	if !m.newProjectNameModel.IsDone() {
		components = append(components, m.newProjectNameModel.View())
	}
	return lipgloss.JoinVertical(lipgloss.Left, components...)
}

func (m *newScreen) IsDone() bool {
	return m.newProjectNameModel.IsDone() && ((m.builder.IsDone() && m.omnigresDownload.IsDone()) || m.revisionReady)
}
