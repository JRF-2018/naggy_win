package jenc

import (
	"bufio"
	"encoding/hex"
	"fmt"
	"os"
	"strings"
	"testing"
)

// TestComparePerl compares with tables dumped from Perl's Encode
// (set NAGGY_PERL_EUC / NAGGY_PERL_CP932 to the dump files).
func TestComparePerl(t *testing.T) {
	for _, c := range []struct {
		env string
		e   Encoding
	}{{"NAGGY_PERL_EUC", EUCJP}, {"NAGGY_PERL_CP932", CP932}} {
		fn := os.Getenv(c.env)
		if fn == "" {
			t.Skip("no dump")
		}
		f, err := os.Open(fn)
		if err != nil {
			t.Fatal(err)
		}
		sc := bufio.NewScanner(f)
		diff := 0
		for sc.Scan() {
			p := strings.SplitN(sc.Text(), "\t", 2)
			b, _ := hex.DecodeString(p[0])
			var got []string
			for _, r := range Decode(c.e, b) {
				got = append(got, fmt.Sprintf("%04X", r))
			}
			if strings.Join(got, " ") != p[1] {
				diff++
				if diff <= 30 {
					t.Logf("%s %s: perl=%s go=%s", c.env, p[0], p[1], strings.Join(got, " "))
				}
			}
		}
		f.Close()
		t.Logf("%s: %d differences", c.env, diff)
	}
}
