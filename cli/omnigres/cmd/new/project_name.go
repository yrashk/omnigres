package new

import (
	"github.com/charmbracelet/bubbles/textinput"
	"github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
	"github.com/omnigres/omnigres/cli/style"
	"path"
)

type newProjectNameModel struct {
	textInput textinput.Model
	err       error
	done      bool
}

func (m newProjectNameModel) IsDone() bool {
	return m.done
}

func makeProjectNameModel(dir string) newProjectNameModel {
	input := textinput.New()
	input.Focus()
	input.Placeholder = path.Base(dir)
	return newProjectNameModel{
		textInput: input,
	}
}

func (m newProjectNameModel) Init() tea.Cmd {
	return textinput.Blink
}

func (m newProjectNameModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmd tea.Cmd

	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.Type {
		case tea.KeyTab:
			if m.textInput.Position() == 0 {
				m.textInput.SetValue(m.textInput.Placeholder)
			}
		case tea.KeyEnter:
			if m.textInput.Position() == 0 {
				m.textInput.SetValue(m.textInput.Placeholder)
			}
			m.textInput.Blur()
			m.done = true
			return m, nil
		}

	// We handle errors just like any other message
	case errMsg:
		m.err = msg
		return m, nil
	}

	m.textInput, cmd = m.textInput.Update(msg)
	return m, cmd
}

func (m newProjectNameModel) View() string {
	return lipgloss.JoinVertical(lipgloss.Left,
		style.Question().Render("What's the project name?"), m.textInput.View())
}
