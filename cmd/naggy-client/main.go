// naggy-client sends commands to a running naggy-server and prints the
// responses.  Useful for testing: naggy-client "convert aka:kou"
package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"runtime"
	"strings"

	"github.com/JRF-2018/naggy_win/internal/server"
)

func dial(name string) (io.ReadWriteCloser, error) {
	if runtime.GOOS == "windows" {
		return os.OpenFile(name, os.O_RDWR, 0)
	}
	return net.Dial("unix", name)
}

func main() {
	pipe := flag.String("pipe", "", "pipe name (default: this session's)")
	flag.Parse()
	name := *pipe
	if name == "" {
		name = server.DefaultPipeName()
	}
	c, err := dial(name)
	if err != nil {
		fmt.Fprintln(os.Stderr, "connect:", err)
		os.Exit(1)
	}
	defer c.Close()
	r := bufio.NewReader(c)
	cmds := flag.Args()
	if len(cmds) == 0 {
		sc := bufio.NewScanner(os.Stdin)
		for sc.Scan() {
			cmds = append(cmds, sc.Text())
		}
	}
	for _, cmd := range cmds {
		fmt.Fprintf(c, "%s\n", cmd)
		inBlock := false
		for {
			l, err := r.ReadString('\n')
			fmt.Print(l)
			if err != nil {
				return
			}
			t := strings.TrimRight(l, "\r\n")
			if strings.HasPrefix(t, "# begin") {
				inBlock = true
			}
			if (t == "# end" && inBlock) || (strings.HasPrefix(t, "# error") && !inBlock) {
				break
			}
		}
	}
}
