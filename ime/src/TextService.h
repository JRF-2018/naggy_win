// TextService.h - the TSF text input processor of naggy_win.
#pragma once
#include <functional>

#include "CandidateWindow.h"
#include "Engine.h"
#include "Globals.h"
#include "PipeBackend.h"

namespace naggy_win {

class CLangBarButton;

class CTextService : public ITfTextInputProcessorEx,
                     public ITfThreadMgrEventSink,
                     public ITfKeyEventSink,
                     public ITfCompositionSink,
                     public ITfDisplayAttributeProvider,
                     public ITfCompartmentEventSink,
                     public ITfTextLayoutSink {
public:
    CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor(Ex)
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;
    STDMETHODIMP Deactivate() override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* focus, ITfDocumentMgr* prev) override;
    STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) override {
        *pfEaten = FALSE;
        return S_OK;
    }

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;

    // ITfTextLayoutSink
    STDMETHODIMP OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView) override;

private:
    ~CTextService();

    enum class Toggle { None, Toggle, On, Off };
    Toggle ToggleKind(WPARAM vk) const;
    naggy::KeyEvent MakeKeyEvent(WPARAM wParam, LPARAM lParam) const;

    bool IsOpen() const;
    void SetOpen(bool open);
    void UpdateConversionMode();
    bool IsKeyboardDisabled(ITfContext* pic) const;
    void ReloadSettingsIfChanged();
    void LaunchConfig(const wchar_t* args);
    void OnLangBarClick(bool right, POINT pt, const RECT* area);

    // Applies the engine state (commit text, composition, candidate
    // window) to the document.
    void UpdateDocument(ITfContext* pic, bool sync);
    HRESULT DoUpdate(TfEditCookie ec, ITfContext* pic);
    void StartComposition(TfEditCookie ec, ITfContext* pic);
    static void SetCompositionAttr(TfEditCookie ec, ITfContext* pic, ITfRange* range, TfGuidAtom atom);
    void UpdateCandidateWindow(TfEditCookie ec, ITfContext* pic);
    // Re-queries the composition position in a read-only edit session.
    void RequestCandidatePosition(ITfContext* pic);
    void AdviseLayoutSink(ITfContext* pic);
    void UnadviseLayoutSink();
    // Ends the composition leaving its text (= commit as it is).
    void EndComposition();
    void OnCandidateClick(int slot, bool terminate);

    LONG ref_ = 1;
    DWORD settingsSerial_ = static_cast<DWORD>(-1);
    ITfThreadMgr* tm_ = nullptr;
    TfClientId tid_ = TF_CLIENTID_NULL;
    DWORD tmSinkCookie_ = TF_INVALID_COOKIE;
    DWORD openSinkCookie_ = TF_INVALID_COOKIE;
    bool keySinkAdvised_ = false;
    bool secure_ = false;
    ITfComposition* composition_ = nullptr;
    ITfContext* compContext_ = nullptr;
    TfGuidAtom atomInput_ = TF_INVALID_GUIDATOM;
    TfGuidAtom atomConverted_ = TF_INVALID_GUIDATOM;
    ITfContext* layoutContext_ = nullptr;
    DWORD layoutSinkCookie_ = TF_INVALID_COOKIE;
    bool layoutPending_ = false;  // GetTextExt failed (no layout yet)
    RECT lastRect_ = {};
    bool lastRectValid_ = false;
    CLangBarButton* langbar_ = nullptr;
    bool langbarAdded_ = false;

    PipeBackend backend_;
    naggy::Engine engine_;
    CandidateWindow candwin_;
    // Keys whose key-down we ate (to eat the key-up too).
    BYTE eatenDown_[256] = {};
};

}  // namespace naggy_win
