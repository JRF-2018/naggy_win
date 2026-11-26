// Package jenc decodes the Japanese encodings used by naggy dictionaries
// (UTF-8, EUC-JP / EUC-JIS-2004 and CP932) without external dependencies.
//
// "euc-jp" is decoded as EUC-JIS-2004 (a superset of EUC-JP for JIS X 0208)
// and, for 3-byte (0x8F) codes not defined in JIS X 0213 plane 2, as
// JIS X 0212.  The two sets use disjoint rows, so this hybrid decodes both
// classic EUC-JP and euc-jisx0213 files correctly.
package jenc

import (
	"bytes"
	"compress/zlib"
	_ "embed"
	"encoding/binary"
	"io"
	"strings"
	"sync"
	"unicode/utf8"
)

//go:embed jtables.bin
var tablesZ []byte

var (
	once        sync.Once
	cp932Single []uint32
	cp932Double []uint32
	eucPlane1   []uint32
	eucPlane2   []uint32
	jis0212     []uint32
	sequences   []string
)

func load() {
	r, err := zlib.NewReader(bytes.NewReader(tablesZ))
	if err != nil {
		panic(err)
	}
	raw, err := io.ReadAll(r)
	if err != nil {
		panic(err)
	}
	words := make([]uint32, len(raw)/4)
	for i := range words {
		words[i] = binary.LittleEndian.Uint32(raw[i*4:])
	}
	p := 0
	take := func(n int) []uint32 { s := words[p : p+n]; p += n; return s }
	cp932Single = take(256)
	cp932Double = take(124 * 189)
	eucPlane1 = take(94 * 94)
	eucPlane2 = take(94 * 94)
	jis0212 = take(94 * 94)
	n := int(take(1)[0])
	sequences = make([]string, n)
	for i := 0; i < n; i++ {
		l := int(take(1)[0])
		var sb strings.Builder
		for _, c := range take(l) {
			sb.WriteRune(rune(c))
		}
		sequences[i] = sb.String()
	}
}

func valueString(v uint32) (string, bool) {
	if v == 0 {
		return "", false
	}
	if v&0x80000000 != 0 {
		return sequences[v&0x7fffffff], true
	}
	return string(rune(v)), true
}

// Encoding identifies a supported encoding.
type Encoding int

const (
	UTF8 Encoding = iota
	EUCJP
	CP932
	Latin1
)

// Lookup returns the Encoding for a name as used by Perl's Encode module.
func Lookup(name string) (Encoding, bool) {
	n := strings.ToLower(strings.TrimSpace(name))
	n = strings.ReplaceAll(n, "_", "-")
	switch n {
	case "utf8", "utf-8", "utf-8-strict":
		return UTF8, true
	case "euc-jp", "eucjp", "euc-jisx0213", "euc-jis-2004", "ujis":
		return EUCJP, true
	case "cp932", "shiftjis", "shift-jis", "sjis", "windows-31j", "ms932":
		return CP932, true
	case "latin1", "latin-1", "iso-8859-1", "binary", "raw":
		return Latin1, true
	}
	return UTF8, false
}

// DecodeTokens decodes b and returns one token per source character.
// A token is usually one rune, but a JIS X 0213 character that maps to a
// Unicode sequence (e.g. U+304B U+309A) is returned as a single token.
func DecodeTokens(e Encoding, b []byte) []string {
	once.Do(load)
	var r []string
	switch e {
	case UTF8:
		for len(b) > 0 {
			c, n := utf8.DecodeRune(b)
			r = append(r, string(c))
			b = b[n:]
		}
	case Latin1:
		for _, c := range b {
			r = append(r, string(rune(c)))
		}
	case EUCJP:
		for len(b) > 0 {
			c := b[0]
			switch {
			case c < 0x80:
				r = append(r, string(rune(c)))
				b = b[1:]
			case c == 0x8E && len(b) >= 2 && b[1] >= 0xA1 && b[1] <= 0xDF:
				r = append(r, string(rune(0xFF61+int(b[1])-0xA1)))
				b = b[2:]
			case c == 0x8F && len(b) >= 3 && b[1] >= 0xA1 && b[1] <= 0xFE && b[2] >= 0xA1 && b[2] <= 0xFE:
				idx := int(b[1]-0xA1)*94 + int(b[2]-0xA1)
				if s, ok := valueString(eucPlane2[idx]); ok {
					r = append(r, s)
				} else if s, ok := valueString(jis0212[idx]); ok {
					r = append(r, s)
				} else {
					r = append(r, "�")
				}
				b = b[3:]
			case c >= 0xA1 && c <= 0xFE && len(b) >= 2 && b[1] >= 0xA1 && b[1] <= 0xFE:
				idx := int(c-0xA1)*94 + int(b[1]-0xA1)
				if s, ok := valueString(eucPlane1[idx]); ok {
					r = append(r, s)
				} else {
					r = append(r, "�")
				}
				b = b[2:]
			default:
				r = append(r, "�")
				b = b[1:]
			}
		}
	case CP932:
		for len(b) > 0 {
			c := b[0]
			if (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC) {
				if len(b) >= 2 && b[1] >= 0x40 && b[1] <= 0xFC {
					if v := cp932Double[int(c-0x81)*189+int(b[1]-0x40)]; v != 0 {
						r = append(r, string(rune(v)))
						b = b[2:]
						continue
					}
					// Like Perl's Encode, an undefined pair consumes only
					// the lead byte when the trail byte is a character
					// by itself.
					if t := b[1]; !(t < 0x80 || (t >= 0xA1 && t <= 0xDF)) {
						r = append(r, "�")
						b = b[2:]
						continue
					}
				}
				r = append(r, "�")
				b = b[1:]
				continue
			}
			if v := cp932Single[c]; v != 0 || c == 0 {
				r = append(r, string(rune(v)))
			} else {
				r = append(r, "�")
			}
			b = b[1:]
		}
	}
	return r
}

// Decode decodes b into a string.
func Decode(e Encoding, b []byte) string {
	if e == UTF8 {
		return strings.ToValidUTF8(string(b), "�")
	}
	return strings.Join(DecodeTokens(e, b), "")
}
