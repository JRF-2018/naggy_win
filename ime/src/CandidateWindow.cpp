// CandidateWindow.cpp - the 40-key virtual keyboard candidate window.
#include "CandidateWindow.h"

#include <strsafe.h>

namespace naggy_win {

namespace {
const wchar_t kClassName[] = L"NaggyWinCandidateWindow";

const COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kBorder = RGB(0x80, 0x80, 0x80);
const COLORREF kCellBorder = RGB(0xD8, 0xD8, 0xD8);
const COLORREF kText = RGB(0x10, 0x10, 0x10);
const COLORREF kLabel = RGB(0x90, 0x90, 0x90);
const COLORREF kHomeLabel = RGB(0x30, 0x30, 0xA0);
const COLORREF kHighlight = RGB(0xFF, 0xEE, 0xB0);
const COLORREF kCursorBg = RGB(0x33, 0x66, 0xCC);
const COLORREF kCursorText = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kStatusBg = RGB(0xF0, 0xF0, 0xF0);
const COLORREF kEmpty = RGB(0xE0, 0xE0, 0xE0);
}  // namespace

void CandidateWindow::RegisterWndClass() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW | CS_IME;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

void CandidateWindow::UnregisterWndClass() { UnregisterClassW(kClassName, g_hInst); }

CandidateWindow::~CandidateWindow() { Destroy(); }

void CandidateWindow::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_) DeleteObject(font_);
    if (smallFont_) DeleteObject(smallFont_);
    font_ = smallFont_ = nullptr;
    fontValid_ = false;
    ReleaseD2D();
}

template <class T> static void ReleaseIf(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void CandidateWindow::ReleaseD2D() {
    ReleaseIf(fmt_);
    ReleaseIf(smallFmt_);
    ReleaseIf(rt_);
    ReleaseIf(dwrite_);
    ReleaseIf(d2d_);
}

bool CandidateWindow::BeginText(HDC dc) {
    drawing_ = false;
    if (d2dFailed_) return false;
    if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_))) {
        d2dFailed_ = true;
        return false;
    }
    if (!dwrite_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                               reinterpret_cast<IUnknown**>(&dwrite_)))) {
        d2dFailed_ = true;
        return false;
    }
    if (!rt_) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
        if (FAILED(d2d_->CreateDCRenderTarget(&props, &rt_))) {
            d2dFailed_ = true;
            return false;
        }
    }
    if (!fmt_ || !smallFmt_) {
        ReleaseIf(fmt_);
        ReleaseIf(smallFmt_);
        if (FAILED(dwrite_->CreateTextFormat(faceName_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             fontPx_, L"ja-jp", &fmt_)) ||
            FAILED(dwrite_->CreateTextFormat(faceName_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             smallFontPx_, L"ja-jp", &smallFmt_))) {
            ReleaseIf(fmt_);
            ReleaseIf(smallFmt_);
            d2dFailed_ = true;
            return false;
        }
        for (IDWriteTextFormat* f : {fmt_, smallFmt_}) {
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    RECT all = {0, 0, width_, height_};
    if (FAILED(rt_->BindDC(dc, &all))) return false;
    rt_->BeginDraw();
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    drawing_ = true;
    return true;
}

void CandidateWindow::EndText() {
    if (!drawing_) return;
    drawing_ = false;
    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) ReleaseIf(rt_);
}

void CandidateWindow::DrawLabel(HDC dc, const std::wstring& t, const RECT& r, COLORREF color,
                               bool isSmall, bool center) {
    if (drawing_) {
        IDWriteTextFormat* f = isSmall ? smallFmt_ : fmt_;
        f->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        IDWriteTextLayout* layout = nullptr;
        float w = static_cast<float>(r.right - r.left), h = static_cast<float>(r.bottom - r.top);
        if (SUCCEEDED(dwrite_->CreateTextLayout(t.c_str(), static_cast<UINT32>(t.size()), f, w, h,
                                                &layout))) {
            if (!center) {
                DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                IDWriteInlineObject* ellipsis = nullptr;
                if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(f, &ellipsis))) {
                    layout->SetTrimming(&trim, ellipsis);
                    ellipsis->Release();
                }
            }
            ID2D1SolidColorBrush* brush = nullptr;
            D2D1_COLOR_F c = D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f,
                                          GetBValue(color) / 255.0f);
            if (SUCCEEDED(rt_->CreateSolidColorBrush(c, &brush))) {
                rt_->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                                                     static_cast<float>(r.right), static_cast<float>(r.bottom)),
                                         D2D1_ANTIALIAS_MODE_ALIASED);
                rt_->DrawTextLayout(D2D1::Point2F(static_cast<float>(r.left), static_cast<float>(r.top)),
                                    layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                rt_->PopAxisAlignedClip();
                brush->Release();
            }
            layout->Release();
            return;
        }
    }
    HGDIOBJ old = SelectObject(dc, isSmall ? smallFont_ : font_);
    SetTextColor(dc, color);
    RECT rr = r;
    DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &rr,
              (center ? DT_CENTER : DT_LEFT | DT_END_ELLIPSIS) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

bool CandidateWindow::Create() {
    if (hwnd_) return true;
    RegisterWndClass();
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClassName, L"",
                            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_hInst, this);
    return hwnd_ != nullptr;
}

void CandidateWindow::Layout(HDC dc) {
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    if (font_ && fontValid_ && dpi == dpi_) return;
    dpi_ = dpi;
    fontValid_ = true;
    if (font_) DeleteObject(font_);
    if (smallFont_) DeleteObject(smallFont_);
    std::wstring face = GetSettingString(L"FontName", L"Meiryo UI");
    int pt = static_cast<int>(GetSettingDword(L"FontSize", 14));
    if (pt < 6 || pt > 72) pt = 14;
    faceName_ = face;
    fontPx_ = static_cast<float>(MulDiv(pt, dpi, 72));
    smallFontPx_ = static_cast<float>(MulDiv(pt * 7 / 10 + 1, dpi, 72));
    ReleaseIf(fmt_);
    ReleaseIf(smallFmt_);
    d2dFailed_ = false;
    font_ = CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, face.c_str());
    smallFont_ = CreateFontW(-MulDiv(pt * 7 / 10 + 1, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face.c_str());
    HGDIOBJ old = SelectObject(dc, font_);
    SIZE sz;
    GetTextExtentPoint32W(dc, L"\x6F22\x5B57", 2, &sz);  // 漢字
    SelectObject(dc, old);
    pad_ = MulDiv(2, dpi, 96);
    cellW_ = sz.cx + pad_ * 3;
    cellH_ = sz.cy + pad_ * 2;
    gap_ = MulDiv(6, dpi, 96);
    statusH_ = sz.cy;
    width_ = cellW_ * 10 + gap_ + pad_ * 2;
    height_ = cellH_ * 4 + statusH_ + pad_ * 3;
}

static RECT CellRect(int i, int pad, int cellW, int cellH, int gap) {
    int row = i / 10, col = i % 10;
    RECT r;
    r.left = pad + col * cellW + (col >= 5 ? gap : 0);
    r.top = pad + row * cellH;
    r.right = r.left + cellW;
    r.bottom = r.top + cellH;
    return r;
}

int CandidateWindow::HitTest(int x, int y) const {
    for (int i = 0; i < 40; ++i) {
        RECT r = CellRect(i, pad_, cellW_, cellH_, gap_);
        POINT pt = {x, y};
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

void CandidateWindow::Paint(HDC dc) {
    RECT all = {0, 0, width_, height_};
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HPEN cellPen = CreatePen(PS_SOLID, 1, kCellBorder);
    HGDIOBJ oldPen = SelectObject(dc, cellPen);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    for (int i = 0; i < 40; ++i) {
        const auto& c = view_.cells[i];
        RECT r = CellRect(i, pad_, cellW_, cellH_, gap_);
        RECT in = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        COLORREF fill = c.cursor ? kCursorBg : (c.highlight ? kHighlight : kBg);
        HBRUSH b = CreateSolidBrush(fill);
        FillRect(dc, &in, b);
        DeleteObject(b);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right + 1, r.bottom + 1);
        // home position (f / j): a small mark at the bottom
        if (c.home) {
            int w = cellW_ / 4, h = MulDiv(2, dpi_, 96);
            RECT hm = {r.left + (cellW_ - w) / 2, r.bottom - h - 1, r.left + (cellW_ + w) / 2, r.bottom - 1};
            HBRUSH hb = CreateSolidBrush(c.cursor ? kCursorText : kHomeLabel);
            FillRect(dc, &hm, hb);
            DeleteObject(hb);
        }
    }
    // status line background
    RECT sr = {pad_, pad_ * 2 + cellH_ * 4, width_ - pad_, height_ - pad_};
    HBRUSH sb = CreateSolidBrush(kStatusBg);
    FillRect(dc, &sr, sb);
    DeleteObject(sb);

    // text
    BeginText(dc);
    for (int i = 0; i < 40; ++i) {
        const auto& c = view_.cells[i];
        RECT r = CellRect(i, pad_, cellW_, cellH_, gap_);
        RECT tr = {r.left + 1, r.top, r.right - 1, r.bottom};
        if (c.index >= 0) {
            std::wstring t = naggy::TruncateToWidth(c.text, 4, L' ');
            while (!t.empty() && t.back() == L' ') t.pop_back();
            DrawLabel(dc, t, tr, c.cursor ? kCursorText : kText, false, true);
        } else {
            DrawLabel(dc, L"\x00B7", tr, c.cursor ? kCursorText : kEmpty, false, true);
        }
    }
    wchar_t page[32];
    StringCchPrintfW(page, ARRAYSIZE(page), L"%d/%d", view_.plane + 1, view_.planes);
    RECT pr = sr;
    pr.left += pad_;
    DrawLabel(dc, page, pr, kLabel, true, false);
    RECT tr = sr;
    tr.left += pad_ + cellW_;
    tr.right -= pad_;
    DrawLabel(dc, view_.status, tr, kText, false, false);
    EndText();

    // border
    HPEN bp = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(dc, bp);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, width_, height_);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldFont);
    DeleteObject(bp);
    DeleteObject(cellPen);
}

void CandidateWindow::Show(const naggy::CandidateView& v, const RECT& rc) {
    if (!Create()) return;
    view_ = v;
    HDC dc = GetDC(hwnd_);
    Layout(dc);
    ReleaseDC(hwnd_, dc);

    // Centered horizontally under the composition (the cursor).
    POINT pt = {(rc.left + rc.right) / 2 - width_ / 2, rc.bottom + MulDiv(2, dpi_, 96)};
    HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork;
    if (pt.y + height_ > wa.bottom) pt.y = rc.top - height_ - MulDiv(2, dpi_, 96);
    if (pt.y < wa.top) pt.y = wa.top;
    if (pt.x + width_ > wa.right) pt.x = wa.right - width_;
    if (pt.x < wa.left) pt.x = wa.left;
    SetWindowPos(hwnd_, HWND_TOPMOST, pt.x, pt.y, width_, height_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
}

void CandidateWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CandidateWindow* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (self) {
            // double buffering
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, self->width_, self->height_);
            HGDIOBJ old = SelectObject(mem, bmp);
            self->Paint(mem);
            BitBlt(dc, 0, 0, self->width_, self->height_, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (self && self->onClick_) {
            int slot = self->HitTest(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
            if (slot >= 0) self->onClick_(slot, msg == WM_LBUTTONUP);
        }
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace naggy_win
