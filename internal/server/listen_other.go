//go:build !windows

package server

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
)

func hideWindow(cmd *exec.Cmd) {}

// DefaultPipeName returns the default socket path for this user.
func DefaultPipeName() string {
	return filepath.Join(os.TempDir(), fmt.Sprintf("naggy_win.%d.sock", os.Getuid()))
}

// Listen listens on a Unix domain socket.  ok is false when another
// server is already listening.
func Listen(name string) (l net.Listener, ok bool, err error) {
	if c, err := net.Dial("unix", name); err == nil {
		c.Close()
		return nil, false, nil
	}
	os.Remove(name)
	l, err = net.Listen("unix", name)
	return l, err == nil, err
}
