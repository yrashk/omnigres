package tui

import tea "github.com/charmbracelet/bubbletea"

func Update[M tea.Model](model M, msg tea.Msg, cmds []tea.Cmd) (M, []tea.Cmd) {
	new_model, cmd := model.Update(msg)
	return new_model.(M), append(cmds, cmd)
}
