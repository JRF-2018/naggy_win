// Package server shares one naggy backend among many clients (the IME
// instances of all applications in a desktop session) over a named pipe
// (Windows) or a Unix domain socket (elsewhere).
package server

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"

	"github.com/JRF-2018/naggy_win/internal/naggy"
)

// Handler processes one protocol line and writes the complete response.
type Handler interface {
	Handle(line string, w io.Writer) (closeConn bool)
	Reload() error
	Close()
}

// InProc runs the Go backend inside the server process.
type InProc struct {
	mu       sync.Mutex
	b        *naggy.Backend
	defines  []string
	initFile string
	exitReq  bool
}

// NewInProc creates and initializes an in-process backend.
func NewInProc(defines []string, initFile string) (*InProc, error) {
	h := &InProc{defines: defines, initFile: initFile}
	if err := h.Reload(); err != nil {
		return nil, err
	}
	return h, nil
}

// Reload re-creates the backend and re-reads the init files.
func (h *InProc) Reload() error {
	b := naggy.NewBackend(naggy.Options{Debug: true, PunctuationFullwidthPeriod: "0"})
	b.InitVar["ARGV"] = ""
	for _, kv := range h.defines {
		k, v := kv, ""
		if j := strings.IndexAny(kv, "= "); j >= 0 {
			k, v = kv[:j], kv[j+1:]
		}
		b.InitVar[k] = v
	}
	b.Out = io.Discard
	if !b.LoadDefaultInit(h.initFile) {
		return errors.New(naggy.UnescapeString(strings.TrimSpace(b.InitTail)))
	}
	h.mu.Lock()
	h.b = b
	h.mu.Unlock()
	return nil
}

// Handle implements Handler.
func (h *InProc) Handle(line string, w io.Writer) bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	exit := false
	h.b.Out = w
	h.b.Exit = func() { exit = true }
	h.b.ProcessLine(line)
	h.b.Out = io.Discard
	return exit
}

// Close implements Handler.
func (h *InProc) Close() {}

// Proxy forwards commands to an external stdio backend such as
// "perl naggy-backend.pl".
type Proxy struct {
	mu    sync.Mutex
	argv  []string
	cmd   *exec.Cmd
	stdin io.WriteCloser
	out   *bufio.Reader
}

// NewProxy starts the external backend.
func NewProxy(argv []string) (*Proxy, error) {
	p := &Proxy{argv: argv}
	if err := p.start(); err != nil {
		return nil, err
	}
	// Make sure it answers.
	var sb strings.Builder
	if err := p.roundTrip("nop", &sb); err != nil {
		p.Close()
		return nil, err
	}
	return p, nil
}

func (p *Proxy) start() error {
	cmd := exec.Command(p.argv[0], p.argv[1:]...)
	hideWindow(cmd)
	in, err := cmd.StdinPipe()
	if err != nil {
		return err
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return err
	}
	p.cmd, p.stdin, p.out = cmd, in, bufio.NewReaderSize(out, 65536)
	return nil
}

func (p *Proxy) stop() {
	if p.cmd != nil {
		p.stdin.Close()
		done := make(chan struct{})
		go func() { p.cmd.Wait(); close(done) }()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
			p.cmd.Process.Kill()
		}
		p.cmd = nil
	}
}

// roundTrip sends one line and copies the response up to "# end" or a
// top-level "# error" line.
func (p *Proxy) roundTrip(line string, w io.Writer) error {
	if p.cmd == nil {
		if err := p.start(); err != nil {
			return err
		}
	}
	if _, err := io.WriteString(p.stdin, strings.TrimRight(line, "\r\n")+"\n"); err != nil {
		p.stop()
		return err
	}
	inBlock := false
	for {
		l, err := p.out.ReadString('\n')
		if err != nil {
			p.stop()
			return fmt.Errorf("backend terminated: %v", err)
		}
		io.WriteString(w, l)
		t := strings.TrimRight(l, "\r\n")
		switch {
		case strings.HasPrefix(t, "# begin"):
			inBlock = true
		case t == "# end" && inBlock:
			return nil
		case strings.HasPrefix(t, "# error") && !inBlock:
			return nil
		}
	}
}

// Handle implements Handler.
func (p *Proxy) Handle(line string, w io.Writer) bool {
	p.mu.Lock()
	defer p.mu.Unlock()
	f := strings.Fields(line)
	if len(f) > 0 && (f[0] == "exit" || f[0] == "bye") {
		io.WriteString(w, "# begin unit\nbye\n# end\n")
		return true
	}
	if err := p.roundTrip(line, w); err != nil {
		io.WriteString(w, "# error "+naggy.EscapeString("naggy-server: "+err.Error())+"\n")
	}
	return false
}

// Reload restarts the external backend.
func (p *Proxy) Reload() error {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.stop()
	return p.start()
}

// Close implements Handler.
func (p *Proxy) Close() {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.stop()
}

// Server serves connections.
type Server struct {
	H    Handler
	Quit chan struct{}
	once sync.Once
}

// Serve handles one client connection until it is closed.
func (s *Server) Serve(conn io.ReadWriteCloser) {
	defer conn.Close()
	r := bufio.NewReaderSize(conn, 65536)
	for {
		line, err := r.ReadString('\n')
		if len(line) > 0 {
			var sb strings.Builder
			t := strings.TrimSpace(line)
			closeConn := false
			switch t {
			case "naggy-server-reload":
				if err := s.H.Reload(); err != nil {
					sb.WriteString("# error " + naggy.EscapeString("reload: "+err.Error()) + "\n")
				} else {
					sb.WriteString("# begin unit\nok\n# end\n")
				}
			case "naggy-server-quit":
				sb.WriteString("# begin unit\nbye\n# end\n")
				closeConn = true
				s.once.Do(func() { close(s.Quit) })
			case "naggy-server-ping":
				sb.WriteString("# begin string\nnaggy-server\n# end\n")
			default:
				closeConn = s.H.Handle(line, &sb)
			}
			if _, werr := io.WriteString(conn, sb.String()); werr != nil {
				return
			}
			if closeConn {
				return
			}
		}
		if err != nil {
			if err != io.EOF {
				log.Printf("read: %v", err)
			}
			return
		}
	}
}

// SplitCommandLine splits a command line, honoring double quotes.
func SplitCommandLine(s string) []string {
	var r []string
	var cur strings.Builder
	inQ, have := false, false
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch {
		case c == '"':
			inQ = !inQ
			have = true
		case c == '\\' && i+1 < len(s) && s[i+1] == '"':
			cur.WriteByte('"')
			i++
			have = true
		case (c == ' ' || c == '\t') && !inQ:
			if have {
				r = append(r, cur.String())
				cur.Reset()
				have = false
			}
		default:
			cur.WriteByte(c)
			have = true
		}
	}
	if have {
		r = append(r, cur.String())
	}
	return r
}
