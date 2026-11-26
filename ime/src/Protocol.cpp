// Protocol.cpp - naggy-backend line protocol helpers (portable C++).
#include "Protocol.h"

#include <cstdio>

namespace naggy {

namespace {

void AppendUtf8(std::string& o, unsigned long c) {
    if (c < 0x80) {
        o += static_cast<char>(c);
    } else if (c < 0x800) {
        o += static_cast<char>(0xC0 | (c >> 6));
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        o += static_cast<char>(0xE0 | (c >> 12));
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (c >> 18));
        o += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (c & 0x3F));
    }
}

void AppendWide(std::wstring& o, unsigned long c) {
    if (sizeof(wchar_t) == 2 && c >= 0x10000) {
        c -= 0x10000;
        o += static_cast<wchar_t>(0xD800 + (c >> 10));
        o += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
    } else {
        o += static_cast<wchar_t>(c);
    }
}

// Decodes one code point from a wide string.
unsigned long NextCodePoint(const std::wstring& s, size_t& i) {
    unsigned long c = static_cast<unsigned long>(s[i++]);
    if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && i < s.size()) {
        unsigned long d = static_cast<unsigned long>(s[i]);
        if (d >= 0xDC00 && d < 0xE000) {
            ++i;
            c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        }
    }
    return c;
}

int HexValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

bool IsAlnum(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

std::vector<std::wstring> SplitWS(const std::wstring& s) {
    std::vector<std::wstring> r;
    std::wstring cur;
    bool have = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t' || c == L'\r') {
            if (have) r.push_back(cur);
            cur.clear();
            have = false;
        } else {
            cur += c;
            have = true;
        }
    }
    if (have) r.push_back(cur);
    return r;
}

}  // namespace

std::string ToUtf8(const std::wstring& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) AppendUtf8(o, NextCodePoint(s, i));
    return o;
}

std::wstring FromUtf8(const std::string& s) {
    std::wstring o;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned long cp;
        int n;
        if (c < 0x80) { cp = c; n = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
        else { AppendWide(o, 0xFFFD); ++i; continue; }
        bool ok = true;
        for (int k = 1; k <= n; ++k) {
            if (i + k >= s.size() || (static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        if (!ok) { AppendWide(o, 0xFFFD); ++i; continue; }
        AppendWide(o, cp);
        i += n + 1;
    }
    return o;
}

std::wstring EscapeString(const std::wstring& s) {
    if (s.empty()) return L"\\0";
    std::wstring o;
    for (wchar_t c : s) {
        if (c <= 0x20 || c == 0x7F || c == L'\\' || c == L'#') {
            wchar_t buf[8];
            swprintf(buf, 8, L"\\x%02X", static_cast<unsigned>(c));
            o += buf;
        } else {
            o += c;
        }
    }
    return o;
}

std::wstring UnescapeString(const std::wstring& s) {
    std::wstring o;
    size_t i = 0;
    while (i < s.size()) {
        wchar_t c = s[i];
        if (c != L'\\' || i + 1 >= s.size()) {
            o += c;
            ++i;
            continue;
        }
        wchar_t d = s[i + 1];
        if (d == L'0') { i += 2; continue; }
        if (d == L'n') { o += L'\n'; i += 2; continue; }
        if (d == L't') { o += L'\t'; i += 2; continue; }
        if (!IsAlnum(d)) { o += d; i += 2; continue; }
        if (d == L'x' && i + 3 < s.size() &&
            HexValue(s[i + 2]) >= 0 && HexValue(s[i + 3]) >= 0) {
            AppendWide(o, HexValue(s[i + 2]) * 16 + HexValue(s[i + 3]));
            i += 4;
            continue;
        }
        if (d == L'u' && i + 2 < s.size() && (s[i + 2] == L'{' || s[i + 2] == L'(')) {
            wchar_t close = s[i + 2] == L'{' ? L'}' : L')';
            size_t j = i + 3;
            unsigned long v = 0;
            while (j < s.size() && HexValue(s[j]) >= 0) {
                v = v * 16 + HexValue(s[j]);
                ++j;
            }
            if (j > i + 3 && j < s.size() && s[j] == close) {
                AppendWide(o, v > 0x10FFFF ? 0xFFFD : v);
                i = j + 1;
                continue;
            }
        }
        o += c;
        ++i;
    }
    return o;
}

std::string BuildCommand(const std::vector<std::wstring>& args) {
    std::wstring line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) line += L' ';
        line += EscapeString(args[i]);
    }
    return ToUtf8(line) + "\n";
}

bool ResponseParser::FeedLine(const std::string& utf8line) {
    if (done_) return true;
    std::string l = utf8line;
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
    std::wstring w = FromUtf8(l);
    if (w.compare(0, 6, L"# warn") == 0 || w.compare(0, 6, L"# info") == 0) {
        std::wstring m = w.size() > 7 ? w.substr(7) : L"";
        res_.messages.push_back(UnescapeString(m));
        return false;
    }
    if (!inBlock_) {
        if (w.compare(0, 7, L"# error") == 0) {
            res_.type = Response::Error;
            res_.str = UnescapeString(w.size() > 8 ? w.substr(8) : L"");
            done_ = true;
            return true;
        }
        if (w.compare(0, 7, L"# begin") == 0) {
            inBlock_ = true;
            std::wstring t = w.substr(7);
            size_t a = t.find_first_not_of(L" \t");
            t = a == std::wstring::npos ? L"" : t.substr(a);
            size_t b = t.find_first_of(L" \t");
            if (b != std::wstring::npos) t = t.substr(0, b);
            if (t == L"unit") res_.type = Response::Unit;
            else if (t == L"string") res_.type = Response::String;
            else if (t == L"base64") res_.type = Response::Base64;
            else res_.type = Response::List;
        }
        return false;
    }
    if (w == L"# end") {
        inBlock_ = false;
        done_ = true;
        if (res_.type == Response::String) {
            res_.str = body_.empty() ? L"" : UnescapeString(body_[0]);
        } else if (res_.type == Response::List) {
            for (auto& row : body_) {
                std::vector<std::wstring> cols;
                for (auto& f : SplitWS(row)) cols.push_back(UnescapeString(f));
                res_.list.push_back(cols);
            }
        } else if (res_.type == Response::Unit) {
            res_.str = body_.empty() ? L"" : body_[0];
        }
        return true;
    }
    body_.push_back(w);
    return false;
}

}  // namespace naggy
