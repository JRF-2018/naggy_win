// NaggyConfig.cpp - settings window of naggy_win (naggy-config.exe).
//
// * Input settings: HKCU\Software\naggy_win (read by the IME; the IME
//   reloads them when SettingsSerial changes).
// * Dictionaries: runs "naggy-dictool setup" elevated, then reloads
//   naggy-server.
// * Backend: %APPDATA%\naggy_win\naggy-server.ini ("backend = ...").
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <strsafe.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

const wchar_t kRegKey[] = L"Software\\naggy_win";

enum {
    IDC_STARTOPEN = 100, IDC_SHIFTSPACE, IDC_LAYOUT_JIS, IDC_LAYOUT_ASCII, IDC_FONTNAME,
    IDC_FONTSIZE, IDC_DEBUGLOG,
    IDC_CURDIC, IDC_DIC_JRF, IDC_DIC_KAZE, IDC_DIC_NONE, IDC_KAZEDIR, IDC_KAZEBROWSE,
    IDC_OPT_SKK, IDC_OPT_EMOJI, IDC_OPT_CT, IDC_DICAPPLY, IDC_EDITINIT,
    IDC_BE_BUILTIN, IDC_BE_EXTERNAL, IDC_BE_COMMAND, IDC_RESTART, IDC_OPENLOG,
    IDC_OK, IDC_CANCEL, IDC_APPLY,
};

HINSTANCE g_inst;
HWND g_wnd;
HFONT g_font;
int g_dpi = 96;
std::wstring g_dir;  // directory of this exe (= installation directory)

int S(int v) { return MulDiv(v, g_dpi, 96); }

// --- registry -----------------------------------------------------------

DWORD GetDword(const wchar_t* name, DWORD def) {
    DWORD v, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
        return v;
    size = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kRegKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
        return v;
    return def;
}

std::wstring GetString(const wchar_t* name, const wchar_t* def) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    return def;
}

void SetDword(const wchar_t* name, DWORD v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_DWORD, &v, sizeof(v));
}

void SetString(const wchar_t* name, const std::wstring& v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_SZ, v.c_str(),
                    static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

// --- files ----------------------------------------------------------------

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ReadFileBytes(const std::wstring& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::vector<std::wstring> Lines(const std::wstring& s) {
    std::vector<std::wstring> r;
    std::wstringstream ss(s);
    std::wstring l;
    while (std::getline(ss, l)) {
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        r.push_back(l);
    }
    return r;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t");
    return s.substr(a, b - a + 1);
}

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH)) return L"";
    return std::wstring(buf) + L"\\naggy_win";
}

std::wstring LocalAppDataDir() {
    wchar_t buf[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) return L"";
    return std::wstring(buf) + L"\\naggy_win";
}

// --- naggy-server ---------------------------------------------------------

std::wstring PipeName() {
    std::wstring sid = L"unknown";
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        BYTE buf[256];
        DWORD n = 0;
        if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n)) {
            LPWSTR s = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &s)) {
                sid = s;
                LocalFree(s);
            }
        }
        CloseHandle(tok);
    }
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    wchar_t name[256];
    StringCchPrintfW(name, ARRAYSIZE(name), L"\\\\.\\pipe\\naggy_win.%s.%u", sid.c_str(), session);
    return name;
}

// Sends one command; returns the raw response (empty on failure).
std::string ServerCommand(const char* cmd) {
    HANDLE h = CreateFileW(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string line = std::string(cmd) + "\n";
    DWORD n;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
    std::string r;
    char buf[4096];
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
        r.append(buf, n);
        if (r.find("# end\n") != std::string::npos || r.find("# error") != std::string::npos) break;
    }
    CloseHandle(h);
    return r;
}

bool ServerRunning() {
    HANDLE h = CreateFileW(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return true;
    }
    return GetLastError() == ERROR_PIPE_BUSY;
}

bool StartServer() {
    std::wstring exe = g_dir + L"\\naggy-server.exe";
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr,
                        g_dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    for (int i = 0; i < 100 && !ServerRunning(); ++i) Sleep(100);
    return ServerRunning();
}

bool RestartServer() {
    ServerCommand("naggy-server-quit");
    for (int i = 0; i < 30 && ServerRunning(); ++i) Sleep(100);
    return StartServer();
}

// --- dictionaries -----------------------------------------------------------

struct DicState {
    int tankanji = 0;  // 0 jrf, 1 kaze, 2 none
    bool skk = true, emoji = true, ct = true;
    std::wstring summary;
};

DicState ReadDicState() {
    DicState d;
    std::wstring init = g_dir + L"\\site-init.nginit";
    if (!FileExists(init)) {
        d.summary = L"(site-init.nginit \x304C\x3042\x308A\x307E\x305B\x3093)";  // がありません
        return d;
    }
    bool hasTk = false, skk = false, emoji = false, ct = false;
    std::wstring sum, skkNames;
    for (auto& l : Lines(Utf8ToWide(ReadFileBytes(init)))) {
        std::wstring t = Trim(l);
        if (t.empty() || t[0] == L'#' || t[0] == L'.') continue;
        if (t.compare(0, 16, L"set-tankanji-dic") == 0) {
            hasTk = true;
            d.tankanji = (t.find(L"Wind2") != std::wstring::npos) ? 1 : 0;
            sum += L"\x5358\x6F22\x5B57: " + Trim(t.substr(16)) + L"\r\n";  // 単漢字:
        } else if (t.compare(0, 11, L"add-skk-dic") == 0) {
            std::wstring f = Trim(t.substr(11));
            if (f.find(L"SKK-JISYO.L") != std::wstring::npos || f.find(L"SKK-JISYO.fixed.L") != std::wstring::npos) skk = true;
            if (f.find(L"emoji") != std::wstring::npos) emoji = true;
            if (f.find(L"china_taiwan") != std::wstring::npos) ct = true;
            size_t sp = f.find_first_of(L" \t");
            skkNames += (skkNames.empty() ? L"" : L", ") + (sp == std::wstring::npos ? f : f.substr(0, sp));
        }
    }
    if (!hasTk) d.tankanji = 2;
    if (!skkNames.empty()) sum += L"SKK: " + skkNames;
    d.skk = skk;
    d.emoji = emoji;
    d.ct = ct;
    d.summary = sum;
    return d;
}

std::wstring FindKazeDir() {
    wchar_t win[MAX_PATH], pd[MAX_PATH], pf[MAX_PATH], pf86[MAX_PATH];
    GetEnvironmentVariableW(L"SystemRoot", win, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
    if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", pf86, MAX_PATH)) pf86[0] = 0;
    std::vector<std::wstring> dirs = {win, std::wstring(pd) + L"\\Wind2", std::wstring(pf) + L"\\Wind2",
                                      std::wstring(pf86) + L"\\Wind2", std::wstring(pf86) + L"\\Wind"};
    for (auto& d : dirs)
        if (FileExists(d + L"\\Wind2.rea") && FileExists(d + L"\\Wind2.dic")) return d;
    return L"";
}

// --- UI ----------------------------------------------------------------------

HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
         DWORD exStyle = 0) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    if ((style & 0xF) == BS_GROUPBOX && wcscmp(cls, L"BUTTON") == 0)
        SetWindowPos(c, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return c;
}

HWND Item(int id) { return GetDlgItem(g_wnd, id); }
bool Checked(int id) { return SendMessageW(Item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void Check(int id, bool b) { SendMessageW(Item(id), BM_SETCHECK, b ? BST_CHECKED : BST_UNCHECKED, 0); }
std::wstring Text(int id) {
    int n = GetWindowTextLengthW(Item(id));
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(Item(id), &s[0], n + 1);
    s.resize(n);
    return s;
}

std::wstring ServerIniPath() { return AppDataDir() + L"\\naggy-server.ini"; }

std::wstring ReadBackend() {
    std::wstring r;
    for (const std::wstring& p : {g_dir + L"\\naggy-server.ini", ServerIniPath()}) {
        if (!FileExists(p)) continue;
        for (auto& l : Lines(Utf8ToWide(ReadFileBytes(p)))) {
            std::wstring t = Trim(l);
            if (t.compare(0, 7, L"backend") == 0) {
                size_t e = t.find(L'=');
                if (e != std::wstring::npos) r = Trim(t.substr(e + 1));
            }
        }
    }
    return r;
}

void WriteBackend(const std::wstring& backend) {
    CreateDirectoryW(AppDataDir().c_str(), nullptr);
    std::wstring p = ServerIniPath();
    std::vector<std::wstring> out;
    bool done = false;
    if (FileExists(p)) {
        for (auto& l : Lines(Utf8ToWide(ReadFileBytes(p)))) {
            std::wstring t = Trim(l);
            if (t.compare(0, 7, L"backend") == 0 && t.find(L'=') != std::wstring::npos) {
                if (!done) out.push_back(L"backend = " + backend);
                done = true;
            } else {
                out.push_back(l);
            }
        }
    }
    if (!done) out.push_back(L"backend = " + backend);
    std::wstring all;
    for (auto& l : out) all += l + L"\r\n";
    std::ofstream f(p, std::ios::binary);
    std::string u = WideToUtf8(all);
    f.write(u.data(), u.size());
}

void UpdateEnables() {
    bool kaze = Checked(IDC_DIC_KAZE);
    EnableWindow(Item(IDC_KAZEDIR), kaze);
    EnableWindow(Item(IDC_KAZEBROWSE), kaze);
    EnableWindow(Item(IDC_BE_COMMAND), Checked(IDC_BE_EXTERNAL));
}

void LoadValues() {
    Check(IDC_STARTOPEN, GetDword(L"StartOpen", 0) != 0);
    Check(IDC_SHIFTSPACE, GetDword(L"ShiftSpaceAltConvert", 1) != 0);
    bool ascii = GetDword(L"KeyLayout", 0) == 1;
    Check(IDC_LAYOUT_JIS, !ascii);
    Check(IDC_LAYOUT_ASCII, ascii);
    SetWindowTextW(Item(IDC_FONTNAME), GetString(L"FontName", L"Meiryo UI").c_str());
    SetWindowTextW(Item(IDC_FONTSIZE), std::to_wstring(GetDword(L"FontSize", 14)).c_str());
    Check(IDC_DEBUGLOG, GetDword(L"DebugLog", 0) != 0);

    DicState d = ReadDicState();
    SetWindowTextW(Item(IDC_CURDIC), d.summary.c_str());
    Check(IDC_DIC_JRF, d.tankanji == 0);
    Check(IDC_DIC_KAZE, d.tankanji == 1);
    Check(IDC_DIC_NONE, d.tankanji == 2);
    std::wstring kz = GetString(L"KazeDir", L"");
    if (kz.empty()) kz = FindKazeDir();
    SetWindowTextW(Item(IDC_KAZEDIR), kz.c_str());
    Check(IDC_OPT_SKK, d.skk);
    Check(IDC_OPT_EMOJI, d.emoji);
    Check(IDC_OPT_CT, d.ct);

    std::wstring be = ReadBackend();
    bool builtin = be.empty() || _wcsicmp(be.c_str(), L"builtin") == 0;
    Check(IDC_BE_BUILTIN, builtin);
    Check(IDC_BE_EXTERNAL, !builtin);
    SetWindowTextW(Item(IDC_BE_COMMAND), builtin ? L"" : be.c_str());
    UpdateEnables();
}

std::wstring g_loadedBackend;

bool ApplyValues() {
    SetDword(L"StartOpen", Checked(IDC_STARTOPEN));
    SetDword(L"ShiftSpaceAltConvert", Checked(IDC_SHIFTSPACE));
    SetDword(L"KeyLayout", Checked(IDC_LAYOUT_ASCII) ? 1 : 0);
    SetString(L"FontName", Text(IDC_FONTNAME));
    int size = _wtoi(Text(IDC_FONTSIZE).c_str());
    if (size < 6 || size > 72) size = 14;
    SetDword(L"FontSize", size);
    SetDword(L"DebugLog", Checked(IDC_DEBUGLOG));
    SetString(L"KazeDir", Text(IDC_KAZEDIR));
    SetDword(L"SettingsSerial", GetDword(L"SettingsSerial", 0) + 1);

    std::wstring be = Checked(IDC_BE_BUILTIN) ? L"builtin" : Trim(Text(IDC_BE_COMMAND));
    if (be.empty()) be = L"builtin";
    std::wstring cur = ReadBackend();
    if (cur.empty()) cur = L"builtin";
    if (be != cur) {
        WriteBackend(be);
        if (!RestartServer())
            MessageBoxW(g_wnd, L"naggy-server \x3092\x8D77\x52D5\x3067\x304D\x307E\x305B\x3093\x3067\x3057\x305F\x3002"
                               L"\r\n%LOCALAPPDATA%\\naggy_win\\naggy-server.log \x3092\x898B\x3066\x304F\x3060\x3055\x3044\x3002",
                        L"naggy_win", MB_ICONWARNING);
    }
    return true;
}

void BrowseKaze() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opt;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(dlg->Show(g_wnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(Item(IDC_KAZEDIR), path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
}

// Runs naggy-dictool setup elevated and reloads the server.
void ApplyDictionaries() {
    std::wstring tk = Checked(IDC_DIC_KAZE) ? L"kaze" : Checked(IDC_DIC_NONE) ? L"none" : L"jrf";
    std::wstring kz = Trim(Text(IDC_KAZEDIR));
    if (tk == L"kaze" && !(FileExists(kz + L"\\Wind2.rea") && FileExists(kz + L"\\Wind2.dic")) &&
        !FileExists(kz + L"\\Wind2.txt")) {
        MessageBoxW(g_wnd,
                    L"\x6307\x5B9A\x3055\x308C\x305F\x30D5\x30A9\x30EB\x30C0\x306B Wind2.rea / Wind2.dic "
                    L"(\x307E\x305F\x306F Wind2.txt) \x304C\x3042\x308A\x307E\x305B\x3093\x3002",
                    L"naggy_win", MB_ICONERROR);
        return;
    }
    if (MessageBoxW(g_wnd,
                    L"\x8F9E\x66F8\x3092\x30C0\x30A6\x30F3\x30ED\x30FC\x30C9\x30FB\x5909\x63DB\x3057\x3001"
                    L"site-init.nginit \x3092\x66F8\x304D\x63DB\x3048\x307E\x3059\x3002\r\n"
                    L"(\x7BA1\x7406\x8005\x6A29\x9650\x304C\x5FC5\x8981\x3067\x3059\x3002\x4EE5\x524D\x306E "
                    L"site-init.nginit \x306F\x4E0A\x66F8\x304D\x3055\x308C\x307E\x3059\x3002)",
                    L"naggy_win", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring log = std::wstring(tmp) + L"naggy-dictool-setup.log";
    DeleteFileW(log.c_str());
    std::wstring args = L"/c \"\"" + g_dir + L"\\naggy-dictool.exe\" setup --force --dest \"" + g_dir +
                        L"\" --tankanji " + tk;
    if (tk == L"kaze") args += L" --kaze-dir \"" + kz + L"\"";
    if (!Checked(IDC_OPT_SKK)) args += L" --no-skk";
    if (!Checked(IDC_OPT_EMOJI)) args += L" --no-emoji";
    if (!Checked(IDC_OPT_CT)) args += L" --no-china-taiwan";
    args += L" > \"" + log + L"\" 2>&1\"";
    wchar_t cmd[MAX_PATH];
    GetEnvironmentVariableW(L"ComSpec", cmd, MAX_PATH);
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = g_wnd;
    sei.lpVerb = L"runas";
    sei.lpFile = cmd;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        SetCursor(old);
        return;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    SetCursor(old);
    SetString(L"KazeDir", kz);
    std::wstring msg = Utf8ToWide(ReadFileBytes(log));
    if (code == 0) {
        std::string r = ServerCommand("naggy-server-reload");
        if (r.find("# begin unit") == std::string::npos) RestartServer();
        MessageBoxW(g_wnd, (L"\x8F9E\x66F8\x3092\x66F4\x65B0\x3057\x307E\x3057\x305F\x3002\r\n\r\n" + msg).c_str(),
                    L"naggy_win", MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_wnd, (L"\x8F9E\x66F8\x306E\x66F4\x65B0\x306B\x5931\x6557\x3057\x307E\x3057\x305F\x3002\r\n\r\n" + msg).c_str(),
                    L"naggy_win", MB_ICONERROR);
    }
    LoadValues();
}

void EditInit() {
    std::wstring init = g_dir + L"\\site-init.nginit";
    ShellExecuteW(g_wnd, L"runas", L"notepad.exe", (L"\"" + init + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void CreateControls() {
    int y = 8;
    const DWORD grp = BS_GROUPBOX;
    const wchar_t* B = L"BUTTON";
    const wchar_t* E = L"EDIT";
    const wchar_t* T = L"STATIC";
    // input
    Ctl(B, L"\x5165\x529B", grp, 8, y, 504, 178, 0);  // 入力
    Ctl(B, L"IME \x3092\x9078\x3093\x3060\x3068\x304D\x6700\x521D\x304B\x3089\x30AA\x30F3\x306B\x3059\x308B",
        BS_AUTOCHECKBOX | WS_TABSTOP, 20, y + 20, 480, 20, IDC_STARTOPEN);  // IME を選んだとき最初からオンにする
    Ctl(B, L"Shift+Space \x3067\x300C:\x300D\x3092\x4ED8\x3051\x3066\x5909\x63DB\x3059\x308B (\x5358\x8A9E\x5909\x63DB)",
        BS_AUTOCHECKBOX | WS_TABSTOP, 20, y + 42, 480, 20, IDC_SHIFTSPACE);
    Ctl(T, L"\x30AD\x30FC\x914D\x5217:", 0, 20, y + 68, 70, 20, 0);  // キー配列:
    Ctl(B, L"JIS \x30AD\x30FC\x30DC\x30FC\x30C9 (\x7121\x5909\x63DB/\x5909\x63DB, Tab = \x5168\x89D2)",
        BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 90, y + 66, 410, 20, IDC_LAYOUT_JIS);
    Ctl(B, L"ASCII \x30AD\x30FC\x30DC\x30FC\x30C9 (vkegg \x98A8: ' \x3068 : \x3092\x5165\x308C\x66FF\x3048, "
           L"Tab = \x3072\x3089\x304C\x306A, S-Tab = \x30AB\x30BF\x30AB\x30CA, C-Tab = \x5168\x89D2)",
        BS_AUTORADIOBUTTON | BS_MULTILINE, 90, y + 88, 410, 34, IDC_LAYOUT_ASCII);
    Ctl(T, L"\x5019\x88DC\x306E\x30D5\x30A9\x30F3\x30C8:", 0, 20, y + 130, 100, 20, 0);  // 候補のフォント:
    Ctl(E, L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP | WS_GROUP, 120, y + 128, 180, 22, IDC_FONTNAME, WS_EX_CLIENTEDGE);
    Ctl(T, L"\x30B5\x30A4\x30BA:", 0, 310, y + 130, 50, 20, 0);  // サイズ:
    Ctl(E, L"", WS_BORDER | ES_NUMBER | WS_TABSTOP, 360, y + 128, 50, 22, IDC_FONTSIZE, WS_EX_CLIENTEDGE);
    Ctl(B, L"\x30C7\x30D0\x30C3\x30B0\x30ED\x30B0 (ime.log)", BS_AUTOCHECKBOX | WS_TABSTOP, 20, y + 154, 200, 20,
        IDC_DEBUGLOG);  // デバッグログ
    Ctl(B, L"\x30ED\x30B0\x30D5\x30A9\x30EB\x30C0", WS_TABSTOP, 230, y + 152, 120, 24, IDC_OPENLOG);  // ログフォルダ
    y += 188;
    // dictionaries
    Ctl(B, L"\x8F9E\x66F8 (\x5909\x66F4\x306B\x306F\x7BA1\x7406\x8005\x6A29\x9650\x304C\x5FC5\x8981)", grp, 8, y, 504,
        232, 0);  // 辞書 (変更には管理者権限が必要)
    Ctl(T, L"", SS_LEFT, 20, y + 20, 480, 52, IDC_CURDIC);
    Ctl(B, L"jrf_tankanji-2026 (\x30C0\x30A6\x30F3\x30ED\x30FC\x30C9)",
        BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 20, y + 76, 300, 20, IDC_DIC_JRF);
    Ctl(B, L"\x300E\x98A8\x300F\x306E\x8F9E\x66F8 (\x300E\x98A8\x300F\x306E\x5229\x7528\x6A29\x304C\x5FC5\x8981)",
        BS_AUTORADIOBUTTON, 20, y + 98, 300, 20, IDC_DIC_KAZE);
    Ctl(E, L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP | WS_GROUP, 40, y + 120, 370, 22, IDC_KAZEDIR, WS_EX_CLIENTEDGE);
    Ctl(B, L"\x53C2\x7167...", WS_TABSTOP, 416, y + 119, 84, 24, IDC_KAZEBROWSE);  // 参照...
    Ctl(B, L"\x5358\x6F22\x5B57\x8F9E\x66F8\x306A\x3057 (\x5358\x8A9E\x5909\x63DB\x306E\x307F)",
        BS_AUTORADIOBUTTON | WS_GROUP, 20, y + 146, 300, 20, IDC_DIC_NONE);
    Ctl(B, L"SKK-JISYO.L", BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 20, y + 172, 120, 20, IDC_OPT_SKK);
    Ctl(B, L"\x7D75\x6587\x5B57", BS_AUTOCHECKBOX | WS_TABSTOP, 150, y + 172, 80, 20, IDC_OPT_EMOJI);  // 絵文字
    Ctl(B, L"\x4E2D\x56FD\x30FB\x53F0\x6E7E\x5730\x540D", BS_AUTOCHECKBOX | WS_TABSTOP, 240, y + 172, 130, 20,
        IDC_OPT_CT);  // 中国・台湾地名
    Ctl(B, L"\x8F9E\x66F8\x3092\x66F4\x65B0...", WS_TABSTOP, 20, y + 198, 150, 26, IDC_DICAPPLY);  // 辞書を更新...
    Ctl(B, L"site-init.nginit \x3092\x7DE8\x96C6...", WS_TABSTOP, 180, y + 198, 180, 26, IDC_EDITINIT);
    y += 242;
    // backend
    Ctl(B, L"\x5909\x63DB\x30A8\x30F3\x30B8\x30F3 (naggy-server)", grp, 8, y, 504, 108, 0);  // 変換エンジン
    Ctl(B, L"\x5185\x8535 (Go \x7248 naggy-backend)", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 20, y + 20, 300,
        20, IDC_BE_BUILTIN);  // 内蔵
    Ctl(B, L"\x5916\x90E8\x30B3\x30DE\x30F3\x30C9 (\x4F8B: \"C:\\...\\perl.exe\" \"C:\\...\\naggy-backend.pl\")",
        BS_AUTORADIOBUTTON, 20, y + 42, 470, 20, IDC_BE_EXTERNAL);  // 外部コマンド
    Ctl(E, L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP | WS_GROUP, 40, y + 64, 460, 22, IDC_BE_COMMAND,
        WS_EX_CLIENTEDGE);
    y += 118;
    Ctl(B, L"naggy-server \x3092\x518D\x8D77\x52D5", WS_TABSTOP, 8, y, 160, 26, IDC_RESTART);  // を再起動
    Ctl(B, L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 260, y, 80, 26, IDC_OK);
    Ctl(B, L"\x30AD\x30E3\x30F3\x30BB\x30EB", WS_TABSTOP, 346, y, 80, 26, IDC_CANCEL);  // キャンセル
    Ctl(B, L"\x9069\x7528", WS_TABSTOP, 432, y, 80, 26, IDC_APPLY);                      // 適用
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DIC_JRF:
        case IDC_DIC_KAZE:
        case IDC_DIC_NONE:
        case IDC_BE_BUILTIN:
        case IDC_BE_EXTERNAL:
            UpdateEnables();
            break;
        case IDC_KAZEBROWSE:
            BrowseKaze();
            break;
        case IDC_DICAPPLY:
            ApplyDictionaries();
            break;
        case IDC_EDITINIT:
            EditInit();
            break;
        case IDC_OPENLOG: {
            std::wstring d = LocalAppDataDir();
            CreateDirectoryW(d.c_str(), nullptr);
            ShellExecuteW(hwnd, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case IDC_RESTART:
            if (!RestartServer()) MessageBeep(MB_ICONWARNING);
            break;
        case IDC_OK:
            if (ApplyValues()) DestroyWindow(hwnd);
            break;
        case IDC_APPLY:
            ApplyValues();
            break;
        case IDC_CANCEL:
        case IDCANCEL:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// naggy-config.exe --menu X Y [L T R B]: the context menu of the input
// mode indicator (shown here because the IME may run inside the taskbar).
int ShowIndicatorMenu(int argc, wchar_t** argv) {
    POINT pt = {_wtoi(argv[2]), _wtoi(argv[3])};
    TPMPARAMS tpm = {sizeof(tpm)};
    TPMPARAMS* ptpm = nullptr;
    if (argc >= 8) {
        tpm.rcExclude = {_wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]), _wtoi(argv[7])};
        ptpm = &tpm;
    }
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, pt.x, pt.y, 1, 1, nullptr,
                                 nullptr, g_inst, nullptr);
    SetForegroundWindow(owner);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"\x8A2D\x5B9A... (&O)");                          // 設定...
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 2, L"naggy-server \x518D\x8AAD\x307F\x8FBC\x307F (&R)");  // 再読み込み
    AppendMenuW(m, MF_STRING, 3, L"naggy-server \x518D\x8D77\x52D5 (&S)");              // 再起動
    UINT id = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_VERTICAL |
                                      TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                               pt.x, pt.y, owner, ptpm);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m);
    DestroyWindow(owner);
    switch (id) {
    case 1: {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case 2:
        if (ServerCommand("naggy-server-reload").find("# begin unit") == std::string::npos)
            MessageBeep(MB_ICONWARNING);
        break;
    case 3:
        if (!RestartServer()) MessageBeep(MB_ICONWARNING);
        break;
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 4 && wcscmp(argv[1], L"--menu") == 0) {
            SetProcessDPIAware();
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            g_dir = exe;
            g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/"));
            int r = ShowIndicatorMenu(argc, argv);
            LocalFree(argv);
            return r;
        }
        if (argv) LocalFree(argv);
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\naggy_win_config");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"NaggyWinConfig", nullptr);
        if (w) SetForegroundWindow(w);
        return 0;
    }
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_dir = exe;
    g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/"));
    HDC dc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    NONCLIENTMETRICSW ncm = {sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"NaggyWinConfig";
    RegisterClassExW(&wc);
    RECT r = {0, 0, S(520), S(600)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    g_wnd = CreateWindowExW(0, L"NaggyWinConfig", L"naggy_win \x306E\x8A2D\x5B9A", style, CW_USEDEFAULT,
                            CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    CreateControls();
    LoadValues();
    ShowWindow(g_wnd, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(g_wnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
