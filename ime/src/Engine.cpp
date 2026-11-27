// Engine.cpp - the input state machine of naggy_win (portable C++).
#include "Engine.h"

#include <cstdio>
#include <cstdlib>

namespace naggy {

namespace {
const int kHomeSlots[] = {23, 26};

bool NoMods(const KeyEvent& k) { return !k.shift && !k.ctrl && !k.alt; }
bool CtrlOnly(const KeyEvent& k) { return k.ctrl && !k.shift && !k.alt; }
bool ShiftOnly(const KeyEvent& k) { return k.shift && !k.ctrl && !k.alt; }
bool IsCtrlChar(const KeyEvent& k, wchar_t c) {
    return CtrlOnly(k) && k.code == KeyCode::Char && (k.ch == c || k.ch == c - 32);
}
}  // namespace

int CharWidth(unsigned long c) {
    // zero width: combining marks, ZWJ, variation selectors, emoji skin
    // tone modifiers (so that emoji sequences are kept whole)
    if ((c >= 0x0300 && c <= 0x036F) || c == 0x200D || (c >= 0xFE00 && c <= 0xFE0F) ||
        (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0100 && c <= 0xE01EF) || c == 0x20E3)
        return 0;
    if (c >= 0x1F1E6 && c <= 0x1F1FF) return 1;  // regional indicators (a flag = 2)
    if (c < 0x1100) return 1;
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) ||
        (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF) ||
        (c >= 0x20000 && c <= 0x3FFFD))
        return 2;
    return 1;
}

std::wstring TruncateToWidth(const std::wstring& s, int width, wchar_t pad) {
    std::wstring r;
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        size_t j = i;
        unsigned long c = static_cast<unsigned long>(s[j++]);
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && j < s.size()) {
            c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<unsigned long>(s[j]) - 0xDC00);
            ++j;
        }
        int cw = CharWidth(c);
        if (w + cw > width) break;
        r.append(s, i, j - i);
        w += cw;
        i = j;
    }
    while (w < width) {
        r += pad;
        ++w;
    }
    return r;
}

void Engine::Reset() {
    state_ = State::Idle;
    buf_.clear();
    cursor_ = 0;
    cands_.clear();
    planes_.clear();
    convText_.clear();
}

void Engine::CommitAsIs() {
    std::wstring c = Composition();
    Reset();
    commit_ += c;
}

void Engine::FinishCommit(std::wstring s) {
    Reset();
    commit_ += s;
}

std::wstring Engine::Composition() const {
    switch (state_) {
    case State::Input: return buf_;
    case State::Convert: return convText_;
    default: return L"";
    }
}

size_t Engine::Caret() const {
    if (state_ == State::Input) return cursor_;
    return Composition().size();
}

bool Engine::WouldEat(const KeyEvent& k) const {
    if (k.code == KeyCode::None || k.code == KeyCode::Modifier) return false;
    if (state_ == State::Idle) {
        if (k.code == KeyCode::Char && !k.ctrl && !k.alt) return true;
        if (k.code == KeyCode::Space && ShiftOnly(k)) return true;
        return false;
    }
    return true;
}

bool Engine::ProcessKey(const KeyEvent& k, bool* passThrough) {
    *passThrough = false;
    if (!WouldEat(k)) return false;
    if (state_ == State::Convert) return ProcessConvert(k, passThrough);
    return ProcessInput(k, passThrough);
}

void Engine::Insert(wchar_t c) {
    if (state_ == State::Idle) {
        buf_.clear();
        cursor_ = 0;
        state_ = State::Input;
    }
    buf_.insert(buf_.begin() + cursor_, c);
    ++cursor_;
}

bool Engine::Translit(const std::wstring& spec, const std::wstring& s, std::wstring& out) {
    Response r;
    if (!backend_->Command({L"translit", spec, s}, r)) {
        lastError_ = L"naggy-backend is not available.";
        return false;
    }
    if (r.type != Response::String) {
        lastError_ = r.type == Response::Error ? r.str : L"unexpected response";
        return false;
    }
    out = r.str;
    return true;
}

void Engine::TerminateTranslit(const std::wstring& spec, const std::wstring& s) {
    std::wstring out;
    if (!Translit(spec, s, out)) {
        beep_ = true;
        return;
    }
    FinishCommit(out);
}

// ASCII layout (vkegg): the key next to L (`'') works as the separator
// `:' and vice versa.  The typed text stays as typed (so that Enter
// commits it as it is); the two are swapped only in what is sent to the
// converter.
std::wstring Engine::ForConvert(const std::wstring& s) const {
    if (!opt_.asciiLayout) return s;
    std::wstring r = s;
    for (auto& c : r) {
        if (c == L'\'') c = L':';
        else if (c == L':') c = L'\'';
    }
    return r;
}

void Engine::TabTerminate(const KeyEvent& k, const std::wstring& s) {
    if (opt_.asciiLayout) {
        if (k.shift || k.alt) TerminateTranslit(L"alpha-kata", s);
        else if (k.ctrl) TerminateTranslit(L"hw-fw", s);
        else TerminateTranslit(L"alpha-hira", s);
    } else {
        if (k.shift) TerminateTranslit(L"alpha-kata", s);
        else if (k.alt) TerminateTranslit(L"alpha-hira", s);
        else TerminateTranslit(L"hw-fw", s);
    }
}

bool Engine::ProcessInput(const KeyEvent& k, bool* passThrough) {
    if (state_ == State::Idle) {
        if (k.code == KeyCode::Char) {
            Insert(k.ch);
            return true;
        }
        if (k.code == KeyCode::Space && ShiftOnly(k)) {
            commit_ += L"\x3000";
            return true;
        }
        return false;
    }
    // State::Input
    switch (k.code) {
    case KeyCode::Char:
        if (!k.ctrl && !k.alt) {
            Insert(k.ch);
            return true;
        }
        if (IsCtrlChar(k, L'g')) { Reset(); return true; }
        if (IsCtrlChar(k, L'h')) goto backspace;
        if (IsCtrlChar(k, L'd')) goto del;
        if (IsCtrlChar(k, L'b')) goto left;
        if (IsCtrlChar(k, L'f')) goto right;
        if (IsCtrlChar(k, L'a')) goto home;
        if (IsCtrlChar(k, L'e')) goto end;
        if (IsCtrlChar(k, L'm') || IsCtrlChar(k, L'j')) { CommitAsIs(); return true; }
        if (IsCtrlChar(k, L'i')) { TerminateTranslit(L"hw-fw", buf_); return true; }
        if (k.alt && !k.ctrl) {
            // M-TAB etc. are not chars; M-<char> commits and passes.
        }
        break;
    case KeyCode::Space:
        if (NoMods(k)) {
            StartConvert(ForConvert(buf_));
            return true;
        }
        if (ShiftOnly(k)) {
            if (opt_.shiftSpaceAltConvert) StartConvert(ForConvert(buf_) + L":");
            else Insert(L' ');
            return true;
        }
        if (CtrlOnly(k)) {
            Insert(L' ');
            return true;
        }
        if (k.alt) {
            std::wstring s = ForConvert(buf_);
            if (!prevModification_.empty()) s += L"#" + prevModification_;
            StartConvert(s);
            return true;
        }
        break;
    case KeyCode::Enter:
        if (k.shift) TerminateTranslit(L"alpha-hira", buf_);
        else CommitAsIs();
        return true;
    case KeyCode::Escape:
        Reset();
        return true;
    case KeyCode::Backspace:
    backspace:
        if (cursor_ > 0) {
            buf_.erase(cursor_ - 1, 1);
            --cursor_;
        }
        if (buf_.empty()) Reset();
        return true;
    case KeyCode::Delete:
    del:
        if (cursor_ < buf_.size()) buf_.erase(cursor_, 1);
        if (buf_.empty()) Reset();
        return true;
    case KeyCode::Left:
    left:
        if (cursor_ > 0) --cursor_;
        else beep_ = true;
        return true;
    case KeyCode::Right:
    right:
        if (cursor_ < buf_.size()) ++cursor_;
        else beep_ = true;
        return true;
    case KeyCode::Home:
    home:
        cursor_ = 0;
        return true;
    case KeyCode::End:
    end:
        cursor_ = buf_.size();
        return true;
    case KeyCode::Tab:
        TabTerminate(k, buf_);
        return true;
    case KeyCode::Muhenkan:
    case KeyCode::F6:
        TerminateTranslit(L"alpha-hira", buf_);
        return true;
    case KeyCode::Henkan:
        if (k.shift) TerminateTranslit(L"alpha-hwkata", buf_);
        else TerminateTranslit(L"alpha-kata", buf_);
        return true;
    case KeyCode::F7:
        TerminateTranslit(L"alpha-kata", buf_);
        return true;
    case KeyCode::F8:
        TerminateTranslit(L"alpha-hwkata", buf_);
        return true;
    case KeyCode::F9:
        TerminateTranslit(L"hw-fw", buf_);
        return true;
    case KeyCode::F10:
        CommitAsIs();
        return true;
    case KeyCode::Other:
        // A character key producing nothing (Shift+0 on JIS): ignore.
        if (k.pos >= 0 && !k.ctrl && !k.alt) return true;
        break;
    default:
        break;
    }
    // Any other key: commit the string as it is and let the key through.
    CommitAsIs();
    *passThrough = true;
    return true;
}

void Engine::StartConvert(const std::wstring& s) {
    size_t h = s.rfind(L'#');
    if (h != std::wstring::npos && h + 1 < s.size()) prevModification_ = s.substr(h + 1);

    Response r;
    if (!backend_->Command({L"convert", s}, r)) {
        lastError_ = L"naggy-backend is not available.";
        beep_ = true;
        return;
    }
    if (r.type == Response::String) {
        FinishCommit(r.str);
        return;
    }
    if (r.type != Response::List || r.list.empty()) {
        if (r.type == Response::Error) lastError_ = r.str;
        beep_ = true;
        return;
    }
    cands_.clear();
    int maxPos = 0;
    for (auto& row : r.list) {
        if (row.size() < 2) continue;
        Candidate c;
        c.word = row[0];
        c.pos = std::atoi(ToUtf8(row[1]).c_str());
        if (c.pos < 0) c.pos = 0;
        c.highlight = row.size() > 2 && row[2] == L"h";
        if (row.size() > 3) c.comment = row[3];
        if (c.pos > maxPos) maxPos = c.pos;
        cands_.push_back(c);
    }
    if (cands_.empty()) {
        beep_ = true;
        return;
    }
    int n = (maxPos + 40) / 40;
    planes_.assign(n, std::vector<int>(40, -1));
    for (size_t i = 0; i < cands_.size(); ++i) {
        int p = cands_[i].pos;
        planes_[p / 40][p % 40] = static_cast<int>(i);
    }
    original_ = buf_;
    originalCursor_ = cursor_;
    state_ = State::Convert;
    kouho_ = 0;
    plane_ = 0;
    vkCursor_ = -1;
    convText_ = original_;
    Display();
    InsertKouho();
}

void Engine::CancelConvert() {
    std::wstring o = original_;
    size_t c = originalCursor_;
    Reset();
    buf_ = o;
    cursor_ = c > o.size() ? o.size() : c;
    state_ = buf_.empty() ? State::Idle : State::Input;
}

int Engine::FirstKouhoOfPlane(int plane) const {
    for (size_t i = 0; i < cands_.size(); ++i)
        if (cands_[i].pos / 40 == plane) return static_cast<int>(i);
    return -1;
}

// naggy-vk-display-kouho-list + naggy-vk-insert-candidate-string.
void Engine::Display() {
    int n = Planes();
    if (kouho_ < 0) {
        if (vkCursor_ < 0) vkCursor_ = 0;
    } else {
        int c = cands_[kouho_].pos;
        if (plane_ % n == c / 40) vkCursor_ = c % 40;
        else vkCursor_ = 0;
    }
    const std::vector<int>& cand = planes_[plane_ % n];
    if (cand[vkCursor_] >= 0 && kouho_ < 0) kouho_ = cand[vkCursor_];
}

void Engine::InsertKouho() {
    if (kouho_ >= 0 && kouho_ < static_cast<int>(cands_.size())) convText_ = cands_[kouho_].word;
}

void Engine::NextPage() {
    ++plane_;
    if (kouho_ < 0) {
        kouho_ = FirstKouhoOfPlane(plane_ % Planes());
    } else {
        ++kouho_;
        if (kouho_ >= static_cast<int>(cands_.size())) kouho_ = 0;
    }
    vkCursor_ = -1;
    Display();
    InsertKouho();
}

void Engine::PrevPage() {
    --plane_;
    if (plane_ < 0) {
        CancelConvert();
        return;
    }
    if (kouho_ < 0) {
        kouho_ = FirstKouhoOfPlane(plane_ % Planes());
    } else {
        --kouho_;
        if (kouho_ < 0) kouho_ = static_cast<int>(cands_.size()) - 1;
    }
    vkCursor_ = -1;
    Display();
    InsertKouho();
}

void Engine::MoveCursor(int delta, bool vertical) {
    int c = vkCursor_ < 0 ? 0 : vkCursor_;
    int nc = c + delta;
    bool bad = vertical ? (nc < 0 || nc >= 40) : (nc < 0 || nc > 39);
    if (bad) {
        beep_ = true;
        return;
    }
    vkCursor_ = nc;
    kouho_ = -1;
    Display();
    InsertKouho();
}

bool Engine::SelectSlot(int slot, bool terminate) {
    if (state_ != State::Convert || slot < 0 || slot >= 40) return false;
    int a = planes_[plane_ % Planes()][slot];
    if (a < 0) {
        beep_ = true;
        return false;
    }
    kouho_ = a;
    vkCursor_ = -1;
    if (terminate) {
        InsertKouho();
        FinishCommit(convText_);
    } else {
        Display();
        InsertKouho();
    }
    return true;
}

bool Engine::ProcessConvert(const KeyEvent& k, bool* passThrough) {
    // Candidate keys are recognized by their physical position, so that
    // keys producing no character (e.g. Shift+0 on a JIS keyboard) work.
    if (k.pos >= 0 && (k.code == KeyCode::Char || k.code == KeyCode::Other) && !k.ctrl) {
        SelectSlot(k.pos, !(k.shift || k.alt));
        return true;
    }
    switch (k.code) {
    case KeyCode::Space:
        if (k.shift) PrevPage();
        else NextPage();
        return true;
    case KeyCode::PageDown:
        NextPage();
        return true;
    case KeyCode::Backspace:
    case KeyCode::Delete:
    case KeyCode::PageUp:
        PrevPage();
        return true;
    case KeyCode::Enter:
        if (k.shift) TerminateTranslit(L"alpha-hira", original_);
        else FinishCommit(convText_);
        return true;
    case KeyCode::Escape:
        CancelConvert();
        return true;
    case KeyCode::Up:
        MoveCursor(-10, true);
        return true;
    case KeyCode::Down:
        MoveCursor(10, true);
        return true;
    case KeyCode::Left:
        MoveCursor(-1, false);
        return true;
    case KeyCode::Right:
        MoveCursor(1, false);
        return true;
    case KeyCode::Tab:
        TabTerminate(k, original_);
        return true;
    case KeyCode::Muhenkan:
    case KeyCode::F6:
        TerminateTranslit(L"alpha-hira", original_);
        return true;
    case KeyCode::Henkan:
        if (k.shift) TerminateTranslit(L"alpha-hwkata", original_);
        else TerminateTranslit(L"alpha-kata", original_);
        return true;
    case KeyCode::F7:
        TerminateTranslit(L"alpha-kata", original_);
        return true;
    case KeyCode::F8:
        TerminateTranslit(L"alpha-hwkata", original_);
        return true;
    case KeyCode::F9:
        TerminateTranslit(L"hw-fw", original_);
        return true;
    case KeyCode::F10:
        FinishCommit(original_);
        return true;
    case KeyCode::Char:
        if (k.ctrl && !k.alt) {
            wchar_t c = k.ch | 0x20;
            switch (c) {
            case L'g': case L'c': CancelConvert(); return true;
            case L'h': PrevPage(); return true;
            case L'm': case L'j': FinishCommit(convText_); return true;
            case L'n': MoveCursor(10, true); return true;
            case L'p': MoveCursor(-10, true); return true;
            case L'f': MoveCursor(1, false); return true;
            case L'b': MoveCursor(-1, false); return true;
            case L'i': TerminateTranslit(L"hw-fw", original_); return true;
            default: break;
            }
        }
        break;
    default:
        break;
    }
    // Not a conversion key: cancel the conversion and process the key
    // again with the original string (as quail-naggy.el does).
    CancelConvert();
    return ProcessInput(k, passThrough) || true;
}

bool Engine::GetCandidateView(CandidateView& v) const {
    if (state_ != State::Convert) return false;
    int n = Planes();
    const std::vector<int>& cand = planes_[plane_ % n];
    for (int i = 0; i < 40; ++i) {
        CandidateView::Cell& c = v.cells[i];
        c = CandidateView::Cell();
        c.index = cand[i];
        if (c.index >= 0) {
            c.text = cands_[c.index].word;
            c.highlight = cands_[c.index].highlight;
        }
        c.home = (i == kHomeSlots[0] || i == kHomeSlots[1]);
        c.cursor = (i == vkCursor_);
    }
    v.plane = plane_ % n;
    v.planes = n;
    v.status.clear();
    if (kouho_ >= 0) {
        v.status = cands_[kouho_].word;
        if (!cands_[kouho_].comment.empty()) v.status += L"; " + cands_[kouho_].comment;
    }
    return true;
}

}  // namespace naggy
