// Globals.h - shared definitions of the naggy_win text service.
#pragma once
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msctf.h>
#include <olectl.h>

#include <string>

namespace naggy_win {

extern HINSTANCE g_hInst;
extern LONG g_cRefDll;
extern CRITICAL_SECTION g_cs;

void DllAddRef();
void DllRelease();

// {851460B4-6E41-402E-B836-FDEFA05F527F}
extern const CLSID CLSID_NaggyTextService;
// {DD579C2D-71EE-4761-8846-E84665E3728E}
extern const GUID GUID_NaggyProfile;
// {6A222D64-7DE8-4A33-8185-FC2345563309}
extern const GUID GUID_NaggyDisplayAttributeInput;
// {8D056A5C-7A28-4575-9363-0E29307EC9B8}
extern const GUID GUID_NaggyDisplayAttributeConverted;

const LANGID kLangId = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
const wchar_t kDescription[] = L"Naggy (\x5358\x6F22\x5B57\x5909\x63DB)";  // 単漢字変換
const wchar_t kRegKey[] = L"Software\\naggy_win";

// Path of this DLL.
std::wstring ModulePath();
// Directory that contains naggy-server.exe (the DLL's parent directory
// or the DLL's directory).
std::wstring FindServerExe();

// Reads a DWORD / string setting from HKCU\Software\naggy_win, falling
// back to HKLM\Software\naggy_win.
DWORD GetSettingDword(const wchar_t* name, DWORD def);
std::wstring GetSettingString(const wchar_t* name, const wchar_t* def);

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

}  // namespace naggy_win
