// Package ujconv reproduces the character conversions of the Perl module
// Unicode::Japanese (z2h, h2z, hira2kata, kata2hira) that naggy-backend.pl
// relies on.  The tables are generated from the real module.
package ujconv

import "strings"

func mapRunes(s string, t map[string]string) string {
	var sb strings.Builder
	for _, c := range s {
		cs := string(c)
		if v, ok := t[cs]; ok {
			sb.WriteString(v)
		} else {
			sb.WriteString(cs)
		}
	}
	return sb.String()
}

// Z2H converts full-width characters to half-width (Unicode::Japanese z2h).
func Z2H(s string) string { return mapRunes(s, z2hTable) }

// H2Z converts half-width characters to full-width (Unicode::Japanese h2z),
// combining half-width kana with following voiced sound marks.
func H2Z(s string) string {
	rs := []rune(s)
	var sb strings.Builder
	for i := 0; i < len(rs); i++ {
		if i+1 < len(rs) {
			if v, ok := h2zPairTable[string(rs[i:i+2])]; ok {
				sb.WriteString(v)
				i++
				continue
			}
		}
		cs := string(rs[i])
		if v, ok := h2zTable[cs]; ok {
			sb.WriteString(v)
		} else {
			sb.WriteString(cs)
		}
	}
	return sb.String()
}

// Hira2Kata converts hiragana to katakana.
func Hira2Kata(s string) string { return mapRunes(s, hira2kataTable) }

// Kata2Hira converts katakana to hiragana.
func Kata2Hira(s string) string { return mapRunes(s, kata2hiraTable) }

// DicKey is the normalization naggy uses for dictionary keys:
// Unicode::Japanese->new($s)->hira2kata->z2h.
func DicKey(s string) string { return Z2H(Hira2Kata(s)) }

func inRange(c rune, ranges [][2]rune) bool {
	for _, r := range ranges {
		if c >= r[0] && c <= r[1] {
			return true
		}
	}
	return false
}

func mapRuns(s string, ranges [][2]rune, f func(string) string) string {
	var sb strings.Builder
	rs := []rune(s)
	for i := 0; i < len(rs); {
		if inRange(rs[i], ranges) {
			j := i
			for j < len(rs) && inRange(rs[j], ranges) {
				j++
			}
			sb.WriteString(f(string(rs[i:j])))
			i = j
		} else {
			sb.WriteRune(rs[i])
			i++
		}
	}
	return sb.String()
}

// HWKata2Kata converts runs of half-width katakana to full-width.
func HWKata2Kata(s string) string {
	return mapRuns(s, [][2]rune{{0xFF61, 0xFF9F}}, H2Z)
}

// Kata2HWKata converts runs of full-width katakana (and kana punctuation)
// to half-width.
func Kata2HWKata(s string) string {
	return mapRuns(s, [][2]rune{{0x3001, 0x301C}, {0x3099, 0x309E}, {0x30A1, 0x30FE}}, Z2H)
}
