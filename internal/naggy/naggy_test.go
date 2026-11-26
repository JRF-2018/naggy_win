package naggy

import (
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestEscape(t *testing.T) {
	cases := map[string]string{
		"":       "\\0",
		"a b":    "a\\x20b",
		"a\\#b":  "a\\x5C\\x23b",
		"あ\tい\n": "あ\\x09い\\x0A",
	}
	for in, want := range cases {
		if got := EscapeString(in); got != want {
			t.Errorf("EscapeString(%q) = %q, want %q", in, got, want)
		}
		if got := UnescapeString(EscapeString(in)); got != in {
			t.Errorf("round trip %q -> %q", in, got)
		}
	}
	if got := UnescapeString(`a\u{3042}b\0c\u(41)\x`); got != "aあbcA\\x" {
		t.Errorf("unescape: %q", got)
	}
}

func TestSplitWS(t *testing.T) {
	if got := splitWS(" a  b "); strings.Join(got, "|") != "|a|b" {
		t.Errorf("splitWS: %q", got)
	}
}

func newTestBackend(t *testing.T) (*Backend, *strings.Builder) {
	_, file, _, _ := runtime.Caller(0)
	data := filepath.Join(filepath.Dir(file), "..", "..", "data")
	ResourcePath = []string{filepath.Join(data, "nginit"), filepath.Join(data, "trl"), data}
	b := NewBackend(Options{Debug: true})
	b.InitVar["FRONT_END"] = "naggy_win"
	var out strings.Builder
	b.Out = &out
	f, ok := LocateFileResource("default-init.nginit")
	if !ok {
		t.Fatal("default-init.nginit not found")
	}
	if !b.LoadInitFile(f) {
		t.Fatalf("init failed: %s", b.InitTail)
	}
	return b, &out
}

func TestTranslit(t *testing.T) {
	b, out := newTestBackend(t)
	cases := []struct{ cmd, want string }{
		{"translit alpha-hira nihongo", "# begin string\nにほんご\n# end\n"},
		{"translit alpha-kata kyoutto", "# begin string\nキョウット\n# end\n"},
		{"translit alpha-hwkata gakkou", "# begin string\nｶﾞｯｺｳ\n# end\n"},
		{"translit hw-fw abc", "# begin string\nａｂｃ\n# end\n"},
		{"translit hira-alpha しんぶん", "# begin string\nSHINNBUNN\n# end\n"},
		{"convert Abraam#greek", "# begin string\nΑβρααμ\n# end\n"},
		{"convert 3042#u", "# begin string\nあ\n# end\n"},
		{"get CAPITALIZED_TANKANJI", "# begin string\nskk\n# end\n"},
		{"nosuch", "# error unknown\\x20command:\\x20nosuch.\n"},
	}
	for _, c := range cases {
		out.Reset()
		b.ProcessLine(c.cmd + "\n")
		if out.String() != c.want {
			t.Errorf("%s:\n got %q\nwant %q", c.cmd, out.String(), c.want)
		}
	}
}

func TestInitFileLanguage(t *testing.T) {
	b, _ := newTestBackend(t)
	src := `.set A 1
.if ${A} == 1
  set B yes
.else
  set B no
.endif
.if defined NOSUCH || is-true ${A}
  set C yes
.endif
.if ${A} > 2
  set D yes
.else
  .if empty
    set D nested
  .endif
.endif
`
	if !b.LoadInitFileFromString(src, "test") {
		t.Fatalf("failed: %s", b.InitTail)
	}
	for k, want := range map[string]string{"B": "yes", "C": "yes", "D": "nested"} {
		if b.InitVar[k] != want {
			t.Errorf("%s = %q, want %q", k, b.InitVar[k], want)
		}
	}
	if b.LoadInitFileFromString(".else\n", "bad") {
		t.Error("else without if should fail")
	}
}
