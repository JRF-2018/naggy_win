package naggy

import (
	"regexp"
	"strconv"
	"strings"
)

// Candidate is one conversion candidate: [d, pos, hlit, com].
type Candidate struct {
	Word      string
	Pos       int
	Highlight bool
	Comment   *string
}

// ConvertResult holds the result of a conversion.  Exactly one of the
// following holds: Null (Perl's (undef)), String != nil, or a List.
type ConvertResult struct {
	Null   bool
	String *string
	List   []Candidate
}

// ConvertMode is an entry of the conversion name table:
// ["translit", spec] or ["convert", "skk"|"tankanji"|"unicode"].
type ConvertMode struct {
	Kind string
	Arg  string
}

// Converter is Naggy::Backend::Convert.
type Converter struct {
	ngb         *Backend
	TankanjiDic *TankanjiDic
	SKKDics     []*SKKDic
	Table       map[string]ConvertMode
}

func newConverter(ngb *Backend) *Converter {
	return &Converter{ngb: ngb, Table: map[string]ConvertMode{}}
}

var (
	reUpper = regexp.MustCompile(`[A-Z]`)
	reLower = regexp.MustCompile(`[a-z]`)
)

func (cv *Converter) makeRomanFilter(s string) *regexp.Regexp {
	ngb := cv.ngb
	rs := []rune(s)
	if len(rs) == 0 {
		return nil
	}
	var l []string
	c := string(rs[0])
	rest := rs[1:]
	for {
		i := -1
		for j, r := range rest {
			if r >= 'A' && r <= 'Z' {
				i = j
				break
			}
		}
		if i < 0 {
			break
		}
		l = append(l, c+string(rest[:i]))
		c = string(rest[i])
		rest = rest[i+1:]
	}
	l = append(l, c+string(rest))
	if len(l) <= 1 {
		return nil
	}
	a, b := "", ""
	const nonHira = `[^\x{3041}-\x{3096}]+`
	for i, p := range l {
		x, _ := ngb.Translit.Translit(ngb.ConvertTranslit, p)
		if i%2 == 1 {
			a += nonHira
			b += regexp.QuoteMeta(x)
		} else {
			a += regexp.QuoteMeta(x)
			b += nonHira
		}
	}
	return regexp.MustCompile(`^(?:` + a + `|` + b + `)$`)
}

var reFilterStrip = regexp.MustCompile(`[\x20-\x7e\x{3041}-\x{3093}\x{309b}\x{309c}\x{30a1}-\x{30F4}\x{30fc}]`)

type convPart struct {
	orig string
	yomi string
}

func (cv *Converter) japaneseConvert(mode, s string) []Candidate {
	ngb := cv.ngb
	useRomanHint := false
	if v, ok := ngb.InitVar["JAPANESE_ROMAN_HINT"]; ok && IsTrue(v) {
		useRomanHint = true
	}
	capitalizedTankanji := ngb.InitVar["CAPITALIZED_TANKANJI"] == "skk"

	l := strings.Split(s, ":")
	var r []string
	for len(l) > 0 {
		c := l[0]
		l = l[1:]
		if c == "" && len(l) > 0 && len(r) > 0 {
			r[len(r)-1] = r[len(r)-1] + ":" + l[0]
			l = l[1:]
		} else {
			r = append(r, c)
		}
	}
	if len(r) == 2 && r[0] == "" && r[1] == "" {
		r = []string{""}
	}
	var parts []convPart
	for _, c := range r {
		d := ""
		if c != "" {
			d, _ = ngb.Translit.Translit(ngb.ConvertTranslit, c)
		}
		parts = append(parts, convPart{c, d})
	}

	c := parts[0]
	filter := parts[1:]
	modeChange := false
	if c.orig == "" && len(filter) > 0 {
		c = filter[0]
		filter = filter[1:]
		modeChange = true
	} else if capitalizedTankanji && mode == "tankanji" && len(c.orig) > 0 &&
		((c.orig[0] >= 'A' && c.orig[0] <= 'Z') || c.orig[0] == '@') {
		c.orig = strings.ToLower(c.orig[:1]) + c.orig[1:]
		modeChange = true
	}
	if len(filter) > 0 && filter[len(filter)-1].orig == "" {
		filter = filter[:len(filter)-1]
		modeChange = !modeChange
	}
	if modeChange {
		if mode == "skk" && cv.TankanjiDic != nil {
			mode = "tankanji"
		} else {
			mode = "skk"
		}
	}

	var res []Candidate
	idx := map[string]int{}
	if mode == "skk" {
		for _, sdic := range cv.SKKDics {
			for _, d := range sdic.Convert(c.orig, c.yomi) {
				if j, ok := idx[d.Word]; ok {
					if res[j].Comment == nil {
						res[j].Comment = d.Comment
					}
				} else {
					i := len(res)
					i = i/40*40 + keyboardPrefOrderInv[i%40]
					res = append(res, Candidate{d.Word, i, false, d.Comment})
					idx[d.Word] = len(res) - 1
				}
			}
		}
		if cv.TankanjiDic != nil {
			for _, d := range cv.TankanjiDic.Convert(c.orig, c.yomi) {
				if _, ok := idx[d.Char]; !ok {
					i := len(res)
					i = i/40*40 + keyboardPrefOrderInv[i%40]
					res = append(res, Candidate{d.Char, i, false, nil})
					idx[d.Char] = len(res) - 1
				}
			}
		}
	} else {
		for _, d := range cv.TankanjiDic.Convert(c.orig, c.yomi) {
			res = append(res, Candidate{d.Char, d.Pos, false, nil})
			if _, ok := idx[d.Char]; !ok {
				idx[d.Char] = len(res) - 1
			}
		}
	}

	var rfilter *regexp.Regexp
	if useRomanHint && reUpper.MatchString(c.orig) && reLower.MatchString(c.orig) {
		rfilter = cv.makeRomanFilter(c.orig)
	}
	if rfilter != nil {
		for i := range res {
			if rfilter.MatchString(res[i].Word) {
				res[i].Highlight = true
			}
		}
	} else if len(filter) > 0 {
		for i := range res {
			res[i].Highlight = true
		}
	}

	for _, fc := range filter {
		set := map[rune]bool{}
		var x strings.Builder
		var rf *regexp.Regexp
		if useRomanHint && reUpper.MatchString(fc.orig) && reLower.MatchString(fc.orig) {
			rf = cv.makeRomanFilter(fc.orig)
		}
		add := func(w string) {
			if rf == nil || rf.MatchString(w) {
				x.WriteString(w)
			}
		}
		for _, sdic := range cv.SKKDics {
			for _, d := range sdic.Convert(fc.orig, fc.yomi) {
				add(d.Word)
			}
		}
		if cv.TankanjiDic != nil {
			for _, d := range cv.TankanjiDic.Convert(fc.orig, fc.yomi) {
				add(d.Char)
			}
		}
		for _, ch := range reFilterStrip.ReplaceAllString(x.String(), "") {
			set[ch] = true
		}
		for i := range res {
			h := false
			for _, ch := range reFilterStrip.ReplaceAllString(res[i].Word, "") {
				if set[ch] {
					h = true
					break
				}
			}
			res[i].Highlight = res[i].Highlight && h
		}
	}

	var hl, rest []Candidate
	for _, x := range res {
		if x.Highlight {
			hl = append(hl, x)
		} else {
			rest = append(rest, x)
		}
	}
	return append(hl, rest...)
}

var reModify = regexp.MustCompile(`#([^#]+)$`)

// Convert is Naggy::Backend::Convert::convert.
func (cv *Converter) Convert(s string) ConvertResult {
	ngb := cv.ngb
	useModify := false
	if v, ok := ngb.InitVar["ALLOW_MODIFY_CONVERT"]; ok && IsTrue(v) {
		useModify = true
	}
	modeName := "tankanji"
	if v, ok := ngb.InitVar["DEFAULT_CONVERT"]; ok {
		modeName = v
	}
	if useModify {
		if m := reModify.FindStringSubmatchIndex(s); m != nil {
			modeName = s[m[2]:m[3]]
			s = s[:m[0]]
		}
	}
	mode, ok := cv.Table[modeName]
	if !ok {
		ngb.rwarn(modeName + ": No coversion infomation.")
		return ConvertResult{Null: true}
	}
	if mode.Kind == "convert" && mode.Arg == "tankanji" && cv.TankanjiDic == nil {
		mode = ConvertMode{"convert", "skk"}
	}
	switch mode.Kind {
	case "convert":
		switch mode.Arg {
		case "unicode":
			h := s
			for i, c := range s {
				if !strings.ContainsRune("0123456789abcdefABCDEF", c) {
					h = s[:i]
					break
				}
			}
			v, err := strconv.ParseUint(h, 16, 32)
			if err != nil {
				v = 0
			}
			r := chrHex(strconv.FormatUint(v, 16))
			return ConvertResult{String: &r}
		case "skk", "tankanji":
			return ConvertResult{List: cv.japaneseConvert(mode.Arg, s)}
		}
	case "translit":
		r, ok := ngb.Translit.Translit(mode.Arg, s)
		if !ok {
			ngb.rwarn(mode.Arg + ": failed to translit.")
			return ConvertResult{Null: true}
		}
		return ConvertResult{String: &r}
	}
	die("Naggy::Backend::Convert: Unreacheable code!")
	return ConvertResult{}
}
