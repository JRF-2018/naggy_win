// Log.cpp - optional debug log written to %LOCALAPPDATA%\naggy_win\ime.log.
#include "Log.h"

#include <cstdarg>
#include <strsafe.h>

#include "Globals.h"

namespace naggy_win {

static bool g_logEnabled = false;

void SetLogEnabled(bool b) { g_logEnabled = b; }
bool LogEnabled() { return g_logEnabled; }

void Log(const wchar_t* fmt, ...) {
    if (!g_logEnabled) return;
    wchar_t msg[1024];
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfW(msg, ARRAYSIZE(msg), fmt, ap);
    va_end(ap);
    wchar_t dir[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) return;
    std::wstring path = std::wstring(dir) + L"\\naggy_win";
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\ime.log";
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* base = wcsrchr(exe, L'\\');
    base = base ? base + 1 : exe;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1400];
    StringCchPrintfW(line, ARRAYSIZE(line), L"%02d:%02d:%02d.%03d %s[%lu:%lu] %s\r\n", st.wHour,
                     st.wMinute, st.wSecond, st.wMilliseconds, base, GetCurrentProcessId(),
                     GetCurrentThreadId(), msg);
    char utf8[4200];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (n <= 1) return;
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size;
    if (GetFileSizeEx(h, &size) && size.QuadPart > (4 << 20)) {
        CloseHandle(h);
        return;  // keep the log small
    }
    DWORD w;
    WriteFile(h, utf8, n - 1, &w, nullptr);
    CloseHandle(h);
}

}  // namespace naggy_win
