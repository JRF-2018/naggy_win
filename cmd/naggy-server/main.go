// naggy-server shares one naggy backend among all IME instances of a
// desktop session through a named pipe (\\.\pipe\naggy_win.<SID>.<session>).
//
// By default it runs the built-in Go backend.  With "backend = ..." in
// naggy-server.ini (or --backend-command) it proxies to any stdio backend,
// e.g. the original Perl naggy-backend.pl.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"

	"github.com/JRF-2018/naggy_win/internal/naggy"
	"github.com/JRF-2018/naggy_win/internal/server"
)

type multiFlag []string

func (m *multiFlag) String() string     { return strings.Join(*m, ",") }
func (m *multiFlag) Set(v string) error { *m = append(*m, v); return nil }

type config struct {
	backend  string
	defines  []string
	includes []string
	initFile string
}

// readConfig reads "key = value" lines.
func readConfig(path string, c *config) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		l := strings.TrimSpace(strings.TrimPrefix(sc.Text(), "\ufeff"))
		if l == "" || l[0] == '#' || l[0] == ';' {
			continue
		}
		i := strings.IndexByte(l, '=')
		if i < 0 {
			continue
		}
		k := strings.ToLower(strings.TrimSpace(l[:i]))
		v := strings.TrimSpace(l[i+1:])
		switch k {
		case "backend":
			c.backend = v
		case "define":
			c.defines = append(c.defines, v)
		case "include":
			c.includes = append(c.includes, v)
		case "init-file":
			c.initFile = v
		}
	}
	return sc.Err()
}

func configDir() string {
	if d := os.Getenv("APPDATA"); d != "" {
		return filepath.Join(d, "naggy_win")
	}
	if d, err := os.UserConfigDir(); err == nil {
		return filepath.Join(d, "naggy_win")
	}
	return ""
}

func logDir() string {
	if d := os.Getenv("LOCALAPPDATA"); d != "" {
		return filepath.Join(d, "naggy_win")
	}
	return os.TempDir()
}

func main() {
	var (
		pipe      = flag.String("pipe", "", "pipe (or socket) name")
		cfgFile   = flag.String("config", "", "configuration file")
		backend   = flag.String("backend-command", "", "external backend command line (default: built-in)")
		initFile  = flag.String("init-file", "", "init file instead of site-init.nginit")
		printName = flag.Bool("print-pipe-name", false, "print the pipe name and exit")
		logStderr = flag.Bool("log-stderr", false, "log to stderr instead of a file")
		defines   multiFlag
		includes  multiFlag
	)
	flag.Var(&defines, "D", "define VAR=VALUE (repeatable)")
	flag.Var(&includes, "I", "prepend a resource directory (repeatable)")
	flag.Parse()

	name := *pipe
	if name == "" {
		name = server.DefaultPipeName()
	}
	if *printName {
		fmt.Println(name)
		return
	}

	if !*logStderr {
		os.MkdirAll(logDir(), 0755)
		lp := filepath.Join(logDir(), "naggy-server.log")
		if st, err := os.Stat(lp); err == nil && st.Size() > 1<<20 {
			os.Rename(lp, lp+".old")
		}
		if f, err := os.OpenFile(lp, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644); err == nil {
			log.SetOutput(f)
			os.Stderr = f
		}
	}

	c := config{defines: []string{"FRONT_END=naggy_win"}}
	paths := []string{filepath.Join(naggy.ExeDir(), "naggy-server.ini")}
	if d := configDir(); d != "" {
		paths = append(paths, filepath.Join(d, "naggy-server.ini"))
	}
	if *cfgFile != "" {
		paths = []string{*cfgFile}
	}
	for _, p := range paths {
		if err := readConfig(p, &c); err == nil {
			log.Printf("read config %s", p)
		}
	}
	if *backend != "" {
		c.backend = *backend
	}
	if *initFile != "" {
		c.initFile = *initFile
	}
	c.defines = append(c.defines, defines...)
	c.includes = append(c.includes, includes...)

	l, ok, err := server.Listen(name)
	if err != nil {
		log.Fatalf("listen %s: %v", name, err)
	}
	if !ok {
		log.Printf("another naggy-server is running on %s", name)
		return
	}

	var h server.Handler
	if c.backend == "" || strings.EqualFold(c.backend, "builtin") {
		naggy.ResourcePath = naggy.DefaultResourcePath()
		if d := configDir(); d != "" {
			naggy.ResourcePath = append([]string{d}, naggy.ResourcePath...)
		}
		for j := len(c.includes) - 1; j >= 0; j-- {
			naggy.ResourcePath = append([]string{c.includes[j]}, naggy.ResourcePath...)
		}
		ip, err := server.NewInProc(c.defines, c.initFile)
		if err != nil {
			log.Fatalf("backend init: %v", err)
		}
		h = ip
		log.Printf("built-in backend %s ready on %s", naggy.Version, name)
	} else {
		argv := server.SplitCommandLine(c.backend)
		for _, d := range c.defines {
			argv = append(argv, "-D", d)
		}
		for _, d := range c.includes {
			argv = append(argv, "-I", d)
		}
		if c.initFile != "" {
			argv = append(argv, "--input-file", c.initFile)
		}
		p, err := server.NewProxy(argv)
		if err != nil {
			log.Fatalf("start backend %q: %v", argv, err)
		}
		h = p
		log.Printf("proxy to %q ready on %s", argv, name)
	}
	defer h.Close()

	s := &server.Server{H: h, Quit: make(chan struct{})}
	go func() {
		<-s.Quit
		l.Close()
	}()
	for {
		conn, err := l.Accept()
		if err != nil {
			select {
			case <-s.Quit:
				log.Printf("quit")
				return
			default:
			}
			log.Printf("accept: %v", err)
			return
		}
		go s.Serve(conn)
	}
}

var _ = io.EOF
