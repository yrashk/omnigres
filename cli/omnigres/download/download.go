package download

import (
	"context"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/mholt/archiver/v4"
	"github.com/omnigres/omnigres/cli/style"
	"github.com/omnigres/omnigres/cli/tui"
	"github.com/omnigres/omnigres/cli/tui/message"
	"github.com/omnigres/omnigres/cli/tui/progress"
	"io"
	"mime"
	"net/http"
	"strings"
	"time"
)

type progressWriter struct {
	total      int
	downloaded int
	writer     io.Writer
	reader     io.ReadCloser
	reportChan chan float64
}

func (pw *progressWriter) start() (err error) {
	defer pw.reader.Close()
	_, err = io.Copy(pw.writer, io.TeeReader(pw.reader, pw))
	return
}

type progressErrMsg struct {
	err error
	message.Target
}

func (pw *progressWriter) Write(p []byte) (int, error) {
	pw.downloaded += len(p)
	if pw.total > 0 {
		pw.reportChan <- float64(pw.downloaded) / float64(pw.total)
	}
	return len(p), nil
}

func finalPause() tea.Cmd {
	return tea.Tick(time.Millisecond*750, func(_ time.Time) tea.Msg {
		return nil
	})
}

type Downloader struct {
	pw       *progressWriter
	progress progress.Model
	err      error
	what     string
	done     bool
	Filename string
	message.Target
}

func (m Downloader) IsDone() bool {
	return m.done && m.progress.Percent() >= 1 && !m.progress.IsAnimating()
}

type reportMsg struct {
	id     int
	report float64
	ok     bool
	message.Target
}

func (m Downloader) report() tea.Msg {
	report, ok := <-m.pw.reportChan
	return reportMsg{report: report, ok: ok, Target: m.Target}
}

type initMsg struct {
	message.Target
}

func (m Downloader) init() tea.Msg {
	err := m.pw.start()
	if err == nil {
		return nil
	} else {
		return progressErrMsg{Target: m.Target, err: err}
	}
}

func (m Downloader) Init() tea.Cmd {
	return tea.Batch(m.init, m.report)
}

const (
	padding  = 2
	maxWidth = 80
)

func (m Downloader) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {

	case tea.WindowSizeMsg:
		m.progress.Width = msg.Width - padding*2 - 4
		if m.progress.Width > maxWidth {
			m.progress.Width = maxWidth
		}
		return m, nil

	case progressErrMsg:

		if !m.IsMine(msg.Target) {
			break
		}

		m.done = true
		m.err = msg.err
		return m, nil

	case reportMsg:

		if !m.IsMine(msg.Target) {
			break
		}

		var cmds []tea.Cmd

		cmds = append(cmds, m.progress.SetPercent(float64(msg.report)), m.report)

		if msg.report >= 1.0 {
			m.done = true
			cmds = append(cmds, finalPause())
		}

		return m, tea.Sequence(cmds...)

	// FrameMsg is sent when the progress bar wants to animate itself
	case progress.FrameMsg:
		var cmds []tea.Cmd
		m.progress, cmds = tui.Update(m.progress, msg, []tea.Cmd{})
		return m, tea.Batch(cmds...)

	default:
	}

	return m, nil
}

func (m Downloader) View() string {
	if m.err != nil {
		return "Error downloading: " + m.err.Error() + "\n\n"
	}

	pad := strings.Repeat(" ", padding)
	return "Downloading " + m.what + " " + pad + m.progress.View()
}

type DownloadHandler func(filename string, reader io.ReadCloser) error

func Download(url string, what string, handle DownloadHandler) (Downloader, error) {
	resp, err := http.Get(url)
	if err != nil {
		return Downloader{}, err
	}

	_, params, err := mime.ParseMediaType(resp.Header.Get("Content-Disposition"))
	if err != nil {
		return Downloader{}, err
	}

	pipe, writer := io.Pipe()

	pw := &progressWriter{
		total:      int(resp.ContentLength),
		writer:     writer,
		reader:     resp.Body,
		reportChan: make(chan float64),
	}

	filename := params["filename"]
	err = handle(filename, pipe)
	if err != nil {
		return Downloader{}, err
	}

	return Downloader{
		pw:       pw,
		progress: progress.New(progress.WithGradient("#5A56E0", style.LogoColor)),
		what:     what,
		Filename: filename,
		Target:   message.NewTarget(),
	}, nil
}

func Extractor(ctx context.Context, handler archiver.FileHandler) (DownloadHandler, chan error) {
	ch := make(chan error)
	f := func(filename string, reader io.ReadCloser) error {
		go func() {
			defer reader.Close()

			format, input, err := archiver.Identify(filename, reader)
			if err != nil {
				ch <- err
			}

			if ex, ok := format.(archiver.Extractor); ok {
				err := ex.Extract(ctx, input, nil, handler)
				if err != nil {
					ch <- err
				}
			}
			close(ch)
		}()
		return nil
	}
	return f, ch
}
