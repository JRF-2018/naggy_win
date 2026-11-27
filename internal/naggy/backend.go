package naggy

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"
)

// Version is reported by the "version" command.
const Version = "0.18-naggy_win-0.3.8"

// CommandFunc implements a backend command.
type CommandFunc func(b *Backend, args []string)

// Backend is Naggy::Backend.
type Backend struct {
	Unsafe          bool
	InInitFile      bool
	InitLog         string
	InitTail        string
	InitVar         map[string]string
	initVarStack    map[string][]*string
	Translit        *TranslitSet
	Convert         *Converter
	ConvertTranslit string
	Commands        map[string]CommandFunc
	BatchMode       []string

	// Out receives normal output; ErrOut receives output in batch mode.
	Out    io.Writer
	ErrOut io.Writer
	// Exit is called by the "exit"/"bye" commands.
	Exit func()
}

// Options for NewBackend.
type Options struct {
	Unsafe                     bool
	NoMinilisp                 bool
	Debug                      bool
	PunctuationFullwidthPeriod string
	KBKana                     string
}

// NewBackend creates a backend.
func NewBackend(opt Options) *Backend {
	b := &Backend{
		Unsafe:          opt.Unsafe,
		InitVar:         map[string]string{"$": "$"},
		initVarStack:    map[string][]*string{},
		Translit:        NewTranslitSet(),
		ConvertTranslit: "alpha-hira",
		Commands:        map[string]CommandFunc{},
		Out:             os.Stdout,
		ErrOut:          os.Stderr,
		Exit:            func() { os.Exit(0) },
	}
	for _, kv := range os.Environ() {
		if i := strings.IndexByte(kv, '='); i > 0 {
			b.InitVar[kv[:i]] = kv[i+1:]
		}
	}
	for k, v := range commandTable {
		b.Commands[k] = v
	}
	if !opt.NoMinilisp {
		for k, v := range minilispCommandTable {
			b.Commands[k] = v
		}
	}
	if opt.Unsafe {
		for k, v := range unsafeCommandTable {
			b.Commands[k] = v
		}
	}
	if opt.Debug {
		for k, v := range testCommandTable {
			b.Commands[k] = v
		}
	}
	b.InitVar["JAPANESE_PUNCT_PERIOD"] = "0"
	if opt.PunctuationFullwidthPeriod != "" {
		b.InitVar["JAPANESE_PUNCT_PERIOD"] = opt.PunctuationFullwidthPeriod
	}
	b.InitVar["JAPANESE_KBKANA"] = "0"
	if opt.KBKana != "" {
		b.InitVar["JAPANESE_KBKANA"] = opt.KBKana
	}
	b.Convert = newConverter(b)
	return b
}

func (b *Backend) rprint(s string) {
	if b.InInitFile {
		b.InitTail = s
		b.InitLog += s
	} else if b.BatchMode != nil {
		io.WriteString(b.ErrOut, s)
	} else {
		io.WriteString(b.Out, s)
	}
}

func (b *Backend) rerror(s string) { b.rprint("# error " + EscapeString(s) + "\n") }
func (b *Backend) rinfo(s string)  { b.rprint("# info " + EscapeString(s) + "\n") }
func (b *Backend) rwarn(s string)  { b.rprint("# warn " + EscapeString(s) + "\n") }

func (b *Backend) printString(s string) {
	b.rprint("# begin string\n" + EscapeString(s) + "\n# end\n")
}

func (b *Backend) printUnit(s string) {
	b.rprint("# begin unit\n" + s + "\n# end\n")
}

// ProcessCommand runs one command (Naggy::Backend::process_command).
func (b *Backend) ProcessCommand(cmd string, args []string) {
	c, ok := b.Commands[cmd]
	if !ok {
		b.rerror("unknown command: " + cmd + ".")
		return
	}
	func() {
		defer func() {
			if r := recover(); r != nil {
				if d, ok := r.(dieError); ok {
					b.rerror(cmd + ": " + d.msg)
					return
				}
				b.rerror(fmt.Sprintf("%s: %v", cmd, r))
			}
		}()
		c(b, args)
	}()
}

// ProcessLine parses a protocol line and runs it.
func (b *Backend) ProcessLine(line string) {
	line = strings.TrimSuffix(line, "\n")
	line = strings.TrimSuffix(line, "\r")
	f := splitWS(line)
	if len(f) == 0 {
		b.ProcessCommand("nop", nil)
		return
	}
	for i := range f {
		f[i] = UnescapeString(f[i])
	}
	b.ProcessCommand(f[0], f[1:])
}

// LoadInitFile is Naggy::Backend::load_init_file.
func (b *Backend) LoadInitFile(f string) bool {
	data, err := os.ReadFile(f)
	if err != nil {
		b.rerror("load_init_file: " + f + ": " + err.Error())
		return false
	}
	return b.LoadInitFileFromString(strings.ToValidUTF8(string(data), "�"), f)
}

var (
	reVarRef     = regexp.MustCompile(`\$\{([^\}]+)\}`)
	reDotPrefix  = regexp.MustCompile(`^\.\s*`)
	reEndif      = regexp.MustCompile(`(?i)^endif$`)
	reIfAny      = regexp.MustCompile(`(?i)^if(?:\s|$)`)
	reElse       = regexp.MustCompile(`(?i)^else$`)
	reSet2       = regexp.MustCompile(`(?i)^set\s+(\S+)\s+(\S.*)$`)
	reSet1       = regexp.MustCompile(`(?i)^set\s+(\S+)$`)
	reUnset      = regexp.MustCompile(`(?i)^unset\s+(\S+)$`)
	reSetU2      = regexp.MustCompile(`(?i)^set-u\s+(\S+)\s+(\S.*)$`)
	reSetU1      = regexp.MustCompile(`(?i)^set-u\s+(\S+)$`)
	rePush       = regexp.MustCompile(`(?i)^push\s+(\S+)\s*`)
	rePop        = regexp.MustCompile(`(?i)^pop\s+(\S+)$`)
	reIf         = regexp.MustCompile(`(?i)^if\s+`)
	reOrSplit    = regexp.MustCompile(`\s+\|\|\s+`)
	reIsTrue     = regexp.MustCompile(`(?i)^is-true\s*`)
	reNotIsTrue  = regexp.MustCompile(`(?i)^!\s*is-true\s*`)
	reDefined    = regexp.MustCompile(`(?i)^defined\s+(\S+)$`)
	reNotDefined = regexp.MustCompile(`(?i)^!\s*defined\s+(\S+)$`)
	reEmpty      = regexp.MustCompile(`(?i)^empty$`)
	reNotEmpty   = regexp.MustCompile(`(?i)^!\s*empty\s+\S`)
	reNumber     = regexp.MustCompile(`(?i)^number\s+[0-9]+(?:\.[0-9]+)?$`)
	reNotNumber  = regexp.MustCompile(`(?i)^!\s*number\s*$`)
	reStrInStr   = regexp.MustCompile(`(?i)^strinstr\s+(\S+)\s*`)
	reNotStrIn   = regexp.MustCompile(`(?i)^!\s*strinstr\s+(\S+)\s*`)
	reNumeric    = regexp.MustCompile(`^[0-9.]+$`)
	reFracNumber = regexp.MustCompile(`^[0-9]+(?:\.[0-9]+)$`)
	reBeginEnd   = regexp.MustCompile(`(?is)# begin\s+\S+\n(.*)\n# end`)
)

// binary operators of .if expressions, in the order Perl tries them.
var reBinOps = []struct {
	op string
	re *regexp.Regexp
}{
	{"==", regexp.MustCompile(`^(.*)\s+==\s+(.*)$`)},
	{"!=", regexp.MustCompile(`^(.*)\s+!=\s+(.*)$`)},
	{">=", regexp.MustCompile(`^(.*)\s+>=\s+(.*)$`)},
	{"<=", regexp.MustCompile(`^(.*)\s+<=\s+(.*)$`)},
	{">", regexp.MustCompile(`^(.*)\s+>\s+(.*)$`)},
	{"<", regexp.MustCompile(`^(.*)\s+<\s+(.*)$`)},
}

func after(re *regexp.Regexp, s string) string {
	return s[len(re.FindString(s)):]
}

func (b *Backend) evalIfExpr(expr string) (bool, bool) {
	expr = strings.TrimSpace(expr)
	switch {
	case reIsTrue.MatchString(expr):
		return IsTrue(strings.TrimSpace(after(reIsTrue, expr))), true
	case reNotIsTrue.MatchString(expr):
		return !IsTrue(strings.TrimSpace(after(reNotIsTrue, expr))), true
	case reDefined.MatchString(expr):
		_, ok := b.InitVar[reDefined.FindStringSubmatch(expr)[1]]
		return ok, true
	case reNotDefined.MatchString(expr):
		_, ok := b.InitVar[reNotDefined.FindStringSubmatch(expr)[1]]
		return !ok, true
	case reEmpty.MatchString(expr):
		return true, true
	case reNotEmpty.MatchString(expr):
		return true, true
	case reNumber.MatchString(expr):
		return true, true
	case reNotNumber.MatchString(expr):
		return !reFracNumber.MatchString(after(reNotNumber, expr)), true
	case reStrInStr.MatchString(expr):
		m := reStrInStr.FindStringSubmatch(expr)[1]
		return strings.Contains(after(reStrInStr, expr), m), true
	case reNotStrIn.MatchString(expr):
		m := reNotStrIn.FindStringSubmatch(expr)[1]
		return !strings.Contains(after(reNotStrIn, expr), m), true
	}
	for _, op := range reBinOps {
		m := op.re.FindStringSubmatch(expr)
		if m == nil {
			continue
		}
		x := strings.TrimSpace(m[1])
		y := strings.TrimSpace(m[2])
		switch op.op {
		case "==", "!=":
			var eq bool
			if reNumeric.MatchString(x) && reNumeric.MatchString(y) {
				eq = perlNum(x) == perlNum(y)
			} else {
				eq = x == y
			}
			if op.op == "==" {
				return eq, true
			}
			return !eq, true
		case ">=":
			return perlNum(x) >= perlNum(y), true
		case "<=":
			return perlNum(x) <= perlNum(y), true
		case ">":
			return perlNum(x) > perlNum(y), true
		case "<":
			return perlNum(x) < perlNum(y), true
		}
	}
	return false, false
}

// LoadInitFileFromString is Naggy::Backend::load_init_file_from_string.
func (b *Backend) LoadInitFileFromString(content, f string) bool {
	lines := perlSplitLines(content)
	var ifStack []int
	ifState := 1
	// ifState: 0: may else. 1: may if. 2: in if in if false.
	//          3: in else(do). 4: in else(don't)
	prev := b.InInitFile
	b.InInitFile = true
	fail := func(msg string) bool {
		if msg != "" {
			b.rerror(msg)
		}
		b.InInitFile = prev
		return false
	}

	for len(lines) > 0 {
		s, _ := NextLine(&lines)
		s = strings.TrimSpace(s)
		s = reVarRef.ReplaceAllStringFunc(s, func(m string) string {
			name := m[2 : len(m)-1]
			if len(name) > 3 && strings.HasSuffix(name, ":e") {
				name = name[:len(name)-2]
				if v, ok := b.InitVar[name]; ok {
					return strings.ReplaceAll(EscapeString(v), "|", "\\u{7C}")
				}
				return "\\0"
			}
			if v, ok := b.InitVar[name]; ok {
				return v
			}
			return ""
		})

		if loc := reDotPrefix.FindStringIndex(s); loc != nil {
			s = s[loc[1]:]
			if ifState == 0 || ifState == 2 || ifState == 4 {
				switch {
				case reEndif.MatchString(s):
					if len(ifStack) > 0 {
						ifState = ifStack[len(ifStack)-1]
						ifStack = ifStack[:len(ifStack)-1]
					} else {
						return fail(f + ": Endif without if.")
					}
				case reIfAny.MatchString(s):
					ifStack = append(ifStack, ifState)
					ifState = 2
				case reElse.MatchString(s):
					if ifState == 0 {
						ifState = 3
					} else if ifState == 4 {
						return fail(f + ": Double else.")
					} else {
						ifState = 2
					}
				}
				continue
			}
			switch {
			case reSet2.MatchString(s):
				m := reSet2.FindStringSubmatch(s)
				b.InitVar[m[1]] = m[2]
			case reSet1.MatchString(s):
				b.InitVar[reSet1.FindStringSubmatch(s)[1]] = ""
			case reUnset.MatchString(s):
				delete(b.InitVar, reUnset.FindStringSubmatch(s)[1])
			case reSetU2.MatchString(s):
				m := reSetU2.FindStringSubmatch(s)
				b.InitVar[m[1]] = UnescapeString(m[2])
			case reSetU1.MatchString(s):
				b.InitVar[reSetU1.FindStringSubmatch(s)[1]] = ""
			case b.Unsafe && rePush.MatchString(s):
				n := rePush.FindStringSubmatch(s)[1]
				rest := strings.TrimSpace(after(rePush, s))
				if v, ok := b.InitVar[n]; ok {
					vv := v
					b.initVarStack[n] = append(b.initVarStack[n], &vv)
				} else {
					b.initVarStack[n] = append(b.initVarStack[n], nil)
				}
				if rest != "" {
					b.InitVar[n] = rest
				}
			case b.Unsafe && rePop.MatchString(s):
				n := rePop.FindStringSubmatch(s)[1]
				if st, ok := b.initVarStack[n]; ok && len(st) > 0 {
					v := st[len(st)-1]
					st = st[:len(st)-1]
					if v != nil {
						b.InitVar[n] = *v
					} else {
						delete(b.InitVar, n)
					}
					if len(st) == 0 {
						delete(b.initVarStack, n)
					} else {
						b.initVarStack[n] = st
					}
				}
			case reIf.MatchString(s):
				r := false
				ifStack = append(ifStack, ifState)
				for _, expr := range reOrSplit.Split(after(reIf, s), -1) {
					v, ok := b.evalIfExpr(expr)
					if !ok {
						return fail(f + ": Illegal if-expression.")
					}
					r = v
					if r {
						break
					}
				}
				if r {
					ifState = 1
				} else {
					ifState = 0
				}
			case reElse.MatchString(s):
				if ifState == 1 {
					if len(ifStack) > 0 {
						ifState = 4
					} else {
						return fail(f + ": Else without if.")
					}
				} else {
					return fail(f + ": Double else.")
				}
			case reEndif.MatchString(s):
				if len(ifStack) > 0 {
					ifState = ifStack[len(ifStack)-1]
					ifStack = ifStack[:len(ifStack)-1]
				} else {
					return fail(f + ": Endif without if.")
				}
			default:
				return fail("cannot interpret # " + s)
			}
			continue
		}

		if ifState&1 == 0 {
			continue
		}
		s = strings.TrimSpace(s)
		if s == "" {
			continue
		}
		b.InitLog += s + "\n"
		prevLogLen := len(b.InitLog)

		fields := splitWS(s)
		for i := range fields {
			fields[i] = UnescapeString(fields[i])
		}
		cmd, args := fields[0], fields[1:]
		if cmd == "return" {
			if len(args) > 0 {
				b.InitVar["RESULT"] = strings.Join(args, " ")
			} else {
				delete(b.InitVar, "RESULT")
			}
			b.rinfo("return from " + f + ".")
			break
		} else if cmd == "return-e" {
			e := make([]string, len(args))
			for i, a := range args {
				e[i] = EscapeString(a)
			}
			b.InitVar["RESULT"] = strings.Join(e, " ")
			b.rinfo("return from " + f + ".")
			break
		}
		b.ProcessCommand(cmd, args)

		output := b.InitLog[prevLogLen:]
		if m := reBeginEnd.FindStringSubmatch(output); m != nil {
			b.InitVar["RESULT"] = UnescapeString(m[1])
		} else if strings.HasPrefix(b.InitTail, "# error") {
			return fail("")
		} else {
			delete(b.InitVar, "RESULT")
		}
	}
	b.InInitFile = prev
	return true
}

// CommandLoop reads commands from r until EOF.
func (b *Backend) CommandLoop(r io.Reader) {
	br := bufio.NewReaderSize(r, 65536)
	for {
		line, err := br.ReadString('\n')
		if len(line) > 0 {
			b.ProcessLine(strings.ToValidUTF8(line, "�"))
			if f, ok := b.Out.(interface{ Flush() error }); ok {
				f.Flush()
			}
		}
		if err != nil {
			return
		}
	}
}

// LoadDefaultInit loads ~/.naggy-backend or site-init.nginit the way
// naggy-backend.pl does.  It returns false (with the error in InitTail)
// when the init file fails.
func (b *Backend) LoadDefaultInit(initFile string) bool {
	if initFile != "" {
		return b.LoadInitFile(initFile)
	}
	fn, ok := LocateFileResource(joinPath(HomeDir(), ".naggy-backend"))
	if ok {
		if st, err := os.Stat(fn); err != nil || st.IsDir() {
			ok = false
		}
	}
	if !ok {
		fn, ok = LocateFileResource("site-init.nginit")
	}
	if ok {
		return b.LoadInitFile(fn)
	}
	return true
}
