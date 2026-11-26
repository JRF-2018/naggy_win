package naggy

import (
	"bytes"
	"fmt"
	"os"
	"regexp"
	"sort"
	"strings"
	"sync"

	"github.com/JRF-2018/naggy_win/internal/jenc"
	"github.com/JRF-2018/naggy_win/internal/ujconv"
)

// The Perl version uses SDBM index files (*.sdb.dir / *.sdb.pag) made by
// make_*_dic_db.pl.  This port builds the same index in memory when the
// dictionary is loaded, so no index files are necessary.

type span struct{ off, size int }

type dicCache struct {
	mu sync.Mutex
	m  map[string]interface{}
}

var cache = dicCache{m: map[string]interface{}{}}

// KEYBOARD_PREF_ORDER of naggy-backend.pl.
var KeyboardPrefOrder = [40]int{
	39, 37, 33, 31, 35, 34, 30, 32, 36, 38,
	27, 21, 11, 9, 13, 12, 8, 10, 20, 26,
	25, 7, 5, 1, 3, 2, 0, 4, 6, 24,
	29, 23, 17, 15, 19, 18, 14, 16, 22, 28,
}

var keyboardPrefOrderInv [40]int

func init() {
	for i := 0; i < 40; i++ {
		keyboardPrefOrderInv[KeyboardPrefOrder[i]] = i
	}
}

// SKKDic is Naggy::SKKDic.
type SKKDic struct {
	Filename string
	enc      jenc.Encoding
	data     []byte
	index    map[string]span
}

func cacheKey(kind, f string, e jenc.Encoding, st os.FileInfo) string {
	return fmt.Sprintf("%s\x00%s\x00%d\x00%d\x00%d", kind, f, e, st.ModTime().UnixNano(), st.Size())
}

// NewSKKDic loads an SKK dictionary.
func NewSKKDic(filename string, e jenc.Encoding) (*SKKDic, error) {
	st, err := os.Stat(filename)
	if err != nil {
		return nil, err
	}
	ck := cacheKey("skk", filename, e, st)
	cache.mu.Lock()
	defer cache.mu.Unlock()
	if d, ok := cache.m[ck]; ok {
		return d.(*SKKDic), nil
	}
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, err
	}
	d := &SKKDic{Filename: filename, enc: e, data: data, index: map[string]span{}}
	pos := 0
	for pos < len(data) {
		end := bytes.IndexByte(data[pos:], '\n')
		if end < 0 {
			end = len(data)
		} else {
			end = pos + end + 1
		}
		line := data[pos:end]
		// Only the reading (up to the first white space) is decoded.
		t := bytes.TrimLeft(line, " \t\r\n\f")
		if len(t) > 0 && t[0] != ';' {
			k := t
			if i := bytes.IndexAny(t, " \t\r\n\f"); i >= 0 {
				k = t[:i]
			}
			key := ujconv.DicKey(jenc.Decode(e, k))
			d.index[key] = span{pos, end - pos}
		}
		pos = end
	}
	cache.m[ck] = d
	return d, nil
}

// SKKCand is a candidate with its annotation.
type SKKCand struct {
	Word    string
	Comment *string
}

func (d *SKKDic) rawConvert(key string) []SKKCand {
	sp, ok := d.index[key]
	if !ok {
		return nil
	}
	s := jenc.Decode(d.enc, d.data[sp.off:sp.off+sp.size])
	s = strings.TrimSpace(s)
	f := strings.FieldsFunc(s, isPerlSpace)
	if len(f) < 2 {
		return nil
	}
	// split(/\s+/, $s, 2)
	i := strings.IndexFunc(s, isPerlSpace)
	rest := strings.TrimLeftFunc(s[i:], isPerlSpace)
	var r []SKKCand
	for _, c := range strings.Split(rest, "/") {
		if c == "" {
			continue
		}
		p := strings.Split(c, ";")
		// Perl's split drops trailing empty fields.
		for len(p) > 0 && p[len(p)-1] == "" {
			p = p[:len(p)-1]
		}
		if len(p) == 0 || p[0] == "" {
			continue
		}
		cand := SKKCand{Word: p[0]}
		if len(p) > 1 {
			cm := p[1]
			cand.Comment = &cm
		}
		r = append(r, cand)
	}
	return r
}

var kana2okuri = map[string]string{
	"あ": "a", "い": "i", "う": "u", "え": "e", "お": "o",
	"か": "k", "き": "k", "く": "k", "け": "k", "こ": "k",
	"さ": "s", "し": "s", "す": "s", "せ": "s", "そ": "s",
	"た": "t", "ち": "t", "つ": "t", "て": "t", "と": "t",
	"な": "n", "に": "n", "ぬ": "n", "ね": "n", "の": "n",
	"は": "h", "ひ": "h", "ふ": "h", "へ": "h", "ほ": "h",
	"ま": "m", "み": "m", "む": "m", "め": "m", "も": "m",
	"や": "y", "ゆ": "y", "よ": "y",
	"ら": "r", "り": "r", "る": "r", "れ": "r", "ろ": "r",
	"わ": "w", "ゐ": "w", "ゑ": "w", "を": "w",
	"が": "g", "ぎ": "g", "ぐ": "g", "げ": "g", "ご": "g",
	"ざ": "z", "じ": "z", "ず": "z", "ぜ": "z", "ぞ": "z",
	"だ": "d", "ぢ": "d", "づ": "d", "で": "d", "ど": "d",
	"ば": "b", "び": "b", "ぶ": "b", "べ": "b", "ぼ": "b",
	"ぱ": "p", "ぴ": "p", "ぷ": "p", "ぺ": "p", "ぽ": "p",
	"う゛": "v",
}

// Convert is Naggy::SKKDic::convert.
func (d *SKKDic) Convert(orig, yomi string) []SKKCand {
	var r []SKKCand
	r = append(r, d.rawConvert(orig)...)
	r = append(r, d.rawConvert(ujconv.DicKey(yomi))...)
	y := []rune(yomi)
	for pos := len(y) - 1; pos > 0; pos-- {
		o := string(y[pos])
		pre := string(y[:pos])
		post := string(y[pos:])
		if (o == "゛" || o == "゜") && pos > 2 {
			o2 := string(y[pos-1 : pos+1])
			if _, ok := kana2okuri[o2]; ok {
				pos--
				o = o2
				pre = string(y[:pos])
				post = string(y[pos:])
			}
		} else if o == "っ" && pos < len(y)-1 {
			o2 := string(y[pos+1])
			if v, ok := kana2okuri[o2]; ok && !strings.ContainsAny(v, "aiueo") {
				o = o2
			}
		}
		if ok, has := kana2okuri[o]; has {
			key := ujconv.DicKey(pre + ok)
			for _, c := range d.rawConvert(key) {
				r = append(r, SKKCand{Word: c.Word + post, Comment: c.Comment})
			}
		}
	}
	return r
}

// TankanjiCand is a tankanji candidate with its keyboard position.
type TankanjiCand struct {
	Char string
	Pos  int
}

// TankanjiDic is Naggy::TankanjiDic.
type TankanjiDic struct {
	Filename string
	enc      jenc.Encoding
	sjisHack bool
	data     []byte
	index    map[string]span
}

var reYomi = regexp.MustCompile(`^#\s*YOMI[ :]`)

// NewTankanjiDic loads a tankanji dictionary.
func NewTankanjiDic(filename string, e jenc.Encoding) (*TankanjiDic, error) {
	st, err := os.Stat(filename)
	if err != nil {
		return nil, err
	}
	ck := cacheKey("tankanji", filename, e, st)
	cache.mu.Lock()
	defer cache.mu.Unlock()
	if d, ok := cache.m[ck]; ok {
		return d.(*TankanjiDic), nil
	}
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, err
	}
	d := &TankanjiDic{Filename: filename, enc: e, sjisHack: e == jenc.CP932,
		data: data, index: map[string]span{}}
	var curYomi *string
	curPos, tail := 0, 0
	pos := 0
	for {
		last := pos >= len(data)
		var line []byte
		end := pos
		if !last {
			i := bytes.IndexByte(data[pos:], '\n')
			if i < 0 {
				end = len(data)
			} else {
				end = pos + i + 1
			}
			line = data[pos:end]
		}
		var s string
		if !last {
			s = jenc.Decode(e, line)
		}
		if last || reYomi.MatchString(s) {
			if curYomi != nil {
				size := tail - curPos
				if size < 0 {
					size = 0
				}
				d.index[*curYomi] = span{curPos, size}
			}
			if last {
				break
			}
			ny := strings.TrimSpace(s[len(reYomi.FindString(s)):])
			ny = ujconv.DicKey(ny)
			curYomi = &ny
			curPos = pos
			pos = end
			continue
		}
		pos = end
		if strings.HasPrefix(s, "#") {
			continue
		}
		s = strings.TrimSuffix(s, "\n")
		s = strings.TrimSuffix(s, "\r")
		s = strings.TrimSuffix(s, "\x1a")
		if s == "" {
			continue
		}
		tail = end
	}
	cache.m[ck] = d
	return d, nil
}

func isASCIIPrintable(t string) bool {
	return len(t) == 1 && t[0] >= 0x20 && t[0] <= 0x7e
}

func (d *TankanjiDic) rawConvert(key string) []TankanjiCand {
	sp, ok := d.index[key]
	if !ok {
		return nil
	}
	block := d.data[sp.off : sp.off+sp.size]
	var r []TankanjiCand
	l := 0
	for _, raw := range bytes.Split(block, []byte{'\n'}) {
		raw = bytes.TrimSuffix(raw, []byte{'\r'})
		raw = bytes.TrimSuffix(raw, []byte{0x1a})
		if len(raw) == 0 || raw[0] == '#' {
			continue
		}
		if !d.sjisHack {
			toks := jenc.DecodeTokens(d.enc, raw)
			x := 0
			for pos := 0; x < 10 && pos < len(toks); pos++ {
				if pos < len(toks)-1 && isASCIIPrintable(toks[pos]) && isASCIIPrintable(toks[pos+1]) {
					pos++
					x++
					continue
				}
				r = append(r, TankanjiCand{toks[pos], l*10 + x})
				x++
			}
		} else {
			x := 0
			s := raw
			for len(s) > 0 {
				f := int(s[0])
				f2 := 0
				if len(s) > 1 {
					f2 = int(s[1])
				}
				c := s
				if len(s) >= 2 {
					c = s[:2]
					s = s[2:]
				} else {
					s = nil
				}
				if f <= 0x7e && f >= 0x20 {
					x++
				} else if f == 0x86 && f2 >= 0xA2 && f2 <= 0xED {
					r = append(r, TankanjiCand{string(rune(0x2500 - 0xa2 + f2)), l*10 + x})
					x++
				} else {
					r = append(r, TankanjiCand{jenc.Decode(jenc.CP932, c), l*10 + x})
					x++
				}
			}
		}
		l++
	}
	sortKey := func(p int) int { return p/40*40 + KeyboardPrefOrder[p%40] }
	sort.SliceStable(r, func(i, j int) bool { return sortKey(r[i].Pos) < sortKey(r[j].Pos) })
	return r
}

func (d *TankanjiDic) merge(l []TankanjiCand) []TankanjiCand {
	m := map[int]string{}
	var r []TankanjiCand
	for _, x := range l {
		pos := x.Pos
		for {
			v, ok := m[pos]
			if !ok || v == x.Char {
				break
			}
			pos += 40
		}
		if _, ok := m[pos]; !ok {
			m[pos] = x.Char
			r = append(r, TankanjiCand{x.Char, pos})
		}
	}
	return r
}

var reAlnum = regexp.MustCompile(`^[0-9A-Za-z]+$`)

// Convert is Naggy::TankanjiDic::convert.
func (d *TankanjiDic) Convert(orig, yomi string) []TankanjiCand {
	if reAlnum.MatchString(orig) {
		r1 := d.rawConvert(ujconv.DicKey(yomi))
		r2 := d.rawConvert(orig)
		if len(r1) > 0 && len(r2) > 0 {
			return d.merge(append(r1, r2...))
		}
		return append(r1, r2...)
	}
	if r := d.rawConvert(orig); len(r) > 0 {
		return r
	}
	if r := d.rawConvert(ujconv.DicKey(yomi)); len(r) > 0 {
		return r
	}
	if strings.ContainsAny(yomi, "、。，．") {
		y := strings.Map(func(c rune) rune {
			switch c {
			case '、':
				return '，'
			case '。':
				return '．'
			case '，':
				return '、'
			case '．':
				return '。'
			}
			return c
		}, yomi)
		if r := d.rawConvert(ujconv.DicKey(y)); len(r) > 0 {
			return r
		}
	}
	return nil
}
