// Package naggy is a Go port of naggy-backend.pl (the backend of
// quail-naggy.el).  It reproduces the command protocol, the init-file
// language, transliteration and the tankanji/SKK conversion.
package naggy

import (
	"fmt"
	"math"
	"regexp"
	"strconv"
	"strings"
	"unicode/utf8"
)

// EscapeString is Naggy::escape_string.
func EscapeString(src string) string {
	if src == "" {
		return "\\0"
	}
	var sb strings.Builder
	for _, c := range src {
		if c <= 0x20 || c == 0x7F || c == '\\' || c == '#' {
			fmt.Fprintf(&sb, "\\x%02X", c)
		} else {
			sb.WriteRune(c)
		}
	}
	return sb.String()
}

var unescapeRe = regexp.MustCompile(`(?s)\\([0nt]|[^0-9A-Za-z]|x[0-9a-fA-F][0-9a-fA-F]|u\{[0-9a-fA-F]+\}|u\([0-9a-fA-F]+\))`)

func chrHex(h string) string {
	v, err := strconv.ParseUint(h, 16, 32)
	if err != nil || v > utf8.MaxRune || (v >= 0xD800 && v <= 0xDFFF) {
		return "�"
	}
	return string(rune(v))
}

// UnescapeString is Naggy::unescape_string.
func UnescapeString(src string) string {
	return unescapeRe.ReplaceAllStringFunc(src, func(m string) string {
		body := m[1:]
		switch {
		case body == "0":
			return ""
		case body[0] == 'x' && len(body) == 3:
			return chrHex(body[1:])
		case body[0] == 'u' && len(body) > 3:
			return chrHex(body[2 : len(body)-1])
		case body == "n":
			return "\n"
		case body == "t":
			return "\t"
		}
		return body
	})
}

var regularRe = regexp.MustCompile(`(?s)\\([^0-9A-Za-z])`)

// RegularString is Naggy::regular_string.
func RegularString(src string) string {
	src = strings.ReplaceAll(src, "\\\\", "\\u{5C}")
	return regularRe.ReplaceAllStringFunc(src, func(m string) string {
		c, _ := utf8.DecodeRuneInString(m[1:])
		return fmt.Sprintf("\\u{%X}", c)
	})
}

var falseRe = regexp.MustCompile(`(?is)^(?:false|0+)$`)

// IsTrue is Naggy::is_true.
func IsTrue(s string) bool {
	return s != "" && !falseRe.MatchString(s)
}

// NextLine is Naggy::next_line: it consumes one logical line (handling
// trailing-backslash continuation and comment-only lines).
func NextLine(lines *[]string) (string, bool) {
	var r string
	defined := false
	done := false
	commentOnly := true
	for !done && len(*lines) > 0 {
		s := (*lines)[0]
		*lines = (*lines)[1:]
		s = strings.TrimSuffix(s, "\x0a")
		s = strings.TrimSuffix(s, "\x0d")
		s = RegularString(s)
		readNext := false
		if strings.HasSuffix(s, "\\") {
			s = s[:len(s)-1]
			readNext = true
		}
		hadComment := false
		if i := strings.IndexByte(s, '#'); i >= 0 {
			s = s[:i]
			hadComment = true
		}
		if !(hadComment && strings.TrimSpace(s) == "") {
			commentOnly = false
		}
		done = !(readNext || commentOnly)
		r += s
		defined = true
	}
	return r, defined
}

func isPerlSpace(c rune) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'
}

// splitWS emulates Perl's split(/\s+/, $s): a leading empty field is kept
// when $s starts with whitespace, trailing empty fields are removed.
func splitWS(s string) []string {
	var r []string
	cur := strings.Builder{}
	inSpace := false
	started := false
	for _, c := range s {
		if isPerlSpace(c) {
			if !inSpace {
				r = append(r, cur.String())
				cur.Reset()
				inSpace = true
			}
		} else {
			inSpace = false
			cur.WriteRune(c)
		}
		started = true
	}
	if started {
		r = append(r, cur.String())
	}
	for len(r) > 0 && r[len(r)-1] == "" {
		r = r[:len(r)-1]
	}
	return r
}

// perlSplitLines emulates split(/\n/, $s).
func perlSplitLines(s string) []string {
	r := strings.Split(s, "\n")
	for len(r) > 0 && r[len(r)-1] == "" {
		r = r[:len(r)-1]
	}
	return r
}

// perlNum converts a string to a number the way Perl does in numeric
// context (leading whitespace, then the longest numeric prefix).
func perlNum(s string) float64 {
	s = strings.TrimLeft(s, " \t\n\r\f\v")
	end := 0
	n := len(s)
	if end < n && (s[end] == '+' || s[end] == '-') {
		end++
	}
	digits := 0
	for end < n && s[end] >= '0' && s[end] <= '9' {
		end++
		digits++
	}
	if end < n && s[end] == '.' {
		end++
		for end < n && s[end] >= '0' && s[end] <= '9' {
			end++
			digits++
		}
	}
	if digits == 0 {
		low := strings.ToLower(s)
		if strings.HasPrefix(low, "inf") || strings.HasPrefix(low, "+inf") {
			return math.Inf(1)
		}
		if strings.HasPrefix(low, "-inf") {
			return math.Inf(-1)
		}
		return 0
	}
	if end < n && (s[end] == 'e' || s[end] == 'E') {
		e := end + 1
		if e < n && (s[e] == '+' || s[e] == '-') {
			e++
		}
		if e < n && s[e] >= '0' && s[e] <= '9' {
			for e < n && s[e] >= '0' && s[e] <= '9' {
				e++
			}
			end = e
		}
	}
	v, _ := strconv.ParseFloat(strings.TrimSuffix(s[:end], "."), 64)
	return v
}

// perlNumString formats a number like Perl's default stringification.
func perlNumString(v float64) string {
	if v == math.Trunc(v) && math.Abs(v) < 1e15 {
		return strconv.FormatInt(int64(v), 10)
	}
	return strconv.FormatFloat(v, 'g', 15, 64)
}

// perlUC approximates Perl's uc.
func perlUC(s string) string { return strings.ToUpper(s) }

func runeLen(s string) int { return utf8.RuneCountInString(s) }

// substrRunes returns substr($s, start, n) in characters.
func substrRunes(s string, start, n int) string {
	rs := []rune(s)
	if start > len(rs) {
		return ""
	}
	end := start + n
	if end > len(rs) {
		end = len(rs)
	}
	return string(rs[start:end])
}
