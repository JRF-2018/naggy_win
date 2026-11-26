#!/usr/bin/env python3
"""Compare the outputs of two naggy backends command by command.

Usage:
  compare_backends.py DIR "CMD_A" "CMD_B" [--define K=V ...] [--n N]

DIR must contain site-init.nginit and the dictionaries.  CMD_A / CMD_B are
shell commands that start a backend in DIR (e.g. "perl naggy-backend.pl"
and "./naggy-backend").  Commands are generated from the dictionaries and
sent to both; differing responses are printed.  "# warn" / "# info" lines
are ignored (quail-naggy.el only shows them as messages).
"""
import random
import re
import subprocess
import sys


def esc(s):
    if s == "":
        return "\\0"
    return re.sub(r'[\x00-\x20\x7f\\#]', lambda m: '\\x%02X' % ord(m.group(0)), s)


def run(cmd, d, lines, defines):
    args = cmd + " " + " ".join("-D " + x for x in defines)
    p = subprocess.run(args, shell=True, cwd=d, input="\n".join(lines) + "\n",
                       capture_output=True, text=True, encoding="utf-8")
    return split_responses(p.stdout), p.stderr


def split_responses(out):
    res, cur = [], []
    inbeg = False
    for l in out.split("\n"):
        if l.startswith("# warn") or l.startswith("# info"):
            continue
        if l.startswith("# error") and not inbeg:
            res.append(l)
            continue
        if l.startswith("# begin"):
            inbeg = True
        if inbeg:
            cur.append(l)
        if l == "# end":
            res.append("\n".join(cur))
            cur, inbeg = [], False
    return res


def main():
    d, ca, cb = sys.argv[1], sys.argv[2], sys.argv[3]
    defines = []
    n = 300
    rest = sys.argv[4:]
    while rest:
        if rest[0] == "--define":
            defines.append(rest[1]); rest = rest[2:]
        elif rest[0] == "--n":
            n = int(rest[1]); rest = rest[2:]
        else:
            raise SystemExit("bad arg " + rest[0])
    random.seed(12345)

    # readings from the SKK dictionary (okuri-nasi, hiragana only)
    yomi = []
    for l in open(d + "/SKK-JISYO.fixed.L.u8", encoding="utf-8"):
        if l.startswith(";"):
            continue
        k = l.split(" ", 1)[0]
        if re.fullmatch(r'[ぁ-ん]{1,6}', k):
            yomi.append(k)
    sample = random.sample(yomi, n)
    # romanize with backend A
    rq = ["translit hira-alpha " + esc(y) for y in sample]
    rr, _ = run(ca, d, rq, defines)
    roman = []
    for r in rr:
        m = re.match(r'# begin string\n(.*)\n# end', r, re.S)
        if m:
            s = m.group(1).lower()
            if re.fullmatch(r"[a-z'\-]+", s):
                roman.append(s)

    fixed = [
        "aka:kou", "aka:akeru", "aka:myoujou", "Akka:bakeru", ":akka:bakeru",
        "akka:bakeru:", "akka:bakeru#j", "Abraam#greek", "3042#u", "1F600#u",
        "zz#u", "@ki", "@@neko", "@@warai", ".", ",", "-", "/", "nihon",
        "kaKu", "kaKou", "hiRaku", "ugoKu", "a::b", "::", ":", "", "ka:",
        "kanji#k", "kanji#h", "kanji#hwkata", "kanji#fw", "abc#a",
        "shalom#hebrew", "privet#russian", "salam#arabic", "cafe#latin",
        "kanji#nosuch", "ni:hon:go", "Nihon:go", "vu", "kyou", "ttsu", "n'a",
        "shenzhen", "Shenzhen", "taipei", "aka:", "Aka", "AKA", "aKA",
        "tooi:kou", "kou:sei:", "sakana", "sakanA", "ugoKi:dou", "1", "a1",
    ]
    cmds = ["nop", "version", "listtest", "warntest", "echo a b\\x20c",
            "get DEFAULT_CONVERT", "get NOSUCH", "set X 1", "get X",
            "unset X", "get X", "nosuchcommand", "translit", "translit no-such x",
            "list-translit", "charmap-of-translit alpha-simple_greek",
            "complete-by-translit alpha-simple_greek ab",
            "complete-by-translit alpha-hira kya",
            "complete-by-translit alpha-hira shit",
            "split-e , a,b,,c d", "echo-e a\\x20b", "join - a b c",
            "add 1 2.5", "list-length a b c", "nth 1 a b c", "nth -1 a b c",
            "nth 5 a", "nthcdr-e 1 a b c", "find-path site-init.nginit .",
            "load-init-file japanese-mode-change.nginit",
            "load-init-file nosuch.nginit"]
    for s in fixed:
        cmds.append("convert " + esc(s))
    specs = ["alpha-hira", "alpha-kata", "alpha-hwkata", "hw-fw", "fw-hw",
             "kana-alpha", "hira-alpha", "kata-alpha", "alpha-simple_greek",
             "alpha-simple_cyrillic", "alpha-j0heb.nes", "alpha-simple_arabic",
             "alpha-simple_latin_1", "simple_greek-alpha", "hira-kata", "kata-hira"]
    for i, r in enumerate(roman):
        cmds.append("convert " + esc(r))
        cmds.append("translit " + specs[i % len(specs)] + " " + esc(r))
        cmds.append("translit-with-space " + specs[(i + 3) % len(specs)] + " " + esc(r + " " + r[::-1]))
        o = roman[(i * 7 + 3) % len(roman)]
        cmds.append("convert " + esc(r[:3] + ":" + o))
        cmds.append("convert " + esc(r.capitalize()))
        cmds.append("convert " + esc(r + ":"))
        cmds.append("convert " + esc(r + "#j"))
        if len(r) > 3:
            k = random.randrange(1, len(r))
            cmds.append("convert " + esc(r[:k] + r[k:].capitalize()))
            cmds.append("convert " + esc(":" + r[:2] + ":" + r))
    for y in sample[:50]:
        cmds.append("translit hira-kata " + esc(y))
        cmds.append("translit fw-hw " + esc(y))
        cmds.append("translit kana-alpha " + esc(y))

    ra, ea = run(ca, d, cmds, defines)
    rb, eb = run(cb, d, cmds, defines)
    print("commands:", len(cmds), "responses:", len(ra), len(rb))
    ndiff = 0
    # responses may be misaligned if one side prints nothing; align by index
    for i in range(max(len(ra), len(rb))):
        a = ra[i] if i < len(ra) else None
        b = rb[i] if i < len(rb) else None
        if a != b:
            ndiff += 1
            if ndiff <= 15:
                print("=== DIFF at response", i, "cmd guess:", cmds[i] if i < len(cmds) else "?")
                print("--- A\n" + str(a)[:800])
                print("--- B\n" + str(b)[:800])
    print("differences:", ndiff)
    if ea.strip():
        print("stderr A:", ea[:500])
    if eb.strip():
        print("stderr B:", eb[:500])


main()
