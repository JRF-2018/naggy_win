// LangBarButton.h - the input mode button (taskbar indicator).
#pragma once
#include <functional>

#include "Globals.h"

namespace naggy_win {

class CLangBarButton : public ITfLangBarItemButton, public ITfSource {
public:
    // onClick(isRightButton, pt, area of the button (may be null))
    explicit CLangBarButton(std::function<void(bool, POINT, const RECT*)> onClick);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;
    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;
    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    void SetOpen(bool open);

private:
    ~CLangBarButton();
    LONG ref_ = 1;
    bool open_ = false;
    ITfLangBarItemSink* sink_ = nullptr;
    std::function<void(bool, POINT, const RECT*)> onClick_;
};

}  // namespace naggy_win
