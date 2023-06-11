package style

import "github.com/charmbracelet/lipgloss"

var Style = lipgloss.NewStyle()

func Question() lipgloss.Style {
	return Style.Border(lipgloss.RoundedBorder()).PaddingLeft(1).PaddingRight(1)
}

var (
	LogoColor = "#72B5F5"
)
