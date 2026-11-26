// Linux test driver for Engine: runs naggy-backend as a child process and
// replays key scripts.  Build: see ime/test/run_tests.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/Engine.h"

using namespace naggy;

class ChildBackend : public IBackend {
public:
    ChildBackend(const char* exe, const char* dir) {
        int in[2], out[2];
        if (pipe(in) || pipe(out)) perror("pipe");
        pid_ = fork();
        if (pid_ == 0) {
            dup2(in[0], 0);
            dup2(out[1], 1);
            close(in[1]);
            close(out[0]);
            if (chdir(dir)) perror("chdir");
            execl(exe, exe, "-D", "FRONT_END=naggy_win", (char*)nullptr);
            _exit(127);
        }
        close(in[0]);
        close(out[1]);
        w_ = fdopen(in[1], "w");
        r_ = fdopen(out[0], "r");
    }
    bool Command(const std::vector<std::wstring>& args, Response& r) override {
        std::string line = BuildCommand(args);
        fputs(line.c_str(), w_);
        fflush(w_);
        ResponseParser p;
        char buf[1 << 16];
        while (fgets(buf, sizeof buf, r_)) {
            if (p.FeedLine(buf)) {
                r = p.Result();
                return true;
            }
        }
        return false;
    }

private:
    pid_t pid_;
    FILE* w_;
    FILE* r_;
};

static const char* kRows[4] = {"1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./"};

static int PosOf(char c) {
    for (int r = 0; r < 4; ++r)
        for (int i = 0; i < 10; ++i)
            if (kRows[r][i] == c) return r * 10 + i;
    return -1;
}

// Script tokens: plain chars; <SPC> <S-SPC> <C-SPC> <M-SPC> <RET> <S-RET>
// <ESC> <BS> <DEL> <LEFT> <RIGHT> <UP> <DOWN> <TAB> <S-TAB> <MUHENKAN>
// <HENKAN> <S-HENKAN> <F6>..<F10> <S-x> (shift+key x) <C-x>
static std::vector<KeyEvent> Parse(const std::string& s) {
    std::vector<KeyEvent> r;
    for (size_t i = 0; i < s.size();) {
        KeyEvent k;
        if (s[i] == '<') {
            size_t j = s.find('>', i);
            std::string t = s.substr(i + 1, j - i - 1);
            i = j + 1;
            if (t.rfind("S-", 0) == 0 && t.size() > 2) { k.shift = true; t = t.substr(2); }
            if (t.rfind("C-", 0) == 0 && t.size() > 2) { k.ctrl = true; t = t.substr(2); }
            if (t.rfind("M-", 0) == 0 && t.size() > 2) { k.alt = true; t = t.substr(2); }
            struct { const char* n; KeyCode c; } m[] = {
                {"SPC", KeyCode::Space}, {"RET", KeyCode::Enter}, {"ESC", KeyCode::Escape},
                {"BS", KeyCode::Backspace}, {"DEL", KeyCode::Delete}, {"LEFT", KeyCode::Left},
                {"RIGHT", KeyCode::Right}, {"UP", KeyCode::Up}, {"DOWN", KeyCode::Down},
                {"TAB", KeyCode::Tab}, {"MUHENKAN", KeyCode::Muhenkan}, {"HENKAN", KeyCode::Henkan},
                {"F6", KeyCode::F6}, {"F7", KeyCode::F7}, {"F8", KeyCode::F8}, {"F9", KeyCode::F9},
                {"F10", KeyCode::F10}, {"HOME", KeyCode::Home}, {"END", KeyCode::End},
                {"PGUP", KeyCode::PageUp}, {"PGDN", KeyCode::PageDown}, {"OTHER", KeyCode::Other}};
            bool found = false;
            for (auto& e : m) if (t == e.n) { k.code = e.c; found = true; }
            if (!found && t.rfind("NC", 0) == 0) {  // key producing no char
                k.code = KeyCode::Other;
                k.pos = PosOf(t[2]);
                found = true;
            }
            if (!found) { k.code = KeyCode::Char; k.ch = t[0]; k.pos = PosOf((char)tolower(t[0])); }
        } else {
            k.code = KeyCode::Char;
            k.ch = s[i];
            k.pos = PosOf((char)tolower(s[i]));
            if (s[i] == ' ') k.code = KeyCode::Space;
            ++i;
        }
        r.push_back(k);
    }
    return r;
}

static void Dump(Engine& e) {
    CandidateView v;
    if (e.GetCandidateView(v)) {
        printf("  [vk %d/%d] ", v.plane + 1, v.planes);
        for (int i = 0; i < 40; ++i) {
            const auto& c = v.cells[i];
            std::string t = c.index >= 0 ? ToUtf8(TruncateToWidth(c.text, 4, L'-')) : "○--";
            printf("%s%s%s%s", c.cursor ? "[" : "", t.c_str(), c.highlight ? "*" : "", c.cursor ? "]" : "");
            printf("%s", i % 10 == 9 ? (i == 39 ? "" : " / ") : (i % 10 == 4 ? "|" : " "));
        }
        printf("\n  status: %s\n", ToUtf8(v.status).c_str());
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s BACKEND DIR [SCRIPT...]\n", argv[0]);
        return 2;
    }
    ChildBackend b(argv[1], argv[2]);
    for (int a = 3; a < argc; ++a) {
        Engine e(&b);
        EngineOptions opt;
        opt.asciiLayout = getenv("NAGGY_ASCII") != nullptr;
        e.SetOptions(opt);
        printf("### %s\n", argv[a]);
        std::string committed;
        for (auto& k : Parse(argv[a])) {
            bool pass = false;
            bool eaten = e.ProcessKey(k, &pass);
            std::string c = ToUtf8(e.TakeCommit());
            committed += c;
            if (!eaten || pass) committed += "{key}";
            if (e.TakeBeep()) committed += "{beep}";
        }
        printf("  state=%d comp=\"%s\" caret=%zu committed=\"%s\"\n", (int)e.GetState(),
               ToUtf8(e.Composition()).c_str(), e.Caret(), committed.c_str());
        Dump(e);
    }
    return 0;
}
