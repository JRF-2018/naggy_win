// DllMain.cpp - DLL entry points and class factory.
#include "Globals.h"
#include "TextService.h"
#include "Log.h"

namespace naggy_win {

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = 0;
CRITICAL_SECTION g_cs;

const CLSID CLSID_NaggyTextService = {
    0x851460b4, 0x6e41, 0x402e, {0xb8, 0x36, 0xfd, 0xef, 0xa0, 0x5f, 0x52, 0x7f}};
const GUID GUID_NaggyProfile = {
    0xdd579c2d, 0x71ee, 0x4761, {0x88, 0x46, 0xe8, 0x46, 0x65, 0xe3, 0x72, 0x8e}};
const GUID GUID_NaggyDisplayAttributeInput = {
    0x6a222d64, 0x7de8, 0x4a33, {0x81, 0x85, 0xfc, 0x23, 0x45, 0x56, 0x33, 0x09}};
const GUID GUID_NaggyDisplayAttributeConverted = {
    0x8d056a5c, 0x7a28, 0x4575, {0x93, 0x63, 0x0e, 0x29, 0x30, 0x7e, 0xc9, 0xb8}};

void DllAddRef() { InterlockedIncrement(&g_cRefDll); }
void DllRelease() { InterlockedDecrement(&g_cRefDll); }

std::wstring ModulePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(g_hInst, buf, ARRAYSIZE(buf));
    return std::wstring(buf, n);
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FindServerExe() {
    std::wstring custom = GetSettingString(L"ServerPath", L"");
    if (!custom.empty() && FileExists(custom)) return custom;
    std::wstring dir = ModulePath();
    size_t p = dir.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L"";
    dir = dir.substr(0, p);
    // The installer records the installation directory (64-bit registry
    // view; the DLLs themselves live in the system directory).
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRegKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH];
        DWORD size = sizeof(buf), type = 0;
        if (RegQueryValueExW(key, L"InstallDir", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) ==
                ERROR_SUCCESS &&
            type == REG_SZ) {
            std::wstring exe = std::wstring(buf) + L"\\naggy-server.exe";
            if (FileExists(exe)) {
                RegCloseKey(key);
                return exe;
            }
        }
        RegCloseKey(key);
    }
    if (FileExists(dir + L"\\naggy-server.exe")) return dir + L"\\naggy-server.exe";
    p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos) {
        std::wstring up = dir.substr(0, p) + L"\\naggy-server.exe";
        if (FileExists(up)) return up;
    }
    return L"";
}

static bool ReadDword(HKEY root, const wchar_t* name, DWORD* v) {
    DWORD size = sizeof(DWORD), type = 0;
    return RegGetValueW(root, kRegKey, name, RRF_RT_REG_DWORD, &type, v, &size) == ERROR_SUCCESS;
}

DWORD GetSettingDword(const wchar_t* name, DWORD def) {
    DWORD v;
    if (ReadDword(HKEY_CURRENT_USER, name, &v)) return v;
    if (ReadDword(HKEY_LOCAL_MACHINE, name, &v)) return v;
    return def;
}

static bool ReadString(HKEY root, const wchar_t* name, std::wstring* out) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, kRegKey, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return false;
    *out = buf;
    return true;
}

std::wstring GetSettingString(const wchar_t* name, const wchar_t* def) {
    std::wstring s;
    if (ReadString(HKEY_CURRENT_USER, name, &s)) return s;
    if (ReadString(HKEY_LOCAL_MACHINE, name, &s)) return s;
    return def;
}

class CClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddRef();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        DllRelease();
        return 1;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        CTextService* ts = new (std::nothrow) CTextService();
        if (!ts) return E_OUTOFMEMORY;
        HRESULT hr = ts->QueryInterface(riid, ppv);
        ts->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) DllAddRef();
        else DllRelease();
        return S_OK;
    }
};

static CClassFactory g_factory;

}  // namespace naggy_win

using namespace naggy_win;

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_cs);
        SetLogEnabled(GetSettingDword(L"DebugLog", 0) != 0);
        Log(L"DLL loaded: %s", ModulePath().c_str());
        break;
    case DLL_PROCESS_DETACH:
        DeleteCriticalSection(&g_cs);
        break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_NaggyTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    Log(L"DllGetClassObject");
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_cRefDll <= 0 ? S_OK : S_FALSE; }

HRESULT RegisterServer();
HRESULT UnregisterServer();

STDAPI DllRegisterServer() { return RegisterServer(); }
STDAPI DllUnregisterServer() { return UnregisterServer(); }
