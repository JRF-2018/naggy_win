// naggy-dictool prepares dictionaries for naggy-backend / naggy_win.
//
//	naggy-dictool setup --dest DIR --tankanji jrf|kaze|none [options]
//	naggy-dictool dic2txt Wind2.rea Wind2.dic [Wind2.txt]
//	naggy-dictool fix-skk-jisyo-l [-e|-s|-u] IN OUT
//	naggy-dictool find-kaze
//
// "setup" downloads (or takes from local files) the dictionaries, converts
// them and writes DIR/site-init.nginit.  It is run by the installer.
package main

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"time"

	"github.com/JRF-2018/naggy_win/internal/jenc"
	"github.com/JRF-2018/naggy_win/internal/tip"
)

const (
	urlTankanji = "https://raw.githubusercontent.com/JRF-2018/jrf_tankanji-2026/master/tankanji-2026.txt"
	urlEmoji    = "https://raw.githubusercontent.com/JRF-2018/naggy-emoji-skk-dic/master/emoji-skk-dic.txt"
)

var (
	urlsSKKL = []string{
		"https://raw.githubusercontent.com/skk-dev/dict/master/SKK-JISYO.L",
		"https://skk-dev.github.io/dict/SKK-JISYO.L.gz",
		"http://openlab.jp/skk/dic/SKK-JISYO.L.gz",
	}
	urlsChinaTaiwan = []string{
		"https://raw.githubusercontent.com/skk-dev/dict/master/SKK-JISYO.china_taiwan",
		"https://skk-dev.github.io/dict/SKK-JISYO.china_taiwan.gz",
		"http://openlab.jp/skk/dic/SKK-JISYO.china_taiwan.gz",
	}
)

func usage() {
	fmt.Fprint(os.Stderr, `Usage:
  naggy-dictool setup --dest DIR --tankanji jrf|kaze|none [options]
      --kaze-dir DIR       directory containing Wind2.rea and Wind2.dic
                           (or Wind2.txt); default: search installed 風
      --kaze-txt FILE      an already converted Wind2.txt (Shift_JIS)
      --cache DIR          use/keep downloaded files in DIR
      --no-skk --no-emoji --no-china-taiwan
      --force              overwrite site-init.nginit even if it exists
  naggy-dictool dic2txt Wind2.rea Wind2.dic [Wind2.txt]
  naggy-dictool fix-skk-jisyo-l [-e|-s|-u] IN OUT     (OUT is UTF-8)
  naggy-dictool find-kaze
  naggy-dictool enable-tip | disable-tip   (add/remove naggy_win in the
                                            current user's keyboard list)
`)
	os.Exit(2)
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	var err error
	switch os.Args[1] {
	case "setup":
		err = cmdSetup(os.Args[2:])
	case "dic2txt":
		a := os.Args[2:]
		if len(a) < 2 || len(a) > 3 {
			usage()
		}
		out := io.Writer(os.Stdout)
		if len(a) == 3 {
			f, e := os.Create(a[2])
			if e != nil {
				fail(e)
			}
			defer f.Close()
			out = f
		}
		err = dic2txt(a[0], a[1], out)
	case "fix-skk-jisyo-l":
		fs := flag.NewFlagSet("fix-skk-jisyo-l", flag.ExitOnError)
		e := fs.Bool("e", false, "euc-jp (default)")
		s := fs.Bool("s", false, "cp932")
		u := fs.Bool("u", false, "utf8")
		fs.Parse(os.Args[2:])
		if fs.NArg() != 2 {
			usage()
		}
		enc := jenc.EUCJP
		if *s {
			enc = jenc.CP932
		} else if *u {
			enc = jenc.UTF8
		}
		_ = e
		err = fixSKKJisyoLFile(fs.Arg(0), fs.Arg(1), enc)
	case "enable-tip":
		err = tip.Enable()
	case "disable-tip":
		err = tip.Disable()
	case "find-kaze":
		d := findKaze()
		if d == "" {
			os.Exit(1)
		}
		fmt.Println(d)
	default:
		usage()
	}
	if err != nil {
		fail(err)
	}
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "naggy-dictool:", err)
	os.Exit(1)
}

// ---------------------------------------------------------------------
// dic2txt: port of dic2txt.pl (converts the 風 dictionary to text).

var kazeOrder = [40]int{
	40, 38, 32, 34, 36, 35, 33, 31, 37, 39,
	22, 16, 6, 10, 14, 13, 9, 5, 15, 21,
	18, 12, 2, 4, 8, 7, 3, 1, 11, 17,
	30, 28, 20, 24, 26, 25, 23, 19, 27, 29,
}

func dic2txt(reaFile, dicFile string, out io.Writer) error {
	posToWin := map[int]int{}
	for i := 0; i < 40; i++ {
		posToWin[kazeOrder[i]-1] = i
	}
	rea, err := os.ReadFile(reaFile)
	if err != nil {
		return err
	}
	dic, err := os.ReadFile(dicFile)
	if err != nil {
		return err
	}
	const pre = 0x50
	if len(rea) < pre+8 {
		return errors.New(reaFile + ": too short")
	}
	num := int(binary.LittleEndian.Uint16(rea[pre:]))
	cache := int(binary.LittleEndian.Uint16(rea[pre+2:]))
	unknown1 := int(binary.LittleEndian.Uint16(rea[pre+4:]))
	p := pre + 8 + cache*8 + unknown1*8 + 0x10
	w := bufio.NewWriter(out)
	defer w.Flush()
	for ; num > 0; num-- {
		if p+12 > len(rea) {
			return errors.New(reaFile + ": unexpected end")
		}
		yomi := rea[p : p+8]
		addr := int(binary.LittleEndian.Uint16(rea[p+8:]))
		n := int(binary.LittleEndian.Uint16(rea[p+10:]))
		p += 12
		// $yomi =~ s/\x00+//  (removes the first run of NULs)
		if i := bytes.IndexByte(yomi, 0); i >= 0 {
			j := i
			for j < len(yomi) && yomi[j] == 0 {
				j++
			}
			yomi = append(append([]byte{}, yomi[:i]...), yomi[j:]...)
		}
		start := pre + addr
		end := start + n
		if start > len(dic) {
			start = len(dic)
		}
		if end > len(dic) {
			end = len(dic)
		}
		buf := dic[start:end]
		page := map[int][]byte{}
		maxIdx := -1
		for len(buf) > 0 {
			pos := int(buf[0])
			buf = buf[1:]
			if pos == 0xFF && len(buf) > 0 {
				sub := int(buf[0])
				buf = buf[1:]
				if sub >= 0x81 {
					buf = append([]byte{byte(sub)}, buf...)
				} else {
					pos += sub
				}
			}
			k := buf
			if len(k) > 2 {
				k = k[:2]
			}
			buf = buf[len(k):]
			pos--
			loc := posToWin[((pos%40)+40)%40]
			pg := pos / 40
			idx := pg*40 + loc
			page[idx] = append([]byte{}, k...)
			if idx > maxIdx {
				maxIdx = idx
			}
		}
		w.WriteString("#YOMI:")
		w.Write(yomi)
		w.WriteString("\n")
		// Pages of 40 cells: 4 lines of 10.
		for base := 0; base <= maxIdx; base += 40 {
			for l := 0; l < 4; l++ {
				for c := 0; c < 10; c++ {
					if v, ok := page[base+l*10+c]; ok && len(v) > 0 {
						w.Write(v)
					} else {
						w.WriteString("  ")
					}
				}
				w.WriteString("\n")
			}
			w.WriteString("#\n")
		}
	}
	return nil
}

// ---------------------------------------------------------------------
// fix_skk_jisyo_l.pl: removes candidates that are only kana or ASCII.

var (
	reAlphaSpace = regexp.MustCompile(`^[a-zA-Z ]+$`)
	reKanaOnly   = regexp.MustCompile(`^[\x{3041}-\x{309F}\x{30A0}-\x{30FF}]+$`)
	reSpaces     = regexp.MustCompile(` +`)
)

func fixSKKJisyoL(in []byte, enc jenc.Encoding, w io.Writer) error {
	bw := bufio.NewWriter(w)
	defer bw.Flush()
	for _, raw := range bytes.SplitAfter(in, []byte{'\n'}) {
		if len(raw) == 0 {
			continue
		}
		s := jenc.Decode(enc, raw)
		s = strings.TrimSuffix(s, "\n")
		t := strings.TrimLeft(s, " \t\r\f\v")
		if strings.HasPrefix(t, ";") || strings.TrimSpace(s) == "" {
			bw.WriteString(s + "\n")
			continue
		}
		loc := reSpaces.FindStringIndex(s)
		if loc == nil {
			return fmt.Errorf("parse error: %s", s)
		}
		key, sp, rest := s[:loc[0]], s[loc[0]:loc[1]], s[loc[1]:]
		kouho := strings.Split(rest, "/")
		for len(kouho) > 0 && kouho[len(kouho)-1] == "" {
			kouho = kouho[:len(kouho)-1]
		}
		var n []string
		for _, k := range kouho {
			c := k
			if i := strings.IndexByte(c, ';'); i >= 0 {
				c = c[:i]
			}
			if c != "" && !reAlphaSpace.MatchString(c) && !reKanaOnly.MatchString(c) {
				n = append(n, k)
			}
		}
		if len(n) > 0 {
			bw.WriteString(key + sp + "/" + strings.Join(n, "/") + "/\n")
		}
	}
	return nil
}

func fixSKKJisyoLFile(in, out string, enc jenc.Encoding) error {
	b, err := os.ReadFile(in)
	if err != nil {
		return err
	}
	f, err := os.Create(out)
	if err != nil {
		return err
	}
	if err := fixSKKJisyoL(b, enc, f); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// china_taiwan.patch of quail-naggy, as string replacements.
func fixChinaTaiwan(in []byte) []byte {
	s := jenc.Decode(jenc.EUCJP, in)
	repl := []struct{ from, to string }{
		{"coding: euc-jp", "coding: utf-8"},
		{"quemoy /アモイ/", "quemoy /廈門/アモイ/"},
		{"shenzhen /深セン/", "shenzhen /深圳/"},
		{"しんせん /Shenzhen/深セン/", "しんせん /Shenzhen/深圳/"},
	}
	for _, r := range repl {
		s = strings.Replace(s, r.from, r.to, 1)
	}
	return []byte(s)
}

// ---------------------------------------------------------------------
// downloads

func download(urls []string, cache, name string) ([]byte, error) {
	if cache != "" {
		if b, err := os.ReadFile(filepath.Join(cache, name)); err == nil && len(b) > 0 {
			fmt.Printf("  using cached %s\n", name)
			return b, nil
		}
	}
	client := &http.Client{Timeout: 5 * time.Minute}
	var lastErr error
	for _, u := range urls {
		fmt.Printf("  downloading %s\n", u)
		req, _ := http.NewRequest("GET", u, nil)
		req.Header.Set("User-Agent", "naggy-dictool/0.1")
		resp, err := client.Do(req)
		if err != nil {
			lastErr = err
			continue
		}
		b, err := io.ReadAll(resp.Body)
		resp.Body.Close()
		if err != nil {
			lastErr = err
			continue
		}
		if resp.StatusCode != 200 {
			lastErr = fmt.Errorf("%s: %s", u, resp.Status)
			continue
		}
		if strings.HasSuffix(u, ".gz") {
			zr, err := gzip.NewReader(bytes.NewReader(b))
			if err != nil {
				lastErr = err
				continue
			}
			b, err = io.ReadAll(zr)
			if err != nil {
				lastErr = err
				continue
			}
		}
		if cache != "" {
			os.MkdirAll(cache, 0755)
			os.WriteFile(filepath.Join(cache, name), b, 0644)
		}
		return b, nil
	}
	return nil, lastErr
}

// ---------------------------------------------------------------------
// 風 (Kaze)

func fileExists(p string) bool {
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}

func kazeDirs() []string {
	var r []string
	if runtime.GOOS == "windows" {
		win := os.Getenv("SystemRoot")
		if win == "" {
			win = `C:\Windows`
		}
		pd := os.Getenv("ProgramData")
		if pd == "" {
			pd = `C:\ProgramData`
		}
		r = append(r, win, filepath.Join(pd, "Wind2"))
		for _, e := range []string{"ProgramFiles", "ProgramFiles(x86)"} {
			if d := os.Getenv(e); d != "" {
				r = append(r, filepath.Join(d, "Wind2"), filepath.Join(d, "Wind"))
			}
		}
	}
	return r
}

// findKaze returns a directory containing Wind2.rea and Wind2.dic.
func findKaze() string {
	for _, d := range kazeDirs() {
		if fileExists(filepath.Join(d, "Wind2.rea")) && fileExists(filepath.Join(d, "Wind2.dic")) {
			return d
		}
	}
	return ""
}

// ---------------------------------------------------------------------
// setup

func cmdSetup(args []string) error {
	fs := flag.NewFlagSet("setup", flag.ExitOnError)
	dest := fs.String("dest", "", "installation directory")
	tk := fs.String("tankanji", "jrf", "tankanji dictionary: jrf, kaze or none")
	kazeDir := fs.String("kaze-dir", "", "directory of Wind2.rea/Wind2.dic (or Wind2.txt)")
	kazeTxt := fs.String("kaze-txt", "", "converted Wind2.txt")
	cache := fs.String("cache", "", "download cache directory")
	noSKK := fs.Bool("no-skk", false, "do not install SKK-JISYO.L")
	noEmoji := fs.Bool("no-emoji", false, "do not install emoji-skk-dic.txt")
	noCT := fs.Bool("no-china-taiwan", false, "do not install SKK-JISYO.china_taiwan")
	force := fs.Bool("force", false, "overwrite site-init.nginit")
	fs.Parse(args)
	if *dest == "" {
		return errors.New("--dest is required")
	}
	dicDir := filepath.Join(*dest, "dic")
	if err := os.MkdirAll(dicDir, 0755); err != nil {
		return err
	}
	var lines []string
	lines = append(lines, "## site-init.nginit written by naggy-dictool setup ("+time.Now().Format("2006-01-02")+").",
		"## You may edit this file.  It is read by naggy-backend / naggy-server.",
		"load-init-file default-init.nginit", "")

	switch *tk {
	case "jrf":
		fmt.Println("tankanji-2026.txt (jrf_tankanji-2026):")
		b, err := download([]string{urlTankanji}, *cache, "tankanji-2026.txt")
		if err != nil {
			return fmt.Errorf("tankanji-2026.txt: %v", err)
		}
		if err := os.WriteFile(filepath.Join(dicDir, "tankanji-2026.txt"), b, 0644); err != nil {
			return err
		}
		lines = append(lines, "set-tankanji-dic tankanji-2026.txt -e")
	case "kaze":
		fmt.Println("風 dictionary:")
		out := filepath.Join(dicDir, "Wind2.txt")
		switch {
		case *kazeTxt != "":
			b, err := os.ReadFile(*kazeTxt)
			if err != nil {
				return err
			}
			if err := os.WriteFile(out, b, 0644); err != nil {
				return err
			}
		default:
			d := *kazeDir
			if d == "" {
				d = findKaze()
			}
			if d == "" {
				return errors.New("Wind2.rea / Wind2.dic not found; specify --kaze-dir")
			}
			if !fileExists(filepath.Join(d, "Wind2.rea")) && fileExists(filepath.Join(d, "Wind2.txt")) {
				b, err := os.ReadFile(filepath.Join(d, "Wind2.txt"))
				if err != nil {
					return err
				}
				if err := os.WriteFile(out, b, 0644); err != nil {
					return err
				}
				break
			}
			fmt.Printf("  converting %s\n", filepath.Join(d, "Wind2.dic"))
			f, err := os.Create(out)
			if err != nil {
				return err
			}
			err = dic2txt(filepath.Join(d, "Wind2.rea"), filepath.Join(d, "Wind2.dic"), f)
			f.Close()
			if err != nil {
				return err
			}
		}
		lines = append(lines, "set-tankanji-dic Wind2.txt -s")
	case "none":
		lines = append(lines, "## No tankanji dictionary: word conversion is the default.")
	default:
		return fmt.Errorf("unknown --tankanji %q", *tk)
	}

	if !*noSKK {
		fmt.Println("SKK-JISYO.L:")
		b, err := download(urlsSKKL, *cache, "SKK-JISYO.L")
		if err != nil {
			return fmt.Errorf("SKK-JISYO.L: %v", err)
		}
		var buf bytes.Buffer
		if err := fixSKKJisyoL(b, jenc.EUCJP, &buf); err != nil {
			return err
		}
		if err := os.WriteFile(filepath.Join(dicDir, "SKK-JISYO.fixed.L.utf8"), buf.Bytes(), 0644); err != nil {
			return err
		}
		lines = append(lines, "add-skk-dic SKK-JISYO.fixed.L.utf8 -u")
	}
	lines = append(lines, "add-skk-dic bushu-skk-dic.txt -e")
	if !*noEmoji {
		fmt.Println("emoji-skk-dic.txt:")
		b, err := download([]string{urlEmoji}, *cache, "emoji-skk-dic.txt")
		if err != nil {
			return fmt.Errorf("emoji-skk-dic.txt: %v", err)
		}
		if err := os.WriteFile(filepath.Join(dicDir, "emoji-skk-dic.txt"), b, 0644); err != nil {
			return err
		}
		lines = append(lines, "add-skk-dic emoji-skk-dic.txt -u")
	}
	if !*noCT {
		fmt.Println("SKK-JISYO.china_taiwan:")
		b, err := download(urlsChinaTaiwan, *cache, "SKK-JISYO.china_taiwan")
		if err != nil {
			return fmt.Errorf("SKK-JISYO.china_taiwan: %v", err)
		}
		if err := os.WriteFile(filepath.Join(dicDir, "SKK-JISYO.china_taiwan.fixed.utf8"), fixChinaTaiwan(b), 0644); err != nil {
			return err
		}
		lines = append(lines, "add-skk-dic SKK-JISYO.china_taiwan.fixed.utf8 -u")
	}
	if *tk == "none" {
		lines = append(lines, "", "set DEFAULT_CONVERT skk")
	}

	init := filepath.Join(*dest, "site-init.nginit")
	if fileExists(init) && !*force {
		alt := init + ".new"
		fmt.Printf("%s exists; writing %s\n", init, alt)
		init = alt
	}
	content := strings.Join(lines, "\r\n") + "\r\n"
	if err := os.WriteFile(init, []byte(content), 0644); err != nil {
		return err
	}
	fmt.Println("wrote", init)
	return nil
}
