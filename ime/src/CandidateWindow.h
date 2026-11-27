// CandidateWindow.h - the 40-key virtual keyboard candidate window.
#pragma once
#include <functional>

#include "Engine.h"
#include "Globals.h"

#include <d2d1.h>
#include <dwrite.h>

namespace naggy_win {

class CandidateWindow {
public:
    // Called on a mouse click: (slot, terminate).
    using ClickHandler = std::function<void(int, bool)>;

    CandidateWindow() = default;
    ~CandidateWindow();
    void SetClickHandler(ClickHandler h) { onClick_ = std::move(h); }
    // Shows the window near rc (the composition rectangle, in screen
    // coordinates).
    void Show(const naggy::CandidateView& v, const RECT& rc);
    void Hide();
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Destroy();
    // Fonts are re-created on the next Show (after a settings change).
    void InvalidateFont() { fontValid_ = false; }

    static void RegisterWndClass();
    static void UnregisterWndClass();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Layout(HDC dc);
    void Paint(HDC dc);
    // Text drawing: Direct2D/DirectWrite (color fonts such as emoji) with
    // a GDI fallback.
    bool BeginText(HDC dc);
    void EndText();
    void DrawLabel(HDC dc, const std::wstring& t, const RECT& r, COLORREF color, bool isSmall,
                  bool center);
    void ReleaseD2D();
    int HitTest(int x, int y) const;
    bool Create();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr, smallFont_ = nullptr;
    std::wstring faceName_;
    float fontPx_ = 0, smallFontPx_ = 0;
    ID2D1Factory* d2d_ = nullptr;
    ID2D1DCRenderTarget* rt_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    IDWriteTextFormat* fmt_ = nullptr;
    IDWriteTextFormat* smallFmt_ = nullptr;
    bool d2dFailed_ = false;
    bool drawing_ = false;
    bool fontValid_ = false;
    int dpi_ = 96;
    naggy::CandidateView view_;
    ClickHandler onClick_;
    // geometry (pixels)
    int cellW_ = 0, cellH_ = 0, gap_ = 0, pad_ = 0, statusH_ = 0, width_ = 0, height_ = 0;
};

}  // namespace naggy_win
