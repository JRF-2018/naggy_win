// naggy-backend: a Go port of naggy-backend.pl, the backend of
// quail-naggy.el and naggy_win.  It is a drop-in replacement: it speaks the
// same line protocol on stdin/stdout, reads the same init files
// (site-init.nginit, default-init.nginit, ...) and dictionaries, and does
// not need Perl or index files (*.sdb.*).
package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"

	"github.com/JRF-2018/naggy_win/internal/naggy"
)

const usage = `Usage: naggy-backend [options] [ARGS...]

The backend program for the input method "Naggy" (Go port of
naggy-backend.pl).  It reads commands from the standard input.

Options:
  -D VAR=VALUE, --define VAR=VALUE   set a variable for initialization
  -I DIR                             prepend DIR to the resource path
  --input-file INIT_FILE             use INIT_FILE instead of site-init.nginit
  --init-file INIT_FILE              same as --input-file
  --translit TABLE_NAME              batch mode: transliterate ARGS[0]
                                     (or stdin) by TABLE_NAME
  --unsafe-init                      enable unsafe init commands
  --punctuation-fullwidth-period     use fullwidth period/comma
  --console-encoding ENC             (accepted for compatibility)
  --filename-encoding ENC            (accepted for compatibility)
  --jisx0213                         (accepted for compatibility)
  --version                          show version
  -h, --help                         show this help
`

func main() {
	args := os.Args[1:]
	var defines []string
	var includes []string
	initFile := ""
	translit := ""
	batch := false
	unsafe := false
	fwperiod := false

	needArg := func(i *int, name string) string {
		if *i+1 >= len(args) {
			fmt.Fprintf(os.Stderr, "Option %s requires an argument\n%s", name, usage)
			os.Exit(2)
		}
		*i++
		return args[*i]
	}
	var rest []string
	i := 0
	for ; i < len(args); i++ {
		a := args[i]
		if a == "--" {
			i++
			break
		}
		if !strings.HasPrefix(a, "-") || a == "-" {
			break
		}
		name, val, hasVal := a, "", false
		if strings.HasPrefix(a, "--") {
			if j := strings.IndexByte(a, '='); j >= 0 {
				name, val, hasVal = a[:j], a[j+1:], true
			}
		} else if len(a) > 2 && (a[1] == 'D' || a[1] == 'I') {
			name, val, hasVal = a[:2], a[2:], true
		}
		get := func() string {
			if hasVal {
				return val
			}
			return needArg(&i, name)
		}
		switch name {
		case "-D", "--define", "--D":
			defines = append(defines, get())
		case "-I":
			includes = append(includes, get())
		case "--input-file", "--init-file":
			initFile = get()
		case "--translit":
			translit = get()
			batch = true
		case "--unsafe-init":
			unsafe = true
		case "--punctuation-fullwidth-period":
			fwperiod = true
		case "--console-encoding", "--filename-encoding":
			get()
		case "--jisx0213":
		case "--version":
			fmt.Printf("naggy-backend version %s\n", naggy.Version)
			os.Exit(0)
		case "-h", "-?", "--help", "--man":
			fmt.Print(usage)
			os.Exit(1)
		default:
			fmt.Fprintf(os.Stderr, "Unknown option: %s\n%s", strings.TrimLeft(name, "-"), usage)
			os.Exit(2)
		}
	}
	rest = append(rest, args[i:]...)

	naggy.ResourcePath = naggy.DefaultResourcePath()
	for j := len(includes) - 1; j >= 0; j-- {
		naggy.ResourcePath = append([]string{includes[j]}, naggy.ResourcePath...)
	}

	opt := naggy.Options{Unsafe: unsafe, Debug: true}
	if fwperiod {
		opt.PunctuationFullwidthPeriod = "1"
	} else {
		opt.PunctuationFullwidthPeriod = "0"
	}
	ngb := naggy.NewBackend(opt)
	out := bufio.NewWriterSize(os.Stdout, 65536)
	ngb.Out = out
	ngb.Exit = func() { out.Flush(); os.Exit(0) }
	esc := make([]string, len(rest))
	for j, s := range rest {
		esc[j] = naggy.EscapeString(s)
	}
	ngb.InitVar["ARGV"] = strings.Join(esc, " ")
	for _, kv := range defines {
		k, v := kv, ""
		if j := strings.IndexAny(kv, "= "); j >= 0 {
			k, v = kv[:j], kv[j+1:]
		}
		ngb.InitVar[k] = v
	}
	if batch {
		ngb.BatchMode = []string{"translit", translit}
	}

	if !ngb.LoadDefaultInit(initFile) {
		out.Flush()
		fmt.Fprintln(os.Stderr, ngb.InitTail)
		os.Exit(255)
	}

	if batch {
		runBatch(ngb, translit, rest, out)
		out.Flush()
		os.Exit(0)
	}
	ngb.CommandLoop(os.Stdin)
	out.Flush()
}

func runBatch(ngb *naggy.Backend, spec string, args []string, out *bufio.Writer) {
	if len(args) > 1 {
		fmt.Fprintln(os.Stderr, "# error Illegal arguments.")
		os.Exit(255)
	}
	if _, ok := ngb.Translit.Table[spec]; !ok {
		fmt.Fprintf(os.Stderr, "# error No such translit table: %s.\n", spec)
		os.Exit(255)
	}
	in := os.Stdin
	if len(args) == 1 {
		f, err := os.Open(args[0])
		if err != nil {
			fmt.Fprintf(os.Stderr, "# error %s: %v\n", args[0], err)
			os.Exit(255)
		}
		defer f.Close()
		in = f
	}
	sc := bufio.NewScanner(in)
	sc.Buffer(make([]byte, 1<<20), 1<<26)
	for sc.Scan() {
		l := strings.TrimSuffix(sc.Text(), "\r")
		out.WriteString(ngb.TranslitWithSpace(spec, l) + "\n")
	}
}
