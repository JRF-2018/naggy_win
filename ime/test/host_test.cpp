// host_test.cpp - loads naggy_ime.dll without registration and activates
// the text service on a TSF thread manager (smoke test).
#include <windows.h>
#include <msctf.h>
#include <cstdio>

static const CLSID CLSID_Naggy = {0x851460b4, 0x6e41, 0x402e, {0xb8, 0x36, 0xfd, 0xef, 0xa0, 0x5f, 0x52, 0x7f}};

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { wprintf(L"usage: host_test DLL\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE h = LoadLibraryW(argv[1]);
    if (!h) { wprintf(L"LoadLibrary failed %lu\n", GetLastError()); return 1; }
    auto getClass = (HRESULT(WINAPI*)(REFCLSID, REFIID, void**))GetProcAddress(h, "DllGetClassObject");
    IClassFactory* cf = nullptr;
    HRESULT hr = getClass(CLSID_Naggy, IID_IClassFactory, (void**)&cf);
    wprintf(L"DllGetClassObject: %08lx\n", hr);
    ITfTextInputProcessorEx* tip = nullptr;
    hr = cf->CreateInstance(nullptr, IID_ITfTextInputProcessorEx, (void**)&tip);
    wprintf(L"CreateInstance: %08lx\n", hr);
    ITfDisplayAttributeProvider* dap = nullptr;
    hr = tip->QueryInterface(IID_ITfDisplayAttributeProvider, (void**)&dap);
    IEnumTfDisplayAttributeInfo* en = nullptr;
    dap->EnumDisplayAttributeInfo(&en);
    ITfDisplayAttributeInfo* info[4] = {};
    ULONG n = 0;
    en->Next(4, info, &n);
    wprintf(L"display attributes: %lu\n", n);
    for (ULONG i = 0; i < n; ++i) { BSTR b; info[i]->GetDescription(&b); wprintf(L"  %s\n", b); SysFreeString(b); info[i]->Release(); }
    en->Release();
    dap->Release();

    ITfThreadMgr* tm = nullptr;
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&tm);
    wprintf(L"ThreadMgr: %08lx\n", hr);
    TfClientId tid = 0;
    hr = tm->Activate(&tid);
    wprintf(L"tm Activate: %08lx tid=%lu\n", hr, tid);
    hr = tip->ActivateEx(tm, tid, 0);
    wprintf(L"tip ActivateEx: %08lx\n", hr);
    ITfKeyEventSink* ks = nullptr;
    tip->QueryInterface(IID_ITfKeyEventSink, (void**)&ks);
    BOOL eaten = FALSE;
    hr = ks->OnTestKeyDown(nullptr, 'A', (0x1E << 16), &eaten);
    wprintf(L"OnTestKeyDown('A'): %08lx eaten=%d\n", hr, eaten);
    ks->Release();
    hr = tip->Deactivate();
    wprintf(L"tip Deactivate: %08lx\n", hr);
    tm->Deactivate();
    tm->Release();
    tip->Release();
    cf->Release();
    wprintf(L"OK\n");
    return 0;
}
