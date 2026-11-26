// TextService.cpp - the TSF text input processor of naggy_win.
#include "TextService.h"

#include "DisplayAttribute.h"
#include "Log.h"
#include "LangBarButton.h"

#include <shellapi.h>

#ifndef VK_IME_ON
#define VK_IME_ON 0x16
#endif
#ifndef VK_IME_OFF
#define VK_IME_OFF 0x1A
#endif

namespace naggy_win {

namespace {

// A synchronous/asynchronous edit session running a lambda.
class CEditSession : public ITfEditSession {
public:
    explicit CEditSession(std::function<HRESULT(TfEditCookie)> f, std::function<void()> done = nullptr)
        : f_(std::move(f)), done_(std::move(done)) {}
    ~CEditSession() {
        if (done_) done_();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return f_(ec); }

private:
    LONG ref_ = 1;
    std::function<HRESULT(TfEditCookie)> f_;
    std::function<void()> done_;
};

// Physical key position (scan code) -> virtual keyboard slot.
int SlotOfScanCode(UINT sc, bool extended) {
    if (extended) return -1;
    if (sc >= 0x02 && sc <= 0x0B) return sc - 0x02;        // 1..0
    if (sc >= 0x10 && sc <= 0x19) return 10 + sc - 0x10;   // q..p
    if (sc >= 0x1E && sc <= 0x27) return 20 + sc - 0x1E;   // a..;
    if (sc >= 0x2C && sc <= 0x35) return 30 + sc - 0x2C;   // z../
    return -1;
}

bool KeyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

ITfCompartment* GetCompartment(ITfThreadMgr* tm, REFGUID guid) {
    ITfCompartmentMgr* cm = nullptr;
    ITfCompartment* c = nullptr;
    if (tm && SUCCEEDED(tm->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&cm)))) {
        cm->GetCompartment(guid, &c);
        cm->Release();
    }
    return c;
}

}  // namespace

CTextService::CTextService() : engine_(&backend_) {
    DllAddRef();
    candwin_.SetClickHandler([this](int slot, bool terminate) { OnCandidateClick(slot, terminate); });
}

CTextService::~CTextService() { DllRelease(); }

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx))
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
        *ppv = static_cast<ITfKeyEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
        *ppv = static_cast<ITfCompositionSink*>(this);
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    else if (IsEqualIID(riid, IID_ITfCompartmentEventSink))
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfTextLayoutSink))
        *ppv = static_cast<ITfTextLayoutSink*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) CTextService::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    tm_ = ptim;
    tm_->AddRef();
    tid_ = tid;
    secure_ = (dwFlags & TF_TMAE_SECUREMODE) != 0;
    backend_.SetAllowLaunch(!secure_);

    settingsSerial_ = static_cast<DWORD>(-1);
    ReloadSettingsIfChanged();
    Log(L"ActivateEx flags=%08lx", dwFlags);

    ITfSource* src = nullptr;
    if (SUCCEEDED(tm_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src)))) {
        src->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this),
                        &tmSinkCookie_);
        src->Release();
    }

    ITfKeystrokeMgr* km = nullptr;
    if (SUCCEEDED(tm_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&km)))) {
        keySinkAdvised_ =
            SUCCEEDED(km->AdviseKeyEventSink(tid_, static_cast<ITfKeyEventSink*>(this), TRUE));
        km->Release();
    }

    ITfCategoryMgr* cat = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&cat)))) {
        cat->RegisterGUID(GUID_NaggyDisplayAttributeInput, &atomInput_);
        cat->RegisterGUID(GUID_NaggyDisplayAttributeConverted, &atomConverted_);
        cat->Release();
    }

    if (ITfCompartment* c = GetCompartment(tm_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        ITfSource* cs = nullptr;
        if (SUCCEEDED(c->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&cs)))) {
            cs->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this),
                           &openSinkCookie_);
            cs->Release();
        }
        VARIANT v;
        VariantInit(&v);
        bool empty = FAILED(c->GetValue(&v)) || v.vt == VT_EMPTY;
        c->Release();
        // The IME starts off (unless StartOpen = 1), whatever state the
        // previous input method left in the compartment.
        (void)empty;
        SetOpen(!secure_ && GetSettingDword(L"StartOpen", 0) != 0);
    }

    langbar_ = new (std::nothrow) CLangBarButton([this](bool right, POINT pt, const RECT* area) { OnLangBarClick(right, pt, area); });
    ITfLangBarItemMgr* lbm = nullptr;
    if (langbar_ && SUCCEEDED(tm_->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&lbm)))) {
        langbarAdded_ = SUCCEEDED(lbm->AddItem(langbar_));
        lbm->Release();
    }
    if (langbar_) langbar_->SetOpen(IsOpen());
    UpdateConversionMode();
    return S_OK;
}

STDMETHODIMP CTextService::Deactivate() {
    Log(L"Deactivate");
    EndComposition();
    UnadviseLayoutSink();
    candwin_.Destroy();

    if (langbar_) {
        ITfLangBarItemMgr* lbm = nullptr;
        if (langbarAdded_ &&
            SUCCEEDED(tm_->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&lbm)))) {
            lbm->RemoveItem(langbar_);
            lbm->Release();
        }
        langbarAdded_ = false;
        SafeRelease(langbar_);
    }
    if (openSinkCookie_ != TF_INVALID_COOKIE) {
        if (ITfCompartment* c = GetCompartment(tm_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
            ITfSource* cs = nullptr;
            if (SUCCEEDED(c->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&cs)))) {
                cs->UnadviseSink(openSinkCookie_);
                cs->Release();
            }
            c->Release();
        }
        openSinkCookie_ = TF_INVALID_COOKIE;
    }
    if (keySinkAdvised_) {
        ITfKeystrokeMgr* km = nullptr;
        if (SUCCEEDED(tm_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&km)))) {
            km->UnadviseKeyEventSink(tid_);
            km->Release();
        }
        keySinkAdvised_ = false;
    }
    if (tmSinkCookie_ != TF_INVALID_COOKIE) {
        ITfSource* src = nullptr;
        if (SUCCEEDED(tm_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src)))) {
            src->UnadviseSink(tmSinkCookie_);
            src->Release();
        }
        tmSinkCookie_ = TF_INVALID_COOKIE;
    }
    backend_.Disconnect();
    SafeRelease(tm_);
    tid_ = TF_CLIENTID_NULL;
    return S_OK;
}

// --- open / close -------------------------------------------------------

bool CTextService::IsOpen() const {
    bool open = false;
    if (ITfCompartment* c = GetCompartment(tm_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4) open = v.lVal != 0;
        c->Release();
    }
    return open;
}

void CTextService::SetOpen(bool open) {
    if (ITfCompartment* c = GetCompartment(tm_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = open ? 1 : 0;
        c->SetValue(tid_, &v);
        c->Release();
    }
}

void CTextService::UpdateConversionMode() {
    if (ITfCompartment* c = GetCompartment(tm_, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = IsOpen() ? (TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE)
                          : TF_CONVERSIONMODE_ALPHANUMERIC;
        c->SetValue(tid_, &v);
        c->Release();
    }
}

STDMETHODIMP CTextService::OnChange(REFGUID rguid) {
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        bool open = IsOpen();
        if (!open) EndComposition();
        if (langbar_) langbar_->SetOpen(open);
        UpdateConversionMode();
    }
    return S_OK;
}

void CTextService::OnLangBarClick(bool right, POINT pt, const RECT* area) {
    Log(L"langbar click right=%d (%ld,%ld)", right, pt.x, pt.y);
    if (!right) {
        SetOpen(!IsOpen());
        return;
    }
    // The menu is shown by naggy-config.exe --menu: a TIP instance running
    // in the taskbar thread (Explorer) cannot show a popup menu reliably.
    wchar_t args[128];
    if (area)
        swprintf(args, 128, L"--menu %ld %ld %ld %ld %ld %ld", pt.x, pt.y, area->left, area->top,
                 area->right, area->bottom);
    else
        swprintf(args, 128, L"--menu %ld %ld", pt.x, pt.y);
    LaunchConfig(args);
}

// --- keys ---------------------------------------------------------------

CTextService::Toggle CTextService::ToggleKind(WPARAM vk) const {
    bool alt = KeyDown(VK_MENU), ctrl = KeyDown(VK_CONTROL);
    switch (vk) {
    case VK_KANJI:
    case VK_OEM_AUTO:
    case VK_OEM_ENLW:
        return ctrl ? Toggle::None : Toggle::Toggle;
    case VK_OEM_3:  // Alt + ` (US keyboards)
        return (alt && !ctrl) ? Toggle::Toggle : Toggle::None;
    case VK_IME_ON:
        return Toggle::On;
    case VK_IME_OFF:
        return Toggle::Off;
    }
    return Toggle::None;
}

naggy::KeyEvent CTextService::MakeKeyEvent(WPARAM wParam, LPARAM lParam) const {
    naggy::KeyEvent k;
    UINT vk = static_cast<UINT>(wParam);
    UINT sc = (lParam >> 16) & 0xFF;
    bool ext = (lParam & (1 << 24)) != 0;
    k.shift = KeyDown(VK_SHIFT);
    k.ctrl = KeyDown(VK_CONTROL);
    k.alt = KeyDown(VK_MENU);
    using naggy::KeyCode;
    switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU: case VK_LWIN:
    case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL: case VK_APPS:
    case VK_KANA: case VK_OEM_ATTN: case VK_OEM_FINISH: case VK_OEM_COPY:
    case VK_OEM_BACKTAB:
        k.code = KeyCode::Modifier;
        return k;
    case VK_SPACE: k.code = KeyCode::Space; return k;
    case VK_RETURN: k.code = KeyCode::Enter; return k;
    case VK_ESCAPE: k.code = KeyCode::Escape; return k;
    case VK_BACK: k.code = KeyCode::Backspace; return k;
    case VK_DELETE: k.code = KeyCode::Delete; return k;
    case VK_LEFT: k.code = KeyCode::Left; return k;
    case VK_RIGHT: k.code = KeyCode::Right; return k;
    case VK_UP: k.code = KeyCode::Up; return k;
    case VK_DOWN: k.code = KeyCode::Down; return k;
    case VK_HOME: k.code = KeyCode::Home; return k;
    case VK_END: k.code = KeyCode::End; return k;
    case VK_PRIOR: k.code = KeyCode::PageUp; return k;
    case VK_NEXT: k.code = KeyCode::PageDown; return k;
    case VK_TAB: k.code = KeyCode::Tab; return k;
    case VK_NONCONVERT: k.code = KeyCode::Muhenkan; return k;
    case VK_CONVERT: k.code = KeyCode::Henkan; return k;
    case VK_F6: k.code = KeyCode::F6; return k;
    case VK_F7: k.code = KeyCode::F7; return k;
    case VK_F8: k.code = KeyCode::F8; return k;
    case VK_F9: k.code = KeyCode::F9; return k;
    case VK_F10: k.code = KeyCode::F10; return k;
    }
    k.code = KeyCode::Other;
    k.pos = SlotOfScanCode(sc, ext);
    BYTE ks[256];
    if (GetKeyboardState(ks)) {
        // The character without Ctrl/Alt (so that C-g, M-q... are known).
        ks[VK_CONTROL] = ks[VK_LCONTROL] = ks[VK_RCONTROL] = 0;
        ks[VK_MENU] = ks[VK_LMENU] = ks[VK_RMENU] = 0;
        ks[VK_KANA] = 0;
        wchar_t buf[8];
        HKL hkl = GetKeyboardLayout(0);
        int n = ToUnicodeEx(vk, sc, ks, buf, ARRAYSIZE(buf), 0x4, hkl);
        if (n == 1 && buf[0] >= 0x21 && buf[0] <= 0x7E) {
            k.code = KeyCode::Char;
            k.ch = buf[0];
        }
    }
    return k;
}

bool CTextService::IsKeyboardDisabled(ITfContext* pic) const {
    if (!pic) return false;
    bool disabled = false;
    ITfCompartmentMgr* cm = nullptr;
    if (SUCCEEDED(pic->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&cm)))) {
        for (const GUID* g : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT}) {
            ITfCompartment* c = nullptr;
            if (SUCCEEDED(cm->GetCompartment(*g, &c))) {
                VARIANT v;
                VariantInit(&v);
                if (SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4 && v.lVal) disabled = true;
                c->Release();
            }
        }
        cm->Release();
    }
    return disabled;
}

void CTextService::ReloadSettingsIfChanged() {
    DWORD serial = GetSettingDword(L"SettingsSerial", 0);
    if (serial == settingsSerial_) return;
    settingsSerial_ = serial;
    SetLogEnabled(GetSettingDword(L"DebugLog", 0) != 0);
    naggy::EngineOptions opt;
    opt.shiftSpaceAltConvert = GetSettingDword(L"ShiftSpaceAltConvert", 1) != 0;
    opt.asciiLayout = GetSettingDword(L"KeyLayout", 0) == 1;
    engine_.SetOptions(opt);
    candwin_.InvalidateFont();
}

void CTextService::LaunchConfig(const wchar_t* args) {
    std::wstring server = FindServerExe();
    if (server.empty()) return;
    std::wstring exe = server.substr(0, server.find_last_of(L"\\/")) + L"\\naggy-config.exe";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(), args, nullptr, SW_SHOWNORMAL);
    Log(L"LaunchConfig %s %s -> %p", exe.c_str(), args ? args : L"", r);
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;
    ReloadSettingsIfChanged();
    if (ToggleKind(wParam) != Toggle::None) {
        *pfEaten = TRUE;
        return S_OK;
    }
    bool open = IsOpen(), disabled = IsKeyboardDisabled(pic);
    if (!open || disabled) {
        Log(L"OnTestKeyDown vk=%02x skipped open=%d disabled=%d", (unsigned)wParam, open, disabled);
        return S_OK;
    }
    *pfEaten = engine_.WouldEat(MakeKeyEvent(wParam, lParam)) ? TRUE : FALSE;
    Log(L"OnTestKeyDown vk=%02x lp=%08lx eaten=%d", (unsigned)wParam, (unsigned long)lParam, *pfEaten);
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;
    Toggle t = ToggleKind(wParam);
    if (t != Toggle::None) {
        bool open = IsOpen();
        bool want = t == Toggle::Toggle ? !open : t == Toggle::On;
        if (!want) EndComposition();
        if (want != open) SetOpen(want);
        *pfEaten = TRUE;
        eatenDown_[wParam & 0xFF] = 1;
        return S_OK;
    }
    if (!IsOpen() || IsKeyboardDisabled(pic)) return S_OK;
    if (compContext_ && compContext_ != pic) EndComposition();
    naggy::KeyEvent k = MakeKeyEvent(wParam, lParam);
    if (!engine_.WouldEat(k)) return S_OK;
    bool pass = false;
    engine_.ProcessKey(k, &pass);
    if (!engine_.LastError().empty()) Log(L"engine error: %s", engine_.LastError().c_str());
    UpdateDocument(pic, true);
    if (engine_.TakeBeep()) MessageBeep(MB_OK);
    *pfEaten = pass ? FALSE : TRUE;
    if (*pfEaten) eatenDown_[wParam & 0xFF] = 1;
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    *pfEaten = eatenDown_[wParam & 0xFF] ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext*, WPARAM wParam, LPARAM, BOOL* pfEaten) {
    *pfEaten = eatenDown_[wParam & 0xFF] ? TRUE : FALSE;
    eatenDown_[wParam & 0xFF] = 0;
    return S_OK;
}

// --- document update -----------------------------------------------------

void CTextService::UpdateDocument(ITfContext* pic, bool sync) {
    if (!pic) return;
    pic->AddRef();
    AddRef();
    CEditSession* es = new (std::nothrow) CEditSession(
        [this, pic](TfEditCookie ec) { return DoUpdate(ec, pic); },
        [this, pic]() {
            pic->Release();
            Release();
        });
    if (!es) {
        pic->Release();
        Release();
    }
    if (es) {
        // TF_ES_ASYNCDONTCARE: run now if possible, otherwise as soon as
        // the document can be locked.  Some applications (web browsers)
        // refuse synchronous locks while they dispatch keys.
        HRESULT hr = E_FAIL;
        (void)sync;
        HRESULT r = pic->RequestEditSession(tid_, es, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &hr);
        Log(L"RequestEditSession r=%08lx hr=%08lx", r, hr);
        es->Release();
    }
}

void CTextService::StartComposition(TfEditCookie ec, ITfContext* pic) {
    ITfInsertAtSelection* ias = nullptr;
    ITfRange* range = nullptr;
    if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&ias)))) return;
    HRESULT hr = ias->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range);
    ias->Release();
    if (FAILED(hr) || !range) return;
    ITfContextComposition* cc = nullptr;
    if (SUCCEEDED(pic->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&cc)))) {
        ITfComposition* comp = nullptr;
        if (SUCCEEDED(cc->StartComposition(ec, range, static_cast<ITfCompositionSink*>(this), &comp)) &&
            comp) {
            composition_ = comp;
            compContext_ = pic;
            compContext_->AddRef();
            TF_SELECTION sel;
            sel.range = range;
            sel.style.ase = TF_AE_NONE;
            sel.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &sel);
        }
        cc->Release();
    }
    range->Release();
}

void CTextService::SetCompositionAttr(TfEditCookie ec, ITfContext* pic, ITfRange* range,
                                      TfGuidAtom atom) {
    ITfProperty* prop = nullptr;
    if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop))) {
        if (atom == TF_INVALID_GUIDATOM) {
            prop->Clear(ec, range);
        } else {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = static_cast<LONG>(atom);
            prop->SetValue(ec, range, &v);
        }
        prop->Release();
    }
}

HRESULT CTextService::DoUpdate(TfEditCookie ec, ITfContext* pic) {
    std::wstring commit = engine_.TakeCommit();
    std::wstring comp = engine_.Composition();
    Log(L"DoUpdate commit=\"%s\" comp=\"%s\" composing=%d", commit.c_str(), comp.c_str(),
        composition_ != nullptr);

    if (composition_ && compContext_ != pic) return S_OK;

    if (!commit.empty() || comp.empty()) {
        if (composition_) {
            ITfRange* r = nullptr;
            if (SUCCEEDED(composition_->GetRange(&r))) {
                r->SetText(ec, 0, commit.c_str(), static_cast<LONG>(commit.size()));
                SetCompositionAttr(ec, pic, r, TF_INVALID_GUIDATOM);
                r->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION sel;
                sel.range = r;
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                pic->SetSelection(ec, 1, &sel);
                r->Release();
            }
            composition_->EndComposition(ec);
            SafeRelease(composition_);
            SafeRelease(compContext_);
        } else if (!commit.empty()) {
            ITfInsertAtSelection* ias = nullptr;
            if (SUCCEEDED(pic->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&ias)))) {
                ITfRange* r = nullptr;
                if (SUCCEEDED(ias->InsertTextAtSelection(ec, 0, commit.c_str(),
                                                         static_cast<LONG>(commit.size()), &r)) && r) {
                    r->Collapse(ec, TF_ANCHOR_END);
                    TF_SELECTION sel;
                    sel.range = r;
                    sel.style.ase = TF_AE_NONE;
                    sel.style.fInterimChar = FALSE;
                    pic->SetSelection(ec, 1, &sel);
                    r->Release();
                }
                ias->Release();
            }
        }
    }

    if (!comp.empty()) {
        if (!composition_) StartComposition(ec, pic);
        if (composition_) {
            ITfRange* r = nullptr;
            if (SUCCEEDED(composition_->GetRange(&r))) {
                r->SetText(ec, 0, comp.c_str(), static_cast<LONG>(comp.size()));
                bool conv = engine_.GetState() == naggy::Engine::State::Convert;
                SetCompositionAttr(ec, pic, r, conv ? atomConverted_ : atomInput_);
                // caret
                ITfRange* caret = nullptr;
                if (SUCCEEDED(r->Clone(&caret))) {
                    LONG moved = 0;
                    caret->Collapse(ec, TF_ANCHOR_START);
                    LONG pos = static_cast<LONG>(engine_.Caret());
                    caret->ShiftEnd(ec, pos, &moved, nullptr);
                    caret->ShiftStart(ec, pos, &moved, nullptr);
                    TF_SELECTION sel;
                    sel.range = caret;
                    sel.style.ase = TF_AE_END;
                    sel.style.fInterimChar = FALSE;
                    pic->SetSelection(ec, 1, &sel);
                    caret->Release();
                }
                r->Release();
            }
        }
    }
    UpdateCandidateWindow(ec, pic);
    return S_OK;
}

void CTextService::UpdateCandidateWindow(TfEditCookie ec, ITfContext* pic) {
    naggy::CandidateView v;
    if (!composition_ || !engine_.GetCandidateView(v)) {
        layoutPending_ = false;
        lastRectValid_ = false;
        candwin_.Hide();
        return;
    }
    AdviseLayoutSink(pic);
    RECT rc = {};
    bool ok = false;
    HRESULT hr = E_FAIL;
    ITfRange* r = nullptr;
    ITfContextView* view = nullptr;
    if (SUCCEEDED(composition_->GetRange(&r)) && SUCCEEDED(pic->GetActiveView(&view))) {
        BOOL clipped = FALSE;
        hr = view->GetTextExt(ec, r, &rc, &clipped);
        ok = SUCCEEDED(hr) && (rc.right > rc.left || rc.bottom > rc.top);
        if (!ok) {
            // Some applications can measure only part of a range (or only
            // the start of it) while the rest is being laid out.
            ITfRange* start = nullptr;
            if (SUCCEEDED(r->Clone(&start))) {
                start->Collapse(ec, TF_ANCHOR_START);
                LONG moved = 0;
                start->ShiftEnd(ec, 1, &moved, nullptr);
                RECT rc2 = {};
                HRESULT hr2 = view->GetTextExt(ec, start, &rc2, &clipped);
                if (SUCCEEDED(hr2) && (rc2.right > rc2.left || rc2.bottom > rc2.top)) {
                    rc = rc2;
                    ok = true;
                }
                start->Release();
            }
        }
    }
    SafeRelease(view);
    SafeRelease(r);
    Log(L"GetTextExt hr=%08lx ok=%d (%ld,%ld)-(%ld,%ld)", hr, ok, rc.left, rc.top, rc.right, rc.bottom);
    if (ok) {
        layoutPending_ = false;
        lastRect_ = rc;
        lastRectValid_ = true;
    } else {
        // TF_E_NOLAYOUT: the application (e.g. Firefox) has not laid out
        // the new text yet.  OnLayoutChange will call us again; in the
        // meantime stay where we were, or guess.
        layoutPending_ = true;
        if (lastRectValid_) {
            rc = lastRect_;
        } else {
            GUITHREADINFO gti = {sizeof(gti)};
            if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
                rc = gti.rcCaret;
                MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            } else if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret) {
                rc = gti.rcCaret;
                MapWindowPoints(gti.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rc), 2);
            } else {
                POINT pt;
                GetCursorPos(&pt);
                rc = {pt.x, pt.y, pt.x + 1, pt.y + 16};
            }
        }
    }
    candwin_.Show(v, rc);
}

void CTextService::RequestCandidatePosition(ITfContext* pic) {
    if (!pic || !composition_ || pic != compContext_) return;
    pic->AddRef();
    AddRef();
    CEditSession* es = new (std::nothrow) CEditSession(
        [this, pic](TfEditCookie ec) {
            if (composition_ && pic == compContext_) UpdateCandidateWindow(ec, pic);
            return S_OK;
        },
        [this, pic]() {
            pic->Release();
            Release();
        });
    if (!es) {
        pic->Release();
        Release();
        return;
    }
    HRESULT hr = E_FAIL;
    pic->RequestEditSession(tid_, es, TF_ES_READ | TF_ES_ASYNCDONTCARE, &hr);
    es->Release();
}

void CTextService::AdviseLayoutSink(ITfContext* pic) {
    if (pic == layoutContext_) return;
    UnadviseLayoutSink();
    ITfSource* src = nullptr;
    if (pic && SUCCEEDED(pic->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src)))) {
        if (SUCCEEDED(src->AdviseSink(IID_ITfTextLayoutSink, static_cast<ITfTextLayoutSink*>(this),
                                      &layoutSinkCookie_))) {
            layoutContext_ = pic;
            layoutContext_->AddRef();
        } else {
            layoutSinkCookie_ = TF_INVALID_COOKIE;
        }
        src->Release();
    }
}

void CTextService::UnadviseLayoutSink() {
    if (layoutContext_ && layoutSinkCookie_ != TF_INVALID_COOKIE) {
        ITfSource* src = nullptr;
        if (SUCCEEDED(layoutContext_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src)))) {
            src->UnadviseSink(layoutSinkCookie_);
            src->Release();
        }
    }
    layoutSinkCookie_ = TF_INVALID_COOKIE;
    SafeRelease(layoutContext_);
}

STDMETHODIMP CTextService::OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView*) {
    // Follows both late layouts (TF_E_NOLAYOUT earlier) and moves of the
    // text (scrolling, window moves).
    if (lcode == TF_LC_DESTROY) {
        if (pic == compContext_) candwin_.Hide();
        return S_OK;
    }
    if (composition_ && pic == compContext_ && (candwin_.IsVisible() || layoutPending_)) {
        Log(L"OnLayoutChange code=%d pending=%d", lcode, layoutPending_);
        RequestCandidatePosition(pic);
    }
    return S_OK;
}

void CTextService::EndComposition() {
    if (composition_ && compContext_) {
        ITfComposition* comp = composition_;
        ITfContext* ctx = compContext_;
        composition_ = nullptr;
        compContext_ = nullptr;
        // The references move into the edit session.
        CEditSession* es = new (std::nothrow) CEditSession(
            [comp, ctx](TfEditCookie ec) {
                ITfRange* r = nullptr;
                if (SUCCEEDED(comp->GetRange(&r))) {
                    SetCompositionAttr(ec, ctx, r, TF_INVALID_GUIDATOM);
                    r->Collapse(ec, TF_ANCHOR_END);
                    TF_SELECTION sel;
                    sel.range = r;
                    sel.style.ase = TF_AE_NONE;
                    sel.style.fInterimChar = FALSE;
                    ctx->SetSelection(ec, 1, &sel);
                    r->Release();
                }
                comp->EndComposition(ec);
                return S_OK;
            },
            [comp, ctx]() {
                comp->Release();
                ctx->Release();
            });
        if (es) {
            HRESULT hr = E_FAIL;
            ctx->RequestEditSession(tid_, es, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &hr);
            es->Release();
        } else {
            comp->Release();
            ctx->Release();
        }
    }
    engine_.Reset();
    candwin_.Hide();
}

void CTextService::OnCandidateClick(int slot, bool terminate) {
    if (!compContext_) return;
    engine_.SelectSlot(slot, terminate);
    UpdateDocument(compContext_, false);
}

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie, ITfComposition* pComposition) {
    if (composition_ && pComposition == composition_) {
        SafeRelease(composition_);
        SafeRelease(compContext_);
        engine_.Reset();
        layoutPending_ = false;
        lastRectValid_ = false;
        candwin_.Hide();
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr*) {
    Log(L"OnSetFocus docmgr=%p", focus);
    if (compContext_) {
        ITfDocumentMgr* dm = nullptr;
        bool same = SUCCEEDED(compContext_->GetDocumentMgr(&dm)) && dm == focus;
        SafeRelease(dm);
        if (!same) candwin_.Hide();
    }
    if (!focus) candwin_.Hide();
    return S_OK;
}

// --- display attributes --------------------------------------------------

STDMETHODIMP CTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new (std::nothrow) CEnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = CreateDisplayAttributeInfo(guid);
    return *ppInfo ? S_OK : E_INVALIDARG;
}

}  // namespace naggy_win
