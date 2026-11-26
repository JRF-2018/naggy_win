//go:build !windows

package tip

import "errors"

// Enable is only available on Windows.
func Enable() error { return errors.New("only on Windows") }

// Disable is only available on Windows.
func Disable() error { return errors.New("only on Windows") }
