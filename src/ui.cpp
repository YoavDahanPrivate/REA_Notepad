#include "ui.h"

#include <dwmapi.h>
#include <uxtheme.h>

HINSTANCE g_inst;
HWND g_hwnd;
Palette g_pal;
bool g_dark = false;
UINT g_dpi = 96;
HFONT g_uiFont, g_smallFont, g_iconFont, g_iconFontSmall;
HFONT g_titleFont, g_pageTitleFont, g_semiboldFont;
const wchar_t* g_uiFace = L"Segoe UI";
const wchar_t* g_iconFace = L"Segoe MDL2 Assets";

int S(int v) { return MulDiv(v, static_cast<int>(g_dpi), 96); }

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void FrameRound(HDC dc, const RECT& r, COLORREF pen, COLORREF fill, int radius) {
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

void FillRound(HDC dc, const RECT& r, COLORREF c, int radius) { FrameRound(dc, r, c, c, radius); }

int TextWidth(HDC dc, const std::wstring& s) {
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    return sz.cx;
}

void* g_textTheme = nullptr;

void DrawTextIn(HDC dc, const std::wstring& s, RECT r, COLORREF c, HFONT f, UINT fmt) {
    HGDIOBJ old = SelectObject(dc, f);
    if (g_textTheme) {
        // Drawing into a translucent (Mica) layer: alpha-correct text.
        DTTOPTS o{sizeof o};
        o.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
        o.crText = c;
        DrawThemeTextEx(static_cast<HTHEME>(g_textTheme), dc, 0, 0, s.c_str(), static_cast<int>(s.size()), fmt | DT_NOPREFIX, &r, &o);
        SelectObject(dc, old);
        return;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, fmt | DT_NOPREFIX);
    SelectObject(dc, old);
}

HFONT MakeFont(int px, const wchar_t* face, int weight, bool italic, bool strike) {
    // WinUI renders UI text with grayscale anti-aliasing (measured: no color fringes), which
    // looks lighter than GDI ClearType, so all UI fonts use grayscale.
    return CreateFontW(-px, 0, 0, 0, weight, italic, FALSE, strike, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}

void ApplyRoundedPopup(HWND hwnd) {
    constexpr DWORD kCornerPreference = 33, kBorderColor = 34;
    int round = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, kCornerPreference, &round, sizeof round);
    COLORREF border = g_pal.flyoutBorder;
    DwmSetWindowAttribute(hwnd, kBorderColor, &border, sizeof border);
}
