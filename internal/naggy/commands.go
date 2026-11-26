package naggy

import (
	"encoding/base64"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/JRF-2018/naggy_win/internal/jenc"
)

var (
	commandTable         = map[string]CommandFunc{}
	minilispCommandTable = map[string]CommandFunc{}
	unsafeCommandTable   = map[string]CommandFunc{}
	testCommandTable     = map[string]CommandFunc{}
)

func joinPath(a ...string) string { return filepath.Join(a...) }

func escapeAll(l []string, twice bool) string {
	r := make([]string, len(l))
	for i, s := range l {
		r[i] = EscapeString(s)
		if twice {
			r[i] = EscapeString(r[i])
		}
	}
	return strings.Join(r, " ")
}

func hasSpace(s string) bool { return strings.IndexFunc(s, isPerlSpace) >= 0 }

func encFlag(name string, b *Backend, flag string) (jenc.Encoding, bool) {
	switch flag {
	case "-e":
		return jenc.EUCJP, true
	case "-s":
		return jenc.CP932, true
	case "-u":
		return jenc.UTF8, true
	}
	b.rerror(name + ": illegal encoding flag \"" + flag + "\".")
	return 0, false
}

func init() {
	c := commandTable
	c["nop"] = func(b *Backend, a []string) { b.printUnit("true") }
	c["exit"] = func(b *Backend, a []string) {
		b.printUnit("bye")
		b.Exit()
	}
	c["bye"] = c["exit"]
	c["version"] = func(b *Backend, a []string) { b.printString(Version) }
	c["set"] = func(b *Backend, a []string) {
		if len(a) != 1 && len(a) != 2 {
			b.rerror("set: illegal arguments.")
			return
		}
		if len(a) == 1 {
			b.InitVar[a[0]] = ""
		} else {
			b.InitVar[a[0]] = a[1]
		}
		b.printUnit("ok")
	}
	c["unset"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("unset: illegal arguments.")
			return
		}
		delete(b.InitVar, a[0])
		b.printUnit("ok")
	}
	c["get"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("get: illegal arguments.")
			return
		}
		v, ok := b.InitVar[a[0]]
		if !ok {
			b.rerror("get: The variable doesn't exist.")
			return
		}
		b.printString(v)
	}
	testCommandTable["base64test"] = func(b *Backend, a []string) {
		b.rprint("# begin base64\n" + base64.StdEncoding.EncodeToString([]byte("OK base64")) + "\n\n# end\n")
	}
	testCommandTable["listtest"] = func(b *Backend, a []string) {
		b.rprint("# begin list\na b c\nc d \\0 e ニホンゴ\n# end\n")
	}
	testCommandTable["warntest"] = func(b *Backend, a []string) {
		b.rprint("# warn test warn.\n")
		b.rprint("# begin list\nOK\n")
		b.rprint("# info test info.\n")
		b.rprint("# end\n")
	}
	c["translit"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("translit: illegal arguments.")
			return
		}
		r, ok := b.Translit.Translit(a[0], a[1])
		if !ok {
			b.rerror("translit: failed.")
			return
		}
		b.printString(r)
	}
	c["translit-with-space"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("translit-with-space: illegal arguments.")
			return
		}
		r := translitWithSpace(b, a[0], a[1])
		b.printString(r)
	}
	loadTranslit := func(name string, inversely, auto bool) CommandFunc {
		return func(b *Backend, a []string) {
			if len(a) != 2 {
				b.rerror(name + ": illegal arguments.")
				return
			}
			if hasSpace(a[0]) {
				b.rerror(name + ": illegal name which contains a space.")
				return
			}
			file, ok := LocateFileResource(a[1])
			if !ok {
				b.rerror(name + ": " + a[1] + ": not found or unavailable.")
				return
			}
			if auto {
				b.Translit.Table[a[0]] = NewAutoLoader(file, inversely)
			} else {
				t := NewTable()
				if err := t.LoadFile(file, inversely); err != nil {
					b.rerror(name + ": " + file + ": " + err.Error())
					return
				}
				b.Translit.Table[a[0]] = t
			}
			b.printUnit("ok")
		}
	}
	c["load-translit"] = loadTranslit("load-translit", false, false)
	c["load-translit-inversely"] = loadTranslit("load-translit-inversely", true, false)
	c["auto-load-translit"] = loadTranslit("auto-load-translit", false, true)
	c["auto-load-translit-inversely"] = loadTranslit("auto-load-translit-inversely", true, true)
	c["load-translit-from-string"] = func(b *Backend, a []string) {
		if len(a) < 2 {
			b.rerror("load-translit-from-string: illegal arguments.")
			return
		}
		if hasSpace(a[0]) {
			b.rerror("load-translit-from-string: illegal name which contains a space.")
			return
		}
		t := NewTable()
		t.LoadFromString(strings.Join(a[1:], "\n"), false)
		b.Translit.Table[a[0]] = t
		b.printUnit("ok")
	}
	c["abbrev-translit"] = func(b *Backend, a []string) {
		if len(a) == 0 {
			b.rerror("abbrev-translit: illegal arguments.")
			return
		}
		if hasSpace(a[0]) {
			b.rerror("abbrev-translit: illegal name which contains a space.")
			return
		}
		switch len(a) - 1 {
		case 0:
			delete(b.Translit.Table, a[0])
		case 1:
			b.Translit.Table[a[0]] = a[1]
		default:
			l := &SpecList{}
			for _, s := range a[1:] {
				l.Items = append(l.Items, s)
			}
			b.Translit.Table[a[0]] = l
		}
		b.printUnit("ok")
	}
	c["rename-translit"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("rename-translit: illegal arguments.")
			return
		}
		if hasSpace(a[0]) || hasSpace(a[1]) || strings.TrimSpace(a[0]) == "" || strings.TrimSpace(a[1]) == "" {
			b.rerror("rename-translit: illegal name.")
			return
		}
		b.Translit.Rename(a[0], a[1])
		b.printUnit("ok")
	}
	c["charmap-of-translit"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("charmap-of-translit: illegal arguments.")
			return
		}
		if hasSpace(a[0]) {
			b.rerror("charmap-of-translit: illegal name which contains a space.")
			return
		}
		cm := b.Translit.AlphaCharmap(a[0])
		if cm == nil {
			b.rerror("charmap-of-translit: cannot make charmap.")
			return
		}
		keys := sortedKeys(cm)
		var sb strings.Builder
		sb.WriteString("# begin list\ndata\n")
		for _, k := range keys {
			sb.WriteString(EscapeString(k) + " " + EscapeString(cm[k]) + "\n")
		}
		sb.WriteString("# end\n")
		b.rprint(sb.String())
	}
	c["complete-by-translit"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("complete-by-translit: illegal arguments.")
			return
		}
		if hasSpace(a[0]) {
			b.rerror("complete-by-translit: illegal name which contains a space.")
			return
		}
		r := b.Translit.Complete(a[0], a[1])
		if r == nil {
			b.rerror("complete-by-translit: failed.")
			return
		}
		compl := ""
		if r.Completion != nil {
			compl = *r.Completion
		}
		var sb strings.Builder
		sb.WriteString("# begin list\n")
		sb.WriteString("prefix " + EscapeString(r.Prefix) + "\n")
		sb.WriteString("word " + EscapeString(r.Word) + "\n")
		sb.WriteString("completion " + EscapeString(compl) + "\n")
		sb.WriteString("data\n")
		var word, other []string
		for k := range r.Map {
			if len(k) > 0 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= 'a' && k[0] <= 'z')) {
				word = append(word, k)
			} else {
				other = append(other, k)
			}
		}
		sort.Strings(word)
		sort.Strings(other)
		for _, k := range append(word, other...) {
			sb.WriteString(EscapeString(k) + " " + EscapeString(r.Map[k]) + "\n")
		}
		sb.WriteString("# end\n")
		b.rprint(sb.String())
	}
	c["list-translit"] = func(b *Backend, a []string) {
		if len(a) != 0 {
			b.rerror("list-translit: illegal arguments.")
			return
		}
		keys := make([]string, 0, len(b.Translit.Table))
		for k := range b.Translit.Table {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		var sb strings.Builder
		sb.WriteString("# begin list\n")
		for _, k := range keys {
			sb.WriteString(EscapeString(k) + " " + EscapeString(specString(b.Translit.Table[k])) + "\n")
		}
		sb.WriteString("# end\n")
		b.rprint(sb.String())
	}
	c["add-convert-name-translit"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("add-convert-name-translit: illegal arguments.")
			return
		}
		if hasSpace(a[1]) {
			b.rerror("add-convert-name-translit: illegal translit name which contains a space.")
			return
		}
		b.Convert.Table[a[0]] = ConvertMode{"translit", a[1]}
		b.printUnit("ok")
	}
	for _, kind := range []string{"skk", "tankanji", "unicode"} {
		kind := kind
		name := "add-convert-name-" + kind
		c[name] = func(b *Backend, a []string) {
			if len(a) != 1 {
				b.rerror(name + ": illegal arguments.")
				return
			}
			b.Convert.Table[a[0]] = ConvertMode{"convert", kind}
			b.printUnit("ok")
		}
	}
	c["delete-convert-name"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("delete-convert-name: illegal arguments.")
			return
		}
		delete(b.Convert.Table, a[0])
		b.printUnit("ok")
	}
	c["set-tankanji-dic"] = func(b *Backend, a []string) {
		if len(a) != 1 && len(a) != 2 {
			b.rerror("set-tankanji-dic: illegal arguments.")
			return
		}
		flag := "-u"
		if len(a) == 2 {
			flag = a[1]
		}
		e, ok := encFlag("set-tankanji-dic", b, flag)
		if !ok {
			return
		}
		file, ok := LocateFileResource(a[0])
		if !ok {
			b.rerror("set-tankanji-dic: " + a[0] + ": not found or unavailable.")
			return
		}
		d, err := NewTankanjiDic(file, e)
		if err != nil {
			die("%s: %v", file, err)
		}
		b.Convert.TankanjiDic = d
		b.printUnit("ok")
	}
	c["delete-tankanji-dic"] = func(b *Backend, a []string) {
		if len(a) != 0 {
			b.rerror("delete-tankanji-dic: illegal arguments.")
			return
		}
		b.Convert.TankanjiDic = nil
		b.printUnit("ok")
	}
	c["add-skk-dic"] = func(b *Backend, a []string) {
		if len(a) != 1 && len(a) != 2 {
			b.rerror("add-skk-dic: illegal arguments.")
			return
		}
		flag := "-u"
		if len(a) == 2 {
			flag = a[1]
		}
		e, ok := encFlag("add-skk-dic", b, flag)
		if !ok {
			return
		}
		file, ok := LocateFileResource(a[0])
		if !ok {
			b.rerror("add-skk-dic: " + a[0] + ": not found or unavailable.")
			return
		}
		d, err := NewSKKDic(file, e)
		if err != nil {
			die("%s: %v", file, err)
		}
		b.Convert.SKKDics = append(b.Convert.SKKDics, d)
		b.printUnit("ok")
	}
	c["delete-skk-dic"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("delete-skk-dic: illegal arguments.")
			return
		}
		file, ok := LocateFileResource(a[0])
		if !ok {
			file = a[0]
		}
		var r []*SKKDic
		done := false
		for _, d := range b.Convert.SKKDics {
			if d.Filename == file {
				done = true
			} else {
				r = append(r, d)
			}
		}
		if !done {
			b.rerror("delete-skk-dic: " + a[0] + ": not found or already unavailable.")
			return
		}
		b.Convert.SKKDics = r
		b.printUnit("ok")
	}
	c["convert"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("convert: illegal arguments.")
			return
		}
		r := b.Convert.Convert(a[0])
		switch {
		case r.Null:
			b.rprint("# begin list\n# end\n")
		case r.String != nil:
			b.printString(*r.String)
		default:
			var sb strings.Builder
			sb.WriteString("# begin list\n")
			for _, x := range r.List {
				com := ""
				if x.Comment != nil {
					com = *x.Comment
				}
				h := "n"
				if x.Highlight {
					h = "h"
				}
				sb.WriteString(EscapeString(x.Word) + " " + EscapeString(itoa(x.Pos)) + " " +
					h + " " + EscapeString(com) + "\n")
			}
			sb.WriteString("# end\n")
			b.rprint(sb.String())
		}
	}
	c["load-init-file"] = func(b *Backend, a []string) {
		if len(a) < 1 {
			b.rerror("load-init-file: illegal arguments.")
			return
		}
		file, ok := LocateFileResource(a[0])
		if !ok {
			b.rerror("load-init-file: " + a[0] + ": not found or unavailable.")
			return
		}
		prevArgv, hadArgv := b.InitVar["ARGV"]
		b.InitVar["ARGV"] = escapeAll(a[1:], false)
		if b.LoadInitFile(file) {
			if v, ok := b.InitVar["RESULT"]; ok {
				b.printString(v)
			} else {
				b.rerror("load-init-file: no result.")
			}
		} else {
			b.rprint(b.InitTail)
		}
		if hadArgv {
			b.InitVar["ARGV"] = prevArgv
		} else {
			// Perl assigns undef; the key keeps existing.
			b.InitVar["ARGV"] = ""
		}
	}
	c["print-init-log"] = func(b *Backend, a []string) {
		if len(a) != 0 {
			b.rerror("print-init-log: illegal arguments.")
			return
		}
		var sb strings.Builder
		sb.WriteString("# begin list\n")
		for _, l := range perlSplitLines(b.InitLog) {
			sb.WriteString(EscapeString(l) + "\n")
		}
		sb.WriteString("# end\n")
		b.rprint(sb.String())
	}
	c["flush-init-log"] = func(b *Backend, a []string) {
		if len(a) != 0 {
			b.rerror("flush-init-log: illegal arguments.")
			return
		}
		b.InitLog = ""
		b.InitTail = ""
		b.printUnit("OK")
	}
	c["find-path"] = func(b *Backend, a []string) {
		if len(a) < 2 {
			b.rerror("find-path: illegal arguments. Usage: find-path file dir1 dir2 ...")
			return
		}
		var r string
		found := false
		if filepath.IsAbs(a[0]) {
			if exists(a[0]) {
				r, found = a[0], true
			}
		} else {
			for _, d := range a[1:] {
				f := catfile(d, a[0])
				if exists(f) {
					r, found = f, true
					break
				}
			}
		}
		if found {
			b.printString(r)
		} else {
			b.rerror("find-path: " + a[0] + " doesn't exists in the path.")
		}
	}
	c["echo"] = func(b *Backend, a []string) {
		b.rprint("# begin string\n" + escapeAll(a, false) + "\n# end\n")
	}
	c["split-e"] = func(b *Backend, a []string) {
		if len(a) < 1 {
			b.rerror("split-e: illegal arguments.")
			return
		}
		var rest []string
		for _, s := range a[1:] {
			rest = append(rest, perlSplitLiteral(a[0], s)...)
		}
		b.rprint("# begin string\n" + escapeAll(rest, true) + "\n# end\n")
	}
	c["list-command"] = func(b *Backend, a []string) {
		if len(a) != 0 {
			b.rerror("list-command: illegal arguments.")
			return
		}
		keys := make([]string, 0, len(b.Commands))
		for k := range b.Commands {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		var sb strings.Builder
		sb.WriteString("# begin list\n")
		for _, k := range keys {
			sb.WriteString(EscapeString(k) + "\n")
		}
		sb.WriteString("# end\n")
		b.rprint(sb.String())
	}
	c["list-commands"] = c["list-command"]

	m := minilispCommandTable
	m["echo-e"] = func(b *Backend, a []string) {
		b.rprint("# begin string\n" + escapeAll(a, true) + "\n# end\n")
	}
	m["join"] = func(b *Backend, a []string) {
		if len(a) < 1 {
			b.rerror("join: illegal arguments.")
			return
		}
		b.printString(strings.Join(a[1:], a[0]))
	}
	m["add"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("add: illegal arguments.")
			return
		}
		b.printString(perlNumString(perlNum(a[0]) + perlNum(a[1])))
	}
	m["list-length"] = func(b *Backend, a []string) {
		b.printString(itoa(len(a)))
	}
	m["nth"] = func(b *Backend, a []string) {
		if len(a) < 1 {
			b.rerror("nth: illegal arguments.")
			return
		}
		num := perlNum(a[0])
		rest := a[1:]
		n := len(rest)
		if num < float64(n) && num >= 0 {
			b.printString(rest[int(num)])
		} else if -num <= float64(n) && num < 0 {
			b.printString(rest[n+int(num)])
		} else {
			b.rerror("nth: out of bounds.")
		}
	}
	m["nthcdr-e"] = func(b *Backend, a []string) {
		if len(a) < 1 {
			b.rerror("nthcdr-e: illegal arguments.")
			return
		}
		num := int(perlNum(a[0]))
		rest := a[1:]
		n := len(rest)
		if (num < n && num >= 0) || (-num <= n && num < 0) {
			if num > 0 {
				rest = rest[num:]
			} else if num < 0 {
				rest = rest[n+num:]
			}
			b.rprint("# begin string\n" + escapeAll(rest, true) + "\n# end\n")
		} else {
			b.rerror("nthcdr-e: out of bounds.\n")
		}
	}
	m["load-init-file-while-not"] = func(b *Backend, a []string) {
		if len(a) < 2 {
			b.rerror("load-init-file-while-not: illegal arguments.")
			return
		}
		v, fn, argv := a[0], a[1], a[2:]
		file, ok := LocateFileResource(fn)
		if !ok {
			b.rerror("load-init-file-while-not: " + fn + ": not found or unavailable.")
			return
		}
		prevArgv := b.InitVar["ARGV"]
		b.InitVar["ARGV"] = escapeAll(argv, false)
		for {
			x, ok := b.InitVar[v]
			if ok && IsTrue(x) {
				break
			}
			delete(b.InitVar, "RESULT")
			if !b.LoadInitFile(file) {
				b.rprint(b.InitTail)
				b.InitVar["ARGV"] = prevArgv
				return
			}
		}
		if r, ok := b.InitVar["RESULT"]; ok {
			b.printString(r)
		} else {
			b.rerror("load-init-file-while-not: no result.")
		}
	}

	u := unsafeCommandTable
	u["concat-filename"] = func(b *Backend, a []string) {
		if len(a) < 2 {
			b.rerror("concat-filename: illegal arguments.")
			return
		}
		b.printString(filepath.Join(a...))
	}
	u["split-path-e"] = func(b *Backend, a []string) {
		if len(a) != 1 && len(a) != 2 {
			b.rerror("split-path-e: illegal arguments.")
			return
		}
		p := a[0]
		vol := filepath.VolumeName(p)
		dir, file := filepath.Split(p[len(vol):])
		if len(a) == 2 && IsTrue(a[1]) {
			dir, file = p[len(vol):], ""
		}
		b.rprint("# begin string\n" + escapeAll([]string{vol, dir, file}, true) + "\n# end\n")
	}
	u["split-extension-e"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("split-extension-e: illegal arguments.")
			return
		}
		fn := a[0]
		ext := ""
		if m := regexp.MustCompile(`\.[^\./\\]+$`).FindStringIndex(fn); m != nil {
			ext = fn[m[0]:]
			fn = fn[:m[0]]
		}
		b.rprint("# begin string\n" + escapeAll([]string{fn, ext}, true) + "\n# end\n")
	}
	u["read-with-encoding-e"] = func(b *Backend, a []string) {
		if len(a) != 2 {
			b.rerror("read-with-encoding-e: illegal arguments.")
			return
		}
		file, encName := a[0], a[1]
		ret := ""
		low := strings.ToLower(encName)
		for _, suf := range []string{"-crlf", "-dos"} {
			if strings.HasSuffix(low, suf) {
				encName, ret = encName[:len(encName)-len(suf)], "\r\n"
			}
		}
		if strings.HasSuffix(low, "-unix") {
			encName, ret = encName[:len(encName)-5], "\n"
		}
		var data []byte
		var err error
		if file == "" {
			data, err = readAllStdin()
		} else {
			data, err = os.ReadFile(file)
		}
		if err != nil {
			b.rerror("read-with-encoding-e: " + file + ": " + err.Error() + ".")
			return
		}
		var r strings.Builder
		le := strings.ToLower(encName)
		if le == "raw" || le == "binary" {
			r.WriteString(EscapeString(EscapeString(jenc.Decode(jenc.Latin1, data))))
		} else {
			e, ok := jenc.Lookup(encName)
			if !ok {
				b.rerror("read-with-encoding-e: no such encoding.")
				return
			}
			for _, l := range strings.SplitAfter(string(data), "\n") {
				if l == "" {
					continue
				}
				if ret != "" {
					l = strings.TrimSuffix(l, ret)
				} else {
					l = strings.TrimSuffix(l, "\n")
					l = strings.TrimSuffix(l, "\r")
				}
				r.WriteString(EscapeString(EscapeString(jenc.Decode(e, []byte(l)))) + "\n")
			}
		}
		b.rprint("# begin list\n" + r.String() + "\n# end\n")
	}
	u["write-with-encoding"] = func(b *Backend, a []string) {
		if len(a) < 2 {
			b.rerror("write-with-encoding: illegal arguments.")
			return
		}
		file, encName, l := a[0], a[1], a[2:]
		ret := "\n"
		low := strings.ToLower(encName)
		if strings.HasSuffix(low, "-crlf") || strings.HasSuffix(low, "-dos") {
			ret = "\r\n"
			encName = encName[:strings.LastIndex(encName, "-")]
		} else if strings.HasSuffix(low, "-unix") {
			encName = encName[:len(encName)-5]
		}
		le := strings.ToLower(encName)
		raw := le == "raw" || le == "binary"
		if !raw {
			if e, ok := jenc.Lookup(encName); !ok || e != jenc.UTF8 {
				b.rerror("write-with-encoding: no such encoding.")
				return
			}
		}
		var sb strings.Builder
		for _, s := range l {
			sb.WriteString(s)
			if !raw {
				sb.WriteString(ret)
			}
		}
		var err error
		if file == "" {
			_, err = b.Out.Write([]byte(sb.String()))
		} else {
			err = os.WriteFile(file, []byte(sb.String()), 0666)
		}
		if err != nil {
			b.rerror("write-with-encoding: " + file + ": " + err.Error() + ".")
			return
		}
		b.printUnit("ok")
	}
	u["set-filename-encoding"] = func(b *Backend, a []string) {
		if len(a) != 1 {
			b.rerror("set-filename-encoding: illegal arguments.")
			return
		}
		if _, ok := jenc.Lookup(a[0]); ok {
			b.printUnit("ok")
		} else {
			b.rerror("set-filename-encoding: No such encoding.")
		}
	}
}

func translitWithSpace(b *Backend, spec, s string) string {
	var sb strings.Builder
	rs := []rune(s)
	for i := 0; i < len(rs); {
		if isPerlSpace(rs[i]) {
			sb.WriteRune(rs[i])
			i++
			continue
		}
		j := i
		for j < len(rs) && !isPerlSpace(rs[j]) {
			j++
		}
		r, _ := b.Translit.Translit(spec, string(rs[i:j]))
		sb.WriteString(r)
		i = j
	}
	return sb.String()
}

func specString(s Spec) string {
	switch v := s.(type) {
	case string:
		return v
	case *SpecList:
		var p []string
		for _, it := range v.Items {
			p = append(p, specString(it))
		}
		return strings.Join(p, " ")
	case *Table:
		return "Naggy::Translit"
	case *FuncTranslit:
		return "Naggy::Translit::Function"
	}
	return ""
}

func sortedKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func itoa(i int) string { return perlNumString(float64(i)) }

// perlSplitLiteral emulates split(/\Q$m\E/s, $s).
func perlSplitLiteral(m, s string) []string {
	var r []string
	if m == "" {
		for _, c := range s {
			r = append(r, string(c))
		}
		return r
	}
	r = strings.Split(s, m)
	for len(r) > 0 && r[len(r)-1] == "" {
		r = r[:len(r)-1]
	}
	return r
}

func readAllStdin() ([]byte, error) {
	var buf []byte
	tmp := make([]byte, 4096)
	for {
		n, err := os.Stdin.Read(tmp)
		buf = append(buf, tmp[:n]...)
		if err != nil {
			return buf, nil
		}
	}
}

// TranslitWithSpace transliterates each non-space run of s by spec.
func (b *Backend) TranslitWithSpace(spec, s string) string {
	return translitWithSpace(b, spec, s)
}

// catfile joins like File::Spec->catfile (without cleaning "./").
func catfile(d, f string) string {
	if d == "" {
		return f
	}
	sep := string(filepath.Separator)
	d = strings.TrimRight(d, "/"+sep)
	if d == "" {
		return sep + f
	}
	return d + sep + f
}
