package naggy

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// ResourcePath is @Resource::RESOURCE_PATH.
var ResourcePath []string

// HomeDir returns $ENV{HOME}, falling back to the user profile directory.
func HomeDir() string {
	if h := os.Getenv("HOME"); h != "" {
		return h
	}
	if h, err := os.UserHomeDir(); err == nil {
		return h
	}
	return ""
}

// ExeDir returns the directory of the running executable ($FindBin::Bin).
func ExeDir() string {
	p, err := os.Executable()
	if err != nil {
		return "."
	}
	if q, err := filepath.EvalSymlinks(p); err == nil {
		p = q
	}
	return filepath.Dir(p)
}

// DefaultResourcePath sets up the same search path as naggy-backend.pl,
// plus "$Bin/dic" for dictionaries.
func DefaultResourcePath() []string {
	bin := ExeDir()
	return []string{
		filepath.Join(HomeDir(), ".naggy-backend"),
		bin,
		filepath.Join(bin, "trl"),
		filepath.Join(bin, "nginit"),
		filepath.Join(bin, "dic"),
		"/usr/share/emacs/site-lisp/quail-naggy",
		"/usr/share/quail-naggy",
	}
}

func splitComponents(p string) []string {
	p = filepath.Clean(p)
	vol := filepath.VolumeName(p)
	rest := p[len(vol):]
	var r []string
	r = append(r, vol)
	for _, c := range strings.FieldsFunc(rest, func(c rune) bool { return c == '/' || c == filepath.Separator }) {
		r = append(r, c)
	}
	return r
}

func compEq(a, b string) bool {
	if runtime.GOOS == "windows" {
		return strings.EqualFold(a, b)
	}
	return a == b
}

func isSubpath(a, b string) bool {
	ad := splitComponents(a)
	bd := splitComponents(b)
	for i := range bd {
		if i >= len(ad) || !compEq(bd[i], ad[i]) {
			return false
		}
	}
	return true
}

func isLastPath(a, b string) bool {
	if !(len(b) >= len(a) && compEq(b[len(b)-len(a):], a)) {
		return false
	}
	ad := splitComponents(a)[1:]
	bd := splitComponents(b)[1:]
	for i := 0; i < len(ad); i++ {
		if i >= len(bd) || !compEq(ad[len(ad)-1-i], bd[len(bd)-1-i]) {
			return false
		}
	}
	return true
}

func exists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

// LocateFileResource is Resource::locate_file_resource.
func LocateFileResource(f string) (string, bool) {
	if f == "" {
		return "", false
	}
	abs := filepath.IsAbs(f)
	if !abs {
		for _, c := range splitComponents(f)[1:] {
			if c == ".." || c == "." {
				return "", false
			}
		}
	}
	for _, d := range ResourcePath {
		var p string
		if abs {
			if d == "" {
				p = f
			} else if ad, err := filepath.Abs(d); err == nil && isSubpath(f, ad) {
				p = f
			}
		} else {
			if isLastPath(f, d) {
				p, _ = filepath.Abs(d)
			} else if d != "" {
				p, _ = filepath.Abs(filepath.Join(d, f))
			}
		}
		if p != "" && exists(p) {
			return p, true
		}
	}
	return "", false
}
