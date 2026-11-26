// Register.cpp - COM and TSF registration.
#include <strsafe.h>

#include "Globals.h"

using namespace naggy_win;

namespace {

const GUID kCategories[] = {
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    // Web browsers (Chromium, Firefox) activate TSF with
    // TF_TMAE_UIELEMENTENABLEDONLY and load only TIPs with this category.
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

std::wstring ClsidKey() {
    wchar_t guid[64];
    StringFromGUID2(CLSID_NaggyTextService, guid, ARRAYSIZE(guid));
    return std::wstring(L"CLSID\\") + guid;
}

LONG SetValue(HKEY key, const wchar_t* name, const std::wstring& v) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                          static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

HRESULT RegisterCom() {
    HKEY key = nullptr, sub = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, ClsidKey().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    SetValue(key, nullptr, kDescription);
    LONG r = RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &sub, nullptr);
    if (r == ERROR_SUCCESS) {
        SetValue(sub, nullptr, ModulePath());
        SetValue(sub, L"ThreadingModel", L"Apartment");
        RegCloseKey(sub);
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS ? S_OK : E_FAIL;
}

void UnregisterCom() { RegDeleteTreeW(HKEY_CLASSES_ROOT, ClsidKey().c_str()); }

HRESULT RegisterProfile() {
    ITfInputProcessorProfileMgr* mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&mgr));
    if (FAILED(hr)) return hr;
    std::wstring icon = ModulePath();
    hr = mgr->RegisterProfile(CLSID_NaggyTextService, kLangId, GUID_NaggyProfile, kDescription,
                              static_cast<ULONG>(wcslen(kDescription)), icon.c_str(),
                              static_cast<ULONG>(icon.size()), 0, nullptr, 0, TRUE, 0);
    mgr->Release();
    return hr;
}

void UnregisterProfile() {
    ITfInputProcessorProfileMgr* mgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfileMgr,
                                   reinterpret_cast<void**>(&mgr)))) {
        mgr->UnregisterProfile(CLSID_NaggyTextService, kLangId, GUID_NaggyProfile, 0);
        mgr->Release();
    }
}

HRESULT RegisterCategories() {
    ITfCategoryMgr* cat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, reinterpret_cast<void**>(&cat));
    if (FAILED(hr)) return hr;
    for (const GUID& g : kCategories) {
        hr = cat->RegisterCategory(CLSID_NaggyTextService, g, CLSID_NaggyTextService);
        if (FAILED(hr)) break;
    }
    cat->Release();
    return hr;
}

void UnregisterCategories() {
    ITfCategoryMgr* cat = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&cat)))) {
        for (const GUID& g : kCategories)
            cat->UnregisterCategory(CLSID_NaggyTextService, g, CLSID_NaggyTextService);
        cat->Release();
    }
}

}  // namespace

HRESULT RegisterServer() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT hr = RegisterCom();
    if (SUCCEEDED(hr)) hr = RegisterProfile();
    if (SUCCEEDED(hr)) hr = RegisterCategories();
    if (FAILED(hr)) {
        UnregisterCategories();
        UnregisterProfile();
        UnregisterCom();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return hr;
}

HRESULT UnregisterServer() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UnregisterCategories();
    UnregisterProfile();
    UnregisterCom();
    if (SUCCEEDED(co)) CoUninitialize();
    return S_OK;
}
