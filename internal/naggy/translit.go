package naggy

import (
	"fmt"
	"os"
	"regexp"
	"sort"
	"strings"

	"github.com/JRF-2018/naggy_win/internal/jenc"
	"github.com/JRF-2018/naggy_win/internal/ujconv"
)

// die is used like Perl's die inside commands; Backend.ProcessCommand
// recovers it and reports "# error".
type dieError struct{ msg string }

func die(format string, a ...interface{}) {
	panic(dieError{fmt.Sprintf(format, a...)})
}

// Spec is a transliteration specification: a string of table names
// separated by spaces, a *SpecList, or a Translator.
type Spec interface{}

// SpecList is an ordered list of specs (Perl array ref).
type SpecList struct{ Items []Spec }

// Translator is anything that can transliterate a string.
type Translator interface {
	Translit(s string) string
}

type cbWord struct {
	word string
	ci   bool
}

// Table is Naggy::Translit.
type Table struct {
	CaseSensitive bool
	maxLen        map[string]int
	table         map[string]string
	ideo          map[string]string
	compBeg       []cbWord
	autoFirst     *string
	autoLast      *string
	recursive     bool
	loaded        bool
	autoFile      string
	autoInversely bool
}

// NewTable returns an empty, loaded table.
func NewTable() *Table {
	return &Table{CaseSensitive: true, maxLen: map[string]int{},
		table: map[string]string{}, ideo: map[string]string{}, loaded: true}
}

// NewAutoLoader returns a table that is loaded on first use
// (Naggy::Translit::AutoLoader).
func NewAutoLoader(file string, inversely bool) *Table {
	t := NewTable()
	t.loaded = false
	t.autoFile = file
	t.autoInversely = inversely
	return t
}

func (t *Table) loadNow() {
	if t.loaded {
		return
	}
	if err := t.LoadFile(t.autoFile, t.autoInversely); err != nil {
		die("%s: %v", t.autoFile, err)
	}
	t.loaded = true
}

var (
	reCaseSens      = regexp.MustCompile(`(?i)^case[- ]sensitive$`)
	reCaseSensArg   = regexp.MustCompile(`(?i)^case[- ]sensitive\s+(\S.*)$`)
	reCaseInsens    = regexp.MustCompile(`(?i)^case[- ]insensitive$`)
	reCompBeg       = regexp.MustCompile(`(?i)^completion[- ]beginning\s+`)
	reAutoInsFirst  = regexp.MustCompile(`(?i)^auto[- ]insert[- ]first\s+(\S+)$`)
	reAutoInsLast   = regexp.MustCompile(`(?i)^auto[- ]insert[- ]last\s+(\S+)$`)
	reRecursive     = regexp.MustCompile(`(?i)^recursive(?:\s+(?:true|1))?$`)
	reTrlEncoding   = regexp.MustCompile(`(?i)^\s*\.\s*encoding\s+(\S+)\s*$`)
	reDotDirective  = regexp.MustCompile(`^\.\S`)
	reLeadingDotSpc = regexp.MustCompile(`^\s*\.`)
)

// LoadFromString is Naggy::Translit::load_from_string.
func (t *Table) LoadFromString(src string, inversely bool) {
	intable := false
	lines := perlSplitLines(src)
	for len(lines) > 0 {
		s, _ := NextLine(&lines)
		s = strings.TrimSpace(s)
		if !intable && reDotDirective.MatchString(s) {
			s = s[1:]
			switch {
			case reCaseSens.MatchString(s) ||
				(reCaseSensArg.MatchString(s) && IsTrue(reCaseSensArg.FindStringSubmatch(s)[1])):
				t.CaseSensitive = true
			case reCaseInsens.MatchString(s) ||
				(reCaseSensArg.MatchString(s) && !IsTrue(reCaseSensArg.FindStringSubmatch(s)[1])):
				t.CaseSensitive = false
			case reCompBeg.MatchString(s):
				rest := s[len(reCompBeg.FindString(s)):]
				for _, w := range splitWS(rest) {
					t.compBeg = append(t.compBeg, cbWord{UnescapeString(w), !t.CaseSensitive})
				}
			case reAutoInsFirst.MatchString(s):
				v := UnescapeString(reAutoInsFirst.FindStringSubmatch(s)[1])
				t.autoFirst = &v
			case reAutoInsLast.MatchString(s):
				v := UnescapeString(reAutoInsLast.FindStringSubmatch(s)[1])
				t.autoLast = &v
			case reRecursive.MatchString(s):
				t.recursive = true
			}
			s = ""
		}
		if s == "" {
			continue
		}
		if !intable && inversely {
			t.compBeg = nil
			t.autoFirst = nil
			t.autoLast = nil
			t.recursive = false
			t.CaseSensitive = true
		}
		intable = true

		f := splitWS(s)
		var from, to, ideo string
		hasTo, hasIdeo := false, false
		if len(f) > 0 {
			from = f[0]
		}
		if len(f) > 1 {
			to, hasTo = f[1], true
		}
		if len(f) > 2 {
			ideo, hasIdeo = f[2], true
		}
		if inversely {
			if !hasTo {
				// Perl: ($from, $to) = ($to, $from) with $to undef.
				continue
			}
			from, to = to, from
			hasIdeo = false
		}
		from = UnescapeString(from)
		if from == "" {
			continue
		}
		if hasTo {
			to = UnescapeString(to)
		} else {
			to = from
		}
		if hasIdeo {
			ideo = UnescapeString(ideo)
		}
		if !t.CaseSensitive {
			from = perlUC(from)
		}
		c := substrRunes(from, 0, 1)
		if _, ok := t.table[from]; !ok {
			if ml, ok := t.maxLen[c]; !ok || runeLen(from) > ml {
				t.maxLen[c] = runeLen(from)
			}
			t.table[from] = to
		}
		if hasIdeo {
			if _, ok := t.ideo[from]; !ok {
				t.ideo[from] = ideo
			}
		}
	}
}

// LoadFile loads a .trl or .wnd file (Naggy::Translit::load_file).
func (t *Table) LoadFile(f string, inversely bool) error {
	b, err := os.ReadFile(f)
	if err != nil {
		return err
	}
	if strings.HasSuffix(strings.ToLower(f), ".wnd") {
		src := jenc.Decode(jenc.CP932, b)
		src = strings.ReplaceAll(src, "\r\n", "\n")
		var sb strings.Builder
		for _, l := range strings.Split(src, "\n") {
			if strings.Contains(l, "\t") {
				p := strings.Split(l, "\t")
				to := ""
				if len(p) > 1 {
					to = p[1]
				}
				sb.WriteString(EscapeString(p[0]) + "\t" + EscapeString(to) + "\n")
			}
		}
		t.CaseSensitive = false
		t.LoadFromString(sb.String(), false)
		return nil
	}
	enc := jenc.UTF8
	for _, l := range strings.Split(string(b), "\n") {
		if strings.TrimSpace(l) == "" {
			break
		}
		if i := strings.IndexByte(l, '#'); i >= 0 {
			l = l[:i]
		}
		if strings.TrimSpace(l) == "" {
			continue
		}
		if !reLeadingDotSpc.MatchString(l) {
			break
		}
		if m := reTrlEncoding.FindStringSubmatch(l); m != nil {
			e, ok := jenc.Lookup(m[1])
			if !ok {
				return fmt.Errorf("unknown encoding %s", m[1])
			}
			enc = e
			break
		}
	}
	t.LoadFromString(jenc.Decode(enc, b), inversely)
	return nil
}

// Translit is Naggy::Translit::translit.
func (t *Table) Translit(src string) string {
	t.loadNow()
	if t.autoFirst != nil {
		src = *t.autoFirst + src
	}
	if t.autoLast != nil {
		src += *t.autoLast
	}
	for {
		prev := src
		var dest strings.Builder
		rs := []rune(src)
		for len(rs) > 0 {
			c := string(rs[0])
			if !t.CaseSensitive {
				c = perlUC(c)
			}
			ml, ok := t.maxLen[c]
			if !ok {
				dest.WriteString(c)
				rs = rs[1:]
				continue
			}
			done := false
			for l := ml; l > 0; l-- {
				if len(rs) < l {
					continue
				}
				from := string(rs[:l])
				if !t.CaseSensitive {
					from = perlUC(from)
				}
				if v, ok := t.table[from]; ok {
					dest.WriteString(v)
					rs = rs[l:]
					done = true
					break
				}
			}
			if !done {
				dest.WriteString(c)
				rs = rs[1:]
			}
		}
		src = dest.String()
		if !t.recursive || src == prev {
			break
		}
	}
	return src
}

// FuncTranslit wraps a Go function (Naggy::Translit::Function).
type FuncTranslit struct{ F func(string) string }

// Translit implements Translator.
func (f *FuncTranslit) Translit(s string) string { return f.F(s) }

var hebrewMarks = regexp.MustCompile(`[\x{591}-\x{5BB}\x{5BD}-\x{5C2}\x{5C4}-\x{5C7}]`)

func simplifyHebrew(s string) string {
	s = hebrewMarks.ReplaceAllString(s, "")
	s = strings.ReplaceAll(s, "װ", "וו")
	s = strings.ReplaceAll(s, "ױ", "וי")
	s = strings.ReplaceAll(s, "ײ", "יי")
	return s
}

// TranslitSet is Naggy::Backend::Translit: a named table of specs.
type TranslitSet struct {
	Table map[string]Spec
}

// NewTranslitSet returns the default set of built-in transliterations.
func NewTranslitSet() *TranslitSet {
	return &TranslitSet{Table: map[string]Spec{
		"identical":              &SpecList{},
		"hw-fw":                  &FuncTranslit{ujconv.H2Z},
		"hwkata-kata":            &FuncTranslit{ujconv.HWKata2Kata},
		"fw-hw":                  &FuncTranslit{ujconv.Z2H},
		"kata-hwkata":            &FuncTranslit{ujconv.Kata2HWKata},
		"hira-kata":              &FuncTranslit{ujconv.Hira2Kata},
		"kata-hira":              &FuncTranslit{ujconv.Kata2Hira},
		"hebrew-simplest_hebrew": &FuncTranslit{simplifyHebrew},
	}}
}

func visitKey(spec Spec) interface{} {
	return spec
}

// Translit applies spec to s.  ok is false when Perl would return undef.
func (ts *TranslitSet) Translit(spec Spec, s string) (string, bool) {
	visited := map[interface{}]bool{}
	var rec func(spec Spec, s string, ok bool) (string, bool)
	rec = func(spec Spec, s string, ok bool) (string, bool) {
		if !ok {
			return "", false
		}
		k := visitKey(spec)
		if visited[k] {
			return "", false
		}
		visited[k] = true
		switch v := spec.(type) {
		case *SpecList:
			for _, t := range v.Items {
				s, ok = rec(t, s, ok)
			}
			return s, ok
		case Translator:
			return v.Translit(s), true
		case string:
			for _, t := range splitWS(v) {
				sub, exists := ts.Table[t]
				if !exists {
					return "", false
				}
				s, ok = rec(sub, s, ok)
			}
			return s, ok
		}
		return "", false
	}
	return rec(spec, s, true)
}

// Rename renames a table entry and all references to it.
func (ts *TranslitSet) Rename(from, to string) bool {
	if _, ok := ts.Table[from]; !ok {
		return false
	}
	renStr := func(s string) string {
		f := splitWS(s)
		for i := range f {
			if f[i] == from {
				f[i] = to
			}
		}
		return strings.Join(f, " ")
	}
	var renList func(l *SpecList) *SpecList
	renList = func(l *SpecList) *SpecList {
		n := &SpecList{}
		for _, it := range l.Items {
			switch v := it.(type) {
			case *SpecList:
				n.Items = append(n.Items, renList(v))
			case string:
				n.Items = append(n.Items, renStr(v))
			default:
				n.Items = append(n.Items, v)
			}
		}
		return n
	}
	for k, spec := range ts.Table {
		switch v := spec.(type) {
		case *SpecList:
			if k == "identical" && len(v.Items) == 0 {
				continue
			}
			ts.Table[k] = renList(v)
		case string:
			ts.Table[k] = renStr(v)
		}
	}
	ts.Table[to] = ts.Table[from]
	delete(ts.Table, from)
	return true
}

// AlphaCharmap is Naggy::Backend::Translit::alpha_charmap.
func (ts *TranslitSet) AlphaCharmap(spec Spec) map[string]string {
	visited := map[interface{}]bool{}
	var rec func(spec Spec, cm map[string]string) map[string]string
	isASCIIKey := regexp.MustCompile(`^[\x21-\x7E]+$`)
	rec = func(spec Spec, cm map[string]string) map[string]string {
		if cm == nil {
			return nil
		}
		k := visitKey(spec)
		if visited[k] {
			return nil
		}
		visited[k] = true
		switch v := spec.(type) {
		case *SpecList:
			for _, t := range v.Items {
				cm = rec(t, cm)
				if cm == nil {
					return nil
				}
			}
			return cm
		case Translator:
			for key, val := range cm {
				cm[key] = v.Translit(val)
			}
			if tb, ok := v.(*Table); ok {
				tb.loadNow()
				for key, val := range tb.table {
					if !isASCIIKey.MatchString(key) {
						continue
					}
					if _, ok := cm[key]; ok {
						continue
					}
					if id, ok := tb.ideo[key]; ok {
						val = id
					}
					cm[key] = val
				}
			}
			return cm
		case string:
			for _, t := range splitWS(v) {
				sub, ok := ts.Table[t]
				if !ok {
					return nil
				}
				cm = rec(sub, cm)
				if cm == nil {
					return nil
				}
			}
			return cm
		}
		return nil
	}
	return rec(spec, map[string]string{})
}

func (ts *TranslitSet) completionBeginning(spec Spec) ([]cbWord, bool) {
	visited := map[interface{}]bool{}
	var rec func(spec Spec, re []cbWord, ok bool) ([]cbWord, bool)
	rec = func(spec Spec, re []cbWord, ok bool) ([]cbWord, bool) {
		k := visitKey(spec)
		if visited[k] {
			return nil, false
		}
		visited[k] = true
		switch v := spec.(type) {
		case *SpecList:
			for _, t := range v.Items {
				re, ok = rec(t, re, ok)
			}
			return re, ok
		case Translator:
			if tb, isTb := v.(*Table); isTb {
				tb.loadNow()
				if len(tb.compBeg) > 0 {
					return append(append([]cbWord{}, re...), tb.compBeg...), true
				}
			}
			return re, ok
		case string:
			for _, t := range splitWS(v) {
				sub, exists := ts.Table[t]
				if !exists {
					return nil, false
				}
				re, ok = rec(sub, re, ok)
			}
			return re, ok
		}
		return nil, false
	}
	re, _ := rec(spec, nil, true)
	return re, len(re) > 0
}

// findCompBeg finds the leftmost match like a Perl alternation regex.
func findCompBeg(words []cbWord, s []rune) (start, end int, found bool) {
	for pos := 0; pos <= len(s); pos++ {
		for _, w := range words {
			wr := []rune(w.word)
			if pos+len(wr) > len(s) || len(wr) == 0 {
				continue
			}
			seg := string(s[pos : pos+len(wr)])
			if seg == w.word || (w.ci && strings.EqualFold(seg, w.word)) {
				return pos, pos + len(wr), true
			}
		}
	}
	return 0, 0, false
}

// Completion is the result of Complete.
type Completion struct {
	Prefix     string
	Word       string
	Completion *string
	Map        map[string]string
}

// Complete is Naggy::Backend::Translit::complete.
func (ts *TranslitSet) Complete(spec Spec, s string) *Completion {
	compbeg, hasCB := ts.completionBeginning(spec)
	charmap := ts.AlphaCharmap(spec)
	if charmap == nil {
		return nil
	}
	prefix := ""
	if hasCB {
		rs := []rune(s)
		var match *string
		for {
			st, en, ok := findCompBeg(compbeg, rs)
			if !ok {
				break
			}
			if match != nil {
				prefix += *match
			}
			prefix += string(rs[:st])
			m := string(rs[st:en])
			match = &m
			rs = rs[en:]
		}
		s = string(rs)
		if match != nil {
			s = *match + s
		}
	}
	r := map[string]string{}
	var compl, ex *string
	keys := make([]string, 0, len(charmap))
	for k := range charmap {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	ls := runeLen(s)
	for _, k := range keys {
		if perlUC(substrRunes(k, 0, ls)) != perlUC(s) {
			continue
		}
		r[k] = charmap[k]
		if perlUC(k) == perlUC(s) {
			if ex == nil || k == s {
				kk := k
				ex = &kk
			}
			continue
		}
		if compl == nil {
			c := s + string([]rune(k)[ls:])
			compl = &c
		} else {
			if runeLen(*compl) <= ls {
				continue
			}
			if runeLen(k) < runeLen(*compl) {
				c := substrRunes(*compl, 0, runeLen(k))
				compl = &c
			}
			for l := ls + 1; runeLen(*compl) >= l && runeLen(k) >= l; l++ {
				if perlUC(substrRunes(k, 0, l)) != perlUC(substrRunes(*compl, 0, l)) {
					c := substrRunes(*compl, 0, l-1)
					compl = &c
					break
				}
			}
		}
	}
	if ex != nil && compl == nil {
		compl = ex
	}
	if len(r) == 0 {
		r = charmap
	}
	return &Completion{Prefix: prefix, Word: s, Completion: compl, Map: r}
}
