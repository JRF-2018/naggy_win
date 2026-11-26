// PipeBackend.cpp - IBackend over the naggy-server named pipe.
#include "PipeBackend.h"

#include <sddl.h>
#include <strsafe.h>

namespace naggy_win {

namespace {
const DWORD kTimeoutMs = 8000;

bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e = {};
    DWORD n = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n);
    CloseHandle(tok);
    return ok && e.TokenIsElevated;
}
}  // namespace

PipeBackend::PipeBackend() { event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

PipeBackend::~PipeBackend() {
    Disconnect();
    if (event_) CloseHandle(event_);
}

std::wstring PipeBackend::PipeName() {
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

void PipeBackend::Disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    rbuf_.clear();
}

bool PipeBackend::LaunchServer() {
    if (!allowLaunch_ || IsElevated()) return false;
    ULONGLONG now = GetTickCount64();
    if (lastLaunch_ && now - lastLaunch_ < 15000) return false;
    lastLaunch_ = now;
    std::wstring exe = FindServerExe();
    if (exe.empty()) return false;
    std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE,
                             flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, dir.c_str(), &si, &pi);
    if (!ok)
        ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags, nullptr,
                            dir.c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool PipeBackend::Connect() {
    if (pipe_ != INVALID_HANDLE_VALUE) return true;
    std::wstring name = PipeName();
    bool launched = false;
    ULONGLONG start = GetTickCount64();
    for (;;) {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            pipe_ = h;
            rbuf_.clear();
            return true;
        }
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(name.c_str(), 500);
        } else if (err == ERROR_FILE_NOT_FOUND && !launched) {
            if (!LaunchServer()) return false;
            launched = true;
        } else if (err == ERROR_FILE_NOT_FOUND) {
            Sleep(100);
        } else {
            return false;
        }
        if (GetTickCount64() - start > kTimeoutMs) return false;
    }
}

bool PipeBackend::WriteAll(const std::string& s, DWORD timeoutMs) {
    size_t off = 0;
    while (off < s.size()) {
        OVERLAPPED ov = {};
        ov.hEvent = event_;
        ResetEvent(event_);
        DWORD n = 0;
        BOOL ok = WriteFile(pipe_, s.data() + off, static_cast<DWORD>(s.size() - off), &n, &ov);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(event_, timeoutMs) != WAIT_OBJECT_0) {
                CancelIo(pipe_);
                GetOverlappedResult(pipe_, &ov, &n, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe_, &ov, &n, FALSE)) return false;
        }
        off += n;
    }
    return true;
}

bool PipeBackend::ReadLine(std::string& line, DWORD timeoutMs) {
    ULONGLONG start = GetTickCount64();
    for (;;) {
        size_t p = rbuf_.find('\n');
        if (p != std::string::npos) {
            line = rbuf_.substr(0, p);
            rbuf_.erase(0, p + 1);
            return true;
        }
        ULONGLONG el = GetTickCount64() - start;
        if (el >= timeoutMs) return false;
        char buf[8192];
        OVERLAPPED ov = {};
        ov.hEvent = event_;
        ResetEvent(event_);
        DWORD n = 0;
        BOOL ok = ReadFile(pipe_, buf, sizeof(buf), &n, &ov);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(event_, static_cast<DWORD>(timeoutMs - el)) != WAIT_OBJECT_0) {
                CancelIo(pipe_);
                GetOverlappedResult(pipe_, &ov, &n, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe_, &ov, &n, FALSE)) return false;
        }
        if (n == 0) return false;
        rbuf_.append(buf, n);
    }
}

bool PipeBackend::TryCommand(const std::string& line, naggy::Response& r) {
    if (!Connect()) return false;
    if (!WriteAll(line, kTimeoutMs)) {
        Disconnect();
        return false;
    }
    naggy::ResponseParser p;
    std::string l;
    while (ReadLine(l, kTimeoutMs)) {
        if (p.FeedLine(l)) {
            r = p.Result();
            return true;
        }
    }
    Disconnect();
    return false;
}

bool PipeBackend::Command(const std::vector<std::wstring>& args, naggy::Response& r) {
    std::string line = naggy::BuildCommand(args);
    if (TryCommand(line, r)) return true;
    // The server may have been restarted: retry once.
    return TryCommand(line, r);
}

}  // namespace naggy_win
