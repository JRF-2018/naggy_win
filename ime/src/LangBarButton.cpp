// LangBarButton.cpp - the input mode button (taskbar indicator).
#include "LangBarButton.h"

#include <vector>

namespace naggy_win {

namespace {
// GUID_LBI_INPUTMODE (ctffunc.h): the input mode item shown in the taskbar.
const GUID kGuidLbiInputMode = {
    0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
const DWORD kSinkCookie = 0x4E414747;  // "NAGG"

bool TaskbarIsDark() {
    DWORD v = 1, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return true;
    return v == 0;
}

// Draws the mode character into an icon.
HICON MakeIcon(bool open) {
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color) {
        DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ old = SelectObject(dc, color);
    memset(bits, 0, cx * cy * 4);
    // Draw white text on black, then use the luminance as alpha.
    HFONT font = CreateFontW(-cy, 0, 0, 0, open ? FW_NORMAL : FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             ANTIALIASED_QUALITY, 0, open ? L"Yu Gothic UI" : L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r = {0, 0, cx, cy};
    const wchar_t* text = open ? L"\x3042" : L"A";  // あ / A
    DrawTextW(dc, text, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    GdiFlush();
    COLORREF fg = TaskbarIsDark() ? RGB(255, 255, 255) : RGB(0, 0, 0);
    DWORD* p = static_cast<DWORD*>(bits);
    for (int i = 0; i < cx * cy; ++i) {
        DWORD a = p[i] & 0xFF;
        DWORD rr = GetRValue(fg) * a / 255, gg = GetGValue(fg) * a / 255, bb = GetBValue(fg) * a / 255;
        p[i] = (a << 24) | (rr << 16) | (gg << 8) | bb;
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, old);
    DeleteDC(dc);
    std::vector<BYTE> zero(((cx + 15) / 16) * 2 * cy, 0);
    HBITMAP mask = CreateBitmap(cx, cy, 1, 1, zero.data());
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
}  // namespace

CLangBarButton::CLangBarButton(std::function<void(bool, POINT, const RECT*)> onClick) : onClick_(std::move(onClick)) {
    DllAddRef();
}
CLangBarButton::~CLangBarButton() {
    SafeRelease(sink_);
    DllRelease();
}

STDMETHODIMP CLangBarButton::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    else if (IsEqualIID(riid, IID_ITfSource))
        *ppv = static_cast<ITfSource*>(this);
    if (!*ppv) return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) CLangBarButton::AddRef() { return InterlockedIncrement(&ref_); }
STDMETHODIMP_(ULONG) CLangBarButton::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP CLangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_NaggyTextService;
    pInfo->guidItem = kGuidLbiInputMode;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    lstrcpynW(pInfo->szDescription, L"Naggy input mode", ARRAYSIZE(pInfo->szDescription));
    return S_OK;
}
STDMETHODIMP CLangBarButton::GetStatus(DWORD* pdwStatus) {
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}
STDMETHODIMP CLangBarButton::Show(BOOL) { return E_NOTIMPL; }
STDMETHODIMP CLangBarButton::GetTooltipString(BSTR* pbstr) {
    if (!pbstr) return E_INVALIDARG;
    *pbstr = SysAllocString(open_ ? L"Naggy: \x65E5\x672C\x8A9E\x5165\x529B (ON)"
                                  : L"Naggy: \x76F4\x63A5\x5165\x529B (OFF)");
    return *pbstr ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP CLangBarButton::OnClick(TfLBIClick click, POINT pt, const RECT* area) {
    if (onClick_) onClick_(click == TF_LBI_CLK_RIGHT, pt, area);
    return S_OK;
}
STDMETHODIMP CLangBarButton::InitMenu(ITfMenu*) { return S_OK; }
STDMETHODIMP CLangBarButton::OnMenuSelect(UINT) { return S_OK; }
STDMETHODIMP CLangBarButton::GetIcon(HICON* phIcon) {
    if (!phIcon) return E_INVALIDARG;
    *phIcon = MakeIcon(open_);
    return *phIcon ? S_OK : E_FAIL;
}
STDMETHODIMP CLangBarButton::GetText(BSTR* pbstr) {
    if (!pbstr) return E_INVALIDARG;
    *pbstr = SysAllocString(open_ ? L"\x3042" : L"A");
    return *pbstr ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP CLangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(&sink_)))) {
        sink_ = nullptr;
        return E_NOINTERFACE;
    }
    *pdwCookie = kSinkCookie;
    return S_OK;
}
STDMETHODIMP CLangBarButton::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != kSinkCookie || !sink_) return CONNECT_E_NOCONNECTION;
    SafeRelease(sink_);
    return S_OK;
}

void CLangBarButton::SetOpen(bool open) {
    open_ = open;
    if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

}  // namespace naggy_win
