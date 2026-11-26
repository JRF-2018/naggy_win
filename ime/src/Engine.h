// Engine.h - the input state machine of naggy_win (portable C++).
//
// This is a port of the behavior of quail-naggy.el: the user types a
// romaji (ASCII) string, which is shown as is, and then chooses how to
// fix it: as it is (Enter), as hiragana (Muhenkan), katakana (Henkan),
// full-width (Tab), or by tankanji/word conversion (Space) with the
// 40-key virtual keyboard candidate window.
#pragma once
#include <string>
#include <vector>

#include "Protocol.h"

namespace naggy {

class IBackend {
public:
    virtual ~IBackend() {}
    // Sends a command; returns false on a communication failure.
    virtual bool Command(const std::vector<std::wstring>& args, Response& r) = 0;
};

enum class KeyCode {
    None, Modifier, Char, Space, Enter, Escape, Backspace, Delete,
    Left, Right, Up, Down, Home, End, PageUp, PageDown, Tab,
    Muhenkan, Henkan, F6, F7, F8, F9, F10, Other
};

struct KeyEvent {
    KeyCode code = KeyCode::None;
    wchar_t ch = 0;   // for Char: the produced ASCII character (0x21..0x7E)
    int pos = -1;     // virtual keyboard position (0..39) of the physical key
    bool shift = false, ctrl = false, alt = false;
};

struct EngineOptions {
    bool shiftSpaceAltConvert = true;  // S-SPC appends ":" and converts
    // ASCII (101/104) keyboard layout as in vkegg.el: the key right of ';'
    // (') types ':' and ':' types ', Tab = hiragana, S-Tab = katakana,
    // C-Tab = full-width.
    bool asciiLayout = false;
};

struct Candidate {
    std::wstring word;
    int pos = 0;
    bool highlight = false;
    std::wstring comment;
};

struct CandidateView {
    struct Cell {
        int index = -1;           // candidate index or -1
        std::wstring text;
        bool highlight = false;
        bool home = false;        // home position (f / j)
        bool cursor = false;
    } cells[40];
    int plane = 0;                // 0-based displayed plane
    int planes = 0;
    std::wstring status;          // current candidate (and comment)
};

class Engine {
public:
    enum class State { Idle, Input, Convert };

    explicit Engine(IBackend* backend) : backend_(backend) {}
    void SetOptions(const EngineOptions& o) { opt_ = o; }

    // True if ProcessKey would eat the key (no side effects).
    bool WouldEat(const KeyEvent& k) const;
    // Processes a key.  Returns true if the key was eaten.  When
    // *passThrough is set the composition was committed and the key should
    // still be passed to the application.
    bool ProcessKey(const KeyEvent& k, bool* passThrough);

    // Clears everything without committing.
    void Reset();
    // Commits the current composition text as it is.
    void CommitAsIs();

    State GetState() const { return state_; }
    // Text to insert into the document (and end the composition).
    std::wstring TakeCommit() { std::wstring s; s.swap(commit_); return s; }
    bool HasCommit() const { return !commit_.empty(); }
    bool TakeBeep() { bool b = beep_; beep_ = false; return b; }
    // The text shown inline (the composition) and the caret in it.
    std::wstring Composition() const;
    size_t Caret() const;

    bool GetCandidateView(CandidateView& v) const;
    // Mouse: select the candidate at slot of the current plane.
    bool SelectSlot(int slot, bool terminate);
    std::wstring LastError() const { return lastError_; }

private:
    bool ProcessInput(const KeyEvent& k, bool* passThrough);
    bool ProcessConvert(const KeyEvent& k, bool* passThrough);
    void Insert(wchar_t c);
    void StartConvert(const std::wstring& s);
    void TerminateTranslit(const std::wstring& spec, const std::wstring& s);
    void TabTerminate(const KeyEvent& k, const std::wstring& s);
    wchar_t MapChar(wchar_t c) const;
    void FinishCommit(std::wstring s);
    void CancelConvert();
    void Display();
    void InsertKouho();
    int FirstKouhoOfPlane(int plane) const;
    int Planes() const { return static_cast<int>(planes_.size()); }
    void NextPage();
    void PrevPage();
    void MoveCursor(int delta, bool vertical);
    bool Translit(const std::wstring& spec, const std::wstring& s, std::wstring& out);

    IBackend* backend_;
    EngineOptions opt_;
    State state_ = State::Idle;
    std::wstring buf_;
    size_t cursor_ = 0;
    std::wstring commit_;
    bool beep_ = false;
    std::wstring lastError_;
    std::wstring prevModification_;

    // conversion
    std::wstring original_;
    size_t originalCursor_ = 0;
    std::vector<Candidate> cands_;
    std::vector<std::vector<int>> planes_;  // [plane][slot] -> index or -1
    int plane_ = 0;
    int kouho_ = -1;     // current candidate index, -1 = nil
    int vkCursor_ = -1;  // current slot, -1 = nil
    std::wstring convText_;
};

// Truncates a string to the given number of display columns
// (full-width characters count as 2), padding with pad.
std::wstring TruncateToWidth(const std::wstring& s, int width, wchar_t pad);
int CharWidth(unsigned long c);

}  // namespace naggy
