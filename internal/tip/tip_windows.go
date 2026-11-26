//go:build windows

// Package tip enables/disables the naggy_win text service in the user's
// keyboard list (InstallLayoutOrTip of input.dll).
package tip

import (
	"fmt"
	"syscall"
	"unsafe"
)

// Profile is "<LANGID>:{CLSID}{PROFILE GUID}" of naggy_win.
const Profile = "0411:{851460B4-6E41-402E-B836-FDEFA05F527F}{DD579C2D-71EE-4761-8846-E84665E3728E}"

var procInstallLayoutOrTip = syscall.NewLazyDLL("input.dll").NewProc("InstallLayoutOrTip")

func call(flags uintptr) error {
	if err := procInstallLayoutOrTip.Find(); err != nil {
		return err
	}
	p, _ := syscall.UTF16PtrFromString(Profile)
	r, _, e := procInstallLayoutOrTip.Call(uintptr(unsafe.Pointer(p)), flags)
	if r == 0 {
		return fmt.Errorf("InstallLayoutOrTip: %v", e)
	}
	return nil
}

// Enable adds naggy_win to the current user's input methods.
func Enable() error { return call(0) }

// Disable removes naggy_win from the current user's input methods.
func Disable() error { return call(1) } // ILOT_UNINSTALL
