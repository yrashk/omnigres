package tui

import tea "github.com/charmbracelet/bubbletea"

type CompletableModel interface {
	tea.Model
	IsDone() bool
}
