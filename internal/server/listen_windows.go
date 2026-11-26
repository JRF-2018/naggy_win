//go:build windows

package server

import (
	"errors"
	"fmt"
	"net"
	"os"
	"os/exec"
	"sync"
	"syscall"
	"time"
	"unsafe"
)

var (
	kernel32                  = syscall.NewLazyDLL("kernel32.dll")
	advapi32                  = syscall.NewLazyDLL("advapi32.dll")
	procCreateNamedPipeW      = kernel32.NewProc("CreateNamedPipeW")
	procConnectNamedPipe      = kernel32.NewProc("ConnectNamedPipe")
	procDisconnectNamedPipe   = kernel32.NewProc("DisconnectNamedPipe")
	procProcessIdToSessionId  = kernel32.NewProc("ProcessIdToSessionId")
	procConvertStringSDToSD   = advapi32.NewProc("ConvertStringSecurityDescriptorToSecurityDescriptorW")
	errPipeConnected          = syscall.Errno(535) // ERROR_PIPE_CONNECTED
	errAccessDenied           = syscall.Errno(5)
	pipeAccessDuplex          = uint32(0x00000003)
	fileFlagFirstPipeInstance = uint32(0x00080000)
	pipeTypeByte              = uint32(0)
	pipeRejectRemoteClients   = uint32(0x00000008)
	pipeUnlimitedInstances    = uint32(255)
	createNoWindow            = uint32(0x08000000)
)

func hideWindow(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: createNoWindow}
}

func userSID() string {
	t, err := syscall.OpenCurrentProcessToken()
	if err != nil {
		return "unknown"
	}
	defer t.Close()
	u, err := t.GetTokenUser()
	if err != nil {
		return "unknown"
	}
	s, err := u.User.Sid.String()
	if err != nil {
		return "unknown"
	}
	return s
}

func sessionID() uint32 {
	var id uint32
	procProcessIdToSessionId.Call(uintptr(os.Getpid()), uintptr(unsafe.Pointer(&id)))
	return id
}

// DefaultPipeName returns \\.\pipe\naggy_win.<user SID>.<session id>.
// The IME computes the same name.
func DefaultPipeName() string {
	return fmt.Sprintf(`\\.\pipe\naggy_win.%s.%d`, userSID(), sessionID())
}

type pipeListener struct {
	name    string
	sa      *syscall.SecurityAttributes
	first   bool
	mu      sync.Mutex
	done    bool
	pending syscall.Handle
}

type pipeConn struct {
	h    syscall.Handle
	once sync.Once
}

func (c *pipeConn) Read(b []byte) (int, error) {
	var n uint32
	err := syscall.ReadFile(c.h, b, &n, nil)
	if err != nil {
		if err == syscall.ERROR_BROKEN_PIPE || err == syscall.Errno(233) { // ERROR_PIPE_NOT_CONNECTED
			return int(n), errEOF
		}
		return int(n), err
	}
	if n == 0 && len(b) > 0 {
		return 0, errEOF
	}
	return int(n), nil
}

func (c *pipeConn) Write(b []byte) (int, error) {
	var n uint32
	err := syscall.WriteFile(c.h, b, &n, nil)
	return int(n), err
}

func (c *pipeConn) Close() error {
	c.once.Do(func() {
		syscall.FlushFileBuffers(c.h)
		procDisconnectNamedPipe.Call(uintptr(c.h))
		syscall.CloseHandle(c.h)
	})
	return nil
}

func (c *pipeConn) LocalAddr() net.Addr                { return pipeAddr("") }
func (c *pipeConn) RemoteAddr() net.Addr               { return pipeAddr("") }
func (c *pipeConn) SetDeadline(t time.Time) error      { return nil }
func (c *pipeConn) SetReadDeadline(t time.Time) error  { return nil }
func (c *pipeConn) SetWriteDeadline(t time.Time) error { return nil }

type pipeAddr string

func (a pipeAddr) Network() string { return "pipe" }
func (a pipeAddr) String() string  { return string(a) }

var errEOF = errors.New("EOF")

func (l *pipeListener) create() (syscall.Handle, error) {
	name, _ := syscall.UTF16PtrFromString(l.name)
	mode := pipeAccessDuplex
	if l.first {
		mode |= fileFlagFirstPipeInstance
	}
	h, _, e := procCreateNamedPipeW.Call(uintptr(unsafe.Pointer(name)), uintptr(mode),
		uintptr(pipeTypeByte|pipeRejectRemoteClients), uintptr(pipeUnlimitedInstances),
		65536, 65536, 0, uintptr(unsafe.Pointer(l.sa)))
	if syscall.Handle(h) == syscall.InvalidHandle {
		return syscall.InvalidHandle, e
	}
	l.first = false
	return syscall.Handle(h), nil
}

func (l *pipeListener) Accept() (net.Conn, error) {
	for {
		l.mu.Lock()
		if l.done {
			l.mu.Unlock()
			return nil, errors.New("listener closed")
		}
		h := l.pending
		l.pending = 0
		if h == 0 {
			var err error
			h, err = l.create()
			if err != nil {
				l.mu.Unlock()
				return nil, err
			}
		}
		l.mu.Unlock()
		r, _, e := procConnectNamedPipe.Call(uintptr(h), 0)
		if r == 0 && e != errPipeConnected {
			syscall.CloseHandle(h)
			l.mu.Lock()
			done := l.done
			l.mu.Unlock()
			if done {
				return nil, errors.New("listener closed")
			}
			continue
		}
		return &pipeConn{h: h}, nil
	}
}

func (l *pipeListener) Close() error {
	l.mu.Lock()
	l.done = true
	l.mu.Unlock()
	// Wake up ConnectNamedPipe by connecting once.
	if f, err := os.OpenFile(l.name, os.O_RDWR, 0); err == nil {
		f.Close()
	}
	return nil
}

func (l *pipeListener) Addr() net.Addr { return pipeAddr(l.name) }

// Listen creates the named pipe.  ok is false when another server already
// owns the pipe name.
func Listen(name string) (net.Listener, bool, error) {
	// Owner and SYSTEM: full access.  AppContainer apps ("AC") may read
	// and write, and low integrity processes may connect.
	sddl := "D:P(A;;GA;;;" + userSID() + ")(A;;GA;;;SY)(A;;GRGW;;;AC)(A;;GRGW;;;RC)S:(ML;;NW;;;LW)"
	p, _ := syscall.UTF16PtrFromString(sddl)
	var sd uintptr
	r, _, e := procConvertStringSDToSD.Call(uintptr(unsafe.Pointer(p)), 1, uintptr(unsafe.Pointer(&sd)), 0)
	if r == 0 {
		return nil, false, e
	}
	sa := &syscall.SecurityAttributes{Length: uint32(unsafe.Sizeof(syscall.SecurityAttributes{})),
		SecurityDescriptor: sd}
	l := &pipeListener{name: name, sa: sa, first: true}
	// Probe: create the first instance now to detect a running server.
	h, err := l.create()
	if err != nil {
		if err == errAccessDenied {
			return nil, false, nil
		}
		return nil, false, err
	}
	l.pending = h
	return l, true, nil
}
