#pragma once

// Shared look-and-feel pieces used by the main window, flyouts, dialogs and the
// settings page: theme palette, fonts, DPI scaling and small drawing helpers.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

struct Palette {
    COLORREF strip, surface, editor, hover, pressed, text, textMuted, border, divider, status, panel, input,
        inputBorder;
    COLORREF flyout, flyoutBorder, flyoutHover, disabled, accent, accentText, accentSoft, smoke, card, cardBorder;
};

extern HINSTANCE g_inst;
extern HWND g_hwnd;
extern Palette g_pal;
extern bool g_dark;
extern UINT g_dpi;
extern HFONT g_uiFont, g_smallFont, g_iconFont, g_iconFontSmall;
extern HFONT g_titleFont;    // 20px semibold dialog titles
extern HFONT g_pageTitleFont;  // 40px settings page title
extern HFONT g_semiboldFont;   // 14px semibold
extern const wchar_t* g_uiFace;
extern const wchar_t* g_iconFace;

// When set, DrawTextIn draws alpha-correct text for translucent (Mica) layers.
extern void* g_textTheme;  // HTHEME
int S(int v);
void FillSolid(HDC dc, const RECT& r, COLORREF c);
void FrameRound(HDC dc, const RECT& r, COLORREF pen, COLORREF fill, int radius);
void FillRound(HDC dc, const RECT& r, COLORREF c, int radius);
int TextWidth(HDC dc, const std::wstring& s);
void DrawTextIn(HDC dc, const std::wstring& s, RECT r, COLORREF c, HFONT f, UINT fmt);
HFONT MakeFont(int px, const wchar_t* face, int weight = FW_NORMAL, bool italic = false, bool strike = false);
void ApplyRoundedPopup(HWND hwnd);  // Windows 11 rounded corners + border for popups

// Glyphs from Segoe Fluent Icons / Segoe MDL2 Assets.
namespace glyph {
constexpr wchar_t Close[] = L"";
constexpr wchar_t Add[] = L"";
constexpr wchar_t Settings[] = L"";
constexpr wchar_t ChevronDown[] = L"";
constexpr wchar_t ChevronUp[] = L"";
constexpr wchar_t ChevronRight[] = L"";
constexpr wchar_t ArrowDown[] = L"";
constexpr wchar_t ArrowUp[] = L"";
constexpr wchar_t Check[] = L"";
constexpr wchar_t Cancel[] = L"";
constexpr wchar_t Back[] = L"";
constexpr wchar_t Search[] = L"";
constexpr wchar_t Options[] = L"";
constexpr wchar_t List[] = L"";
constexpr wchar_t Link[] = L"";
constexpr wchar_t Bold[] = L"";
constexpr wchar_t Italic[] = L"";
constexpr wchar_t Strike[] = L"";
constexpr wchar_t Eraser[] = L"";
constexpr wchar_t Pen[] = L"";
constexpr wchar_t PenLines[] = L"";
constexpr wchar_t Sparkle[] = L"";
constexpr wchar_t Megaphone[] = L"";
constexpr wchar_t Person[] = L"";
constexpr wchar_t Undo[] = L"";
constexpr wchar_t Cut[] = L"";
constexpr wchar_t Copy[] = L"";
constexpr wchar_t Paste[] = L"";
constexpr wchar_t SelectAll[] = L"";
constexpr wchar_t Delete[] = L"";
constexpr wchar_t Grid[] = L"";
constexpr wchar_t Font[] = L"";
constexpr wchar_t Theme[] = L"";
constexpr wchar_t WordWrap[] = L"";
constexpr wchar_t OpenFile[] = L"";
constexpr wchar_t Window[] = L"";
constexpr wchar_t History[] = L"";
constexpr wchar_t Spell[] = L"";
constexpr wchar_t Document[] = L"";
constexpr wchar_t Feedback[] = L"";
constexpr wchar_t Unicode[] = L"";
}  // namespace glyph
