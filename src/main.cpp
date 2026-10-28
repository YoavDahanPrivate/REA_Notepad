// Notepad-- (ReaNotepad): a Windows 11 Notepad lookalike written from scratch.
//
// The structure mirrors what rea found in Notepad 11.2607 (see ANALYSIS.md):
// a Win32 host window with a DWM-extended title bar holding the tab strip,
// a menu row (File / Edit / View + settings), a RichEdit text surface per tab,
// an inline find/replace panel and a status bar. No Microsoft code or assets.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "ascii.h"
#include "dialog.h"
#include "flyout.h"
#include "markdown.h"
#include "resource.h"
#include "ui.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr wchar_t kAppName[] = L"Notepad--";
constexpr wchar_t kMainClass[] = L"ReaNotepadWindow";
constexpr wchar_t kFindClass[] = L"ReaNotepadFindPanel";
constexpr wchar_t kRegKey[] = L"Software\\ReaNotepad";
constexpr size_t kMaxRecent = 10;

// Layout metrics at 96 DPI, measured from Notepad 11.2607 through UI Automation.
constexpr int kStripH = 42;    // tabs occupy y 10..42
constexpr int kTabTop = 10;
constexpr int kTabsLeft = 40;  // app icon sits left of the tabs
constexpr int kTabMaxW = 240;
constexpr int kMenuH = 33;     // 32px items + 1px divider
constexpr int kStatusH = 32;
constexpr int kFindW = 620;

// Art styles in menu order; #S1#..#S8# codes index this list.
const int kStyleOrder[ascii::kStyleCount] = {0, 3, 4, 5, 6, 7, 1, 2};

// Letter sources offered for ##text## art (empty = our built-in block letters).
const wchar_t* const kArtFonts[7] = {L"", L"Segoe UI", L"Arial Black", L"Impact", L"Georgia", L"Comic Sans MS",
                                     L"Courier New"};
const wchar_t* const kArtFontNames[7] = {L"Block letters (built-in)", L"Segoe UI", L"Arial Black", L"Impact",
                                         L"Georgia", L"Comic Sans MS", L"Courier New"};

// DWM attributes that older SDK headers may lack.
constexpr DWORD kDwmUseDarkMode = 20;
constexpr DWORD kDwmBackdropType = 38;
constexpr int kBackdropTabbed = 4;

// Segoe Fluent Icons / MDL2 glyphs.
constexpr wchar_t kGlyphClose[] = L"";
constexpr wchar_t kGlyphAdd[] = L"";
constexpr wchar_t kGlyphSettings[] = L"";
constexpr wchar_t kGlyphChevronDown[] = L"";
constexpr wchar_t kGlyphChevronUp[] = L"";
constexpr wchar_t kGlyphArrowDown[] = L"";
constexpr wchar_t kGlyphArrowUp[] = L"";
constexpr wchar_t kGlyphMore[] = L"";
constexpr wchar_t kGlyphCancel[] = L"";

constexpr wchar_t kGlyphList[] = L"";
constexpr wchar_t kGlyphLink[] = L"";
constexpr wchar_t kGlyphTable[] = L"";
constexpr wchar_t kGlyphClearFormat[] = L"";

// Paragraph style values stored in PARAFORMAT2::sStyle (via ITextPara::SetStyle) so
// headings and list markers survive editing and can be written back as Markdown.
constexpr long kStyleHeading = 100;  // + level 1..6
constexpr long kStyleBullet = 200;   // + 1 '-', 2 '*', 3 '+'
constexpr long kStyleNumber = 300;   // + 1 '.', 2 ')'
const double kHeadingScale[7] = {1.0, 2.0, 1.6, 1.4, 1.2, 1.1, 1.0};

enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE, Ansi };
enum class Eol { CRLF, LF, CR };
enum class ThemeMode { System = 0, Light = 1, Dark = 2 };
// Text: .txt/untitled (formatting allowed). Markdown: .md. Code: anything else (plain only).
enum class Kind { Text = 0, Markdown = 1, Code = 2 };

struct Settings {
    ThemeMode theme = ThemeMode::System;
    std::wstring fontFace = L"Consolas";
    int fontSize10 = 110;  // tenths of a point
    bool bold = false;
    bool italic = false;
    bool wordWrap = true;
    bool statusBar = true;
    bool openInNewWindow = false;
    bool matchCase = false;
    bool wrapAround = true;
    bool formatting = true;
    bool spellCheck = true;
    bool recentEnabled = true;
    bool continueSession = true;
    bool autocorrect = true;
    bool writingTools = true;
    bool spellExt[5] = {true, true, true, true, true};  // .txt, .md, .srt/.ass, .lrc, .lic
    // ##text## ASCII art
    bool asciiOn = true;
    int asciiStyle = 0;   // ascii::Style
    int asciiHeight = 1;  // ascii::Height (Normal, 6 lines)
    int asciiFont = 0;    // index into kArtFonts (0 = built-in block letters)
    bool asciiCenter = false;
    int asciiPlacement = 0;  // 0 remove ##text## and put art below, 1 keep its words, 2 art in place of it
    std::vector<std::wstring> recent;
};

// Light values sampled from captures of Notepad 11.2607; dark values are estimates.
const Palette kLight{RGB(232, 232, 232), RGB(248, 248, 248), RGB(249, 249, 249), RGB(236, 236, 236),
                     RGB(226, 226, 226), RGB(26, 26, 26),    RGB(93, 93, 93),    RGB(234, 234, 234),
                     RGB(218, 218, 218), RGB(248, 248, 248), RGB(255, 255, 255), RGB(255, 255, 255),
                     RGB(208, 208, 208),
                     /*flyout*/ RGB(249, 249, 249), RGB(222, 222, 222), RGB(240, 240, 240), RGB(160, 160, 160),
                     /*accent*/ RGB(0, 95, 184), RGB(255, 255, 255), RGB(204, 228, 247), RGB(0, 0, 0),
                     /*card*/ RGB(251, 251, 251), RGB(229, 229, 229)};
const Palette kDark{RGB(32, 32, 32),    RGB(43, 43, 43),    RGB(39, 39, 39),    RGB(55, 55, 55),
                    RGB(66, 66, 66),    RGB(255, 255, 255), RGB(200, 200, 200), RGB(29, 29, 29),
                    RGB(70, 70, 70),    RGB(43, 43, 43),    RGB(44, 44, 44),    RGB(56, 56, 56),
                    RGB(78, 78, 78),
                    /*flyout*/ RGB(44, 44, 44), RGB(64, 64, 64), RGB(58, 58, 58), RGB(120, 120, 120),
                    /*accent*/ RGB(96, 205, 255), RGB(0, 0, 0), RGB(40, 70, 95), RGB(0, 0, 0),
                    /*card*/ RGB(43, 43, 43), RGB(29, 29, 29)};

struct Tab {
    HWND edit = nullptr;
    ITextDocument* doc = nullptr;
    std::wstring path;  // empty = untitled
    Encoding enc = Encoding::Utf8;
    Eol eol = Eol::CRLF;
    int zoom = 100;
    bool shownModified = false;
    Kind kind = Kind::Text;
    bool rich = false;    // control in rich-text mode with the formatting toolbar
    bool syntax = false;  // Markdown shown as raw syntax
};

struct Layout {
    RECT strip, menu, editor, status;
};

enum { SEG_ZOOM = 1, SEG_EOL, SEG_ENCODING };
struct StatusSeg {
    RECT rc;
    int id;
};

HACCEL g_accel;
Settings g_set;
bool g_mica = false;
int g_uiFontH = 16;
HICON g_smallIcon;
HFONT g_tabActiveFont;  // semibold title of the selected tab
HTHEME g_theme;
HBRUSH g_inputBrush;
HWND g_lastFocus;

std::vector<std::unique_ptr<Tab>> g_tabs;
int g_active = -1;

std::vector<RECT> g_tabRc;
RECT g_addRc{};
int g_hoverTab = -1;
bool g_hoverClose = false;
bool g_hoverAdd = false;
// Menu-row hit targets: 0-2 File/Edit/View, 3 settings gear, 4-11 formatting toolbar
// (headings, lists, bold, italic, strikethrough, link, table, clear formatting).
// 12-14: Writing tools dropdown, What's new, account (right side, before the gear).
constexpr int kToolFirst = 4;
constexpr int kToolCount = 8;
constexpr int kWriting = kToolFirst + kToolCount;
constexpr int kWhatsNew = kWriting + 1;
constexpr int kAvatar = kWriting + 2;
constexpr int kMenuTargets = kWriting + 3;
RECT g_menuRc[kMenuTargets]{};
bool g_whatsNewSeen = false;
bool g_toolbarFits = false;
bool g_fmtBold = false, g_fmtItalic = false, g_fmtStrike = false;
HFONT g_fmtFonts[3];  // B, I, S glyph fonts
int g_hoverMenu = -1;
int g_openMenu = -1;
bool g_tracking = false;
std::vector<StatusSeg> g_segs;

HWND g_find, g_findEdit, g_replEdit;
HWND g_settings = nullptr;  // in-window settings page, when open
RECT g_backRc{};            // its back button in the title bar
bool g_expTheme = true, g_expFont = true, g_expStart = false, g_expSpell = false;
void PositionSettings();
bool g_replaceMode = false;

HGLOBAL g_devMode, g_devNames;
RECT g_margins{750, 1000, 750, 1000};  // thousandths of an inch

using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
using FlushMenuThemesFn = void(WINAPI*)();
SetPreferredAppModeFn pSetPreferredAppMode;
AllowDarkModeForWindowFn pAllowDarkModeForWindow;
FlushMenuThemesFn pFlushMenuThemes;

// ---------------------------------------------------------------------------
// Small helpers

Tab* Cur() { return g_active >= 0 && g_active < static_cast<int>(g_tabs.size()) ? g_tabs[g_active].get() : nullptr; }

std::wstring TabName(const Tab& t) { return t.path.empty() ? L"Untitled" : PathFindFileNameW(t.path.c_str()); }

bool IsModified(const Tab& t) { return SendMessageW(t.edit, EM_GETMODIFY, 0, 0) != 0; }

std::wstring WindowText(HWND h) {
    std::wstring s(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    s.resize(static_cast<size_t>(GetWindowTextW(h, s.data(), static_cast<int>(s.size()))));
    return s;
}

std::wstring ErrorText(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring s = buf ? buf : L"";
    if (buf) LocalFree(buf);
    return s;
}

void ShowError(const std::wstring& msg, DWORD err) {
    std::wstring full = msg;
    if (err) full += L"\n\n" + ErrorText(err);
    dlg::Spec s;
    s.title = kAppName;
    s.body = full;
    s.close = L"OK";
    s.width = 420;
    dlg::Show(g_hwnd, s);
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

std::wstring FullPath(const std::wstring& p) {
    DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (!n) return p;
    std::wstring out(n, L'\0');
    n = GetFullPathNameW(p.c_str(), n, out.data(), nullptr);
    out.resize(n);
    return out;
}

// ---------------------------------------------------------------------------
// Settings (HKCU\Software\ReaNotepad)

DWORD RegDword(const wchar_t* name, DWORD def) {
    DWORD v = 0, cb = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_DWORD, nullptr, &v, &cb) == ERROR_SUCCESS ? v : def;
}

std::wstring RegString(const wchar_t* name, const std::wstring& def) {
    DWORD cb = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_SZ, nullptr, nullptr, &cb) != ERROR_SUCCESS ||
        cb < sizeof(wchar_t))
        return def;
    std::wstring s(cb / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_SZ, nullptr, s.data(), &cb) != ERROR_SUCCESS)
        return def;
    s.resize(wcslen(s.c_str()));
    return s;
}

void RegSetDword(const wchar_t* name, DWORD v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_DWORD, &v, sizeof v);
}

void RegSetString(const wchar_t* name, const std::wstring& s) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_SZ, s.c_str(),
                    static_cast<DWORD>((s.size() + 1) * sizeof(wchar_t)));
}

void LoadSettings() {
    g_set.theme = static_cast<ThemeMode>(std::min<DWORD>(RegDword(L"Theme", 0), 2));
    g_set.fontFace = RegString(L"FontFace", g_set.fontFace);
    g_set.fontSize10 = static_cast<int>(std::clamp<DWORD>(RegDword(L"FontSize10", 110), 40, 1000));
    g_set.bold = RegDword(L"FontBold", 0) != 0;
    g_set.italic = RegDword(L"FontItalic", 0) != 0;
    g_set.wordWrap = RegDword(L"WordWrap", 1) != 0;
    g_set.statusBar = RegDword(L"StatusBar", 1) != 0;
    g_set.openInNewWindow = RegDword(L"OpenInNewWindow", 0) != 0;
    g_set.matchCase = RegDword(L"MatchCase", 0) != 0;
    g_set.wrapAround = RegDword(L"WrapAround", 1) != 0;
    g_set.formatting = RegDword(L"Formatting", 1) != 0;
    g_set.spellCheck = RegDword(L"SpellCheck", 1) != 0;
    g_set.recentEnabled = RegDword(L"RecentFiles", 1) != 0;
    g_set.continueSession = RegDword(L"ContinueSession", 1) != 0;
    g_set.autocorrect = RegDword(L"Autocorrect", 1) != 0;
    g_set.writingTools = RegDword(L"WritingTools", 1) != 0;
    DWORD ext = RegDword(L"SpellExtensions", 0x1F);
    for (int i = 0; i < 5; ++i) g_set.spellExt[i] = (ext >> i) & 1;
    g_set.asciiOn = RegDword(L"AsciiArt", 1) != 0;
    g_set.asciiStyle = static_cast<int>(std::min<DWORD>(RegDword(L"AsciiStyle", 0), ascii::kStyleCount - 1));
    g_set.asciiHeight = static_cast<int>(std::min<DWORD>(RegDword(L"AsciiHeight", 1), 3));
    g_set.asciiFont = static_cast<int>(std::min<DWORD>(RegDword(L"AsciiFont", 0), 6));
    g_set.asciiCenter = RegDword(L"AsciiCenter", 0) != 0;
    g_set.asciiPlacement = static_cast<int>(std::min<DWORD>(RegDword(L"AsciiPlacement", 0), 2));
    g_set.recent.clear();
    for (size_t i = 0; i < kMaxRecent; ++i) {
        std::wstring name = L"Recent" + std::to_wstring(i);
        std::wstring v = RegString(name.c_str(), L"");
        if (!v.empty()) g_set.recent.push_back(v);
    }
}

void SaveRecent() {
    for (size_t i = 0; i < kMaxRecent; ++i) {
        std::wstring name = L"Recent" + std::to_wstring(i);
        if (i < g_set.recent.size())
            RegSetString(name.c_str(), g_set.recent[i]);
        else
            RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegKey, name.c_str());
    }
}

void SaveSettings() {
    RegSetDword(L"Theme", static_cast<DWORD>(g_set.theme));
    RegSetString(L"FontFace", g_set.fontFace);
    RegSetDword(L"FontSize10", static_cast<DWORD>(g_set.fontSize10));
    RegSetDword(L"FontBold", g_set.bold);
    RegSetDword(L"FontItalic", g_set.italic);
    RegSetDword(L"WordWrap", g_set.wordWrap);
    RegSetDword(L"StatusBar", g_set.statusBar);
    RegSetDword(L"OpenInNewWindow", g_set.openInNewWindow);
    RegSetDword(L"MatchCase", g_set.matchCase);
    RegSetDword(L"WrapAround", g_set.wrapAround);
    RegSetDword(L"Formatting", g_set.formatting);
    RegSetDword(L"SpellCheck", g_set.spellCheck);
    RegSetDword(L"RecentFiles", g_set.recentEnabled);
    RegSetDword(L"ContinueSession", g_set.continueSession);
    RegSetDword(L"Autocorrect", g_set.autocorrect);
    RegSetDword(L"WritingTools", g_set.writingTools);
    DWORD ext = 0;
    for (int i = 0; i < 5; ++i) ext |= (g_set.spellExt[i] ? 1u : 0u) << i;
    RegSetDword(L"SpellExtensions", ext);
    RegSetDword(L"AsciiArt", g_set.asciiOn);
    RegSetDword(L"AsciiStyle", static_cast<DWORD>(g_set.asciiStyle));
    RegSetDword(L"AsciiHeight", static_cast<DWORD>(g_set.asciiHeight));
    RegSetDword(L"AsciiFont", static_cast<DWORD>(g_set.asciiFont));
    RegSetDword(L"AsciiCenter", g_set.asciiCenter);
    RegSetDword(L"AsciiPlacement", static_cast<DWORD>(g_set.asciiPlacement));
}

void AddRecent(const std::wstring& p) {
    if (!g_set.recentEnabled) return;
    SHAddToRecentDocs(SHARD_PATHW, p.c_str());
    auto& r = g_set.recent;
    r.erase(std::remove_if(r.begin(), r.end(), [&](const std::wstring& x) { return SamePath(x, p); }), r.end());
    r.insert(r.begin(), p);
    if (r.size() > kMaxRecent) r.resize(kMaxRecent);
    SaveRecent();
}

// ---------------------------------------------------------------------------
// Theme, fonts and frame

void InitDarkModeApi() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    pSetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    pAllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
    pFlushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
}

bool SystemPrefersDark() {
    DWORD v = 1, cb = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &cb);
    return v == 0;
}

bool FontExists(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(
        dc, &lf,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            *reinterpret_cast<bool*>(p) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

void CreateFonts() {
    for (HFONT f : {g_uiFont, g_smallFont, g_iconFont, g_iconFontSmall, g_titleFont, g_pageTitleFont, g_semiboldFont})
        if (f) DeleteObject(f);
    static const wchar_t* iconFace = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    static const wchar_t* uiFace = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    static const wchar_t* displayFace =
        FontExists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    g_uiFace = uiFace;
    g_iconFace = iconFace;
    g_uiFont = MakeFont(S(14), uiFace);     // menu bar, flyouts, find panel
    g_smallFont = MakeFont(S(12), uiFace);  // tab titles, status bar, shortcuts
    g_iconFont = MakeFont(S(16), iconFace);
    g_iconFontSmall = MakeFont(S(10), iconFace);
    g_titleFont = MakeFont(S(20), displayFace, FW_SEMIBOLD);
    g_pageTitleFont = MakeFont(S(40), displayFace, FW_SEMIBOLD);
    g_semiboldFont = MakeFont(S(14), uiFace, FW_SEMIBOLD);
    if (g_tabActiveFont) DeleteObject(g_tabActiveFont);
    g_tabActiveFont = MakeFont(S(12), uiFace, FW_SEMIBOLD);
    for (HFONT f : g_fmtFonts)
        if (f) DeleteObject(f);
    g_fmtFonts[0] = CreateFontW(-S(15), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fmtFonts[1] = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fmtFonts[2] = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, TRUE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_uiFont);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    g_uiFontH = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);

    if (g_findEdit) SendMessageW(g_findEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    if (g_replEdit) SendMessageW(g_replEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
}

// Mica shows through the tab strip, menu row and status bar (all drawn as translucent layers);
// the settings page covers everything below the title bar, so then only the strip is glass.
void ExtendFrame() {
    bool page = g_settings != nullptr;
    MARGINS m{0, 0, S(page ? kStripH : kStripH + kMenuH), page || !g_set.statusBar ? 0 : S(kStatusH)};
    DwmExtendFrameIntoClientArea(g_hwnd, &m);
}

template <class F>
void WithUndoSuspended(Tab& t, F f) {
    if (t.doc) t.doc->Undo(tomSuspend, nullptr);
    f();
    if (t.doc) t.doc->Undo(tomResume, nullptr);
}

float BaseSizePt() { return static_cast<float>(g_set.fontSize10) / 10.0f; }

long ParaStyle(Tab& t, long cp) {
    long style = 0;
    ITextRange* r = nullptr;
    if (t.doc && SUCCEEDED(t.doc->Range(cp, cp, &r)) && r) {
        ITextPara* p = nullptr;
        if (SUCCEEDED(r->GetPara(&p)) && p) {
            p->GetStyle(&style);
            p->Release();
        }
        r->Release();
    }
    return style;
}

// Rich documents keep per-run bold/italic and per-paragraph heading sizes, so a font or
// theme change only resets face, color and the size implied by each paragraph's style.
void ReapplyRichFonts(Tab& t) {
    if (!t.doc) return;
    ITextRange* all = nullptr;
    if (FAILED(t.doc->Range(0, 0, &all)) || !all) return;
    long len = 0;
    all->GetStoryLength(&len);
    all->Release();
    BSTR face = SysAllocString(g_set.fontFace.c_str());
    for (long cp = 0; cp < len;) {
        ITextRange* r = nullptr;
        if (FAILED(t.doc->Range(cp, cp, &r)) || !r) break;
        r->Expand(tomParagraph, nullptr);
        long s = 0, e = 0, style = 0;
        r->GetStart(&s);
        r->GetEnd(&e);
        ITextPara* p = nullptr;
        if (SUCCEEDED(r->GetPara(&p)) && p) {
            p->GetStyle(&style);
            p->Release();
        }
        int level = style > kStyleHeading && style <= kStyleHeading + 6 ? static_cast<int>(style - kStyleHeading) : 0;
        ITextFont* f = nullptr;
        if (SUCCEEDED(r->GetFont(&f)) && f) {
            f->SetName(face);
            f->SetSize(BaseSizePt() * static_cast<float>(kHeadingScale[level]));
            f->Release();
        }
        r->Release();
        cp = e > cp ? e : cp + 1;
    }
    SysFreeString(face);
}

void ColorLinks(Tab& t);

void ApplyEditorLook(Tab& t) {
    HWND e = t.edit;
    SendMessageW(e, EM_SETBKGNDCOLOR, 0, g_pal.editor);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_COLOR | CFM_CHARSET;
    cf.dwEffects = (g_set.bold ? CFE_BOLD : 0) | (g_set.italic ? CFE_ITALIC : 0);
    cf.yHeight = g_set.fontSize10 * 2;  // tenths of a point -> twips
    cf.crTextColor = g_pal.text;
    cf.bCharSet = DEFAULT_CHARSET;
    wcsncpy_s(cf.szFaceName, g_set.fontFace.c_str(), _TRUNCATE);

    WithUndoSuspended(t, [&] {
        LRESULT modified = SendMessageW(e, EM_GETMODIFY, 0, 0);
        SendMessageW(e, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
        if (t.rich) {
            CHARFORMAT2W color{};
            color.cbSize = sizeof color;
            color.dwMask = CFM_COLOR;
            color.crTextColor = g_pal.text;
            SendMessageW(e, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&color));
            ReapplyRichFonts(t);
            ColorLinks(t);
        } else {
            SendMessageW(e, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
        }
        SendMessageW(e, EM_SETMODIFY, modified, 0);
    });

    LRESULT lang = SendMessageW(e, EM_GETLANGOPTIONS, 0, 0);
    lang &= ~(IMF_AUTOFONT | IMF_DUALFONT | IMF_AUTOKEYBOARD | IMF_SPELLCHECKING);
    // Spell check per file type, as in Notepad's settings (.txt, .md, .srt/.ass, .lrc, .lic).
    const wchar_t* ext = t.path.empty() ? L".txt" : PathFindExtensionW(t.path.c_str());
    int slot = !_wcsicmp(ext, L".txt") ? 0
               : (!_wcsicmp(ext, L".md") || !_wcsicmp(ext, L".markdown")) ? 1
               : (!_wcsicmp(ext, L".srt") || !_wcsicmp(ext, L".ass")) ? 2
               : !_wcsicmp(ext, L".lrc") ? 3
               : !_wcsicmp(ext, L".lic") ? 4
                                         : -1;
    bool spell = g_set.spellCheck && slot >= 0 && g_set.spellExt[slot];
    if (spell) lang |= IMF_SPELLCHECKING;
    SendMessageW(e, EM_SETLANGOPTIONS, 0, lang);
    const LPARAM ctf = SES_USECTF | SES_CTFALLOWPROOFING;
    SendMessageW(e, EM_SETEDITSTYLE, spell ? ctf : 0, ctf);

    SendMessageW(e, EM_SETTARGETDEVICE, 0, g_set.wordWrap ? 0 : 1);
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(e, g_dark);
    SetWindowTheme(e, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ApplyTheme() {
    g_dark = g_set.theme == ThemeMode::Dark || (g_set.theme == ThemeMode::System && SystemPrefersDark());
    g_pal = g_dark ? kDark : kLight;

    BOOL dark = g_dark;
    DwmSetWindowAttribute(g_hwnd, kDwmUseDarkMode, &dark, sizeof dark);
    int backdrop = kBackdropTabbed;
    g_mica = SUCCEEDED(DwmSetWindowAttribute(g_hwnd, kDwmBackdropType, &backdrop, sizeof backdrop));

    if (pSetPreferredAppMode) pSetPreferredAppMode(g_dark ? 2 : 3);  // ForceDark / ForceLight
    if (pFlushMenuThemes) pFlushMenuThemes();
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(g_hwnd, g_dark);

    if (g_inputBrush) DeleteObject(g_inputBrush);
    g_inputBrush = CreateSolidBrush(g_pal.input);

    for (auto& t : g_tabs) ApplyEditorLook(*t);
    for (HWND e : {g_findEdit, g_replEdit}) {
        if (!e) continue;
        if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(e, g_dark);
        SetWindowTheme(e, g_dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
    }
    RedrawWindow(g_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// Our own drawing of a notepad in the style of the Windows 11 icon (colors measured from the
// real title-bar icon): blue gradient pad, four dark-teal binding rings, three text lines,
// a white page edge and a brown back cover. Supersampled 4x4 for smooth edges.
HICON MakeAppIcon(int size) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) {
        DeleteDC(dc);
        return nullptr;
    }
    struct Rgb {
        double r, g, b;
    };
    const Rgb ring{0x05, 0x70, 0x93}, top{0x7D, 0xD9, 0xF4}, bottom{0x43, 0xAF, 0xCF};
    const Rgb page{0xEA, 0xEA, 0xEA}, cover{0x98, 0x4B, 0x0A};
    // Returns the color at a point in 16-unit icon space, or false for transparent.
    // Pixel-aligned at 16px: a 16-unit grid matching the measured 13x16 shape.
    auto sample = [&](double x, double y, Rgb& c) {
        auto in = [&](double x0, double y0, double x1, double y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; };
        static const double rings[4][2] = {{2, 4}, {5, 6}, {7, 9}, {10, 11}};
        for (const auto& r : rings)
            if (in(r[0], 0.0, r[1], 1.6)) {
                c = ring;
                return true;
            }
        if (in(1.0, 14.0, 14.0, 15.0)) {
            c = page;
            return true;
        }
        if (in(1.0, 15.0, 14.0, 16.0)) {
            c = cover;
            return true;
        }
        if (in(1.0, 1.0, 14.0, 14.0)) {
            for (double ly : {4.0, 7.0, 9.0})
                if (in(3.0, ly, 11.0, ly + 1.0)) {
                    c = ring;
                    return true;
                }
            double t = ((x - 1.0) / 13.0 + (y - 1.0) / 13.0) / 2.0;
            c = {top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t};
            return true;
        }
        return false;
    };
    auto* p = static_cast<DWORD*>(bits);
    double unit = 16.0 / size;
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            double r = 0, g = 0, b = 0;
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    Rgb c{};
                    if (sample((px + (sx + 0.5) / 4.0) * unit, (py + (sy + 0.5) / 4.0) * unit, c)) {
                        r += c.r;
                        g += c.g;
                        b += c.b;
                        ++hits;
                    }
                }
            // Premultiplied alpha: summed color over 16 samples already scales by coverage.
            DWORD a = static_cast<DWORD>(hits * 255 / 16);
            p[py * size + px] = (a << 24) | (static_cast<DWORD>(r / 16) << 16) | (static_cast<DWORD>(g / 16) << 8) |
                                static_cast<DWORD>(b / 16);
        }
    std::vector<BYTE> zeros(static_cast<size_t>((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    return icon;
}

// ---------------------------------------------------------------------------
// Layout

Layout GetLayout() {
    RECT c;
    GetClientRect(g_hwnd, &c);
    Layout l;
    l.strip = {0, 0, c.right, S(kStripH)};
    l.menu = {0, l.strip.bottom, c.right, l.strip.bottom + S(kMenuH)};
    int sh = g_set.statusBar ? S(kStatusH) : 0;
    l.status = {0, std::max<LONG>(l.menu.bottom, c.bottom - sh), c.right, c.bottom};
    l.editor = {0, l.menu.bottom, c.right, l.status.top};
    return l;
}

int CaptionButtonsWidth() {
    RECT rc{};
    if (SUCCEEDED(DwmGetWindowAttribute(g_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rc, sizeof rc)) && rc.right > rc.left)
        return rc.right - rc.left;
    return S(138);
}

void LayoutTabs() {
    Layout l = GetLayout();
    int n = static_cast<int>(g_tabs.size());
    g_tabRc.assign(n, RECT{});
    int left = S(kTabsLeft);
    int avail = l.strip.right - left - S(38) - CaptionButtonsWidth() - S(48);
    int w = std::min(S(kTabMaxW), std::max(S(40), avail / std::max(1, n)));
    int x = left;
    for (int i = 0; i < n; ++i) {
        g_tabRc[i] = {x, S(kTabTop), x + w, l.strip.bottom};
        x += w;
    }
    g_addRc = {x + S(3), S(16), x + S(35), S(40)};
}

RECT TabCloseRect(const RECT& t) {
    int cy = (t.top + t.bottom) / 2;
    return {t.right - S(9) - S(32), cy - S(12), t.right - S(9), cy + S(12)};
}

void LayoutMenuRects() {
    Layout l = GetLayout();
    static const wchar_t* names[3] = {L"File", L"Edit", L"View"};
    HDC dc = GetDC(g_hwnd);
    HGDIOBJ old = SelectObject(dc, g_uiFont);
    int x = S(4);
    for (int i = 0; i < 3; ++i) {
        int w = TextWidth(dc, names[i]) + S(20);
        g_menuRc[i] = {x, l.menu.top, x + w, l.menu.top + S(32)};
        x += w + S(8);
    }
    // Right side, measured: Writing tools 56px, What's new 30x30, account 30, settings 30.
    int r = l.menu.right;
    g_menuRc[3] = {r - S(38), l.menu.top, r - S(8), l.menu.top + S(32)};
    g_menuRc[kAvatar] = {r - S(72), l.menu.top, r - S(42), l.menu.top + S(32)};
    g_menuRc[kWhatsNew] = {r - S(106), l.menu.top + S(1), r - S(76), l.menu.top + S(31)};
    g_menuRc[kWriting] = g_set.writingTools ? RECT{r - S(166), l.menu.top, r - S(110), l.menu.top + S(32)}
                                            : RECT{r - S(106), l.menu.top, r - S(106), l.menu.top};
    SelectObject(dc, old);
    ReleaseDC(g_hwnd, dc);

    // Formatting toolbar: 328px wide at 100%, centered in the row (measured from Notepad).
    static const int widths[kToolCount] = {56, 56, 32, 32, 32, 32, 56, 32};
    int total = 0;
    for (int w : widths) total += S(w);
    int tx = std::max<int>((l.menu.right - total) / 2, x + S(16));
    g_toolbarFits = tx + total <= g_menuRc[kWriting].left - S(8);
    for (int i = 0; i < kToolCount; ++i) {
        g_menuRc[kToolFirst + i] = {tx, l.menu.top, tx + S(widths[i]), l.menu.top + S(32)};
        tx += S(widths[i]);
    }
}

bool ToolbarVisible();

void PositionFind(const Layout& l) {
    int w = std::min<int>(S(kFindW), l.editor.right - l.editor.left - S(16));
    int h = S(g_replaceMode ? 88 : 48);
    int x = std::max<int>(l.editor.left + S(8), (l.editor.left + l.editor.right - w) / 2);
    SetWindowPos(g_find, HWND_TOP, x, l.editor.top + S(8), w, h, SWP_NOACTIVATE);
}

void LayoutChildren() {
    Layout l = GetLayout();
    LayoutTabs();
    if (Tab* t = Cur()) {
        SetWindowPos(t->edit, nullptr, l.editor.left, l.editor.top, l.editor.right - l.editor.left,
                     l.editor.bottom - l.editor.top, SWP_NOZORDER | SWP_NOACTIVATE);
        // Notepad's text starts 16px in and ~14px down; inset the formatting rect so the
        // scrollbars still run the full height.
        RECT rc;
        GetClientRect(t->edit, &rc);
        rc.left += S(14);
        rc.top += S(12);
        rc.right = std::max(rc.left + 1, rc.right - S(8));
        SendMessageW(t->edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rc));
    }
    if (g_find && IsWindowVisible(g_find)) PositionFind(l);
    PositionSettings();
    ExtendFrame();
}

void InvalidateStrip() {
    Layout l = GetLayout();
    InvalidateRect(g_hwnd, &l.strip, FALSE);
}

void InvalidateMenu() {
    Layout l = GetLayout();
    InvalidateRect(g_hwnd, &l.menu, FALSE);
}

void InvalidateStatus() {
    Layout l = GetLayout();
    InvalidateRect(g_hwnd, &l.status, FALSE);
}

void UpdateTitle() {
    Tab* t = Cur();
    std::wstring title = (t ? TabName(*t) : std::wstring(L"Untitled")) + L" - " + kAppName;
    SetWindowTextW(g_hwnd, title.c_str());
}

// ---------------------------------------------------------------------------
// Text, encodings and files

struct StreamSource {
    const BYTE* p;
    size_t left;
};

DWORD CALLBACK StreamInCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    auto* s = reinterpret_cast<StreamSource*>(cookie);
    LONG n = static_cast<LONG>(std::min<size_t>(static_cast<size_t>(cb), s->left));
    memcpy(buf, s->p, static_cast<size_t>(n));
    s->p += n;
    s->left -= static_cast<size_t>(n);
    *pcb = n;
    return 0;
}

// EM_STREAMIN never interprets the data as RTF, unlike WM_SETTEXT.
void StreamText(HWND e, const std::wstring& text, bool selectionOnly) {
    StreamSource src{reinterpret_cast<const BYTE*>(text.data()), text.size() * sizeof(wchar_t)};
    EDITSTREAM es{reinterpret_cast<DWORD_PTR>(&src), 0, StreamInCallback};
    SendMessageW(e, EM_STREAMIN, SF_TEXT | SF_UNICODE | (selectionOnly ? SFF_SELECTION : 0),
                 reinterpret_cast<LPARAM>(&es));
}

LONG TextLength(HWND e) {
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_PRECISE, 1200};
    return static_cast<LONG>(SendMessageW(e, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0));
}

// Paragraph breaks come back as '\r'.
std::wstring GetEditText(HWND e) {
    LONG n = TextLength(e);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GETTEXTEX gt{};
    gt.cb = static_cast<DWORD>((n + 1) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    LRESULT got = SendMessageW(e, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(s.data()));
    s.resize(static_cast<size_t>(std::max<LRESULT>(0, got)));
    return s;
}

void StreamRtf(HWND e, const std::string& rtf, bool selectionOnly) {
    StreamSource src{reinterpret_cast<const BYTE*>(rtf.data()), rtf.size()};
    EDITSTREAM es{reinterpret_cast<DWORD_PTR>(&src), 0, StreamInCallback};
    SendMessageW(e, EM_STREAMIN, SF_RTF | (selectionOnly ? SFF_SELECTION : 0), reinterpret_cast<LPARAM>(&es));
}

DWORD CALLBACK StreamOutCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    reinterpret_cast<std::string*>(cookie)->append(reinterpret_cast<const char*>(buf), static_cast<size_t>(cb));
    *pcb = cb;
    return 0;
}

std::string StreamOutRtf(HWND e, bool selectionOnly) {
    std::string out;
    EDITSTREAM es{reinterpret_cast<DWORD_PTR>(&out), 0, StreamOutCallback};
    SendMessageW(e, EM_STREAMOUT, SF_RTF | (selectionOnly ? SFF_SELECTION : 0), reinterpret_cast<LPARAM>(&es));
    return out;
}

std::string RtfEscape(const std::wstring& s) {
    std::string out;
    for (wchar_t c : s) {
        if (c == L'\\' || c == L'{' || c == L'}') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        } else {
            out += "\\u" + std::to_string(static_cast<short>(c)) + "?";
        }
    }
    return out;
}

bool ReadAllBytes(const std::wstring& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 512LL * 1024 * 1024) {
        CloseHandle(h);
        SetLastError(ERROR_FILE_TOO_LARGE);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    size_t off = 0;
    while (off < out.size()) {
        DWORD rd = 0;
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - off, 1u << 30));
        if (!ReadFile(h, out.data() + off, chunk, &rd, nullptr) || rd == 0) break;
        off += rd;
    }
    out.resize(off);
    CloseHandle(h);
    return true;
}

bool WriteAllBytes(const std::wstring& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t off = 0;
    bool ok = true;
    while (off < data.size()) {
        DWORD wr = 0;
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - off, 1u << 30));
        if (!WriteFile(h, data.data() + off, chunk, &wr, nullptr)) {
            ok = false;
            break;
        }
        off += wr;
    }
    DWORD err = GetLastError();
    if (ok) ok = SetEndOfFile(h) != FALSE;
    CloseHandle(h);
    if (!ok) SetLastError(err);
    return ok;
}

Encoding Decode(const std::string& b, std::wstring& out) {
    auto u = reinterpret_cast<const unsigned char*>(b.data());
    size_t n = b.size();
    auto mb = [&](UINT cp, const char* p, size_t len, DWORD flags) {
        out.clear();
        if (!len) return true;
        int w = MultiByteToWideChar(cp, flags, p, static_cast<int>(len), nullptr, 0);
        if (w <= 0) return false;
        out.resize(static_cast<size_t>(w));
        MultiByteToWideChar(cp, flags, p, static_cast<int>(len), out.data(), w);
        return true;
    };
    if (n >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        mb(CP_UTF8, b.data() + 3, n - 3, 0);
        return Encoding::Utf8Bom;
    }
    if (n >= 2 && u[0] == 0xFF && u[1] == 0xFE) {
        out.assign(reinterpret_cast<const wchar_t*>(b.data() + 2), (n - 2) / 2);
        return Encoding::Utf16LE;
    }
    if (n >= 2 && u[0] == 0xFE && u[1] == 0xFF) {
        size_t m = (n - 2) / 2;
        out.resize(m);
        for (size_t i = 0; i < m; ++i) out[i] = static_cast<wchar_t>((u[2 + 2 * i] << 8) | u[3 + 2 * i]);
        return Encoding::Utf16BE;
    }
    if (mb(CP_UTF8, b.data(), n, MB_ERR_INVALID_CHARS)) return Encoding::Utf8;
    mb(CP_ACP, b.data(), n, 0);
    return Encoding::Ansi;
}

std::string Encode(const std::wstring& w, Encoding e) {
    auto wc = [&](UINT cp) {
        std::string s;
        if (w.empty()) return s;
        int n = WideCharToMultiByte(cp, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        s.resize(static_cast<size_t>(n));
        WideCharToMultiByte(cp, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    };
    switch (e) {
    case Encoding::Utf8Bom:
        return "\xEF\xBB\xBF" + wc(CP_UTF8);
    case Encoding::Utf16LE: {
        std::string s("\xFF\xFE", 2);
        s.append(reinterpret_cast<const char*>(w.data()), w.size() * sizeof(wchar_t));
        return s;
    }
    case Encoding::Utf16BE: {
        std::string s("\xFE\xFF", 2);
        for (wchar_t c : w) {
            s.push_back(static_cast<char>(c >> 8));
            s.push_back(static_cast<char>(c & 0xFF));
        }
        return s;
    }
    case Encoding::Ansi:
        return wc(CP_ACP);
    default:
        return wc(CP_UTF8);
    }
}

Eol DetectEol(const std::wstring& s) {
    size_t i = s.find_first_of(L"\r\n");
    if (i == std::wstring::npos) return Eol::CRLF;
    if (s[i] == L'\n') return Eol::LF;
    return i + 1 < s.size() && s[i + 1] == L'\n' ? Eol::CRLF : Eol::CR;
}

const wchar_t* EncodingName(Encoding e) {
    switch (e) {
    case Encoding::Utf8Bom: return L"UTF-8 with BOM";
    case Encoding::Utf16LE: return L"UTF-16 LE";
    case Encoding::Utf16BE: return L"UTF-16 BE";
    case Encoding::Ansi: return L"ANSI";
    default: return L"UTF-8";
    }
}

const wchar_t* EolName(Eol e) {
    switch (e) {
    case Eol::LF: return L"Unix (LF)";
    case Eol::CR: return L"Macintosh (CR)";
    default: return L"Windows (CRLF)";
    }
}

void CaretLineCol(Tab& t, long& ln, long& col) {
    ln = 1;
    col = 1;
    if (!t.doc) return;
    ITextSelection* sel = nullptr;
    if (FAILED(t.doc->GetSelection(&sel)) || !sel) return;
    long start = 0, end = 0, flags = 0;
    sel->GetStart(&start);
    sel->GetEnd(&end);
    sel->GetFlags(&flags);
    sel->Release();
    long cp = (flags & tomSelStartActive) ? start : end;
    ITextRange* r = nullptr;
    if (FAILED(t.doc->Range(cp, cp, &r)) || !r) return;
    r->GetIndex(tomParagraph, &ln);
    r->StartOf(tomParagraph, tomMove, nullptr);
    long ps = cp;
    r->GetStart(&ps);
    col = cp - ps + 1;
    r->Release();
}

// ---------------------------------------------------------------------------
// Rich text <-> Markdown model
//
// RichEdit table rows appear in the text as U+FFF9 ... cells ending in U+0007 ...
// U+FFFB, and friendly links as hidden "HYPERLINK "url"" text followed by the
// visible link text.

constexpr wchar_t kRowStart = 0xFFF9;
constexpr wchar_t kRowEnd = 0xFFFB;
constexpr wchar_t kCellEnd = 0x0007;

Kind KindForPath(const std::wstring& path) {
    if (path.empty()) return Kind::Text;
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (!_wcsicmp(ext, L".md") || !_wcsicmp(ext, L".markdown")) return Kind::Markdown;
    if (!_wcsicmp(ext, L".txt")) return Kind::Text;
    return Kind::Code;
}

std::wstring RangeText(ITextRange* r) {
    BSTR b = nullptr;
    std::wstring s;
    if (SUCCEEDED(r->GetText(&b)) && b) {
        s.assign(b, SysStringLen(b));
        SysFreeString(b);
    }
    return s;
}

long StoryLength(Tab& t) {
    long n = 0;
    ITextRange* r = nullptr;
    if (t.doc && SUCCEEDED(t.doc->Range(0, 0, &r)) && r) {
        r->GetStoryLength(&n);
        r->Release();
    }
    return n;
}

std::wstring RawText(Tab& t, long start, long end) {
    std::wstring s;
    ITextRange* r = nullptr;
    if (t.doc && SUCCEEDED(t.doc->Range(start, end, &r)) && r) {
        s = RangeText(r);
        r->Release();
    }
    return s;
}

std::wstring UrlFromInstruction(const std::wstring& s) {
    size_t a = s.find(L"HYPERLINK");
    if (a == std::wstring::npos) return L"";
    size_t q1 = s.find(L'"', a);
    size_t q2 = q1 == std::wstring::npos ? std::wstring::npos : s.find(L'"', q1 + 1);
    if (q2 == std::wstring::npos) return L"";
    return s.substr(q1 + 1, q2 - q1 - 1);
}

struct Run {
    long start, end;
    bool bold, italic, strike, hidden, link;
};

std::vector<Run> CollectRuns(Tab& t, long len) {
    std::vector<Run> runs;
    ITextRange* r = nullptr;
    if (!t.doc || FAILED(t.doc->Range(0, 0, &r)) || !r) return runs;
    for (long guard = 0; guard < len + 2; ++guard) {
        long delta = 0;
        r->MoveEnd(tomCharFormat, 1, &delta);
        long s = 0, e = 0;
        r->GetStart(&s);
        r->GetEnd(&e);
        if (e <= s) break;
        Run run{s, e, false, false, false, false, false};
        ITextFont* f = nullptr;
        if (SUCCEEDED(r->GetFont(&f)) && f) {
            long v = 0;
            f->GetBold(&v);
            run.bold = v == tomTrue;
            f->GetItalic(&v);
            run.italic = v == tomTrue;
            f->GetStrikeThrough(&v);
            run.strike = v == tomTrue;
            f->GetHidden(&v);
            run.hidden = v == tomTrue;
            ITextFont2* f2 = nullptr;
            if (SUCCEEDED(f->QueryInterface(__uuidof(ITextFont2), reinterpret_cast<void**>(&f2))) && f2) {
                long lt = 0;
                f2->GetLinkType(&lt);
                run.link = lt != tomNoLink;
                f2->Release();
            }
            f->Release();
        }
        runs.push_back(run);
        r->Collapse(tomEnd);
        if (e >= len) break;
    }
    r->Release();
    return runs;
}

// Reads the editor into the Markdown model.
std::vector<md::Para> DocFromEditor(Tab& t) {
    std::vector<md::Para> doc;
    long len = StoryLength(t);
    std::wstring raw = RawText(t, 0, len);
    if (!raw.empty() && raw.back() == L'\r') raw.pop_back();  // the story's final paragraph mark
    long n = static_cast<long>(raw.size());
    std::vector<Run> runs = CollectRuns(t, len);

    long ps = 0;
    size_t firstRun = 0;
    while (ps <= n) {
        while (firstRun < runs.size() && runs[firstRun].end <= ps) ++firstRun;
        if (ps < n && raw[static_cast<size_t>(ps)] == kRowStart) {
            size_t rowEnd = raw.find(kRowEnd, static_cast<size_t>(ps));
            if (rowEnd == std::wstring::npos) rowEnd = raw.size();
            md::Para row;
            row.kind = md::Block::TableRow;
            std::wstring cell;
            for (size_t k = static_cast<size_t>(ps) + 1; k < rowEnd; ++k) {
                wchar_t c = raw[k];
                if (c == kCellEnd) {
                    row.cells.push_back(cell);
                    cell.clear();
                } else if (c == L'\r') {
                    if (!cell.empty()) cell.push_back(L' ');
                } else {
                    cell.push_back(c);
                }
            }
            doc.push_back(row);
            long next = static_cast<long>(rowEnd) + 1;
            if (next < n && raw[static_cast<size_t>(next)] == L'\r') ++next;
            if (next > n) break;
            ps = next;
            if (ps == n) break;  // table was the last thing in the document
            continue;
        }
        long pe = static_cast<long>(raw.find(L'\r', static_cast<size_t>(ps)));
        if (pe < 0) pe = n;

        md::Para p;
        long style = 0, listType = tomListNone, level = 0;
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(ps, ps, &r)) && r) {
            ITextPara* para = nullptr;
            if (SUCCEEDED(r->GetPara(&para)) && para) {
                para->GetStyle(&style);
                para->GetListType(&listType);
                para->GetListLevelIndex(&level);
                para->Release();
            }
            r->Release();
        }
        long base = listType & 0xFFFF;
        if (base == tomListBullet) {
            p.kind = md::Block::Bullet;
            p.marker = style == kStyleBullet + 2 ? L'*' : style == kStyleBullet + 3 ? L'+' : L'-';
            p.level = static_cast<int>(std::clamp<long>(level, 0, 8));
        } else if (base >= tomListNumberAsArabic && base <= tomListNumberAsSequence) {
            p.kind = md::Block::Numbered;
            p.marker = style == kStyleNumber + 2 ? L')' : L'.';
            p.level = static_cast<int>(std::clamp<long>(level, 0, 8));
        } else if (style > kStyleHeading && style <= kStyleHeading + 6) {
            p.kind = md::Block::Heading;
            p.level = static_cast<int>(style - kStyleHeading);
        }

        std::wstring pendingUrl;
        std::wstring linkUrl;
        md::Span cur;
        bool open = false;
        auto flush = [&] {
            if (open && cur.end > cur.start && (cur.bold || cur.italic || cur.strike || !cur.url.empty()))
                p.spans.push_back(cur);
            open = false;
        };
        for (size_t ri = firstRun; ri < runs.size() && runs[ri].start < pe; ++ri) {
            const Run& run = runs[ri];
            long s = std::max(run.start, ps), e = std::min(run.end, pe);
            if (e <= s) continue;
            std::wstring chunk = raw.substr(static_cast<size_t>(s), static_cast<size_t>(e - s));
            if (run.hidden) {
                std::wstring url = UrlFromInstruction(chunk);
                if (!url.empty()) pendingUrl = url;
                continue;
            }
            if (!pendingUrl.empty()) {
                linkUrl = pendingUrl;
                pendingUrl.clear();
            } else if (!run.link) {
                linkUrl.clear();
            }
            size_t at = p.text.size();
            p.text += chunk;
            bool bold = run.bold && p.kind != md::Block::Heading;  // headings are bold by style
            bool same = open && cur.bold == bold && cur.italic == run.italic && cur.strike == run.strike &&
                        cur.url == linkUrl && cur.end == at;
            if (same) {
                cur.end = p.text.size();
            } else {
                flush();
                cur = md::Span{};
                cur.start = at;
                cur.end = p.text.size();
                cur.bold = bold;
                cur.italic = run.italic;
                cur.strike = run.strike;
                cur.url = linkUrl;
                open = true;
            }
        }
        flush();
        doc.push_back(p);
        if (pe >= n) break;
        ps = pe + 1;
    }
    return doc;
}

std::wstring PlainOf(Tab& t) {
    if (!t.rich) return GetEditText(t.edit);
    std::wstring out;
    std::vector<md::Para> doc = DocFromEditor(t);
    for (size_t i = 0; i < doc.size(); ++i) {
        if (i) out += L'\r';
        if (doc[i].kind == md::Block::TableRow) {
            for (size_t c = 0; c < doc[i].cells.size(); ++c) out += (c ? L"\t" : L"") + doc[i].cells[c];
        } else {
            out += doc[i].text;
        }
    }
    return out;
}

void SetParaList(ITextPara* para, md::Block kind, wchar_t marker, int level) {
    if (kind == md::Block::Bullet) {
        para->SetListType(tomListBullet);
        para->SetStyle(kStyleBullet + (marker == L'*' ? 2 : marker == L'+' ? 3 : 1));
    } else if (kind == md::Block::Numbered) {
        para->SetListType(tomListNumberAsArabic | (marker == L')' ? tomListParentheses : tomListPeriod));
        para->SetListStart(1);
        para->SetStyle(kStyleNumber + (marker == L')' ? 2 : 1));
    } else {
        para->SetListType(tomListNone);
        para->SetStyle(0);
        para->SetIndents(0, 0, 0);
        return;
    }
    para->SetListLevelIndex(level);
    para->SetIndents(-18.0f, 18.0f * static_cast<float>(level + 1), 0);
}

void SetParaHeading(ITextRange* r, int level) {
    ITextPara* para = nullptr;
    if (SUCCEEDED(r->GetPara(&para)) && para) {
        para->SetListType(tomListNone);
        para->SetIndents(0, 0, 0);
        para->SetStyle(level ? kStyleHeading + level : 0);
        para->Release();
    }
    ITextFont* f = nullptr;
    if (SUCCEEDED(r->GetFont(&f)) && f) {
        f->SetSize(BaseSizePt() * static_cast<float>(kHeadingScale[level]));
        f->SetBold(level ? tomTrue : tomFalse);
        f->Release();
    }
}

std::string LinkRtf(const std::wstring& text, const std::wstring& url) {
    COLORREF c = g_pal.text;
    std::string rtf = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 " + RtfEscape(g_set.fontFace) + ";}}";
    rtf += "{\\colortbl;\\red" + std::to_string(GetRValue(c)) + "\\green" + std::to_string(GetGValue(c)) + "\\blue" +
           std::to_string(GetBValue(c)) + ";}";
    rtf += "\\f0\\cf1\\fs" + std::to_string(g_set.fontSize10 / 5) + " ";
    rtf += "{\\field{\\*\\fldinst{HYPERLINK \"" + RtfEscape(url) + "\"}}{\\fldrslt{" + RtfEscape(text) + "}}}}";
    return rtf;
}

COLORREF LinkColor() { return g_dark ? RGB(96, 205, 255) : RGB(0, 95, 184); }

// Gives visible link text the accent color (RichEdit would otherwise use the text color).
void ColorLinks(Tab& t) {
    for (const Run& run : CollectRuns(t, StoryLength(t))) {
        if (run.hidden || !run.link) continue;
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(run.start, run.end, &r)) && r) {
            ITextFont* f = nullptr;
            if (SUCCEEDED(r->GetFont(&f)) && f) {
                f->SetForeColor(static_cast<long>(LinkColor()));
                f->Release();
            }
            r->Release();
        }
    }
}

// Replaces [start, end) with a friendly-name link.
void InsertLinkAt(Tab& t, long start, long end, const std::wstring& text, const std::wstring& url) {
    CHARRANGE cr{start, end};
    SendMessageW(t.edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    StreamRtf(t.edit, LinkRtf(text, url), true);
    ColorLinks(t);
}

int EditorWidthTwips(Tab& t) {
    RECT rc;
    SendMessageW(t.edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rc));
    int px = std::max<int>(S(200), rc.right - rc.left - S(24));
    return MulDiv(px, 1440, static_cast<int>(g_dpi));
}

// Inserts a rows x cols table at cp (start of an empty paragraph) and fills its cells.
void InsertTableAt(Tab& t, long cp, int rows, int cols, const std::vector<std::vector<std::wstring>>& cells) {
    CHARRANGE cr{cp, cp};
    SendMessageW(t.edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    TABLEROWPARMS rp{};
    rp.cbRow = sizeof rp;
    rp.cbCell = sizeof(TABLECELLPARMS);
    rp.cCell = static_cast<BYTE>(cols);
    rp.cRow = static_cast<BYTE>(rows);
    rp.dxCellMargin = 108;
    rp.cpStartRow = -1;
    rp.fIdentCells = 1;
    TABLECELLPARMS cp1{};
    cp1.dxWidth = std::min(EditorWidthTwips(t) / std::max(1, cols), 3600);
    cp1.dxBrdrLeft = cp1.dyBrdrTop = cp1.dxBrdrRight = cp1.dyBrdrBottom = 15;
    cp1.crBrdrLeft = cp1.crBrdrTop = cp1.crBrdrRight = cp1.crBrdrBottom = g_pal.divider;
    cp1.crBackPat = cp1.crForePat = static_cast<COLORREF>(tomAutoColor);
    SendMessageW(t.edit, EM_INSERTTABLE, reinterpret_cast<WPARAM>(&rp), reinterpret_cast<LPARAM>(&cp1));

    // Locate the cell marks and fill from the end so earlier positions stay valid.
    std::wstring raw = RawText(t, cp, StoryLength(t));
    std::vector<long> marks;
    for (size_t k = 0; k < raw.size() && static_cast<int>(marks.size()) < rows * cols; ++k)
        if (raw[k] == kCellEnd) marks.push_back(cp + static_cast<long>(k));
    for (int i = static_cast<int>(marks.size()) - 1; i >= 0; --i) {
        int row = i / cols, col = i % cols;
        if (row >= static_cast<int>(cells.size()) || col >= static_cast<int>(cells[row].size())) continue;
        if (cells[row][col].empty()) continue;
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(marks[i], marks[i], &r)) && r) {
            BSTR b = SysAllocString(cells[row][col].c_str());
            r->SetText(b);
            SysFreeString(b);
            r->Release();
        }
    }
}

// Loads the Markdown model into an empty rich editor.
void LoadDocIntoEditor(Tab& t, const std::vector<md::Para>& doc) {
    struct Pending {
        long cp;
        bool table;
        size_t first, count;  // table rows in doc
        long end;             // link end
        std::wstring url;
    };
    std::wstring text;
    std::vector<long> offsets(doc.size(), -1);
    std::vector<Pending> pending;
    for (size_t i = 0; i < doc.size(); ++i) {
        if (!text.empty() || i) text += L'\r';
        long at = static_cast<long>(text.size());
        if (doc[i].kind == md::Block::TableRow) {
            size_t j = i;
            while (j < doc.size() && doc[j].kind == md::Block::TableRow) ++j;
            pending.push_back({at, true, i, j - i, 0, L""});
            i = j - 1;
            continue;
        }
        offsets[i] = at;
        text += doc[i].text;
    }
    StreamText(t.edit, text, false);

    for (size_t i = 0; i < doc.size(); ++i) {
        if (offsets[i] < 0) continue;
        const md::Para& p = doc[i];
        long ps = offsets[i];
        ITextRange* r = nullptr;
        if (FAILED(t.doc->Range(ps, ps, &r)) || !r) continue;
        r->Expand(tomParagraph, nullptr);
        if (p.kind == md::Block::Heading) {
            SetParaHeading(r, p.level);
        } else if (p.kind == md::Block::Bullet || p.kind == md::Block::Numbered) {
            ITextPara* para = nullptr;
            if (SUCCEEDED(r->GetPara(&para)) && para) {
                SetParaList(para, p.kind, p.marker, p.level);
                para->Release();
            }
        }
        r->Release();
        for (const md::Span& sp : p.spans) {
            long s = ps + static_cast<long>(sp.start), e = ps + static_cast<long>(sp.end);
            ITextRange* sr = nullptr;
            if (FAILED(t.doc->Range(s, e, &sr)) || !sr) continue;
            ITextFont* f = nullptr;
            if (SUCCEEDED(sr->GetFont(&f)) && f) {
                if (sp.bold) f->SetBold(tomTrue);
                if (sp.italic) f->SetItalic(tomTrue);
                if (sp.strike) f->SetStrikeThrough(tomTrue);
                f->Release();
            }
            sr->Release();
            if (!sp.url.empty()) pending.push_back({s, false, 0, 0, e, sp.url});
        }
    }

    // Links and tables change the text length, so apply them from the end backwards.
    std::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) { return a.cp > b.cp; });
    for (const Pending& pd : pending) {
        if (pd.table) {
            int cols = 0;
            std::vector<std::vector<std::wstring>> cells;
            for (size_t k = 0; k < pd.count; ++k) {
                cells.push_back(doc[pd.first + k].cells);
                cols = std::max(cols, static_cast<int>(cells.back().size()));
            }
            InsertTableAt(t, pd.cp, static_cast<int>(pd.count), std::max(1, cols), cells);
            // Drop the placeholder paragraph left after the table unless it ends the story.
            std::wstring after = RawText(t, pd.cp, StoryLength(t));
            size_t lastRow = std::wstring::npos;
            for (size_t k = 0, from = 0; k < pd.count; ++k) {
                lastRow = after.find(kRowEnd, from);
                if (lastRow == std::wstring::npos) break;
                from = lastRow + 1;
            }
            if (lastRow != std::wstring::npos) {
                long ph = pd.cp + static_cast<long>(lastRow) + 1;
                if (ph < StoryLength(t) && RawText(t, ph, ph + 1) == L"\r") ++ph;
                if (ph < StoryLength(t) - 1 && RawText(t, ph, ph + 1) == L"\r") {
                    ITextRange* r = nullptr;
                    if (SUCCEEDED(t.doc->Range(ph, ph + 1, &r)) && r) {
                        r->Delete(tomCharacter, 0, nullptr);
                        r->Release();
                    }
                }
            }
        } else {
            InsertLinkAt(t, pd.cp, pd.end, RawText(t, pd.cp, pd.end), pd.url);
        }
    }
}

// Clears the editor and switches it between plain and rich text mode.
void ResetEditorMode(Tab& t, bool rich) {
    SetWindowTextW(t.edit, L"");
    SendMessageW(t.edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(t.edit, EM_SETTEXTMODE, (rich ? TM_RICHTEXT : TM_PLAINTEXT) | TM_MULTILEVELUNDO | TM_MULTICODEPAGE,
                 0);
    t.rich = rich;
    ApplyEditorLook(t);
}

// Shows Markdown text formatted when that is lossless; otherwise keeps it as syntax.
bool LoadMarkdown(Tab& t, const std::wstring& text) {
    ResetEditorMode(t, true);
    LoadDocIntoEditor(t, md::Parse(text));
    if (md::Serialize(DocFromEditor(t)) == md::NormalizeBreaks(text)) return true;
    ResetEditorMode(t, false);
    StreamText(t.edit, text, false);
    return false;
}

void FinishLoad(Tab& t) {
    SendMessageW(t.edit, EM_SETSEL, 0, 0);
    SendMessageW(t.edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(t.edit, EM_SETMODIFY, FALSE, 0);
    t.shownModified = false;
}

// Puts saved content (paragraphs separated by '\r') into a tab according to its kind.
void FillTab(Tab& t, const std::wstring& text) {
    bool formatting = g_set.formatting && t.kind != Kind::Code;
    if (t.kind == Kind::Markdown && formatting && !t.syntax) {
        t.syntax = !LoadMarkdown(t, text);
    } else {
        ResetEditorMode(t, formatting && t.kind == Kind::Text);
        StreamText(t.edit, text, false);
    }
}

// ---------------------------------------------------------------------------
// Tabs

LRESULT CALLBACK EditSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void HideFind();
void ClearSession();
void SetZoom(Tab& t, int z);
void InsertTableHere(Tab& t, int rows, int cols);
void Notice(const std::wstring& title, const std::wstring& body);
bool Confirm(const std::wstring& title, const std::wstring& body, const wchar_t* yes, const wchar_t* no);

void ActivateTab(int i) {
    int n = static_cast<int>(g_tabs.size());
    if (i < 0 || i >= n) return;
    if (Tab* old = Cur(); old && g_active != i) ShowWindow(old->edit, SW_HIDE);
    g_active = i;
    LayoutChildren();
    ShowWindow(Cur()->edit, SW_SHOW);
    if (g_find) SetWindowPos(g_find, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetFocus(Cur()->edit);
    UpdateTitle();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

Tab* NewTab(Kind kind = Kind::Text) {
    auto t = std::make_unique<Tab>();
    t->kind = kind;
    t->rich = g_set.formatting && kind != Kind::Code;
    t->edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                              WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                  ES_AUTOHSCROLL | ES_NOHIDESEL,
                              0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDITOR)), g_inst,
                              nullptr);
    if (!t->edit) return nullptr;
    HWND e = t->edit;
    SendMessageW(e, EM_SETTEXTMODE,
                 (t->rich ? TM_RICHTEXT : TM_PLAINTEXT) | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(e, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(e, EM_SETUNDOLIMIT, 500, 0);
    SendMessageW(e, EM_AUTOURLDETECT, FALSE, 0);
    SendMessageW(e, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_DROPFILES | ENM_LINK);
    DragAcceptFiles(e, TRUE);

    IRichEditOle* ole = nullptr;
    SendMessageW(e, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole));
    if (ole) {
        ole->QueryInterface(__uuidof(ITextDocument), reinterpret_cast<void**>(&t->doc));
        ole->Release();
    }
    SetWindowSubclass(e, EditSubclass, 1, 0);
    ApplyEditorLook(*t);

    Tab* raw = t.get();
    g_tabs.push_back(std::move(t));
    ActivateTab(static_cast<int>(g_tabs.size()) - 1);
    return raw;
}

void DestroyTab(int i) {
    Tab& t = *g_tabs[i];
    if (t.doc) t.doc->Release();
    DestroyWindow(t.edit);
    g_tabs.erase(g_tabs.begin() + i);
}

bool IsPristine(const Tab& t) { return t.path.empty() && !IsModified(t) && GetWindowTextLengthW(t.edit) == 0; }

int FindTabByPath(const std::wstring& p) {
    for (size_t i = 0; i < g_tabs.size(); ++i)
        if (!g_tabs[i]->path.empty() && SamePath(g_tabs[i]->path, p)) return static_cast<int>(i);
    return -1;
}

void MarkModifiedChanged(Tab& t) {
    bool m = IsModified(t);
    if (m != t.shownModified) {
        t.shownModified = m;
        InvalidateStrip();
    }
}

bool LoadFile(Tab& t, const std::wstring& path) {
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        ShowError(L"Couldn't open " + path + L".", GetLastError());
        return false;
    }
    std::wstring text;
    t.enc = Decode(bytes, text);
    t.eol = DetectEol(text);
    std::replace(text.begin(), text.end(), L'\0', L' ');
    t.path = path;
    t.kind = KindForPath(path);
    t.syntax = false;
    FillTab(t, text);
    FinishLoad(t);
    AddRecent(path);
    return true;
}

void OpenPath(const std::wstring& path) {
    std::wstring full = FullPath(path);
    int existing = FindTabByPath(full);
    if (existing >= 0) {
        ActivateTab(existing);
        return;
    }
    Tab* t = Cur();
    bool created = false;
    if (!t || !IsPristine(*t)) {
        t = NewTab();
        if (!t) return;
        created = true;
    }
    if (!LoadFile(*t, full)) {
        if (created && g_tabs.size() > 1) {
            int idx = g_active;
            DestroyTab(idx);
            g_active = -1;
            ActivateTab(std::min(idx, static_cast<int>(g_tabs.size()) - 1));
        }
        return;
    }
    UpdateTitle();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void LaunchNewWindow(const std::wstring& file) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    if (!file.empty()) cmd += L" \"" + file + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void OpenOrRoute(const std::wstring& path) {
    Tab* t = Cur();
    if (g_set.openInNewWindow && !(t && IsPristine(*t)))
        LaunchNewWindow(path);
    else
        OpenPath(path);
}

void OpenDroppedFiles(HDROP drop) {
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring p(len + 1, L'\0');
        DragQueryFileW(drop, i, p.data(), len + 1);
        p.resize(len);
        if (!PathIsDirectoryW(p.c_str())) OpenPath(p);
    }
}

void CmdOpen() {
    std::vector<wchar_t> buf(32768, L'\0');
    OPENFILENAMEW of{sizeof of};
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"Text documents (*.txt)\0*.txt\0Markdown (*.md)\0*.md;*.markdown\0All files (*.*)\0*.*\0";
    of.lpstrFile = buf.data();
    of.nMaxFile = static_cast<DWORD>(buf.size());
    of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    std::vector<std::wstring> files;
    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (!*p) {
        files.push_back(first);
    } else {
        while (*p) {
            files.push_back(first + L"\\" + p);
            p += wcslen(p) + 1;
        }
    }
    for (auto& f : files) OpenOrRoute(f);
}

bool AskSavePath(const Tab& t, std::wstring& path, bool markdown) {
    std::vector<wchar_t> buf(32768, L'\0');
    if (t.path.empty()) {
        wcscpy_s(buf.data(), buf.size(), markdown ? L"*.md" : L"*.txt");
    } else {
        wcsncpy_s(buf.data(), buf.size(), t.path.c_str(), _TRUNCATE);
        if (markdown && KindForPath(t.path) != Kind::Markdown) {
            wchar_t* ext = PathFindExtensionW(buf.data());
            wcscpy_s(ext, buf.size() - static_cast<size_t>(ext - buf.data()), L".md");
        }
    }
    OPENFILENAMEW of{sizeof of};
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"Text documents (*.txt)\0*.txt\0Markdown (*.md)\0*.md\0All files (*.*)\0*.*\0";
    of.nFilterIndex = markdown ? 2 : 1;
    of.lpstrFile = buf.data();
    of.nMaxFile = static_cast<DWORD>(buf.size());
    of.lpstrDefExt = markdown ? L"md" : L"txt";
    of.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&of)) return false;
    path = buf.data();
    return true;
}

// Asked when formatted text is saved to a file type that can't hold formatting.
int AskDropFormatting() {
    dlg::Spec s;
    s.title = L"Keep formatting?";
    s.body = L"This file type can't keep formatting. Save as Markdown (.md) to keep headings, lists, bold, links and "
             L"tables, or save as plain text without them.";
    s.primary = L"Save as Markdown";
    s.secondary = L"Remove formatting";
    s.close = L"Cancel";
    s.width = 440;
    int r = dlg::Show(g_hwnd, s);
    return r == dlg::Primary ? 1 : r == dlg::Secondary ? 2 : 0;
}

bool SaveTab(Tab& t, bool saveAs) {
    std::wstring path = t.path;
    bool formatted = t.rich && md::HasFormatting(DocFromEditor(t));
    if (saveAs || path.empty()) {
        if (!AskSavePath(t, path, formatted || t.kind == Kind::Markdown)) return false;
    }
    Kind target = KindForPath(path);
    if (target != Kind::Markdown && formatted) {
        int choice = AskDropFormatting();
        if (choice == 0) return false;
        if (choice == 1) {
            if (!AskSavePath(t, path, true)) return false;
            target = KindForPath(path);
        }
    }
    std::wstring text;
    if (target == Kind::Markdown)
        text = t.rich ? md::Serialize(DocFromEditor(t)) : GetEditText(t.edit);
    else
        text = PlainOf(t);
    const wchar_t* eol = t.eol == Eol::CRLF ? L"\r\n" : t.eol == Eol::LF ? L"\n" : L"\r";
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (wchar_t c : text) {
        if (c == L'\r')
            out += eol;
        else
            out.push_back(c);
    }
    if (!WriteAllBytes(path, Encode(out, t.enc))) {
        ShowError(L"Couldn't save " + path + L".", GetLastError());
        return false;
    }
    t.path = path;
    // A new file type may change how the tab can show its content (e.g. .txt -> .bat drops formatting).
    bool wasRich = t.rich;
    t.kind = target;
    bool wantRich = g_set.formatting && target != Kind::Code && !(target == Kind::Markdown && t.syntax);
    if (wantRich != wasRich) {
        t.syntax = false;
        FillTab(t, text);
        FinishLoad(t);
    }
    SendMessageW(t.edit, EM_SETMODIFY, FALSE, 0);
    MarkModifiedChanged(t);
    AddRecent(path);
    UpdateTitle();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    return true;
}

void SaveAll() {
    for (size_t i = 0; i < g_tabs.size(); ++i) {
        Tab& t = *g_tabs[i];
        if (!IsModified(t)) continue;
        if (t.path.empty()) ActivateTab(static_cast<int>(i));
        if (!SaveTab(t, false)) return;
    }
}

int AskSave(const std::wstring& name) {
    dlg::Spec s;
    s.title = kAppName;
    s.body = L"Do you want to save changes to " + name + L"?";
    s.primary = L"Save";
    s.secondary = L"Don't save";
    s.close = L"Cancel";
    s.width = 440;
    int r = dlg::Show(g_hwnd, s);
    return r == dlg::Primary ? IDYES : r == dlg::Secondary ? IDNO : IDCANCEL;
}

// Plain notice in a ContentDialog (OK only).
void Notice(const std::wstring& title, const std::wstring& body) {
    dlg::Spec s;
    s.title = title;
    s.body = body;
    s.close = L"OK";
    s.width = 420;
    dlg::Show(g_hwnd, s);
}

bool Confirm(const std::wstring& title, const std::wstring& body, const wchar_t* yes, const wchar_t* no) {
    dlg::Spec s;
    s.title = title;
    s.body = body;
    s.primary = yes;
    s.close = no;
    s.width = 420;
    return dlg::Show(g_hwnd, s) == dlg::Primary;
}

bool ConfirmDiscard(int i) {
    Tab& t = *g_tabs[i];
    if (!IsModified(t)) return true;
    ActivateTab(i);
    int r = AskSave(TabName(t));
    if (r == IDYES) return SaveTab(t, false);
    return r == IDNO;
}

void SaveWindowState() {
    WINDOWPLACEMENT wp{sizeof wp};
    if (GetWindowPlacement(g_hwnd, &wp))
        RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"Placement", REG_BINARY, &wp, sizeof wp);
}

void CloseTab(int i) {
    if (!ConfirmDiscard(i)) return;
    bool wasActive = i == g_active;
    DestroyTab(i);
    if (g_tabs.empty()) {
        g_active = -1;
        ClearSession();
        SaveWindowState();
        DestroyWindow(g_hwnd);
        return;
    }
    if (wasActive) {
        g_active = -1;
        ActivateTab(std::min(i, static_cast<int>(g_tabs.size()) - 1));
    } else {
        if (i < g_active) --g_active;
        LayoutTabs();
        InvalidateStrip();
    }
}

// ---------------------------------------------------------------------------
// Find and replace

std::wstring FindText() { return WindowText(g_findEdit); }

void LayoutFindPanel();

void SetReplaceMode(bool on) {
    g_replaceMode = on;
    SetDlgItemTextW(g_find, IDC_FIND_TOGGLE, on ? kGlyphChevronUp : kGlyphChevronDown);
    for (int id : {IDC_REPL_EDIT, IDC_REPL_ONE, IDC_REPL_ALL}) ShowWindow(GetDlgItem(g_find, id), on ? SW_SHOW : SW_HIDE);
    LayoutChildren();
    LayoutFindPanel();
    InvalidateRect(g_find, nullptr, TRUE);
}

void ShowFind(bool replace) {
    Tab* t = Cur();
    if (!t) return;
    CHARRANGE cr{};
    SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    if (cr.cpMax > cr.cpMin && cr.cpMax - cr.cpMin < 256) {
        std::wstring s(static_cast<size_t>(cr.cpMax - cr.cpMin) + 1, L'\0');
        LRESULT n = SendMessageW(t->edit, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(s.data()));
        s.resize(static_cast<size_t>(n));
        if (s.find(L'\r') == std::wstring::npos) SetWindowTextW(g_findEdit, s.c_str());
    }
    ShowWindow(g_find, SW_SHOW);
    SetReplaceMode(replace);
    SetFocus(g_findEdit);
    SendMessageW(g_findEdit, EM_SETSEL, 0, -1);
}

void HideFind() {
    ShowWindow(g_find, SW_HIDE);
    if (Tab* t = Cur()) SetFocus(t->edit);
}

bool FindNext(bool forward) {
    Tab* t = Cur();
    if (!t) return false;
    std::wstring what = FindText();
    if (what.empty()) {
        ShowFind(false);
        return false;
    }
    CHARRANGE sel{};
    SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    FINDTEXTEXW ft{};
    ft.lpstrText = what.c_str();
    WPARAM flags = (forward ? FR_DOWN : 0) | (g_set.matchCase ? FR_MATCHCASE : 0);
    ft.chrg = forward ? CHARRANGE{sel.cpMax, -1} : CHARRANGE{sel.cpMin, 0};
    LRESULT r = SendMessageW(t->edit, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
    if (r < 0 && g_set.wrapAround) {
        ft.chrg = forward ? CHARRANGE{0, -1} : CHARRANGE{TextLength(t->edit), 0};
        r = SendMessageW(t->edit, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
    }
    if (r < 0) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    SendMessageW(t->edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
    SendMessageW(t->edit, EM_SCROLLCARET, 0, 0);
    return true;
}

void ReplaceOne() {
    Tab* t = Cur();
    if (!t) return;
    std::wstring what = FindText(), with = WindowText(g_replEdit);
    if (what.empty()) return;
    CHARRANGE cr{};
    SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    std::wstring sel(static_cast<size_t>(cr.cpMax - cr.cpMin) + 1, L'\0');
    sel.resize(static_cast<size_t>(SendMessageW(t->edit, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(sel.data()))));
    bool match = g_set.matchCase ? sel == what
                                 : CompareStringOrdinal(sel.c_str(), static_cast<int>(sel.size()), what.c_str(),
                                                        static_cast<int>(what.size()), TRUE) == CSTR_EQUAL;
    if (match) SendMessageW(t->edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(with.c_str()));
    FindNext(true);
}

void ReplaceAll() {
    Tab* t = Cur();
    if (!t) return;
    std::wstring what = FindText(), with = WindowText(g_replEdit);
    if (what.empty()) return;
    if (t->rich) {
        // Replace in place so surrounding formatting survives.
        size_t count = 0;
        FINDTEXTEXW ft{};
        ft.lpstrText = what.c_str();
        ft.chrg = {0, -1};
        WPARAM flags = FR_DOWN | (g_set.matchCase ? FR_MATCHCASE : 0);
        while (SendMessageW(t->edit, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft)) >= 0) {
            SendMessageW(t->edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
            SendMessageW(t->edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(with.c_str()));
            ft.chrg = {ft.chrgText.cpMin + static_cast<LONG>(with.size()), -1};
            ++count;
        }
        if (!count) MessageBeep(MB_ICONWARNING);
        return;
    }
    std::wstring text = GetEditText(t->edit);
    std::wstring hay = text, needle = what;
    if (!g_set.matchCase) {
        CharLowerBuffW(hay.data(), static_cast<DWORD>(hay.size()));
        CharLowerBuffW(needle.data(), static_cast<DWORD>(needle.size()));
    }
    std::wstring out;
    size_t pos = 0, count = 0;
    for (;;) {
        size_t f = hay.find(needle, pos);
        if (f == std::wstring::npos) break;
        out.append(text, pos, f - pos);
        out += with;
        pos = f + needle.size();
        ++count;
    }
    if (!count) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    out.append(text, pos, std::wstring::npos);
    SendMessageW(t->edit, EM_SETSEL, 0, -1);
    StreamText(t->edit, out, true);
    SendMessageW(t->edit, EM_SETSEL, 0, 0);
    SendMessageW(t->edit, EM_SCROLLCARET, 0, 0);
}

// Find panel geometry measured from Notepad: 620x88 (48 collapsed), centered over the
// editor; find box 48..452, four 32px buttons 40px apart from 460, Replace 88px and
// Replace all 90px on the second row.
RECT FindBoxRect(int row) {
    RECT rc;
    GetClientRect(g_find, &rc);
    int top = S(8) + row * S(40);
    return {S(48), top, row ? rc.right - S(209) : rc.right - S(168), top + S(32)};
}

void LayoutFindPanel() {
    RECT rc;
    GetClientRect(g_find, &rc);
    int bx = rc.right - S(160);
    int eh = g_uiFontH + S(4);
    auto move = [](int id, int x, int y, int w, int h) { MoveWindow(GetDlgItem(g_find, id), x, y, w, h, TRUE); };
    move(IDC_FIND_TOGGLE, S(12), S(8), S(32), S(32));
    for (int row = 0; row < 2; ++row) {
        RECT box = FindBoxRect(row);
        int iconW = row ? 0 : S(28);  // search glyph inside the find box
        MoveWindow(row ? g_replEdit : g_findEdit, box.left + S(10), box.top + (S(32) - eh) / 2,
                   box.right - box.left - S(20) - iconW, eh, TRUE);
    }
    move(IDC_FIND_NEXT, bx, S(8), S(32), S(32));
    move(IDC_FIND_PREV, bx + S(40), S(8), S(32), S(32));
    move(IDC_FIND_OPTIONS, bx + S(80), S(8), S(32), S(32));
    move(IDC_FIND_CLOSE, bx + S(120), S(8), S(32), S(32));
    move(IDC_REPL_ONE, rc.right - S(198), S(48), S(88), S(32));
    move(IDC_REPL_ALL, rc.right - S(100), S(48), S(90), S(32));
}

void FindOptionsMenu() {
    RECT r;
    GetWindowRect(GetDlgItem(g_find, IDC_FIND_OPTIONS), &r);
    std::vector<flyout::Item> v(2);
    v[0].id = IDM_MATCHCASE;
    v[0].text = L"Match case";
    v[0].checkable = true;
    v[0].checked = g_set.matchCase;
    v[1].id = IDM_WRAPAROUND;
    v[1].text = L"Wrap around";
    v[1].checkable = true;
    v[1].checked = g_set.wrapAround;
    flyout::Options o;
    o.above = true;
    o.minWidth = S(196);
    int cx = (r.left + r.right) / 2;
    UINT cmd = flyout::Show(g_hwnd, v, {cx, r.top, cx, r.top}, o).id;
    if (cmd == IDM_MATCHCASE) g_set.matchCase = !g_set.matchCase;
    if (cmd == IDM_WRAPAROUND) g_set.wrapAround = !g_set.wrapAround;
    if (cmd) SaveSettings();
}

void DrawFindButton(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    bool textButton = d->CtlID == IDC_REPL_ONE || d->CtlID == IDC_REPL_ALL;
    FillSolid(dc, rc, g_pal.panel);
    if (textButton)
        FrameRound(dc, rc, g_pal.inputBorder, pressed ? g_pal.pressed : g_pal.input, S(8));
    else if (pressed)
        FillRound(dc, rc, g_pal.pressed, S(8));
    wchar_t txt[64]{};
    GetWindowTextW(d->hwndItem, txt, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_pal.text);
    HGDIOBJ old = SelectObject(dc, textButton ? g_uiFont : g_iconFont);
    DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT f = rc;
        InflateRect(&f, -S(2), -S(2));
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, g_pal.text);
        RoundRect(dc, f.left, f.top, f.right, f.bottom, S(6), S(6));
        SelectObject(dc, ob);
        SelectObject(dc, op);
    }
}

LRESULT CALLBACK FindEditSubclass(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (m == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            if (h == g_replEdit)
                ReplaceOne();
            else
                FindNext(GetKeyState(VK_SHIFT) >= 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            HideFind();
            return 0;
        }
        if (wp == VK_TAB) {
            if (g_replaceMode)
                SetFocus(h == g_findEdit ? g_replEdit : g_findEdit);
            else if (Tab* t = Cur())
                SetFocus(t->edit);
            return 0;
        }
    }
    if (m == WM_CHAR && (wp == L'\r' || wp == 27 || wp == L'\t')) return 0;
    return DefSubclassProc(h, m, wp, lp);
}

LRESULT CALLBACK FindProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_find = h;
        auto button = [h](int id, const wchar_t* text, bool visible) {
            CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | (visible ? WS_VISIBLE : 0), 0, 0,
                            0, 0, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
        };
        auto edit = [h](int id, const wchar_t* cue, bool visible) {
            HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | (visible ? WS_VISIBLE : 0),
                                     0, 0, 0, 0, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst, nullptr);
            SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), FALSE);
            SendMessageW(e, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue));
            SetWindowSubclass(e, FindEditSubclass, 1, 0);
            return e;
        };
        button(IDC_FIND_TOGGLE, kGlyphChevronDown, true);
        g_findEdit = edit(IDC_FIND_EDIT, L"Find", true);
        button(IDC_FIND_NEXT, kGlyphArrowDown, true);
        button(IDC_FIND_PREV, kGlyphArrowUp, true);
        button(IDC_FIND_OPTIONS, glyph::Options, true);
        button(IDC_FIND_CLOSE, kGlyphCancel, true);
        g_replEdit = edit(IDC_REPL_EDIT, L"Replace", false);
        button(IDC_REPL_ONE, L"Replace", false);
        button(IDC_REPL_ALL, L"Replace all", false);
        return 0;
    }
    case WM_SIZE:
        LayoutFindPanel();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        FillSolid(dc, rc, g_pal.editor);
        FrameRound(dc, rc, g_pal.inputBorder, g_pal.panel, S(16));
        for (int row = 0; row < (g_replaceMode ? 2 : 1); ++row) {
            RECT box = FindBoxRect(row);
            FrameRound(dc, box, g_pal.inputBorder, g_pal.input, S(8));
            if (GetFocus() == (row ? g_replEdit : g_findEdit))
                FillSolid(dc, {box.left + S(1), box.bottom - S(2), box.right - S(1), box.bottom}, g_pal.accent);
            if (!row)
                DrawTextIn(dc, glyph::Search, {box.right - S(30), box.top, box.right - S(8), box.bottom},
                           g_pal.textMuted, g_iconFontSmall, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, g_pal.text);
        SetBkColor(dc, g_pal.input);
        return reinterpret_cast<LRESULT>(g_inputBrush);
    }
    case WM_DRAWITEM:
        DrawFindButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) {
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        switch (LOWORD(wp)) {
        case IDC_FIND_NEXT:
            FindNext(true);
            SetFocus(g_findEdit);
            break;
        case IDC_FIND_PREV:
            FindNext(false);
            SetFocus(g_findEdit);
            break;
        case IDC_FIND_TOGGLE:
            SetReplaceMode(!g_replaceMode);
            SetFocus(g_replaceMode ? g_replEdit : g_findEdit);
            break;
        case IDC_FIND_OPTIONS:
            FindOptionsMenu();
            break;
        case IDC_FIND_CLOSE:
            HideFind();
            break;
        case IDC_REPL_ONE:
            ReplaceOne();
            break;
        case IDC_REPL_ALL:
            ReplaceAll();
            break;
        }
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------------------------------------------------------------------------
// Dialogs: go to, font, settings, print

void CmdGoto() {
    Tab* t = Cur();
    if (!t) return;
    long ln = 1, col = 1;
    CaretLineCol(*t, ln, col);
    dlg::Spec s;
    s.title = L"Go to line";
    s.fields = {{L"Line number", std::to_wstring(ln), L"", true}};
    s.primary = L"Go to";
    s.close = L"Cancel";
    if (dlg::Show(g_hwnd, s) != dlg::Primary) return;
    long line = _wtol(s.fields[0].value.c_str());
    if (line <= 0) return;
    std::wstring text = GetEditText(t->edit);
    long cur = 1;
    size_t pos = 0;
    while (cur < line) {
        size_t f = text.find(L'\r', pos);
        if (f == std::wstring::npos) break;
        pos = f + 1;
        ++cur;
    }
    if (cur < line) {
        Notice(L"Go to line", L"The line number is beyond the total number of lines.");
        return;
    }
    CHARRANGE cr{static_cast<LONG>(pos), static_cast<LONG>(pos)};
    SendMessageW(t->edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    SendMessageW(t->edit, EM_SCROLLCARET, 0, 0);
    SetFocus(t->edit);
}

void ApplySettings() {
    SaveSettings();
    ApplyTheme();
    LayoutMenuRects();
    LayoutChildren();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void OpenSettingsPage();
void CmdSettings() { OpenSettingsPage(); }

void CmdPageSetup() {
    PAGESETUPDLGW ps{sizeof ps};
    ps.hwndOwner = g_hwnd;
    ps.hDevMode = g_devMode;
    ps.hDevNames = g_devNames;
    ps.Flags = PSD_INTHOUSANDTHSOFINCHES | PSD_MARGINS;
    ps.rtMargin = g_margins;
    if (PageSetupDlgW(&ps)) {
        g_margins = ps.rtMargin;
        g_devMode = ps.hDevMode;
        g_devNames = ps.hDevNames;
    }
}

void CmdPrint() {
    Tab* t = Cur();
    if (!t) return;
    CHARRANGE sel{};
    SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    PRINTDLGEXW pd{sizeof pd};
    pd.hwndOwner = g_hwnd;
    pd.hDevMode = g_devMode;
    pd.hDevNames = g_devNames;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOPAGENUMS | PD_NOCURRENTPAGE |
               (sel.cpMax > sel.cpMin ? 0 : PD_NOSELECTION);
    pd.nStartPage = START_PAGE_GENERAL;
    if (FAILED(PrintDlgExW(&pd))) return;
    g_devMode = pd.hDevMode;
    g_devNames = pd.hDevNames;
    if (pd.dwResultAction != PD_RESULT_PRINT) {
        if (pd.hDC) DeleteDC(pd.hDC);
        return;
    }
    HDC dc = pd.hDC;
    // Print from a hidden copy (as RTF, so formatting survives) with black text, so
    // dark-theme colors and zoom never reach the paper.
    std::string rtf = StreamOutRtf(t->edit, (pd.Flags & PD_SELECTION) != 0);
    HWND re = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE, 0, 0, 0, 0, g_hwnd, nullptr, g_inst, nullptr);
    SendMessageW(re, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    StreamRtf(re, rtf, false);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = RGB(0, 0, 0);
    SendMessageW(re, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));

    int dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    auto twX = [dpiX](int px) { return MulDiv(px, 1440, dpiX); };
    auto twY = [dpiY](int px) { return MulDiv(px, 1440, dpiY); };
    int pageW = twX(GetDeviceCaps(dc, PHYSICALWIDTH)), pageH = twY(GetDeviceCaps(dc, PHYSICALHEIGHT));
    int offX = twX(GetDeviceCaps(dc, PHYSICALOFFSETX)), offY = twY(GetDeviceCaps(dc, PHYSICALOFFSETY));
    RECT body{MulDiv(g_margins.left, 1440, 1000) - offX, MulDiv(g_margins.top, 1440, 1000) - offY,
              pageW - MulDiv(g_margins.right, 1440, 1000) - offX, pageH - MulDiv(g_margins.bottom, 1440, 1000) - offY};

    std::wstring docName = TabName(*t);
    DOCINFOW di{sizeof di};
    di.lpszDocName = docName.c_str();
    if (StartDocW(dc, &di) > 0) {
        LONG len = TextLength(re);
        LONG cp = 0;
        FORMATRANGE fr{};
        fr.hdc = fr.hdcTarget = dc;
        fr.rcPage = {0, 0, twX(GetDeviceCaps(dc, HORZRES)), twY(GetDeviceCaps(dc, VERTRES))};
        do {
            StartPage(dc);
            fr.rc = body;
            fr.chrg = {cp, -1};
            LONG next = static_cast<LONG>(SendMessageW(re, EM_FORMATRANGE, TRUE, reinterpret_cast<LPARAM>(&fr)));
            EndPage(dc);
            if (next <= cp) break;
            cp = next;
        } while (cp < len);
        SendMessageW(re, EM_FORMATRANGE, FALSE, 0);
        EndDoc(dc);
    }
    DestroyWindow(re);
    DeleteDC(dc);
}

// ---------------------------------------------------------------------------
// Menus

// Menu contents mirror Notepad 11.2607 as captured through UI Automation: same items,
// order, separators and shortcut labels.

using flyout::Item;

Item Cmd(UINT id, const wchar_t* text, const wchar_t* shortcut = L"", bool enabled = true) {
    Item it;
    it.id = id;
    it.text = text;
    it.shortcut = shortcut;
    it.enabled = enabled;
    return it;
}

Item Check(UINT id, const wchar_t* text, bool checked, bool enabled = true) {
    Item it = Cmd(id, text, L"", enabled);
    it.checkable = true;
    it.checked = checked;
    return it;
}

Item Sub(const wchar_t* text, std::vector<Item> sub, bool enabled = true, const wchar_t* icon = L"") {
    Item it;
    it.text = text;
    it.sub = std::move(sub);
    it.enabled = enabled;
    it.icon = icon;
    return it;
}

Item Sep() {
    Item it;
    it.kind = flyout::Kind::Separator;
    return it;
}

Item Icon(UINT id, const wchar_t* icon, const wchar_t* text, bool enabled = true) {
    Item it = Cmd(id, text, L"", enabled);
    it.icon = icon;
    return it;
}

bool SelectionInTable(Tab& t) {
    CHARRANGE c{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&c));
    std::wstring before = RawText(t, std::max<LONG>(0, c.cpMin - 4096), c.cpMin);
    size_t rs = before.rfind(kRowStart), re = before.rfind(kRowEnd);
    return rs != std::wstring::npos && (re == std::wstring::npos || rs > re);
}

std::vector<Item> ToneItems() {
    return {Cmd(IDM_AI_BASE + 5, L"Formal"), Cmd(IDM_AI_BASE + 6, L"Casual"), Cmd(IDM_AI_BASE + 7, L"Inspirational"),
            Cmd(IDM_AI_BASE + 8, L"Humor")};
}

std::vector<Item> FormatItems() {
    return {Cmd(IDM_AI_BASE + 9, L"Paragraph"),  Cmd(IDM_AI_BASE + 10, L"List"),
            Cmd(IDM_AI_BASE + 11, L"Business"),  Cmd(IDM_AI_BASE + 12, L"Academic"),
            Cmd(IDM_AI_BASE + 13, L"Marketing"), Cmd(IDM_AI_BASE + 14, L"Poetry")};
}

std::vector<Item> TableEditItems() {
    return {Cmd(IDM_TABLE_ROW_ABOVE, L"Insert row above"), Cmd(IDM_TABLE_ROW_BELOW, L"Insert row below"),
            Cmd(IDM_TABLE_COL_LEFT, L"Insert column left"), Cmd(IDM_TABLE_COL_RIGHT, L"Insert column right"),
            Sep(),
            Cmd(IDM_TABLE_DEL_ROW, L"Delete row"),          Cmd(IDM_TABLE_DEL_COL, L"Delete column"),
            Cmd(IDM_TABLE_DELETE, L"Delete table")};
}

std::vector<Item> UccItems() {
    static const wchar_t* names[9] = {L"LRM  Left-to-right mark",
                                      L"RLM  Right-to-left mark",
                                      L"ZWJ  Zero width joiner",
                                      L"ZWNJ  Zero width non-joiner",
                                      L"LRE  Start of left-to-right embedding",
                                      L"RLE  Start of right-to-left embedding",
                                      L"LRO  Start of left-to-right override",
                                      L"RLO  Start of right-to-left override",
                                      L"PDF  Pop directional formatting"};
    std::vector<Item> v;
    for (int i = 0; i < 9; ++i) v.push_back(Cmd(IDM_UCC_BASE + i, names[i]));
    return v;
}

std::vector<Item> AsciiArtItems() {
    // Labels carry the codes used inline: #S3#text##, #H2#text##, #S3H2#text##.
    static const wchar_t* heights[4] = {L"(H1) Compact (3 lines)", L"(H2) Normal (6 lines)", L"(H3) Large (11 lines)",
                                        L"(H4) Huge (16 lines)"};
    static const wchar_t* styles[ascii::kStyleCount] = {L"3D shadow", L"Solid blocks", L"Hash marks (#)",
                                                         L"Hollow 3D", L"Soft shadow", L"Gradient fade",
                                                         L"Made of your text", L"Fine dots (braille)"};
    std::vector<Item> h, st, fonts;
    for (int i = 0; i < 4; ++i) h.push_back(Check(IDM_ASCII_HEIGHT_BASE + i, heights[i], g_set.asciiHeight == i));
    for (int k = 0; k < ascii::kStyleCount; ++k) {
        int i = kStyleOrder[k];
        std::wstring label = L"(S" + std::to_wstring(k + 1) + L") " + styles[i];
        st.push_back(Check(IDM_ASCII_STYLE_BASE + i, label.c_str(), g_set.asciiStyle == i));
    }
    for (int i = 0; i < 7; ++i) fonts.push_back(Check(IDM_ASCII_FONT_BASE + i, kArtFontNames[i], g_set.asciiFont == i));
    return {Check(IDM_ASCII_TOGGLE, L"Turn ##text## into art", g_set.asciiOn),
            Cmd(IDM_ASCII_CONVERT, L"Convert ##text## on this line", L"Ctrl+Shift+A"),
            Sep(),
            Sub(L"Height", h),
            Sub(L"Style", st),
            Sub(L"Letters", fonts),
            Sub(L"Alignment", {Check(IDM_ASCII_LEFT, L"Left", !g_set.asciiCenter),
                               Check(IDM_ASCII_CENTER, L"Centered", g_set.asciiCenter)}),
            Sub(L"Placement", {Check(IDM_ASCII_REMOVE, L"Remove ##text##, art below the line", g_set.asciiPlacement == 0),
                               Check(IDM_ASCII_KEEP, L"Keep the words, art below the line", g_set.asciiPlacement == 1),
                               Check(IDM_ASCII_REPLACE, L"Art in place of ##text##", g_set.asciiPlacement == 2)})};
}

// which: 0-2 File/Edit/View, kToolFirst+0 headings, +1 lists, +6 table, kWriting writing tools.
std::vector<Item> BuildItems(int which) {
    Tab* t = Cur();
    std::vector<Item> v;
    if (which == 0) {
        v.push_back(Cmd(IDM_NEWTAB, L"New tab", L"Ctrl+N"));
        v.push_back(Cmd(IDM_NEWWINDOW, L"New window", L"Ctrl+Shift+N"));
        v.push_back(Cmd(IDM_NEWMDTAB, L"New Markdown tab"));
        v.push_back(Cmd(IDM_OPEN, L"Open", L"Ctrl+O"));
        if (g_set.recentEnabled) {
            std::vector<Item> recent;
            for (size_t i = 0; i < g_set.recent.size(); ++i)
                recent.push_back(Cmd(static_cast<UINT>(IDM_RECENT_BASE + i), g_set.recent[i].c_str()));
            if (recent.empty()) recent.push_back(Cmd(0, L"No recent files", L"", false));
            recent.push_back(Sep());
            recent.push_back(Cmd(IDM_CLEARRECENT, L"Clear list", L"", !g_set.recent.empty()));
            v.push_back(Sub(L"Recent", recent));
        }
        v.push_back(Cmd(IDM_SAVE, L"Save", L"Ctrl+S"));
        v.push_back(Cmd(IDM_SAVEAS, L"Save as", L"Ctrl+Shift+S"));
        v.push_back(Cmd(IDM_SAVEALL, L"Save all", L"Ctrl+Alt+S"));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_PAGESETUP, L"Page setup"));
        v.push_back(Cmd(IDM_PRINT, L"Print", L"Ctrl+P"));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_CLOSETAB, L"Close tab", L"Ctrl+W"));
        v.push_back(Cmd(IDM_CLOSEWINDOW, L"Close window", L"Ctrl+Shift+W"));
        v.push_back(Cmd(IDM_EXIT, L"Exit"));
    } else if (which == 1) {
        CHARRANGE cr{};
        if (t) SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        bool hasSel = cr.cpMax > cr.cpMin;
        bool canUndo = t && SendMessageW(t->edit, EM_CANUNDO, 0, 0);
        bool canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
        bool hasFind = GetWindowTextLengthW(g_findEdit) > 0;
        v.push_back(Cmd(IDM_UNDO, L"Undo", L"Ctrl+Z", canUndo));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_CUT, L"Cut", L"Ctrl+X", hasSel));
        v.push_back(Cmd(IDM_COPY, L"Copy", L"Ctrl+C", hasSel));
        v.push_back(Cmd(IDM_PASTE, L"Paste", L"Ctrl+V", canPaste));
        v.push_back(Cmd(IDM_DELETE, L"Delete", L"Del"));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_CLEARFMT, L"Clear formatting", L"", t && t->rich));
        v.push_back(Cmd(IDM_BING_SEARCH, L"Search with Bing", L"Ctrl+E"));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_BING_DEFINE, L"Define with Bing", L"Ctrl+E"));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_FIND, L"Find", L"Ctrl+F"));
        v.push_back(Cmd(IDM_FINDNEXT, L"Find next", L"F3", hasFind));
        v.push_back(Cmd(IDM_FINDPREV, L"Find previous", L"Shift+F3", hasFind));
        v.push_back(Cmd(IDM_REPLACE, L"Replace", L"Ctrl+H"));
        v.push_back(Cmd(IDM_GOTO, L"Go to", L"Ctrl+G"));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_SELECTALL, L"Select all", L"Ctrl+A"));
        v.push_back(Cmd(IDM_TIMEDATE, L"Time/Date", L"F5"));
        v.push_back(Sub(L"ASCII art", AsciiArtItems()));
        v.push_back(Sep());
        v.push_back(Cmd(IDM_FONT, L"Font"));
    } else if (which == 2) {
        v.push_back(Sub(L"Zoom", {Cmd(IDM_ZOOMIN, L"Zoom in", L"Ctrl+Plus"), Cmd(IDM_ZOOMOUT, L"Zoom out", L"Ctrl+Minus"),
                                  Cmd(IDM_ZOOMRESET, L"Restore default zoom", L"Ctrl+0")}));
        v.push_back(Check(IDM_STATUSBAR, L"Status bar", g_set.statusBar));
        v.push_back(Check(IDM_WORDWRAP, L"Word wrap", g_set.wordWrap));
        bool isMd = t && t->kind == Kind::Markdown;
        v.push_back(Sub(L"Markdown", {Check(IDM_MD_FORMATTED, L"Formatted", isMd && !t->syntax, isMd && g_set.formatting),
                                      Check(IDM_MD_SYNTAX, L"Syntax", isMd && t->syntax, isMd)},
                        isMd));
    } else if (which == kToolFirst) {  // Headings: each style drawn in its own size
        long style = 0;
        if (t) {
            CHARRANGE c{};
            SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&c));
            style = ParaStyle(*t, c.cpMin);
        }
        static const wchar_t* names[7] = {L"Body", L"Title", L"Subtitle", L"Heading", L"Subheading", L"Section",
                                          L"Subsection"};
        static const int px[7] = {14, 32, 28, 24, 20, 17, 16};
        bool isHeading = style > kStyleHeading && style <= kStyleHeading + 6;
        for (int level = 1; level <= 7; ++level) {
            int l = level % 7;  // 1..6 then 0 (Body last)
            Item it;
            it.kind = flyout::Kind::StylePreview;
            it.id = IDM_HEADING_BASE + l;
            it.text = names[l];
            it.previewPx = px[l];
            it.current = l ? style == kStyleHeading + l : !isHeading;
            v.push_back(it);
        }
    } else if (which == kToolFirst + 1) {  // Lists
        long listType = tomListNone;
        if (t && t->doc) {
            ITextSelection* sel = nullptr;
            if (SUCCEEDED(t->doc->GetSelection(&sel)) && sel) {
                ITextPara* p = nullptr;
                if (SUCCEEDED(sel->GetPara(&p)) && p) {
                    p->GetListType(&listType);
                    p->Release();
                }
                sel->Release();
            }
        }
        bool inList = (listType & 0xFFFF) != tomListNone;
        v.push_back(Cmd(IDM_LIST_BULLET, L"Bulleted list"));
        v.push_back(Cmd(IDM_LIST_NUMBER, L"Numbered list"));
        v.push_back(Cmd(IDM_INDENT_INC, L"Increase indent", L"", inList));
        v.push_back(Cmd(IDM_INDENT_DEC, L"Decrease indent", L"", inList));
    } else if (which == kToolFirst + 6) {  // Table
        Item grid;
        grid.kind = flyout::Kind::TableGrid;
        grid.id = IDM_TABLE_GRID;
        grid.text = L"Insert table";
        v.push_back(grid);
        v.push_back(Sep());
        v.push_back(Cmd(IDM_TABLE_INSERT, L"Insert table"));
        v.push_back(Sub(L"Edit table", TableEditItems(), t && SelectionInTable(*t)));
    } else if (which == kWriting) {
        Item write = Cmd(IDM_AI_BASE + 0, L"Write", L"Ctrl+Q");
        write.badge = L"New";
        v.push_back(write);
        v.push_back(Cmd(IDM_AI_BASE + 1, L"Rewrite", L"Ctrl+D"));
        v.push_back(Cmd(IDM_AI_BASE + 2, L"Summarize", L"Ctrl+M"));
        v.push_back(Cmd(IDM_AI_BASE + 3, L"Make shorter"));
        v.push_back(Cmd(IDM_AI_BASE + 4, L"Make longer"));
        v.push_back(Sub(L"Change tone", ToneItems()));
        v.push_back(Sub(L"Change format", FormatItems()));
    }
    return v;
}

RECT ClientToScreenRect(RECT r) {
    MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

int MenuBarHit(POINT screen) {
    ScreenToClient(g_hwnd, &screen);
    for (int i = 0; i < 3; ++i)
        if (PtInRect(&g_menuRc[i], screen)) return i;
    return -1;
}

void HandleFlyoutResult(const flyout::Result& r) {
    if (r.id == IDM_TABLE_GRID && r.gridRows > 0) {
        if (Tab* t = Cur()) InsertTableHere(*t, r.gridRows, r.gridCols);
        return;
    }
    if (r.id) SendMessageW(g_hwnd, WM_COMMAND, r.id, 0);
}

// Opens a menu bar menu or toolbar dropdown; keeps switching while the user moves
// across the menu bar, like WinUI's MenuBar.
void OpenMenu(int i, bool fromKeyboard) {
    for (;;) {
        std::vector<Item> items = BuildItems(i);
        flyout::Options o;
        o.keyboard = fromKeyboard;
        RECT anchor = ClientToScreenRect(g_menuRc[i]);
        if (i < 3) {
            o.menuBarHit = MenuBarHit;
            o.menuBarIndex = i;
            anchor.left += S(2);
        } else if (i >= kToolFirst && i < kToolFirst + kToolCount) {
            // Toolbar dropdowns are centered 6px right of the button's left edge.
            anchor.left = anchor.right = anchor.left + S(6);
        } else {
            anchor.left = anchor.right = (anchor.left + anchor.right) / 2;
        }
        if (i < 3) o.minWidth = S(230);
        if (i == kToolFirst) o.minWidth = S(180);
        if (i == kToolFirst + 6) o.minWidth = S(200);
        if (i == kToolFirst + 1) o.minWidth = S(178);
        if (i == kWriting) o.minWidth = S(230);
        g_openMenu = i;
        InvalidateMenu();
        UpdateWindow(g_hwnd);
        flyout::Result r = flyout::Show(g_hwnd, items, anchor, o);
        g_openMenu = -1;
        InvalidateMenu();
        if (r.switchTo != -1 && i < 3) {
            i = (r.switchTo + 3) % 3;
            continue;
        }
        HandleFlyoutResult(r);
        return;
    }
}

void ShowEditorContextMenu(HWND edit, LPARAM lp) {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (lp == -1) {
        GetCaretPos(&pt);
        ClientToScreen(edit, &pt);
    }
    Tab* t = Cur();
    CHARRANGE cr{};
    SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    bool hasSel = cr.cpMax > cr.cpMin;
    bool rich = t && t->rich;
    std::vector<Item> v;
    v.push_back(Icon(IDM_UNDO, glyph::Undo, L"Undo", SendMessageW(edit, EM_CANUNDO, 0, 0) != 0));
    if (rich) {
        v.push_back(Sep());
        v.push_back(Icon(IDM_TABLE_INSERT, glyph::Grid, L"Insert table"));
    }
    v.push_back(Sep());
    v.push_back(Icon(hasSel ? IDM_BING_SEARCH : IDM_BING_DEFINE, glyph::Search,
                     hasSel ? L"Search with Bing" : L"Define with Bing"));
    v.push_back(Sep());
    v.push_back(Icon(IDM_AI_BASE + 0, glyph::Pen, L"Write"));
    v.push_back(Icon(IDM_AI_BASE + 1, glyph::PenLines, L"Rewrite"));
    std::vector<Item> custom = {Cmd(IDM_AI_BASE + 3, L"Make shorter"), Cmd(IDM_AI_BASE + 4, L"Make longer"),
                                Sub(L"Change tone", ToneItems()), Sub(L"Change format", FormatItems())};
    Item customRewrite = Sub(L"Custom rewrite", custom);
    customRewrite.icon = L" ";  // keeps the icon column aligned without drawing one
    v.push_back(customRewrite);
    v.push_back(Icon(IDM_AI_BASE + 2, glyph::Document, L"Summarize"));
    v.push_back(Sep());
    v.push_back(Sub(L"Spelling",
                    {Cmd(0, L"No suggestions", L"", false), Sep(),
                     Cmd(IDM_SPELL_TOGGLE, g_set.spellCheck ? L"Turn off spell check" : L"Turn on spell check")},
                    true, glyph::Spell));
    v.push_back(Sep());
    bool rtl = false;
    if (t && t->doc) {
        PARAFORMAT2 pf{};
        pf.cbSize = sizeof pf;
        SendMessageW(edit, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
        rtl = (pf.wEffects & PFE_RTLPARA) != 0;
    }
    v.push_back(Sub(L"Display and Unicode",
                    {Check(IDM_RTL, L"Right to left reading order", rtl),
                     Check(IDM_SHOWUCC, L"Show Unicode control characters", false, false),
                     Sub(L"Insert Unicode control character", UccItems())},
                    true, glyph::Unicode));
    v.push_back(Sep());
    Item bar;
    bar.kind = flyout::Kind::CommandBar;
    bar.sub = {Icon(IDM_CUT, glyph::Cut, L"Cut", hasSel), Icon(IDM_COPY, glyph::Copy, L"Copy", hasSel),
               Icon(IDM_PASTE, glyph::Paste, L"Paste", IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE),
               Icon(IDM_SELECTALL, glyph::SelectAll, L"Select all"), Icon(IDM_DELETE, glyph::Delete, L"Delete", hasSel)};
    v.push_back(bar);
    flyout::Options o;
    o.minWidth = S(340);
    o.keyboard = lp == -1;
    HandleFlyoutResult(flyout::Show(g_hwnd, v, {pt.x, pt.y - S(2), pt.x, pt.y - S(2)}, o));
}

void ShowTabContextMenu(int tab, POINT client) {
    int n = static_cast<int>(g_tabs.size());
    std::vector<Item> v = {Cmd(IDM_NEWTAB, L"New tab", L"Ctrl+N"), Sep(), Cmd(IDM_CLOSETAB, L"Close tab", L"Ctrl+W"),
                           Cmd(IDM_CLOSEOTHERS, L"Close other tabs", L"", n > 1),
                           Cmd(IDM_CLOSERIGHT, L"Close tabs to the right", L"", tab < n - 1)};
    ActivateTab(tab);
    POINT pt = client;
    ClientToScreen(g_hwnd, &pt);
    flyout::Options o;
    o.minWidth = S(280);
    HandleFlyoutResult(flyout::Show(g_hwnd, v, {pt.x, pt.y, pt.x + 1, pt.y}, o));
}

void SetZoom(Tab& t, int z) {
    t.zoom = std::clamp(z, 10, 500);
    if (t.zoom == 100)
        SendMessageW(t.edit, EM_SETZOOM, 0, 0);
    else
        SendMessageW(t.edit, EM_SETZOOM, static_cast<WPARAM>(t.zoom), 100);
    InvalidateStatus();
}

void StatusMenu(int seg, RECT segRect) {
    Tab* t = Cur();
    if (!t) return;
    std::vector<Item> v;
    if (seg == SEG_ENCODING) {
        const std::pair<UINT, Encoding> items[] = {{IDM_ENC_UTF8, Encoding::Utf8},
                                                   {IDM_ENC_UTF8BOM, Encoding::Utf8Bom},
                                                   {IDM_ENC_UTF16LE, Encoding::Utf16LE},
                                                   {IDM_ENC_UTF16BE, Encoding::Utf16BE},
                                                   {IDM_ENC_ANSI, Encoding::Ansi}};
        for (auto& [id, e] : items) v.push_back(Check(id, EncodingName(e), t->enc == e));
    } else if (seg == SEG_EOL) {
        const std::pair<UINT, Eol> items[] = {{IDM_EOL_CRLF, Eol::CRLF}, {IDM_EOL_LF, Eol::LF}, {IDM_EOL_CR, Eol::CR}};
        for (auto& [id, e] : items) v.push_back(Check(id, EolName(e), t->eol == e));
    } else {
        v = {Cmd(IDM_ZOOMIN, L"Zoom in", L"Ctrl+Plus"), Cmd(IDM_ZOOMOUT, L"Zoom out", L"Ctrl+Minus"),
             Cmd(IDM_ZOOMRESET, L"Restore default zoom", L"Ctrl+0")};
    }
    flyout::Options o;
    o.above = true;
    HandleFlyoutResult(flyout::Show(g_hwnd, v, ClientToScreenRect(segRect), o));
}

// ---------------------------------------------------------------------------
// Painting

void DrawTextComposited(HDC dc, const std::wstring& s, RECT r, COLORREF c, HFONT f, DWORD fmt) {
    HGDIOBJ old = SelectObject(dc, f);
    DTTOPTS o{sizeof o};
    o.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
    o.crText = c;
    if (g_theme) DrawThemeTextEx(g_theme, dc, 0, 0, s.c_str(), -1, fmt, &r, &o);
    SelectObject(dc, old);
}

// GDI leaves alpha at zero; mark every painted (non-black) pixel as opaque so it
// covers the Mica backdrop, while untouched pixels stay see-through.
void MakeOpaque(HPAINTBUFFER pb, const RECT& area) {
    RGBQUAD* bits = nullptr;
    int row = 0;
    if (FAILED(GetBufferedPaintBits(pb, &bits, &row))) return;
    RECT tr{};
    GetBufferedPaintTargetRect(pb, &tr);
    for (LONG y = std::max(area.top, tr.top); y < std::min(area.bottom, tr.bottom); ++y) {
        RGBQUAD* line = bits + static_cast<size_t>(y - tr.top) * row;
        for (LONG x = std::max(area.left, tr.left); x < std::min(area.right, tr.right); ++x) {
            RGBQUAD& q = line[x - tr.left];
            if (q.rgbRed | q.rgbGreen | q.rgbBlue) q.rgbReserved = 255;
        }
    }
}

// Translucent layers over Mica. Notepad's tab strip, menu row and status bar are not solid:
// measured on a focused window, the selected tab is white at ~70% over the Mica backdrop and
// the menu row and status bar white at ~50% (which is why the status bar picks up a warm tint
// from the wallpaper). Shapes are first drawn with GDI in reserved "sentinel" colors, then
// ResolveLayers turns each sentinel into its premultiplied translucent color; text is drawn
// afterwards with alpha-correct composited text. Unfocused, DWM swaps Mica for a flat color,
// so both states follow Windows automatically.
enum Sentinel { SEN_HOVER = 3, SEN_PRESSED, SEN_DIVIDER, SEN_ACTIVE_TAB, SEN_LINE };
COLORREF Sen(int s) { return RGB(1, 2, s); }

struct LayerColor {
    COLORREF color;
    BYTE alpha;
};

LayerColor LayerFor(int sentinel) {
    switch (sentinel) {
    case SEN_HOVER: return g_dark ? LayerColor{RGB(255, 255, 255), 15} : LayerColor{RGB(0, 0, 0), 10};
    case SEN_PRESSED: return g_dark ? LayerColor{RGB(255, 255, 255), 10} : LayerColor{RGB(0, 0, 0), 18};
    case SEN_DIVIDER: return g_dark ? LayerColor{RGB(255, 255, 255), 25} : LayerColor{RGB(0, 0, 0), 30};
    case SEN_ACTIVE_TAB: return g_dark ? LayerColor{RGB(58, 58, 58), 180} : LayerColor{RGB(255, 255, 255), 179};
    case SEN_LINE: return g_dark ? LayerColor{RGB(0, 0, 0), 60} : LayerColor{RGB(0, 0, 0), 14};
    default: return g_dark ? LayerColor{RGB(58, 58, 58), 77} : LayerColor{RGB(255, 255, 255), 128};  // base layer
    }
}

// Converts the GDI pass into premultiplied ARGB: sentinels become translucent layers, other
// painted pixels become opaque, and untouched pixels get the base layer (if `base`).
void ResolveLayers(HPAINTBUFFER pb, const RECT& area, bool base) {
    RGBQUAD* bits = nullptr;
    int row = 0;
    if (FAILED(GetBufferedPaintBits(pb, &bits, &row))) return;
    RECT tr{};
    GetBufferedPaintTargetRect(pb, &tr);
    COLORREF under = g_pal.strip;  // stands in for Mica when it isn't available
    for (LONG y = std::max(area.top, tr.top); y < std::min(area.bottom, tr.bottom); ++y) {
        RGBQUAD* line = bits + static_cast<size_t>(y - tr.top) * row;
        for (LONG x = std::max(area.left, tr.left); x < std::min(area.right, tr.right); ++x) {
            RGBQUAD& q = line[x - tr.left];
            if (q.rgbReserved) continue;
            int r = q.rgbRed, g = q.rgbGreen, b = q.rgbBlue;
            LayerColor lc{};
            if (r == 0 && g == 0 && b == 0) {
                if (!base) continue;
                lc = LayerFor(0);
            } else if (r == 1 && g == 2 && b >= SEN_HOVER && b <= SEN_LINE) {
                lc = LayerFor(b);
            } else {
                q.rgbReserved = 255;
                continue;
            }
            int a = lc.alpha;
            if (g_mica) {
                q.rgbRed = static_cast<BYTE>(GetRValue(lc.color) * a / 255);
                q.rgbGreen = static_cast<BYTE>(GetGValue(lc.color) * a / 255);
                q.rgbBlue = static_cast<BYTE>(GetBValue(lc.color) * a / 255);
                q.rgbReserved = static_cast<BYTE>(a);
            } else {
                q.rgbRed = static_cast<BYTE>((GetRValue(lc.color) * a + GetRValue(under) * (255 - a)) / 255);
                q.rgbGreen = static_cast<BYTE>((GetGValue(lc.color) * a + GetGValue(under) * (255 - a)) / 255);
                q.rgbBlue = static_cast<BYTE>((GetBValue(lc.color) * a + GetBValue(under) * (255 - a)) / 255);
                q.rgbReserved = 255;
            }
        }
    }
}

void PaintStrip(HDC hdc, const Layout& l) {
    BP_PAINTPARAMS pp{sizeof pp};
    pp.dwFlags = BPPF_ERASE;
    HDC dc = nullptr;
    HPAINTBUFFER pb = BeginBufferedPaint(hdc, &l.strip, BPBF_TOPDOWNDIB, &pp, &dc);
    if (!pb) return;
    if (!g_mica) {
        RECT bg = l.strip;
        bg.right -= CaptionButtonsWidth();
        FillSolid(dc, bg, g_pal.strip);
    }
    if (g_settings) {
        // Settings page title bar: back arrow, app icon, "Notepad" (measured from Notepad).
        g_backRc = {S(4), S(5), S(36), S(37)};
        if (g_hoverAdd) FillRound(dc, g_backRc, Sen(SEN_HOVER), S(8));
        ResolveLayers(pb, l.strip, false);
        if (g_smallIcon) DrawIconEx(dc, S(48), S(13), g_smallIcon, S(16), S(16), 0, nullptr, DI_NORMAL);
        DrawTextComposited(dc, glyph::Back, g_backRc, g_pal.text, g_iconFontSmall,
                           DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        DrawTextComposited(dc, kAppName, {S(80), 0, S(300), S(42)}, g_pal.text, g_smallFont,
                           DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        EndBufferedPaint(pb, TRUE);
        return;
    }
    int n = static_cast<int>(g_tabs.size());
    for (int i = 0; i < n && i < static_cast<int>(g_tabRc.size()); ++i) {
        RECT t = g_tabRc[i];
        bool active = i == g_active, hover = i == g_hoverTab;
        if (active) {
            RECT r = t;
            r.bottom += S(16);  // the rounded bottom falls outside the buffer, leaving square corners
            FillRound(dc, r, Sen(SEN_ACTIVE_TAB), S(16));
        } else if (hover) {
            RECT r = t;
            r.top += S(2);
            r.bottom -= S(4);
            FillRound(dc, r, Sen(SEN_HOVER), S(12));
        }
        if (!active && !hover && i + 1 < n && i + 1 != g_active && i + 1 != g_hoverTab)
            FillSolid(dc, {t.right - 1, t.top + S(8), t.right, t.bottom - S(8)}, Sen(SEN_DIVIDER));
        RECT cr = TabCloseRect(t);
        if (hover && g_hoverClose) FillRound(dc, cr, Sen(SEN_PRESSED), S(8));
        if (IsModified(*g_tabs[i]) && !hover) {
            int d = S(8), cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
            FrameRound(dc, {cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2}, g_pal.text, g_pal.text, d);
        }
    }
    if (g_hoverAdd) FillRound(dc, g_addRc, Sen(SEN_HOVER), S(8));
    ResolveLayers(pb, l.strip, false);
    if (g_smallIcon) {  // after resolving so its anti-aliased edges blend correctly
        int sz = S(16), cy = (S(kTabTop) + l.strip.bottom) / 2;
        DrawIconEx(dc, S(14), cy - sz / 2, g_smallIcon, sz, sz, 0, nullptr, DI_NORMAL);
    }

    const DWORD single = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    for (int i = 0; i < n && i < static_cast<int>(g_tabRc.size()); ++i) {
        RECT t = g_tabRc[i];
        RECT cr = TabCloseRect(t);
        bool hover = i == g_hoverTab, active = i == g_active;
        bool modified = IsModified(*g_tabs[i]);
        // Measured: the selected tab's title is semibold, 9-10px from the tab edge.
        RECT tr{t.left + S(10), t.top, cr.left - S(4), t.bottom};
        DrawTextComposited(dc, TabName(*g_tabs[i]), tr, g_pal.text, active ? g_tabActiveFont : g_smallFont,
                           single | DT_LEFT | DT_END_ELLIPSIS);
        if (hover || (active && !modified))
            DrawTextComposited(dc, kGlyphClose, cr, g_pal.text, g_iconFontSmall, single | DT_CENTER);
    }
    DrawTextComposited(dc, kGlyphAdd, g_addRc, g_pal.text, g_iconFontSmall, single | DT_CENTER);
    EndBufferedPaint(pb, TRUE);
}

void DrawMenuButtonBg(HDC dc, int i, bool toggled) {
    RECT rc = g_menuRc[i];
    if (i >= kToolFirst) InflateRect(&rc, 0, -S(1));
    if (i == g_openMenu || toggled)
        FillRound(dc, rc, Sen(SEN_PRESSED), S(8));
    else if (i == g_hoverMenu)
        FillRound(dc, rc, Sen(SEN_HOVER), S(8));
}

// A 56px dropdown button: glyph area on the left, small chevron on the right (measured).
void DropdownParts(const RECT& r, RECT& glyphRc, RECT& chevRc) {
    glyphRc = {r.left + S(4), r.top, r.left + S(36), r.bottom};
    chevRc = {r.left + S(34), r.top, r.right - S(6), r.bottom};
}

void DrawTableGlyph(HDC dc, const RECT& r) {
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, h = S(7);
    HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetDCPenColor(dc, g_pal.text);
    RoundRect(dc, cx - h, cy - h, cx + h + 1, cy + h + 1, S(4), S(4));
    MoveToEx(dc, cx, cy - h, nullptr);
    LineTo(dc, cx, cy + h);
    MoveToEx(dc, cx - h, cy, nullptr);
    LineTo(dc, cx + h, cy);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

// One pass draws shapes (shapes == true), the other draws text and glyphs.
void PaintMenuPass(HDC dc, const Layout& l, bool shapes) {
    const UINT center = DT_SINGLELINE | DT_VCENTER | DT_CENTER;
    auto text = [&](const std::wstring& s, const RECT& r, COLORREF c, HFONT f) {
        if (!shapes) DrawTextIn(dc, s, r, c, f, center);
    };
    if (shapes) FillSolid(dc, {l.menu.left, l.menu.bottom - 1, l.menu.right, l.menu.bottom}, Sen(SEN_LINE));
    static const wchar_t* names[3] = {L"File", L"Edit", L"View"};
    for (int i = 0; i < 3; ++i) {
        if (shapes) DrawMenuButtonBg(dc, i, false);
        text(names[i], g_menuRc[i], g_pal.text, g_uiFont);
    }

    // Right side: Writing tools, What's new, account, settings.
    RECT gr, cr;
    if (g_set.writingTools) {
        if (shapes) DrawMenuButtonBg(dc, kWriting, false);
        DropdownParts(g_menuRc[kWriting], gr, cr);
        text(glyph::Pen, gr, g_pal.text, g_iconFont);
        text(glyph::Sparkle, {gr.left + S(2), gr.top + S(6), gr.left + S(14), gr.top + S(18)}, g_pal.text,
             g_iconFontSmall);
        text(glyph::ChevronDown, cr, g_pal.text, g_iconFontSmall);
    }
    if (shapes) DrawMenuButtonBg(dc, kWhatsNew, false);
    text(glyph::Megaphone, g_menuRc[kWhatsNew], g_pal.text, g_iconFont);
    if (shapes && !g_whatsNewSeen) {
        int dx = g_menuRc[kWhatsNew].right - S(9), dy = g_menuRc[kWhatsNew].top + S(7);
        FillRound(dc, {dx - S(3), dy - S(3), dx + S(3), dy + S(3)}, g_pal.accent, S(6));
    }
    if (shapes) DrawMenuButtonBg(dc, kAvatar, false);
    {
        RECT a = g_menuRc[kAvatar];
        int cx = (a.left + a.right) / 2, cy = (a.top + a.bottom) / 2;
        if (shapes) FrameRound(dc, {cx - S(10), cy - S(10), cx + S(10), cy + S(10)}, g_pal.inputBorder, g_pal.hover, S(20));
        text(glyph::Person, {cx - S(10), cy - S(10), cx + S(10), cy + S(10)}, g_pal.textMuted, g_iconFontSmall);
    }
    if (shapes) DrawMenuButtonBg(dc, 3, false);
    text(glyph::Settings, g_menuRc[3], g_pal.text, g_iconFont);

    if (!ToolbarVisible()) return;
    // Formatting toolbar, matching Notepad's icons: H1, list, B, I, S, link, table, clear.
    const bool toggled[kToolCount] = {false, false, g_fmtBold, g_fmtItalic, g_fmtStrike, false, false, false};
    for (int k = 0; k < kToolCount; ++k) {
        int i = kToolFirst + k;
        if (shapes) DrawMenuButtonBg(dc, i, toggled[k]);
        RECT r = g_menuRc[i];
        bool dropdown = k == 0 || k == 1 || k == 6;
        if (dropdown) {
            DropdownParts(r, gr, cr);
            text(glyph::ChevronDown, cr, g_pal.text, g_iconFontSmall);
            r = gr;
        }
        switch (k) {
        case 0: text(L"H1", r, g_pal.text, g_uiFont); break;
        case 1: text(glyph::List, r, g_pal.text, g_iconFont); break;
        case 2: text(glyph::Bold, r, g_pal.text, g_iconFont); break;
        case 3: text(glyph::Italic, r, g_pal.text, g_iconFont); break;
        case 4: text(glyph::Strike, r, g_pal.text, g_iconFont); break;
        case 5: text(glyph::Link, r, g_pal.text, g_iconFont); break;
        case 6:
            if (shapes) DrawTableGlyph(dc, r);
            break;
        default: {
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            // "Ab" with an eraser, like Notepad's clear-formatting icon.
            text(L"A", {cx - S(10), cy - S(9), cx + S(1), cy + S(5)}, g_pal.text, g_uiFont);
            text(L"b", {cx - S(1), cy - S(7), cx + S(7), cy + S(4)}, g_pal.text, g_smallFont);
            text(glyph::Eraser, {cx - S(1), cy - S(1), cx + S(11), cy + S(10)}, g_pal.text, g_iconFontSmall);
        }
        }
    }
}

void PaintMenu(HDC hdc, const Layout& l) {
    BP_PAINTPARAMS pp{sizeof pp};
    pp.dwFlags = BPPF_ERASE;
    HDC dc = nullptr;
    HPAINTBUFFER pb = BeginBufferedPaint(hdc, &l.menu, BPBF_TOPDOWNDIB, &pp, &dc);
    if (!pb) return;
    PaintMenuPass(dc, l, true);
    ResolveLayers(pb, l.menu, true);
    g_textTheme = g_theme;
    PaintMenuPass(dc, l, false);
    g_textTheme = nullptr;
    EndBufferedPaint(pb, TRUE);
}

void PaintStatus(HDC hdc, const Layout& l) {
    g_segs.clear();
    if (!g_set.statusBar || l.status.bottom <= l.status.top) return;
    BP_PAINTPARAMS pp{sizeof pp};
    pp.dwFlags = BPPF_ERASE;
    HDC dc = nullptr;
    HPAINTBUFFER pb = BeginBufferedPaint(hdc, &l.status, BPBF_TOPDOWNDIB, &pp, &dc);
    if (!pb) return;
    FillSolid(dc, {l.status.left, l.status.top, l.status.right, l.status.top + 1}, Sen(SEN_LINE));
    Tab* t = Cur();
    if (!t) {
        ResolveLayers(pb, l.status, true);
        EndBufferedPaint(pb, TRUE);
        return;
    }

    long ln = 1, col = 1;
    CaretLineCol(*t, ln, col);
    CHARRANGE cr{};
    SendMessageW(t->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    LONG total = TextLength(t->edit);
    wchar_t pos[64], count[96], zoom[16];
    swprintf_s(pos, L"Ln %ld, Col %ld", ln, col);
    if (cr.cpMax > cr.cpMin)
        swprintf_s(count, L"%ld of %ld characters", static_cast<long>(cr.cpMax - cr.cpMin), static_cast<long>(total));
    else
        swprintf_s(count, total == 1 ? L"%ld character" : L"%ld characters", static_cast<long>(total));
    swprintf_s(zoom, L"%d%%", t->zoom);

    HGDIOBJ old = SelectObject(dc, g_smallFont);
    RECT band{l.status.left, l.status.top + 1, l.status.right, l.status.bottom};
    std::vector<std::pair<std::wstring, RECT>> texts;
    auto separator = [&](int x) { FillSolid(dc, {x, band.top + S(4), x + 1, band.bottom - S(4)}, Sen(SEN_DIVIDER)); };
    auto draw = [&](const std::wstring& s, int x, int right) { texts.push_back({s, {x, band.top, right, band.bottom}}); };

    // Left: position and count, packed. Right: fixed-width columns measured from Notepad
    // (start offsets from the right edge: plain text 654, zoom 379, line ending 312, encoding 142).
    int x = S(16);
    int w = TextWidth(dc, pos);
    draw(pos, x, x + w);
    x += w + S(8);
    separator(x);
    x += S(8);
    int countRight = x + TextWidth(dc, count);
    draw(count, x, countRight);

    const struct {
        std::wstring text;
        int fromRight;
        int seg;
    } cols[4] = {{t->kind == Kind::Markdown ? (t->syntax ? L"Markdown syntax" : L"Markdown") : L"Plain text", 654, 0},
                 {zoom, 379, SEG_ZOOM},
                 {EolName(t->eol), 312, SEG_EOL},
                 {EncodingName(t->enc), 142, SEG_ENCODING}};
    for (int i = 0; i < 4; ++i) {
        int cx = band.right - S(cols[i].fromRight);
        int next = i + 1 < 4 ? band.right - S(cols[i + 1].fromRight) : band.right;
        if (cx < countRight + S(16)) continue;  // narrow window: drop columns that would overlap
        separator(cx);
        draw(cols[i].text, cx + S(10), next - S(4));
        if (cols[i].seg) g_segs.push_back({{cx + 1, band.top, next, band.bottom}, cols[i].seg});
    }
    SelectObject(dc, old);
    ResolveLayers(pb, l.status, true);
    g_textTheme = g_theme;
    for (const auto& [s, r] : texts)
        DrawTextIn(dc, s, r, g_pal.textMuted, g_smallFont, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    g_textTheme = nullptr;
    EndBufferedPaint(pb, TRUE);
}

void UpdateHover(POINT pt) {
    int tab = -1;
    bool close = false;
    for (size_t i = 0; i < g_tabRc.size(); ++i) {
        if (PtInRect(&g_tabRc[i], pt)) {
            tab = static_cast<int>(i);
            RECT cr = TabCloseRect(g_tabRc[i]);
            close = PtInRect(&cr, pt) != FALSE;
        }
    }
    bool add = PtInRect(g_settings ? &g_backRc : &g_addRc, pt) != FALSE;  // back button while in settings
    if (g_settings) tab = -1;
    int menu = -1;
    bool toolbar = ToolbarVisible();
    for (int i = 0; i < kMenuTargets; ++i) {
        if (i >= kToolFirst && i < kToolFirst + kToolCount && !toolbar) continue;
        if (PtInRect(&g_menuRc[i], pt)) menu = i;
    }
    if (tab != g_hoverTab || close != g_hoverClose || add != g_hoverAdd) {
        g_hoverTab = tab;
        g_hoverClose = close;
        g_hoverAdd = add;
        InvalidateStrip();
    }
    if (menu != g_hoverMenu) {
        g_hoverMenu = menu;
        InvalidateMenu();
    }
}

// ---------------------------------------------------------------------------
// Formatting commands

bool ToolbarVisible() {
    Tab* t = Cur();
    return t && t->rich && g_toolbarFits;
}

ITextSelection* Selection(Tab& t) {
    ITextSelection* sel = nullptr;
    if (t.doc) t.doc->GetSelection(&sel);
    return sel;
}

void UpdateFormatState() {
    bool b = false, i = false, s = false;
    if (Tab* t = Cur(); t && t->rich) {
        if (ITextSelection* sel = Selection(*t)) {
            ITextFont* f = nullptr;
            if (SUCCEEDED(sel->GetFont(&f)) && f) {
                long v = 0, start = 0;
                f->GetBold(&v);
                sel->GetStart(&start);
                long style = ParaStyle(*t, start);
                b = v == tomTrue && !(style > kStyleHeading && style <= kStyleHeading + 6);
                f->GetItalic(&v);
                i = v == tomTrue;
                f->GetStrikeThrough(&v);
                s = v == tomTrue;
                f->Release();
            }
            sel->Release();
        }
    }
    if (b != g_fmtBold || i != g_fmtItalic || s != g_fmtStrike) {
        g_fmtBold = b;
        g_fmtItalic = i;
        g_fmtStrike = s;
        InvalidateMenu();
    }
}

void ToggleFont(Tab& t, int which) {
    if (!t.rich) return;
    if (ITextSelection* sel = Selection(t)) {
        ITextFont* f = nullptr;
        if (SUCCEEDED(sel->GetFont(&f)) && f) {
            if (which == 0) f->SetBold(tomToggle);
            if (which == 1) f->SetItalic(tomToggle);
            if (which == 2) f->SetStrikeThrough(tomToggle);
            sel->SetFont(f);
            f->Release();
        }
        sel->Release();
    }
    UpdateFormatState();
}

// Runs f on the paragraphs touched by the selection.
template <class F>
void ForSelectedParagraphs(Tab& t, F f) {
    ITextSelection* sel = Selection(t);
    if (!sel) return;
    ITextRange* r = nullptr;
    if (SUCCEEDED(sel->GetDuplicate(&r)) && r) {
        r->Expand(tomParagraph, nullptr);
        f(r);
        r->Release();
    }
    sel->Release();
}

void ApplyHeading(Tab& t, int level) {
    if (!t.rich) return;
    ForSelectedParagraphs(t, [&](ITextRange* r) { SetParaHeading(r, level); });
    // Keep the insertion point's format in step when nothing is selected.
    if (ITextSelection* sel = Selection(t)) {
        long s = 0, e = 0;
        sel->GetStart(&s);
        sel->GetEnd(&e);
        if (s == e) {
            ITextFont* f = nullptr;
            if (SUCCEEDED(sel->GetFont(&f)) && f) {
                f->SetSize(BaseSizePt() * static_cast<float>(kHeadingScale[level]));
                f->SetBold(level ? tomTrue : tomFalse);
                sel->SetFont(f);
                f->Release();
            }
        }
        sel->Release();
    }
}

void ApplyList(Tab& t, bool numbered) {
    if (!t.rich) return;
    ForSelectedParagraphs(t, [&](ITextRange* r) {
        ITextPara* para = nullptr;
        if (FAILED(r->GetPara(&para)) || !para) return;
        long type = tomListNone, style = 0;
        para->GetListType(&type);
        para->GetStyle(&style);
        long base = type & 0xFFFF;
        bool isBullet = base == tomListBullet, isNumber = base != tomListNone && !isBullet;
        if (style > kStyleHeading && style <= kStyleHeading + 6) {
            ITextFont* f = nullptr;
            if (SUCCEEDED(r->GetFont(&f)) && f) {
                f->SetSize(BaseSizePt());
                f->SetBold(tomFalse);
                f->Release();
            }
        }
        if ((numbered && isNumber) || (!numbered && isBullet))
            SetParaList(para, md::Block::Body, L'-', 0);
        else
            SetParaList(para, numbered ? md::Block::Numbered : md::Block::Bullet, numbered ? L'.' : L'-', 0);
        para->Release();
    });
}

void ChangeIndent(Tab& t, int delta) {
    if (!t.rich) return;
    ForSelectedParagraphs(t, [&](ITextRange* r) {
        ITextPara* para = nullptr;
        if (FAILED(r->GetPara(&para)) || !para) return;
        long type = tomListNone, level = 0;
        para->GetListType(&type);
        para->GetListLevelIndex(&level);
        if ((type & 0xFFFF) != tomListNone) {
            level = std::clamp<long>(level + delta, 0, 8);
            para->SetListLevelIndex(level);
            para->SetIndents(-18.0f, 18.0f * static_cast<float>(level + 1), 0);
        }
        para->Release();
    });
}

void ClearFormatting(Tab& t) {
    if (!t.rich) return;
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    if (cr.cpMin == cr.cpMax) {
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(cr.cpMin, cr.cpMin, &r)) && r) {
            r->Expand(tomParagraph, nullptr);
            r->GetStart(&cr.cpMin);
            r->GetEnd(&cr.cpMax);
            r->Release();
        }
    }
    // Remove hidden link instructions first so their text never becomes visible.
    std::vector<Run> runs = CollectRuns(t, StoryLength(t));
    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
        if (!it->hidden || it->end <= cr.cpMin || it->start >= cr.cpMax) continue;
        if (UrlFromInstruction(RawText(t, it->start, it->end)).empty()) continue;
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(it->start, it->end, &r)) && r) {
            r->Delete(tomCharacter, 0, nullptr);
            r->Release();
            cr.cpMax -= it->end - it->start;
        }
    }
    SendMessageW(t.edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_STRIKEOUT | CFM_UNDERLINE | CFM_LINK | CFM_SIZE | CFM_FACE | CFM_COLOR;
    cf.yHeight = g_set.fontSize10 * 2;
    cf.crTextColor = g_pal.text;
    wcsncpy_s(cf.szFaceName, g_set.fontFace.c_str(), _TRUNCATE);
    SendMessageW(t.edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    ForSelectedParagraphs(t, [](ITextRange* r) {
        ITextPara* para = nullptr;
        if (SUCCEEDED(r->GetPara(&para)) && para) {
            SetParaList(para, md::Block::Body, L'-', 0);
            para->Release();
        }
    });
    UpdateFormatState();
}

void CmdLink(Tab& t) {
    if (!t.rich) return;
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    std::wstring sel(static_cast<size_t>(cr.cpMax - cr.cpMin) + 1, L'\0');
    sel.resize(static_cast<size_t>(SendMessageW(t.edit, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(sel.data()))));
    dlg::Spec s;
    s.title = L"Insert link";
    s.fields = {{L"Display text", sel.find(L'\r') == std::wstring::npos ? sel : L"", L"Text to display"},
                {L"Address", L"", L"https://"}};
    s.primary = L"Insert";
    s.close = L"Cancel";
    s.width = 400;
    if (dlg::Show(g_hwnd, s) != dlg::Primary || s.fields[1].value.empty()) return;
    std::wstring text = s.fields[0].value, url = s.fields[1].value;
    if (url.find(L':') == std::wstring::npos) url = L"https://" + url;
    if (text.empty()) text = url;
    InsertLinkAt(t, cr.cpMin, cr.cpMax, text, url);
}

// Finds the URL of the link whose visible text contains cp.
std::wstring LinkUrlAt(Tab& t, long cp) {
    std::wstring before = RawText(t, std::max(0L, cp - 4096), cp);
    size_t at = before.rfind(L"HYPERLINK");
    return at == std::wstring::npos ? L"" : UrlFromInstruction(before.substr(at));
}

void FollowLink(Tab& t, long cp) {
    std::wstring url = LinkUrlAt(t, cp);
    if (url.empty()) return;
    bool web = !_wcsnicmp(url.c_str(), L"http://", 7) || !_wcsnicmp(url.c_str(), L"https://", 8) ||
               !_wcsnicmp(url.c_str(), L"mailto:", 7);
    if (!web && !Confirm(L"This link may not be safe", L"Do you want to open " + url + L"?", L"Open", L"Cancel"))
        return;
    ShellExecuteW(g_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Starts a table on its own empty paragraph at the caret and inserts rows x cols.
void InsertTableHere(Tab& t, int rows, int cols) {
    if (!t.rich) return;
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    ITextRange* r = nullptr;
    long at = cr.cpMax;
    if (SUCCEEDED(t.doc->Range(at, at, &r)) && r) {
        r->Expand(tomParagraph, nullptr);
        long ps = 0, pe = 0;
        r->GetStart(&ps);
        r->GetEnd(&pe);
        r->Release();
        if (pe - ps > 1) {
            ITextRange* ins = nullptr;
            if (SUCCEEDED(t.doc->Range(pe - 1, pe - 1, &ins)) && ins) {
                BSTR b = SysAllocString(L"\r");
                ins->SetText(b);
                SysFreeString(b);
                ins->Release();
            }
            at = pe;
        } else {
            at = ps;
        }
    }
    InsertTableAt(t, at, rows, cols, {});
}

void CmdInsertTable(Tab& t) {
    if (!t.rich) return;
    dlg::Spec s;
    s.title = L"Insert table";
    s.fields = {{L"Rows", L"3", L"", true}, {L"Columns", L"3", L"", true}};
    s.primary = L"Insert";
    s.close = L"Cancel";
    if (dlg::Show(g_hwnd, s) != dlg::Primary) return;
    int rows = std::clamp(_wtoi(s.fields[0].value.c_str()), 1, 63);
    int cols = std::clamp(_wtoi(s.fields[1].value.c_str()), 1, 20);
    InsertTableHere(t, rows, cols);
}

// Row bounds [start, end) of the table row containing cp; false if cp is not in a table.
bool TableRowAt(Tab& t, long cp, long& start, long& end, int& cells) {
    std::wstring before = RawText(t, std::max(0L, cp - 8192), cp);
    size_t rs = before.rfind(kRowStart), re = before.rfind(kRowEnd);
    if (rs == std::wstring::npos || (re != std::wstring::npos && re > rs)) return false;
    start = std::max(0L, cp - 8192) + static_cast<long>(rs);
    std::wstring after = RawText(t, start, std::min(StoryLength(t), start + 65536));
    size_t e = after.find(kRowEnd);
    if (e == std::wstring::npos) return false;
    end = start + static_cast<long>(e) + 1;
    if (RawText(t, end, end + 1) == L"\r") ++end;
    cells = static_cast<int>(std::count(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(e), kCellEnd));
    return true;
}

void DeleteRange(Tab& t, long from, long to) {
    ITextRange* r = nullptr;
    if (SUCCEEDED(t.doc->Range(from, to, &r)) && r) {
        r->Delete(tomCharacter, 0, nullptr);
        r->Release();
    }
}

// Reads every cell of the table occupying [from, to).
std::vector<std::vector<std::wstring>> TableCells(Tab& t, long from, long to) {
    std::vector<std::vector<std::wstring>> rows;
    std::wstring raw = RawText(t, from, to), cell;
    for (wchar_t c : raw) {
        if (c == kRowStart) {
            rows.emplace_back();
        } else if (c == kCellEnd) {
            if (!rows.empty()) rows.back().push_back(cell);
            cell.clear();
        } else if (c == kRowEnd) {
            cell.clear();
        } else if (c == L'\r') {
            if (!cell.empty()) cell.push_back(L' ');
        } else {
            cell.push_back(c);
        }
    }
    return rows;
}

// Replaces the table in [from, to) with one built from cells (columns are rebuilt this way,
// since RichEdit only inserts whole rows).
void RebuildTable(Tab& t, long from, long to, const std::vector<std::vector<std::wstring>>& cells) {
    DeleteRange(t, from, to);
    if (cells.empty() || cells[0].empty()) return;
    ITextRange* ins = nullptr;
    if (SUCCEEDED(t.doc->Range(from, from, &ins)) && ins) {
        BSTR b = SysAllocString(L"\r");
        ins->SetText(b);
        SysFreeString(b);
        ins->Release();
    }
    InsertTableAt(t, from, static_cast<int>(cells.size()), static_cast<int>(cells[0].size()), cells);
    std::wstring after = RawText(t, from, StoryLength(t));
    size_t lastRow = std::wstring::npos;
    for (size_t k = 0, pos = 0; k < cells.size(); ++k) {
        lastRow = after.find(kRowEnd, pos);
        if (lastRow == std::wstring::npos) break;
        pos = lastRow + 1;
    }
    if (lastRow == std::wstring::npos) return;
    long ph = from + static_cast<long>(lastRow) + 1;
    if (RawText(t, ph, ph + 1) == L"\r") ++ph;
    if (ph < StoryLength(t) - 1 && RawText(t, ph, ph + 1) == L"\r") DeleteRange(t, ph, ph + 1);
}

void CmdTableEdit(Tab& t, UINT id) {
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    long start = 0, end = 0;
    int cells = 0;
    if (!TableRowAt(t, cr.cpMin, start, end, cells)) return;
    if (id == IDM_TABLE_ROW_ABOVE || id == IDM_TABLE_ROW_BELOW) {
        InsertTableAt(t, id == IDM_TABLE_ROW_ABOVE ? start : end, 1, std::max(1, cells), {});
        return;
    }
    if (id == IDM_TABLE_DEL_ROW) {
        DeleteRange(t, start, end);
        return;
    }
    // Whole-table operations: find the neighbouring rows of the same table.
    long from = start, to = end, s2, e2;
    int c2;
    while (from > 0 && TableRowAt(t, from - 1, s2, e2, c2) && e2 == from) from = s2;
    while (TableRowAt(t, to + 1, s2, e2, c2) && s2 == to) to = e2;
    if (id == IDM_TABLE_DELETE) {
        DeleteRange(t, from, to);
        return;
    }
    std::wstring inRow = RawText(t, start, cr.cpMin);
    int col = static_cast<int>(std::count(inRow.begin(), inRow.end(), kCellEnd));
    auto table = TableCells(t, from, to);
    for (auto& row : table) {
        int c = std::min<int>(col, static_cast<int>(row.size()));
        if (id == IDM_TABLE_COL_LEFT) row.insert(row.begin() + c, L"");
        if (id == IDM_TABLE_COL_RIGHT) row.insert(row.begin() + std::min<int>(c + 1, static_cast<int>(row.size())), L"");
        if (id == IDM_TABLE_DEL_COL && c < static_cast<int>(row.size())) row.erase(row.begin() + c);
    }
    if (!table.empty() && table[0].empty()) {
        DeleteRange(t, from, to);
        return;
    }
    RebuildTable(t, from, to, table);
}

// ---------------------------------------------------------------------------
// ##text## ASCII art: typing any character after a closing ## turns the text between
// the markers into a banner on its own lines.



// How many monospace columns fit in the editor at the current font size and zoom.
int EditorColumns(Tab& t) {
    RECT rc;
    SendMessageW(t.edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rc));
    int px = MulDiv(g_set.fontSize10, static_cast<int>(g_dpi), 720) * t.zoom / 100;
    HFONT f = MakeFont(std::max(4, px), L"Consolas");
    HDC dc = GetDC(t.edit);
    HGDIOBJ old = SelectObject(dc, f);
    int cw = std::max(1, TextWidth(dc, L"MMMMMMMMMM") / 10);
    SelectObject(dc, old);
    ReleaseDC(t.edit, dc);
    DeleteObject(f);
    return std::max(20, static_cast<int>(rc.right - rc.left) / cw - 1);
}

void SetRangeText(Tab& t, long at, const std::wstring& text) {
    ITextRange* r = nullptr;
    if (SUCCEEDED(t.doc->Range(at, at, &r)) && r) {
        BSTR b = SysAllocString(text.c_str());
        r->SetText(b);
        SysFreeString(b);
        r->Release();
    }
}

// Replaces ##inner## (opening "##" at openAbs, closing "##" at closeAbs) with art.
struct ArtMatch {
    long openAbs = 0, openLen = 2, closeAbs = 0;
    int style = -1, height = -1;  // from #S<n>H<n># codes; -1 = menu setting
};

void ConvertArt(Tab& t, const ArtMatch& m, long caret, bool byEnter) {
    long openAbs = m.openAbs, closeAbs = m.closeAbs;
    std::wstring inner = RawText(t, openAbs + m.openLen, closeAbs);
    ascii::Options o;
    o.style = static_cast<ascii::Style>(m.style >= 0 ? m.style : g_set.asciiStyle);
    o.height = static_cast<ascii::Height>(m.height >= 0 ? m.height : g_set.asciiHeight);
    o.font = kArtFonts[g_set.asciiFont];
    o.center = g_set.asciiCenter;
    o.maxColumns = EditorColumns(t);
    std::vector<std::wstring> lines = ascii::Render(inner, o);
    if (lines.empty()) return;
    std::wstring art;
    for (size_t i = 0; i < lines.size(); ++i) art += (i ? L"\r" : L"") + lines[i];

    SendMessageW(t.edit, EM_STOPGROUPTYPING, 0, 0);
    long artStart = 0;
    if (g_set.asciiPlacement == 2) {
        // The art takes the place of ##text##, on lines of its own; the caret follows it.
        DeleteRange(t, openAbs, closeAbs + 2);
        std::wstring ins = L"\r" + art + L"\r";
        SetRangeText(t, openAbs, ins);
        artStart = openAbs + 1;
        caret = openAbs + static_cast<long>(ins.size());
        if (byEnter) ++caret;
    } else {
        // Default: remove ##text## (or, placement 1, keep its words without the markers),
        // put the art on new lines below the sentence, and continue on a new line after it.
        if (g_set.asciiPlacement == 1) {
            DeleteRange(t, closeAbs, closeAbs + 2);
            DeleteRange(t, openAbs, openAbs + m.openLen);
        } else {
            DeleteRange(t, openAbs, closeAbs + 2);
        }
        long pe = openAbs;
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(openAbs, openAbs, &r)) && r) {
            r->Expand(tomParagraph, nullptr);
            r->GetEnd(&pe);
            r->Release();
            --pe;  // before the paragraph mark
        }
        // After Enter the empty line below already exists; otherwise add one for the caret.
        std::wstring ins = L"\r" + art + (byEnter ? L"" : L"\r");
        SetRangeText(t, pe, ins);
        artStart = pe + 1;
        caret = pe + static_cast<long>(ins.size()) + 1;
    }
    long artEnd = artStart + static_cast<long>(art.size());
    if (t.rich) {
        // Art needs a monospace font and plain paragraphs to line up.
        ITextRange* r = nullptr;
        if (SUCCEEDED(t.doc->Range(artStart, artEnd, &r)) && r) {
            ITextFont* f = nullptr;
            if (SUCCEEDED(r->GetFont(&f)) && f) {
                // Braille dots aren't in Consolas; Segoe UI Symbol has them all at one width.
                BSTR face = SysAllocString(o.style == ascii::Style::Dots ? L"Segoe UI Symbol" : L"Consolas");
                f->SetName(face);
                SysFreeString(face);
                f->SetSize(BaseSizePt());
                f->SetBold(tomFalse);
                f->SetItalic(tomFalse);
                f->SetStrikeThrough(tomFalse);
                f->Release();
            }
            r->Expand(tomParagraph, nullptr);
            ITextPara* p = nullptr;
            if (SUCCEEDED(r->GetPara(&p)) && p) {
                SetParaList(p, md::Block::Body, L'-', 0);
                p->Release();
            }
            r->Release();
        }
    }
    caret = std::min(caret, StoryLength(t) - 1);
    CHARRANGE cr{caret, caret};
    SendMessageW(t.edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    SendMessageW(t.edit, EM_SCROLLCARET, 0, 0);
    SendMessageW(t.edit, EM_STOPGROUPTYPING, 0, 0);
}

// Finds a closed ##text## (or #codes#text##, codes like S3, H2, S3H2) ending exactly at
// closeEnd (just after the closing "##").
bool FindArtMarkers(Tab& t, long closeEnd, ArtMatch& m) {
    long base = std::max(0L, closeEnd - 2000);
    std::wstring s = RawText(t, base, closeEnd);
    if (s.size() < 5 || s.compare(s.size() - 2, 2, L"##") != 0) return false;
    size_t close = s.size() - 2;
    size_t lineStart = s.find_last_of(L"\r\x0007\xFFF9\xFFFB", close);
    lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
    bool found = false;
    for (size_t p = lineStart; p + 2 < close; ++p) {
        if (s[p] != L'#') continue;
        size_t len = 0;
        int style = -1, height = -1;
        if (s[p + 1] == L'#') {
            len = 2;
        } else {
            // #S3#, #H2#, #S3H2#, #H2S3# ...
            size_t j = p + 1;
            bool ok = false;
            while (j < close && wcschr(L"SsHh", s[j])) {
                wchar_t kind = static_cast<wchar_t>(towupper(s[j]));
                size_t d = j + 1;
                int n = 0;
                while (d < close && iswdigit(s[d]) && d - j <= 2) n = n * 10 + (s[d++] - L'0');
                if (d == j + 1) break;
                (kind == L'S' ? style : height) = n;
                j = d;
                ok = true;
            }
            if (!ok || j >= close || s[j] != L'#') continue;
            len = j - p + 1;
        }
        if (p + len >= close) continue;
        std::wstring inner = s.substr(p + len, close - p - len);
        if (inner.front() == L'#' || inner.back() == L'#' || inner.find(L"##") != std::wstring::npos ||
            inner.find_first_not_of(L" \t") == std::wstring::npos)
            continue;
        // Unknown numbers fall back to the menu defaults.
        m.style = style >= 1 && style <= ascii::kStyleCount ? kStyleOrder[style - 1] : -1;
        m.height = height >= 1 && height <= 4 ? height - 1 : -1;
        m.openAbs = base + static_cast<long>(p);
        m.openLen = static_cast<long>(len);
        m.closeAbs = base + static_cast<long>(close);
        found = true;  // keep scanning: the opening closest to the closing ## wins
    }
    return found;
}

// Called after a character was typed: converts when it directly follows a closing ##.
void MaybeConvertArt(Tab& t) {
    if (!g_set.asciiOn || !t.doc) return;
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    if (cr.cpMin != cr.cpMax || cr.cpMin < 6) return;
    ArtMatch m;
    if (FindArtMarkers(t, cr.cpMin - 1, m)) ConvertArt(t, m, cr.cpMin, RawText(t, cr.cpMin - 1, cr.cpMin) == L"\r");
}

// Edit > ASCII art > Convert now: the first closed ##text## on the caret's line.
void ConvertArtOnLine(Tab& t) {
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    ITextRange* r = nullptr;
    long ps = 0, pe = 0;
    if (FAILED(t.doc->Range(cr.cpMin, cr.cpMin, &r)) || !r) return;
    r->Expand(tomParagraph, nullptr);
    r->GetStart(&ps);
    r->GetEnd(&pe);
    r->Release();
    std::wstring s = RawText(t, ps, pe);
    for (size_t close = s.find(L"##", 3); close != std::wstring::npos; close = s.find(L"##", close + 1)) {
        ArtMatch m;
        if (FindArtMarkers(t, ps + static_cast<long>(close) + 2, m)) {
            ConvertArt(t, m, cr.cpMin, false);
            return;
        }
    }
    Notice(L"ASCII art", L"Put the text between hashes on this line, like ##Hello there## or #S3H2#Hello there##, "
                         L"then try again.");
}

std::wstring UrlEncode(const std::wstring& s) {
    std::string utf8;
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    utf8.resize(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), utf8.data(), n, nullptr, nullptr);
    std::wstring out;
    wchar_t hex[4];
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<wchar_t>(c);
        } else if (c == ' ') {
            out += L'+';
        } else {
            swprintf_s(hex, L"%%%02X", c);
            out += hex;
        }
    }
    return out;
}

void SearchWithBing(Tab& t, UINT id) {
    CHARRANGE cr{};
    SendMessageW(t.edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    if (cr.cpMax <= cr.cpMin) {
        // No selection: use the word at the caret, as Notepad does.
        ITextRange* r = nullptr;
        if (t.doc && SUCCEEDED(t.doc->Range(cr.cpMin, cr.cpMin, &r)) && r) {
            r->Expand(tomWord, nullptr);
            r->GetStart(&cr.cpMin);
            r->GetEnd(&cr.cpMax);
            r->Release();
            SendMessageW(t.edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        }
        if (cr.cpMax <= cr.cpMin) return;
    }
    std::wstring sel(static_cast<size_t>(cr.cpMax - cr.cpMin) + 1, L'\0');
    sel.resize(static_cast<size_t>(SendMessageW(t.edit, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(sel.data()))));
    std::replace(sel.begin(), sel.end(), L'\r', L' ');
    if (sel.size() > 500) sel.resize(500);
    bool define = id == IDM_BING_DEFINE ||
                  (id == IDM_BING_AUTO && sel.find(L' ') == std::wstring::npos && sel.size() < 40);
    std::wstring url = L"https://www.bing.com/search?q=" + UrlEncode(define ? L"define " + sel : sel);
    ShellExecuteW(g_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SetMarkdownView(Tab& t, bool syntax) {
    if (t.kind != Kind::Markdown || t.syntax == syntax) return;
    bool modified = IsModified(t);
    if (syntax) {
        std::wstring text = md::Serialize(DocFromEditor(t));
        ResetEditorMode(t, false);
        StreamText(t.edit, text, false);
        t.syntax = true;
    } else {
        std::wstring text = GetEditText(t.edit);
        if (md::Serialize(md::Parse(text)) != md::NormalizeBreaks(text)) {
            if (!Confirm(L"Switch to formatted view?",
                         L"Some Markdown in this document isn't supported in formatted view and would be rewritten.",
                         L"Switch", L"Cancel"))
                return;
        }
        ResetEditorMode(t, true);
        LoadDocIntoEditor(t, md::Parse(text));
        t.syntax = false;
    }
    SendMessageW(t.edit, EM_SETSEL, 0, 0);
    SendMessageW(t.edit, EM_SETMODIFY, modified, 0);
    LayoutMenuRects();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Commands

void InsertTimeDate(Tab& t) {
    wchar_t tbuf[64]{}, dbuf[64]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, tbuf, 64);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, dbuf, 64, nullptr);
    std::wstring s = std::wstring(tbuf) + L" " + dbuf;
    SendMessageW(t.edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(s.c_str()));
}

void OnCommand(UINT id) {
    Tab* t = Cur();
    if (id >= IDM_AI_BASE && id < IDM_AI_BASE + 20) {
        Notice(L"Writing tools aren't available",
               L"Write, Rewrite, Summarize and the tone and format options use Microsoft's Copilot service and a "
               L"Microsoft account. This replica shows the same menus, but it can't reach that service.");
        return;
    }
    if (id >= IDM_RECENT_BASE && id < IDM_RECENT_BASE + kMaxRecent) {
        size_t i = id - IDM_RECENT_BASE;
        if (i >= g_set.recent.size()) return;
        std::wstring p = g_set.recent[i];
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (Confirm(L"File not found", p + L"\n\nThis file no longer exists. Remove it from the recent list?",
                        L"Remove", L"Cancel")) {
                g_set.recent.erase(g_set.recent.begin() + static_cast<std::ptrdiff_t>(i));
                SaveRecent();
            }
            return;
        }
        OpenOrRoute(p);
        return;
    }
    switch (id) {
    case IDM_NEWTAB: NewTab(); break;
    case IDM_NEWMDTAB: NewTab(Kind::Markdown); break;
    case IDM_WHATSNEW:
        g_whatsNewSeen = true;
        InvalidateMenu();
        dlg::ShowWhatsNew(g_hwnd);
        break;
    case IDM_ACCOUNT: {
        std::vector<flyout::Item> v(2);
        v[0].kind = flyout::Kind::Header;
        v[0].text = L"Not signed in";
        v[1].id = IDM_SIGNIN;
        v[1].icon = glyph::Person;
        v[1].text = L"Sign in with a Microsoft account";
        RECT a = ClientToScreenRect(g_menuRc[kAvatar]);
        flyout::Options o;
        o.minWidth = S(300);
        int cx = (a.left + a.right) / 2;
        HandleFlyoutResult(flyout::Show(g_hwnd, v, {cx - S(150), a.top, cx - S(150), a.bottom}, o));
        break;
    }
    case IDM_SIGNIN:
        Notice(L"Sign in isn't available",
               L"Signing in uses Microsoft's account service, which only the real Notepad can reach. This replica "
               L"shows the same button but can't sign you in.");
        break;
    case IDM_SPELL_TOGGLE:
        g_set.spellCheck = !g_set.spellCheck;
        ApplySettings();
        break;
    case IDM_ASCII_TOGGLE: g_set.asciiOn = !g_set.asciiOn; SaveSettings(); break;
    case IDM_ASCII_CENTER:
    case IDM_ASCII_LEFT: g_set.asciiCenter = id == IDM_ASCII_CENTER; SaveSettings(); break;
    case IDM_ASCII_REMOVE: g_set.asciiPlacement = 0; SaveSettings(); break;
    case IDM_ASCII_KEEP: g_set.asciiPlacement = 1; SaveSettings(); break;
    case IDM_ASCII_REPLACE: g_set.asciiPlacement = 2; SaveSettings(); break;
    case IDM_ASCII_CONVERT: if (t) ConvertArtOnLine(*t); break;
    case IDM_ASCII_HEIGHT_BASE:
    case IDM_ASCII_HEIGHT_BASE + 1:
    case IDM_ASCII_HEIGHT_BASE + 2:
    case IDM_ASCII_HEIGHT_BASE + 3: g_set.asciiHeight = static_cast<int>(id - IDM_ASCII_HEIGHT_BASE); SaveSettings(); break;
    case IDM_ASCII_STYLE_BASE:
    case IDM_ASCII_STYLE_BASE + 1:
    case IDM_ASCII_STYLE_BASE + 2:
    case IDM_ASCII_STYLE_BASE + 3:
    case IDM_ASCII_STYLE_BASE + 4:
    case IDM_ASCII_STYLE_BASE + 5:
    case IDM_ASCII_STYLE_BASE + 6:
    case IDM_ASCII_STYLE_BASE + 7: g_set.asciiStyle = static_cast<int>(id - IDM_ASCII_STYLE_BASE); SaveSettings(); break;
    case IDM_ASCII_FONT_BASE:
    case IDM_ASCII_FONT_BASE + 1:
    case IDM_ASCII_FONT_BASE + 2:
    case IDM_ASCII_FONT_BASE + 3:
    case IDM_ASCII_FONT_BASE + 4:
    case IDM_ASCII_FONT_BASE + 5:
    case IDM_ASCII_FONT_BASE + 6: g_set.asciiFont = static_cast<int>(id - IDM_ASCII_FONT_BASE); SaveSettings(); break;
    case IDM_CLOSEOTHERS:
    case IDM_CLOSERIGHT: {
        int keep = g_active;
        for (int i = static_cast<int>(g_tabs.size()) - 1; i >= 0; --i) {
            if (i == keep || (id == IDM_CLOSERIGHT && i < keep)) continue;
            Tab* target = g_tabs[i].get();
            CloseTab(i);
            if (i < static_cast<int>(g_tabs.size()) && g_tabs[i].get() == target) break;  // cancelled
            if (i < keep) --keep;
        }
        if (keep >= 0 && keep < static_cast<int>(g_tabs.size())) ActivateTab(keep);
        break;
    }
    case IDM_NEWWINDOW: LaunchNewWindow(L""); break;
    case IDM_OPEN: CmdOpen(); break;
    case IDM_SAVE: if (t) SaveTab(*t, false); break;
    case IDM_SAVEAS: if (t) SaveTab(*t, true); break;
    case IDM_SAVEALL: SaveAll(); break;
    case IDM_PAGESETUP: CmdPageSetup(); break;
    case IDM_PRINT: CmdPrint(); break;
    case IDM_CLOSETAB: if (g_active >= 0) CloseTab(g_active); break;
    case IDM_CLOSEWINDOW:
    case IDM_EXIT: PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
    case IDM_CLEARRECENT:
        g_set.recent.clear();
        SaveRecent();
        break;
    case IDM_SETTINGS: CmdSettings(); break;
    case IDM_NEXTTAB:
    case IDM_PREVTAB:
        if (!g_tabs.empty()) {
            int n = static_cast<int>(g_tabs.size());
            ActivateTab((g_active + (id == IDM_NEXTTAB ? 1 : n - 1)) % n);
        }
        break;
    case IDM_FIND: ShowFind(false); break;
    case IDM_REPLACE: ShowFind(true); break;
    case IDM_FINDNEXT: FindNext(true); break;
    case IDM_FINDPREV: FindNext(false); break;
    case IDM_GOTO: CmdGoto(); break;
    case IDM_FONT:  // Notepad opens Settings with the Font card expanded
        g_expFont = true;
        OpenSettingsPage();
        break;
    case IDM_STATUSBAR:
        g_set.statusBar = !g_set.statusBar;
        ApplySettings();
        break;
    case IDM_WORDWRAP:
        g_set.wordWrap = !g_set.wordWrap;
        ApplySettings();
        break;
    default:
        break;
    }
    if (!t) return;
    HWND e = t->edit;
    switch (id) {
    case IDM_UNDO: SendMessageW(e, EM_UNDO, 0, 0); break;
    case IDM_CUT: SendMessageW(e, WM_CUT, 0, 0); break;
    case IDM_COPY: SendMessageW(e, WM_COPY, 0, 0); break;
    case IDM_PASTE: SendMessageW(e, EM_PASTESPECIAL, CF_UNICODETEXT, 0); break;
    case IDM_DELETE: {
        CHARRANGE cr{};
        SendMessageW(e, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        if (cr.cpMax == cr.cpMin) {
            cr.cpMax = cr.cpMin + 1;
            SendMessageW(e, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
        }
        SendMessageW(e, WM_CLEAR, 0, 0);
        break;
    }
    case IDM_SELECTALL: SendMessageW(e, EM_SETSEL, 0, -1); break;
    case IDM_TIMEDATE: InsertTimeDate(*t); break;
    case IDM_RTL: {
        PARAFORMAT2 pf{};
        pf.cbSize = sizeof pf;
        SendMessageW(e, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
        bool rtl = (pf.wEffects & PFE_RTLPARA) != 0;
        pf.dwMask = PFM_RTLPARA | PFM_ALIGNMENT;
        pf.wEffects = rtl ? 0 : PFE_RTLPARA;
        pf.wAlignment = rtl ? PFA_LEFT : PFA_RIGHT;
        SendMessageW(e, EM_SETSEL, 0, -1);
        SendMessageW(e, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
        SendMessageW(e, EM_SETSEL, 0, 0);
        break;
    }
    case IDM_BOLD: ToggleFont(*t, 0); break;
    case IDM_ITALIC: ToggleFont(*t, 1); break;
    case IDM_STRIKE: ToggleFont(*t, 2); break;
    case IDM_LINK: CmdLink(*t); break;
    case IDM_CLEARFMT: ClearFormatting(*t); break;
    case IDM_LIST_BULLET: ApplyList(*t, false); break;
    case IDM_LIST_NUMBER: ApplyList(*t, true); break;
    case IDM_INDENT_INC: ChangeIndent(*t, 1); break;
    case IDM_INDENT_DEC: ChangeIndent(*t, -1); break;
    case IDM_TABLE_INSERT: CmdInsertTable(*t); break;
    case IDM_TABLE_ROW_ABOVE:
    case IDM_TABLE_ROW_BELOW:
    case IDM_TABLE_DEL_ROW:
    case IDM_TABLE_DELETE: CmdTableEdit(*t, id); break;
    case IDM_BING_SEARCH:
    case IDM_BING_DEFINE:
    case IDM_BING_AUTO: SearchWithBing(*t, id); break;
    case IDM_MD_FORMATTED: SetMarkdownView(*t, false); break;
    case IDM_MD_SYNTAX: SetMarkdownView(*t, true); break;
    case IDM_HEADING_BASE:
    case IDM_HEADING_BASE + 1:
    case IDM_HEADING_BASE + 2:
    case IDM_HEADING_BASE + 3:
    case IDM_HEADING_BASE + 4:
    case IDM_HEADING_BASE + 5:
    case IDM_HEADING_BASE + 6: ApplyHeading(*t, static_cast<int>(id - IDM_HEADING_BASE)); break;
    case IDM_UCC_BASE:
    case IDM_UCC_BASE + 1:
    case IDM_UCC_BASE + 2:
    case IDM_UCC_BASE + 3:
    case IDM_UCC_BASE + 4:
    case IDM_UCC_BASE + 5:
    case IDM_UCC_BASE + 6:
    case IDM_UCC_BASE + 7:
    case IDM_UCC_BASE + 8: {
        static const wchar_t ucc[9] = {0x200E, 0x200F, 0x200D, 0x200C, 0x202A, 0x202B, 0x202D, 0x202E, 0x202C};
        wchar_t s[2] = {ucc[id - IDM_UCC_BASE], 0};
        SendMessageW(e, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(s));
        break;
    }
    case IDM_ZOOMIN: SetZoom(*t, t->zoom + 10); break;
    case IDM_ZOOMOUT: SetZoom(*t, t->zoom - 10); break;
    case IDM_ZOOMRESET: SetZoom(*t, 100); break;
    case IDM_ENC_UTF8:
    case IDM_ENC_UTF8BOM:
    case IDM_ENC_UTF16LE:
    case IDM_ENC_UTF16BE:
    case IDM_ENC_ANSI:
        t->enc = static_cast<Encoding>(id - IDM_ENC_UTF8);
        SendMessageW(e, EM_SETMODIFY, TRUE, 0);
        MarkModifiedChanged(*t);
        InvalidateStatus();
        break;
    case IDM_EOL_CRLF:
    case IDM_EOL_LF:
    case IDM_EOL_CR:
        t->eol = static_cast<Eol>(id - IDM_EOL_CRLF);
        SendMessageW(e, EM_SETMODIFY, TRUE, 0);
        MarkModifiedChanged(*t);
        InvalidateStatus();
        break;
    default:
        return;
    }
    if (GetFocus() != g_findEdit && GetFocus() != g_replEdit) SetFocus(e);
}

LRESULT CALLBACK EditSubclass(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            if (Tab* t = Cur()) SetZoom(*t, t->zoom + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 10 : -10));
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        ShowEditorContextMenu(h, lp);
        return 0;
    case WM_PASTE:
        // Paste as plain text; formatting comes only from Notepad's own tools.
        return DefSubclassProc(h, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
    case WM_KEYDOWN: {
        if (wp == VK_ESCAPE && g_find && IsWindowVisible(g_find)) {
            HideFind();
            return 0;
        }
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        if (wp == VK_RETURN && !ctrl) {
            // RichEdit inserts the paragraph on key-down, so Enter is handled here: it can end
            // a ##text## (ASCII art), and a new paragraph after a heading starts as body text.
            Tab* t = Cur();
            if (t && t->edit == h) {
                CHARRANGE before{};
                SendMessageW(h, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before));
                bool hashes = g_set.asciiOn && before.cpMin == before.cpMax && before.cpMin >= 5 &&
                              RawText(*t, before.cpMin - 2, before.cpMin) == L"##";
                LRESULT r = DefSubclassProc(h, m, wp, lp);
                if (hashes) {
                    MaybeConvertArt(*t);
                } else if (t->rich) {
                    CHARRANGE cr{};
                    SendMessageW(h, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
                    long style = ParaStyle(*t, cr.cpMin);
                    if (style > kStyleHeading && style <= kStyleHeading + 6 &&
                        RawText(*t, cr.cpMin, cr.cpMin + 1) == L"\r")
                        ApplyHeading(*t, 0);
                }
                return r;
            }
        }
        if ((ctrl && wp == 'V') || (shift && wp == VK_INSERT)) {
            SendMessageW(h, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
            return 0;
        }
        // RichEdit's own formatting shortcuts (alignment, line spacing, underline, sub/superscript,
        // font size, list cycling, bold/italic in plain files) produce formatting Notepad can't save.
        if (ctrl) {
            switch (wp) {
            case 'L': case 'R': case 'E': case 'J': case 'U': case '1': case '2': case '5':
            case VK_OEM_PLUS: case VK_OEM_PERIOD: case VK_OEM_COMMA:
                return 0;
            case 'B': case 'I':
                return 0;  // handled by the accelerator table for formatted tabs
            case 'A':
                if (shift) return 0;  // all caps
                break;
            }
        }
        break;
    }
    case WM_CHAR:
        if (wp != L'#' && (wp >= 0x20 || wp == L'\r' || wp == L'\t')) {
            // Any key typed right after a closing ## converts ##text## to ASCII art.
            Tab* t = Cur();
            if (t && t->edit == h && g_set.asciiOn) {
                CHARRANGE before{};
                SendMessageW(h, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before));
                if (before.cpMin >= 5 && RawText(*t, before.cpMin - 2, before.cpMin) == L"##") {
                    LRESULT r = DefSubclassProc(h, m, wp, lp);
                    MaybeConvertArt(*t);
                    return r;
                }
            }
        }
        if (wp == L'\r') {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            // A new paragraph after a heading starts as body text.
            Tab* t = Cur();
            if (t && t->rich && t->edit == h) {
                CHARRANGE cr{};
                SendMessageW(h, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
                long style = ParaStyle(*t, cr.cpMin);
                if (style > kStyleHeading && style <= kStyleHeading + 6 && RawText(*t, cr.cpMin, cr.cpMin + 1) == L"\r")
                    ApplyHeading(*t, 0);
            }
            return r;
        }
        break;
    }
    return DefSubclassProc(h, m, wp, lp);
}

// ---------------------------------------------------------------------------
// Session restore ("Continue previous session")
//
// %LOCALAPPDATA%\ReaNotepad\session\session.ini lists the tabs; tabs with unsaved
// content get a tabN.txt next to it (UTF-16, Markdown when the tab is formatted).

std::wstring SessionDir() {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) dir = base;
    CoTaskMemFree(base);
    return dir + L"\\ReaNotepad\\session";
}

void DeleteDirFiles(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void ClearSession() {
    std::wstring dir = SessionDir();
    DeleteDirFiles(dir);
    RemoveDirectoryW(dir.c_str());
}

void SaveSession() {
    std::wstring dir = SessionDir();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    DeleteDirFiles(dir);
    std::wstring ini = dir + L"\\session.ini";
    int n = 0, active = 0;
    for (size_t i = 0; i < g_tabs.size(); ++i) {
        Tab& t = *g_tabs[i];
        if (IsPristine(t)) continue;
        std::wstring sec = L"tab" + std::to_wstring(n);
        auto put = [&](const wchar_t* key, const std::wstring& v) {
            WritePrivateProfileStringW(sec.c_str(), key, v.c_str(), ini.c_str());
        };
        bool modified = IsModified(t);
        put(L"path", t.path);
        put(L"enc", std::to_wstring(static_cast<int>(t.enc)));
        put(L"eol", std::to_wstring(static_cast<int>(t.eol)));
        put(L"kind", std::to_wstring(static_cast<int>(t.kind)));
        put(L"syntax", t.syntax ? L"1" : L"0");
        put(L"zoom", std::to_wstring(t.zoom));
        put(L"modified", modified ? L"1" : L"0");
        if (modified || t.path.empty()) {
            std::wstring content = t.rich ? md::Serialize(DocFromEditor(t)) : GetEditText(t.edit);
            WriteAllBytes(dir + L"\\" + sec + L".txt", Encode(content, Encoding::Utf16LE));
            put(L"markdown", t.rich ? L"1" : L"0");
        }
        if (static_cast<int>(i) == g_active) active = n;
        ++n;
    }
    WritePrivateProfileStringW(L"session", L"count", std::to_wstring(n).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"session", L"active", std::to_wstring(active).c_str(), ini.c_str());
}

// Restores the last session; returns false when there was none. The folder is moved
// aside first so a second window started at the same time doesn't restore it again.
bool RestoreSession() {
    std::wstring dir = SessionDir(), taken = dir + L".restoring";
    DeleteDirFiles(taken);
    RemoveDirectoryW(taken.c_str());
    if (!MoveFileExW(dir.c_str(), taken.c_str(), 0)) return false;
    std::wstring ini = taken + L"\\session.ini";
    int n = static_cast<int>(GetPrivateProfileIntW(L"session", L"count", 0, ini.c_str()));
    int active = static_cast<int>(GetPrivateProfileIntW(L"session", L"active", 0, ini.c_str()));
    for (int i = 0; i < n; ++i) {
        std::wstring sec = L"tab" + std::to_wstring(i);
        wchar_t buf[4096];
        GetPrivateProfileStringW(sec.c_str(), L"path", L"", buf, 4096, ini.c_str());
        std::wstring path = buf;
        auto num = [&](const wchar_t* key, int def) {
            return static_cast<int>(GetPrivateProfileIntW(sec.c_str(), key, def, ini.c_str()));
        };
        std::string bytes;
        bool hasContent = ReadAllBytes(taken + L"\\" + sec + L".txt", bytes);
        if (!hasContent) {
            if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) OpenPath(path);
            continue;
        }
        std::wstring text;
        Decode(bytes, text);
        Kind kind = static_cast<Kind>(std::clamp(num(L"kind", 0), 0, 2));
        Tab* t = Cur();
        if (!t || !IsPristine(*t)) t = NewTab(kind);
        if (!t) continue;
        t->kind = kind;
        t->path = path;
        t->enc = static_cast<Encoding>(std::clamp(num(L"enc", 0), 0, 4));
        t->eol = static_cast<Eol>(std::clamp(num(L"eol", 0), 0, 2));
        t->syntax = num(L"syntax", 0) != 0;
        if (num(L"markdown", 0) && kind == Kind::Text && g_set.formatting) {
            ResetEditorMode(*t, true);
            LoadDocIntoEditor(*t, md::Parse(text));
        } else {
            FillTab(*t, text);
        }
        FinishLoad(*t);
        if (num(L"modified", 1)) SendMessageW(t->edit, EM_SETMODIFY, TRUE, 0);
        t->shownModified = IsModified(*t);
        SetZoom(*t, num(L"zoom", 100));
    }
    DeleteDirFiles(taken);
    RemoveDirectoryW(taken.c_str());
    if (active >= 0 && active < static_cast<int>(g_tabs.size())) ActivateTab(active);
    return n > 0;
}

// ---------------------------------------------------------------------------
// Settings page: a full-window page like Notepad's (back arrow in the title bar, cards
// with expanders, toggles and combo boxes on the left, "About this app" on the right).
// Geometry is from the UI Automation capture at 100% scale.

constexpr wchar_t kSettingsClass[] = L"ReaNotepadSettings";
int g_setScroll = 0;
int g_setHot = 0;
std::vector<std::wstring> g_fontFamilies;

enum SetId {
    SID_NONE,
    SID_THEME_EXP, SID_THEME_LIGHT, SID_THEME_DARK, SID_THEME_SYS,
    SID_FONT_EXP, SID_FAMILY, SID_STYLE, SID_SIZE,
    SID_WORDWRAP, SID_FORMATTING, SID_OPENFILES,
    SID_START_EXP, SID_START_CONTINUE, SID_START_NEW,
    SID_RECENT, SID_SPELL_EXP, SID_SPELL, SID_SPELL_EXT0,
    SID_AUTOCORRECT = SID_SPELL_EXT0 + 5, SID_WRITING,
    SID_LINK_FOLDER, SID_LINK_NOTES, SID_FEEDBACK, SID_HELP,
};

enum class SType { Title, Section, Card, Header, Radio, ComboRow, Sample, ToggleRow, Text, Link, Button };
enum { CTL_NONE, CTL_CHEVRON, CTL_TOGGLE, CTL_COMBO, CTL_TOGGLE_CHEVRON };

struct SElem {
    SType type;
    int id = SID_NONE;
    RECT rc{};
    std::wstring text, sub, icon, value;
    int ctl = CTL_NONE;
    bool on = false;
    bool expanded = false;
};

std::wstring FontStyleName() {
    return g_set.bold && g_set.italic ? L"Bold Italic" : g_set.bold ? L"Bold" : g_set.italic ? L"Italic" : L"Regular";
}

std::vector<SElem> BuildSettings(int width, int& height) {
    std::vector<SElem> v;
    bool wide = width >= S(900);
    int x0 = wide ? S(58) : S(36);
    int colW = wide ? S(542) : std::max<int>(S(320), width - 2 * x0);
    int right = x0 + colW;
    v.push_back({SType::Title, SID_NONE, {x0, S(32), right, S(84)}, L"Settings"});
    int y = S(106);
    auto section = [&](const wchar_t* name) {
        v.push_back({SType::Section, SID_NONE, {x0, y, right, y + S(20)}, name});
        y += S(32);
    };
    // A card: header row (66px) plus optional sub rows; returns nothing, advances y.
    struct Row {
        SType type;
        int id;
        std::wstring text, value;
        bool on;
        int h;
    };
    auto card = [&](int id, const wchar_t* icon, const wchar_t* title, const wchar_t* sub, int ctl, bool on,
                    const std::wstring& value, bool expanded, const std::vector<Row>& rows) {
        int h = S(66);
        if (expanded)
            for (const Row& r : rows) h += r.h;
        if (expanded && !rows.empty()) h += S(8);
        v.push_back({SType::Card, SID_NONE, {x0, y, right, y + h}});
        SElem head{SType::Header, id, {x0, y, right, y + S(66)}, title, sub, icon, value, ctl, on, expanded};
        v.push_back(head);
        int ry = y + S(66);
        if (expanded)
            for (const Row& r : rows) {
                v.push_back({r.type, r.id, {x0, ry, right, ry + r.h}, r.text, L"", L"", r.value, CTL_NONE, r.on});
                ry += r.h;
            }
        y += h + S(6);
    };

    section(L"Appearance");
    card(SID_THEME_EXP, glyph::Theme, L"App theme", L"Select which app theme to display", CTL_CHEVRON, false, L"",
         g_expTheme,
         {{SType::Radio, SID_THEME_LIGHT, L"Light", L"", g_set.theme == ThemeMode::Light, S(40)},
          {SType::Radio, SID_THEME_DARK, L"Dark", L"", g_set.theme == ThemeMode::Dark, S(40)},
          {SType::Radio, SID_THEME_SYS, L"Use system setting", L"", g_set.theme == ThemeMode::System, S(40)}});
    y += S(7);
    section(L"Text Formatting");
    wchar_t size[16];
    swprintf_s(size, L"%d", g_set.fontSize10 / 10);
    card(SID_FONT_EXP, glyph::Font, L"Font", L"", CTL_CHEVRON, false, L"", g_expFont,
         {{SType::ComboRow, SID_FAMILY, L"Family", g_set.fontFace, false, S(57)},
          {SType::ComboRow, SID_STYLE, L"Style", FontStyleName(), false, S(57)},
          {SType::ComboRow, SID_SIZE, L"Size", size, false, S(57)},
          {SType::Sample, SID_NONE, L"The sound of ocean waves calms my soul.", L"", false, S(77)}});
    card(SID_WORDWRAP, glyph::WordWrap, L"Word wrap", L"Fit text within window by default", CTL_TOGGLE,
         g_set.wordWrap, L"", false, {});
    card(SID_FORMATTING, glyph::Eraser, L"Formatting", L"", CTL_TOGGLE, g_set.formatting, L"", false, {});
    y += S(7);
    section(L"Opening Notepad");
    card(SID_OPENFILES, glyph::OpenFile, L"Opening files", L"Choose where your files are opened", CTL_COMBO, false,
         g_set.openInNewWindow ? L"Open in a new window" : L"Open in a new tab", false, {});
    card(SID_START_EXP, glyph::Window, L"When Notepad starts", L"", CTL_CHEVRON, false, L"", g_expStart,
         {{SType::Radio, SID_START_CONTINUE, L"Continue previous session", L"", g_set.continueSession, S(40)},
          {SType::Radio, SID_START_NEW, L"Start new session and discard unsaved changes", L"", !g_set.continueSession,
           S(40)}});
    card(SID_RECENT, glyph::History, L"Recent Files", L"", CTL_TOGGLE, g_set.recentEnabled, L"", false, {});
    y += S(7);
    section(L"Spelling");
    static const wchar_t* exts[5] = {L".txt", L".md / .markdown", L".srt / .ass", L".lrc", L".lic"};
    std::vector<Row> extRows;
    for (int i = 0; i < 5; ++i)
        extRows.push_back({SType::ToggleRow, SID_SPELL_EXT0 + i, exts[i], L"", g_set.spellExt[i], S(48)});
    card(SID_SPELL_EXP, glyph::Spell, L"Spell check", L"", CTL_TOGGLE_CHEVRON, g_set.spellCheck, L"", g_expSpell,
         extRows);
    card(SID_AUTOCORRECT, glyph::PenLines, L"Autocorrect",
         L"Typos are automatically corrected when spell check is turned on", CTL_TOGGLE, g_set.autocorrect, L"",
         false, {});
    y += S(7);
    section(L"Advanced Features");
    card(SID_WRITING, glyph::Pen, L"Writing tools", L"Needs a Microsoft account (not available in this replica)",
         CTL_TOGGLE, g_set.writingTools, L"", false, {});
    height = y + S(24);

    // About column: to the right when wide, otherwise below.
    int ax = wide ? right + S(56) : x0;
    int ay = wide ? S(106) : height;
    v.push_back({SType::Section, SID_NONE, {ax, ay, ax + S(300), ay + S(20)}, L"About this app"});
    v.push_back({SType::Text, SID_NONE, {ax, ay + S(28), ax + S(320), ay + S(70)},
                 L"Notepad-- 1.2\r\nA Notepad lookalike written from scratch; not affiliated with Microsoft."});
    v.push_back({SType::Link, SID_LINK_FOLDER, {ax, ay + S(87), ax + S(300), ay + S(107)}, L"Open the project folder"});
    v.push_back({SType::Link, SID_LINK_NOTES, {ax, ay + S(121), ax + S(300), ay + S(141)},
                 L"Analysis notes and feature checklist"});
    v.push_back({SType::Button, SID_FEEDBACK, {ax, ay + S(181), ax + S(120), ay + S(211)}, L"Send feedback"});
    v.push_back({SType::Button, SID_HELP, {ax, ay + S(227), ax + S(120), ay + S(258)}, L"Help"});
    if (!wide) height = ay + S(290);
    return v;
}

void DrawToggle(HDC dc, int right, int cy, bool on, bool hot) {
    RECT tr{right - S(85), cy - S(10), right - S(45), cy + S(10)};
    if (on) {
        FillRound(dc, tr, g_pal.accent, S(20));
        int kx = tr.right - S(10);
        FillRound(dc, {kx - S(6), cy - S(6), kx + S(6), cy + S(6)}, g_pal.accentText, S(12));
    } else {
        FrameRound(dc, tr, g_pal.textMuted, hot ? g_pal.flyoutHover : g_pal.card, S(20));
        int kx = tr.left + S(10);
        FillRound(dc, {kx - S(5), cy - S(5), kx + S(5), cy + S(5)}, g_pal.textMuted, S(10));
    }
    DrawTextIn(dc, on ? L"On" : L"Off", {right - S(33), cy - S(10), right - S(8), cy + S(10)}, g_pal.text, g_uiFont,
               DT_SINGLELINE | DT_VCENTER);
}

void DrawCombo(HDC dc, RECT r, const std::wstring& value, bool hot) {
    FrameRound(dc, r, g_pal.inputBorder, hot ? g_pal.flyoutHover : g_pal.input, S(8));
    DrawTextIn(dc, value, {r.left + S(12), r.top, r.right - S(32), r.bottom}, g_pal.text, g_uiFont,
               DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    DrawTextIn(dc, glyph::ChevronDown, {r.right - S(30), r.top, r.right - S(10), r.bottom}, g_pal.text,
               g_iconFontSmall, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

RECT ComboRect(const SElem& e) {
    int cy = (e.rc.top + e.rc.bottom) / 2;
    if (e.type == SType::ComboRow) return {e.rc.left + S(291), cy - S(16), e.rc.left + S(489), cy + S(16)};
    return {e.rc.right - S(177), cy - S(16), e.rc.right - S(13), cy + S(16)};
}

void PaintSettings(HWND h, HDC hdc) {
    RECT rc;
    GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(dc, bmp);
    FillSolid(dc, rc, g_pal.editor);
    int height = 0;
    std::vector<SElem> els = BuildSettings(rc.right, height);
    const UINT left = DT_SINGLELINE | DT_VCENTER;
    for (SElem& e : els) {
        OffsetRect(&e.rc, 0, -g_setScroll);
        if (e.rc.bottom < 0 || e.rc.top > rc.bottom) continue;
        bool hot = e.id != SID_NONE && e.id == g_setHot;
        int cy = (e.rc.top + e.rc.bottom) / 2;
        switch (e.type) {
        case SType::Title:
            DrawTextIn(dc, e.text, e.rc, g_pal.text, g_pageTitleFont, left);
            break;
        case SType::Section:
            DrawTextIn(dc, e.text, e.rc, g_pal.text, g_semiboldFont, left);
            break;
        case SType::Card:
            FrameRound(dc, e.rc, g_pal.cardBorder, g_pal.card, S(8));
            break;
        case SType::Header: {
            if (hot && (e.ctl == CTL_CHEVRON || e.ctl == CTL_TOGGLE_CHEVRON)) {
                RECT hl = e.rc;
                InflateRect(&hl, -S(1), -S(1));
                FillRound(dc, hl, g_pal.flyoutHover, S(8));
            }
            DrawTextIn(dc, e.icon, {e.rc.left + S(16), e.rc.top, e.rc.left + S(36), e.rc.bottom}, g_pal.text,
                       g_iconFont, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            if (e.sub.empty()) {
                DrawTextIn(dc, e.text, {e.rc.left + S(49), e.rc.top, e.rc.right - S(200), e.rc.bottom}, g_pal.text,
                           g_uiFont, left);
            } else {
                DrawTextIn(dc, e.text, {e.rc.left + S(49), cy - S(19), e.rc.right - S(100), cy + S(1)}, g_pal.text,
                           g_uiFont, DT_SINGLELINE | DT_BOTTOM);
                DrawTextIn(dc, e.sub, {e.rc.left + S(49), cy + S(1), e.rc.right - S(100), cy + S(19)},
                           g_pal.textMuted, g_smallFont, DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS);
            }
            int r = e.rc.right;
            if (e.ctl == CTL_TOGGLE || e.ctl == CTL_TOGGLE_CHEVRON)
                DrawToggle(dc, e.ctl == CTL_TOGGLE_CHEVRON ? r - S(40) : r, cy, e.on, hot);
            if (e.ctl == CTL_CHEVRON || e.ctl == CTL_TOGGLE_CHEVRON)
                DrawTextIn(dc, e.expanded ? glyph::ChevronUp : glyph::ChevronDown, {r - S(36), cy - S(10), r - S(16), cy + S(10)},
                           g_pal.text, g_iconFontSmall, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            if (e.ctl == CTL_COMBO) DrawCombo(dc, ComboRect(e), e.value, hot);
            if (e.expanded) FillSolid(dc, {e.rc.left + 1, e.rc.bottom - 1, e.rc.right - 1, e.rc.bottom}, g_pal.cardBorder);
            break;
        }
        case SType::Radio: {
            int cx = e.rc.left + S(59);
            RECT c{cx - S(10), cy - S(10), cx + S(10), cy + S(10)};
            if (e.on) {
                FillRound(dc, c, g_pal.accent, S(20));
                FillRound(dc, {cx - S(4), cy - S(4), cx + S(4), cy + S(4)}, g_pal.accentText, S(8));
            } else {
                FrameRound(dc, c, g_pal.textMuted, hot ? g_pal.flyoutHover : g_pal.card, S(20));
            }
            DrawTextIn(dc, e.text, {e.rc.left + S(77), e.rc.top, e.rc.right - S(16), e.rc.bottom}, g_pal.text,
                       g_uiFont, left);
            break;
        }
        case SType::ComboRow:
            FillSolid(dc, {e.rc.left + 1, e.rc.top, e.rc.right - 1, e.rc.top + 1}, g_pal.cardBorder);
            DrawTextIn(dc, e.text, {e.rc.left + S(49), e.rc.top, e.rc.left + S(280), e.rc.bottom}, g_pal.text,
                       g_uiFont, left);
            DrawCombo(dc, ComboRect(e), e.value, hot);
            break;
        case SType::Sample: {
            FillSolid(dc, {e.rc.left + 1, e.rc.top, e.rc.right - 1, e.rc.top + 1}, g_pal.cardBorder);
            HFONT f = MakeFont(MulDiv(g_set.fontSize10, static_cast<int>(g_dpi), 720), g_set.fontFace.c_str(),
                               g_set.bold ? FW_BOLD : FW_NORMAL, g_set.italic);
            DrawTextIn(dc, e.text, e.rc, g_pal.text, f, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            DeleteObject(f);
            break;
        }
        case SType::ToggleRow:
            FillSolid(dc, {e.rc.left + 1, e.rc.top, e.rc.right - 1, e.rc.top + 1}, g_pal.cardBorder);
            DrawTextIn(dc, e.text, {e.rc.left + S(49), e.rc.top, e.rc.right - S(100), e.rc.bottom}, g_pal.text,
                       g_uiFont, left);
            DrawToggle(dc, e.rc.right - S(40), cy, e.on, hot);
            break;
        case SType::Text:
            DrawTextIn(dc, e.text, e.rc, g_pal.text, g_uiFont, DT_WORDBREAK);
            break;
        case SType::Link:
            DrawTextIn(dc, e.text, e.rc, g_pal.accent, g_uiFont, left);
            if (hot) FillSolid(dc, {e.rc.left, e.rc.bottom - 1, e.rc.left + TextWidth(dc, e.text), e.rc.bottom}, g_pal.accent);
            break;
        case SType::Button:
            FrameRound(dc, e.rc, g_pal.inputBorder, hot ? g_pal.flyoutHover : g_pal.input, S(8));
            DrawTextIn(dc, e.text, e.rc, g_pal.text, g_uiFont, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            break;
        }
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
}

int SettingsHit(HWND h, POINT pt, SElem* out = nullptr) {
    RECT rc;
    GetClientRect(h, &rc);
    int height = 0;
    pt.y += g_setScroll;
    for (const SElem& e : BuildSettings(rc.right, height)) {
        if (e.id == SID_NONE || !PtInRect(&e.rc, pt)) continue;
        if (out) *out = e;
        return e.id;
    }
    return SID_NONE;
}

void ShowSettingsCombo(HWND h, const SElem& e) {
    RECT cr = ComboRect(e);
    OffsetRect(&cr, 0, -g_setScroll);
    MapWindowPoints(h, nullptr, reinterpret_cast<POINT*>(&cr), 2);
    std::vector<flyout::Item> v;
    auto add = [&](UINT id, const std::wstring& text, bool current) {
        flyout::Item it;
        it.id = id;
        it.text = text;
        it.current = current;
        v.push_back(it);
    };
    static const int sizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
    static const wchar_t* styles[4] = {L"Regular", L"Italic", L"Bold", L"Bold Italic"};
    if (e.id == SID_FAMILY) {
        if (g_fontFamilies.empty()) {
            LOGFONTW lf{};
            lf.lfCharSet = DEFAULT_CHARSET;
            HDC dc = GetDC(nullptr);
            EnumFontFamiliesExW(
                dc, &lf,
                [](const LOGFONTW* f, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
                    if (f->lfFaceName[0] != L'@')
                        reinterpret_cast<std::vector<std::wstring>*>(p)->push_back(f->lfFaceName);
                    return 1;
                },
                reinterpret_cast<LPARAM>(&g_fontFamilies), 0);
            ReleaseDC(nullptr, dc);
            std::sort(g_fontFamilies.begin(), g_fontFamilies.end());
            g_fontFamilies.erase(std::unique(g_fontFamilies.begin(), g_fontFamilies.end()), g_fontFamilies.end());
        }
        for (size_t i = 0; i < g_fontFamilies.size() && i < 2000; ++i)
            add(static_cast<UINT>(10000 + i), g_fontFamilies[i], g_fontFamilies[i] == g_set.fontFace);
    } else if (e.id == SID_STYLE) {
        for (int i = 0; i < 4; ++i) add(static_cast<UINT>(20000 + i), styles[i], FontStyleName() == styles[i]);
    } else if (e.id == SID_SIZE) {
        for (int s : sizes) add(static_cast<UINT>(30000 + s), std::to_wstring(s), g_set.fontSize10 == s * 10);
    } else if (e.id == SID_OPENFILES) {
        add(40000, L"Open in a new tab", !g_set.openInNewWindow);
        add(40001, L"Open in a new window", g_set.openInNewWindow);
    }
    flyout::Options o;
    o.minWidth = cr.right - cr.left;
    o.maxHeight = S(360);
    UINT id = flyout::Show(g_hwnd, v, cr, o).id;
    if (!id) return;
    if (id >= 10000 && id < 20000) g_set.fontFace = g_fontFamilies[id - 10000];
    if (id >= 20000 && id < 20004) {
        g_set.italic = (id - 20000) == 1 || (id - 20000) == 3;
        g_set.bold = (id - 20000) >= 2;
    }
    if (id >= 30000 && id < 40000) g_set.fontSize10 = static_cast<int>(id - 30000) * 10;
    if (id == 40000 || id == 40001) g_set.openInNewWindow = id == 40001;
    ApplySettings();
}

void SettingsAction(HWND h, int id, const SElem& e) {
    switch (id) {
    case SID_THEME_EXP: g_expTheme = !g_expTheme; break;
    case SID_FONT_EXP: g_expFont = !g_expFont; break;
    case SID_START_EXP: g_expStart = !g_expStart; break;
    case SID_THEME_LIGHT: g_set.theme = ThemeMode::Light; ApplySettings(); break;
    case SID_THEME_DARK: g_set.theme = ThemeMode::Dark; ApplySettings(); break;
    case SID_THEME_SYS: g_set.theme = ThemeMode::System; ApplySettings(); break;
    case SID_WORDWRAP: g_set.wordWrap = !g_set.wordWrap; ApplySettings(); break;
    case SID_FORMATTING: g_set.formatting = !g_set.formatting; ApplySettings(); break;
    case SID_START_CONTINUE: g_set.continueSession = true; SaveSettings(); break;
    case SID_START_NEW: g_set.continueSession = false; SaveSettings(); break;
    case SID_RECENT: g_set.recentEnabled = !g_set.recentEnabled; SaveSettings(); break;
    case SID_AUTOCORRECT: g_set.autocorrect = !g_set.autocorrect; SaveSettings(); break;
    case SID_WRITING: g_set.writingTools = !g_set.writingTools; SaveSettings(); LayoutMenuRects(); break;
    case SID_SPELL_EXP: {
        // The toggle switches spell check; the rest of the header expands the card.
        RECT rc;
        GetClientRect(h, &rc);
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if (pt.x > e.rc.right - S(140) && pt.x < e.rc.right - S(40)) {
            g_set.spellCheck = !g_set.spellCheck;
            ApplySettings();
        } else {
            g_expSpell = !g_expSpell;
        }
        break;
    }
    case SID_FAMILY:
    case SID_STYLE:
    case SID_SIZE:
    case SID_OPENFILES: ShowSettingsCombo(h, e); break;
    case SID_LINK_FOLDER: {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        PathRemoveFileSpecW(exe);
        PathRemoveFileSpecW(exe);
        ShellExecuteW(g_hwnd, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case SID_LINK_NOTES:
    case SID_HELP: {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        PathRemoveFileSpecW(exe);
        PathRemoveFileSpecW(exe);
        std::wstring notes = std::wstring(exe) + L"\\ANALYSIS.md";
        wchar_t here[MAX_PATH];  // installed copies keep the notes next to the exe
        GetModuleFileNameW(nullptr, here, MAX_PATH);
        PathRemoveFileSpecW(here);
        if (GetFileAttributesW((std::wstring(here) + L"\\ANALYSIS.md").c_str()) != INVALID_FILE_ATTRIBUTES)
            notes = std::wstring(here) + L"\\ANALYSIS.md";
        if (GetFileAttributesW(notes.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DestroyWindow(h);
            OpenPath(notes);
        }
        return;
    }
    case SID_FEEDBACK:
        Notice(L"Send feedback", L"Feedback goes to Microsoft's Feedback Hub in the real Notepad. This replica has "
                                 L"no feedback service; note issues in ANALYSIS.md instead.");
        break;
    default:
        if (id >= SID_SPELL_EXT0 && id < SID_SPELL_EXT0 + 5) {
            g_set.spellExt[id - SID_SPELL_EXT0] = !g_set.spellExt[id - SID_SPELL_EXT0];
            ApplySettings();
        }
    }
    if (IsWindow(h)) InvalidateRect(h, nullptr, FALSE);
}

LRESULT CALLBACK SettingsPageProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintSettings(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int hot = SettingsHit(h, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hot != g_setHot) {
            g_setHot = hot;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_setHot = SID_NONE;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        SElem e{SType::Text};
        int id = SettingsHit(h, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, &e);
        if (id != SID_NONE) {
            OffsetRect(&e.rc, 0, 0);
            SettingsAction(h, id, e);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        RECT rc;
        GetClientRect(h, &rc);
        int height = 0;
        BuildSettings(rc.right, height);
        g_setScroll = std::clamp(g_setScroll - GET_WHEEL_DELTA_WPARAM(wp), 0, std::max<int>(0, height - rc.bottom));
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_settings = nullptr;
        LayoutChildren();
        if (Tab* t = Cur()) {
            ShowWindow(t->edit, SW_SHOW);
            SetFocus(t->edit);
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void PositionSettings() {
    if (!g_settings) return;
    RECT c;
    GetClientRect(g_hwnd, &c);
    SetWindowPos(g_settings, HWND_TOP, 0, S(kStripH), c.right, c.bottom - S(kStripH), SWP_NOACTIVATE);
}

void OpenSettingsPage() {
    if (g_settings) return;
    g_setScroll = 0;
    if (Tab* t = Cur()) ShowWindow(t->edit, SW_HIDE);
    if (g_find) ShowWindow(g_find, SW_HIDE);
    g_settings = CreateWindowExW(0, kSettingsClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, g_hwnd,
                                 nullptr, g_inst, nullptr);
    PositionSettings();
    ExtendFrame();
    SetFocus(g_settings);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void RegisterSettingsClass(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = SettingsPageProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClass;
    RegisterClassExW(&wc);
}

// ---------------------------------------------------------------------------
// Main window

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(hwnd, msg, wp, lp, &dwmResult)) return dwmResult;

    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_dpi = GetDpiForWindow(hwnd);
        g_theme = OpenThemeData(hwnd, L"WINDOW");
        CreateFonts();
        ExtendFrame();
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        CreateWindowExW(0, kFindClass, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FINDPANEL)), g_inst, nullptr);
        ApplyTheme();
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_NCCALCSIZE:
        if (wp) {
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            LONG top = p->rgrc[0].top;
            LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            p->rgrc[0].top = top;  // drop the caption; the tab strip lives there
            if (IsZoomed(hwnd)) {
                UINT dpi = GetDpiForWindow(hwnd);
                p->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            return r;
        }
        break;

    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
        if (r != HTCLIENT) return r;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        int frame = GetSystemMetricsForDpi(SM_CYFRAME, g_dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, g_dpi);
        if (!IsZoomed(hwnd) && pt.y < frame) return HTTOP;
        if (pt.y < S(kStripH)) {
            if (g_settings) return PtInRect(&g_backRc, pt) ? HTCLIENT : HTCAPTION;
            for (const RECT& t : g_tabRc)
                if (PtInRect(&t, pt)) return HTCLIENT;
            if (PtInRect(&g_addRc, pt)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) g_lastFocus = GetFocus();
        InvalidateRect(hwnd, nullptr, FALSE);  // Mica switches between active and inactive looks
        break;

    case WM_SETFOCUS:
        if (g_lastFocus && IsWindow(g_lastFocus) && IsChild(hwnd, g_lastFocus) && IsWindowVisible(g_lastFocus))
            SetFocus(g_lastFocus);
        else if (Tab* t = Cur())
            SetFocus(t->edit);
        return 0;

    case WM_SIZE:
        LayoutMenuRects();
        LayoutChildren();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {S(420), S(260)};
        return 0;
    }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        CreateFonts();
        ExtendFrame();
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        for (auto& t : g_tabs) ApplyEditorLook(*t);
        LayoutMenuRects();
        LayoutChildren();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Layout l = GetLayout();
        PaintStrip(dc, l);
        PaintMenu(dc, l);
        FillSolid(dc, l.editor, g_pal.editor);
        PaintStatus(dc, l);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        UpdateHover({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (!g_tracking) {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_tracking = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_tracking = false;
        UpdateHover({-1, -1});
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (g_settings) {
            if (PtInRect(&g_backRc, pt)) DestroyWindow(g_settings);
            return 0;
        }
        for (size_t i = 0; i < g_tabRc.size(); ++i) {
            if (!PtInRect(&g_tabRc[i], pt)) continue;
            RECT cr = TabCloseRect(g_tabRc[i]);
            if (PtInRect(&cr, pt))
                CloseTab(static_cast<int>(i));
            else
                ActivateTab(static_cast<int>(i));
            return 0;
        }
        if (PtInRect(&g_addRc, pt)) {
            NewTab();
            return 0;
        }
        for (int i = 0; i < 3; ++i) {
            if (PtInRect(&g_menuRc[i], pt)) {
                OpenMenu(i, false);
                return 0;
            }
        }
        if (PtInRect(&g_menuRc[3], pt)) {
            CmdSettings();
            return 0;
        }
        if (ToolbarVisible()) {
            static const UINT direct[kToolCount] = {0, 0, IDM_BOLD, IDM_ITALIC, IDM_STRIKE, IDM_LINK, 0, IDM_CLEARFMT};
            for (int k = 0; k < kToolCount; ++k) {
                if (!PtInRect(&g_menuRc[kToolFirst + k], pt)) continue;
                if (direct[k])
                    OnCommand(direct[k]);
                else
                    OpenMenu(kToolFirst + k, false);
                return 0;
            }
        }
        if (PtInRect(&g_menuRc[kWriting], pt)) {
            OpenMenu(kWriting, false);
            return 0;
        }
        if (PtInRect(&g_menuRc[kWhatsNew], pt)) {
            OnCommand(IDM_WHATSNEW);
            return 0;
        }
        if (PtInRect(&g_menuRc[kAvatar], pt)) {
            OnCommand(IDM_ACCOUNT);
            return 0;
        }
        for (const auto& s : g_segs) {
            if (PtInRect(&s.rc, pt)) {
                StatusMenu(s.id, s.rc);
                return 0;
            }
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        for (size_t i = 0; i < g_tabRc.size(); ++i)
            if (PtInRect(&g_tabRc[i], pt)) {
                ShowTabContextMenu(static_cast<int>(i), pt);
                return 0;
            }
        break;
    }

    case WM_MBUTTONUP: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        for (size_t i = 0; i < g_tabRc.size(); ++i)
            if (PtInRect(&g_tabRc[i], pt)) {
                CloseTab(static_cast<int>(i));
                break;
            }
        return 0;
    }

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) {
            wchar_t c = static_cast<wchar_t>(towlower(static_cast<wint_t>(lp)));
            if (c == L'f') { OpenMenu(0, true); return 0; }
            if (c == L'e') { OpenMenu(1, true); return 0; }
            if (c == L'v') { OpenMenu(2, true); return 0; }
            if (lp == 0) return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_EDITOR && HIWORD(wp) == EN_CHANGE) {
            for (auto& t : g_tabs)
                if (t->edit == reinterpret_cast<HWND>(lp)) MarkModifiedChanged(*t);
            InvalidateStatus();
            return 0;
        }
        OnCommand(LOWORD(wp));
        return 0;

    case WM_NOTIFY: {
        auto* nh = reinterpret_cast<NMHDR*>(lp);
        if (nh->code == EN_SELCHANGE) {
            InvalidateStatus();
            UpdateFormatState();
            return 0;
        }
        if (nh->code == EN_LINK) {
            auto* link = reinterpret_cast<ENLINK*>(lp);
            if (link->msg == WM_LBUTTONUP && GetKeyState(VK_CONTROL) < 0) {
                if (Tab* t = Cur()) FollowLink(*t, link->chrg.cpMin);
                return 1;
            }
            if (link->msg == WM_SETCURSOR && GetKeyState(VK_CONTROL) >= 0) return 0;
            return 0;
        }
        if (nh->code == EN_DROPFILES) {
            OpenDroppedFiles(static_cast<HDROP>(reinterpret_cast<ENDROPFILES*>(lp)->hDrop));
            return 0;
        }
        break;
    }

    case WM_DROPFILES:
        OpenDroppedFiles(reinterpret_cast<HDROP>(wp));
        DragFinish(reinterpret_cast<HDROP>(wp));
        return 0;

    case WM_SETTINGCHANGE:
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0 &&
            g_set.theme == ThemeMode::System)
            ApplyTheme();
        break;

    case WM_THEMECHANGED:
        if (g_theme) CloseThemeData(g_theme);
        g_theme = OpenThemeData(hwnd, L"WINDOW");
        break;

    case WM_CLOSE:
        if (g_set.continueSession) {
            SaveSession();  // unsaved work comes back next time instead of prompting now
        } else {
            for (size_t i = 0; i < g_tabs.size(); ++i)
                if (!ConfirmDiscard(static_cast<int>(i))) return 0;
            ClearSession();
        }
        SaveWindowState();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        for (auto& t : g_tabs)
            if (t->doc) t->doc->Release();
        g_tabs.clear();
        if (g_theme) CloseThemeData(g_theme);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenArg(const std::wstring& arg) {
    if (arg.empty() || arg[0] == L'/' || arg[0] == L'-') return;
    std::wstring full = FullPath(arg);
    DWORD attr = GetFileAttributesW(full.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) OpenPath(full);
        return;
    }
    std::wstring msg = L"Cannot find the " + full + L" file.\n\nDo you want to create a new file?";
    if (!Confirm(kAppName, msg, L"Yes", L"No")) return;
    Tab* t = Cur();
    if (!t || !IsPristine(*t)) t = NewTab();
    if (!t) return;
    t->path = full;
    UpdateTitle();
    InvalidateStrip();
}

void RestorePlacement(int show) {
    WINDOWPLACEMENT wp{};
    DWORD cb = sizeof wp;
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Placement", RRF_RT_REG_BINARY, nullptr, &wp, &cb) == ERROR_SUCCESS &&
        cb == sizeof wp && wp.length == sizeof wp) {
        if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWNORMAL;
        if (show == SW_SHOWMINIMIZED || show == SW_MINIMIZE || show == SW_SHOWMINNOACTIVE) wp.showCmd = static_cast<UINT>(show);
        SetWindowPlacement(g_hwnd, &wp);
    } else {
        ShowWindow(g_hwnd, show);
    }
}

// Writes our drawn icon as a .ico (16-256 px) so the exe and its Start menu shortcut use it.
bool WriteIconFile(const std::wstring& path) {
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 256};
    std::vector<std::string> images;
    for (int size : sizes) {
        HICON icon = MakeAppIcon(size);
        ICONINFO ii{};
        if (!icon || !GetIconInfo(icon, &ii)) return false;
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
        std::vector<DWORD> px(static_cast<size_t>(size) * size);
        HDC dc = GetDC(nullptr);
        GetDIBits(dc, ii.hbmColor, 0, static_cast<UINT>(size), px.data(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        DestroyIcon(icon);
        // .ico wants straight (not premultiplied) alpha, rows bottom-up, plus an AND mask.
        BITMAPINFOHEADER h{sizeof(BITMAPINFOHEADER), size, size * 2, 1, 32, BI_RGB};
        std::string img(reinterpret_cast<const char*>(&h), sizeof h);
        for (int y = size - 1; y >= 0; --y)
            for (int x = 0; x < size; ++x) {
                DWORD p = px[static_cast<size_t>(y) * size + x];
                DWORD a = p >> 24;
                auto un = [a](DWORD c) { return a ? std::min<DWORD>(255, c * 255 / a) : 0; };
                DWORD out = (a << 24) | (un((p >> 16) & 0xFF) << 16) | (un((p >> 8) & 0xFF) << 8) | un(p & 0xFF);
                img.append(reinterpret_cast<const char*>(&out), 4);
            }
        img.append(static_cast<size_t>((size + 31) / 32 * 4) * size, '\0');
        images.push_back(img);
    }
    std::string file;
    auto put16 = [&](WORD v) { file.append(reinterpret_cast<const char*>(&v), 2); };
    auto put32 = [&](DWORD v) { file.append(reinterpret_cast<const char*>(&v), 4); };
    put16(0);
    put16(1);
    put16(static_cast<WORD>(images.size()));
    DWORD offset = 6 + 16 * static_cast<DWORD>(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        file += static_cast<char>(sizes[i] >= 256 ? 0 : sizes[i]);
        file += static_cast<char>(sizes[i] >= 256 ? 0 : sizes[i]);
        file += '\0';
        file += '\0';
        put16(1);
        put16(32);
        put32(static_cast<DWORD>(images[i].size()));
        put32(offset);
        offset += static_cast<DWORD>(images[i].size());
    }
    for (const auto& img : images) file += img;
    return WriteAllBytes(path, file);
}

// `ReaNotepad.exe --selftest <log>` checks Markdown round trips and the formatting
// commands against a hidden window, writes PASS/FAIL lines to <log>, and exits.
int RunSelfTest(const std::wstring& logPath) {
    std::wstring log;
    int failures = 0;
    auto visible = [](const std::wstring& s) {
        std::wstring out;
        for (wchar_t c : s) {
            if (c == L'\r') {
                out += L"\\r";
            } else if (c < 0x20 || (c >= 0xFFF0)) {
                wchar_t b[12];
                swprintf_s(b, L"\\u%04X", c);
                out += b;
            } else {
                out += c;
            }
        }
        return out;
    };
    auto check = [&](const wchar_t* name, const std::wstring& got, const std::wstring& want) {
        bool pass = got == want;
        if (!pass) ++failures;
        log += std::wstring(pass ? L"PASS " : L"FAIL ") + name + L"\r\n";
        if (!pass) log += L"  want: " + visible(want) + L"\r\n  got:  " + visible(got) + L"\r\n";
    };
    auto select = [](Tab& t, long a, long b) {
        CHARRANGE cr{a, b};
        SendMessageW(t.edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    };

    const std::wstring sample =
        L"# Title\r## Sub heading\rPlain **bold** and *italic* and ~~strike~~ text.\r- item one\r- item **two**\r"
        L"  - nested\r1. first\r2. second\r[link text](https://example.com) end\r| a | b |\r| --- | --- |\r"
        L"| 1 | 2 |\rafter table\rC:\\Work\\path and a \\* star\r";
    check(L"model round trip", md::Serialize(md::Parse(sample)), sample);

    Tab* t = NewTab(Kind::Markdown);
    ResetEditorMode(*t, true);
    LoadDocIntoEditor(*t, md::Parse(sample));
    check(L"editor round trip (formatted view)", md::Serialize(DocFromEditor(*t)), sample);

    Tab* f = NewTab(Kind::Text);
    StreamText(f->edit, L"Hello world\rSecond line\rItem\rSee docs", false);
    select(*f, 0, 5);
    ToggleFont(*f, 0);
    select(*f, 6, 11);
    ToggleFont(*f, 1);
    select(*f, 14, 14);
    ApplyHeading(*f, 2);
    select(*f, 25, 25);
    ApplyList(*f, false);
    InsertLinkAt(*f, 33, 37, L"docs", L"https://example.com");
    check(L"toolbar commands -> Markdown", md::Serialize(DocFromEditor(*f)),
          L"**Hello** *world*\r## Second line\r- Item\rSee [docs](https://example.com)");
    check(L"plain text of formatted doc", PlainOf(*f), L"Hello world\rSecond line\rItem\rSee docs");
    SendMessageW(f->edit, EM_SETSEL, 0, -1);
    ClearFormatting(*f);
    check(L"clear formatting", md::Serialize(DocFromEditor(*f)), L"Hello world\rSecond line\rItem\rSee docs");

    Tab* tb = NewTab(Kind::Text);
    StreamText(tb->edit, L"Before\r", false);
    InsertTableAt(*tb, 7, 2, 2, {{L"a", L"b"}, {L"1", L"2"}});
    check(L"insert table", md::Serialize(DocFromEditor(*tb)), L"Before\r| a | b |\r| --- | --- |\r| 1 | 2 |");

    Tab* lossy = NewTab(Kind::Markdown);
    check(L"unsupported Markdown stays as syntax", LoadMarkdown(*lossy, L"Some *unclosed text") ? L"formatted" : L"syntax",
          L"syntax");

    {
        ascii::Options ao;
        auto art = ascii::Render(L"Hi", ao);
        check(L"ascii art: 6 lines", std::to_wstring(art.size()), L"6");
        check(L"ascii art: shadow letters", art.empty() ? L"" : art[0], L"\u2588\u2588\u2557 \u2588\u2588\u2557\u2588\u2588\u2557");
        auto heb = ascii::Render(L"\u05E9\u05DC\u05D5\u05DD", ao);  // Hebrew falls back to a Windows font
        check(L"ascii art: other scripts via a font", std::to_wstring(heb.size()) + (heb.empty() || heb[0].empty() ? L" empty" : L" ok"), L"6 ok");
        Tab* a = NewTab(Kind::Text);
        StreamText(a->edit, L"Say ##Hi##", false);
        SendMessageW(a->edit, EM_SETSEL, 10, 10);
        bool was = g_set.asciiOn; int keep = g_set.asciiPlacement;
        int height = g_set.asciiHeight, style = g_set.asciiStyle, font = g_set.asciiFont;
        g_set.asciiOn = true; g_set.asciiPlacement = 0; g_set.asciiHeight = 1; g_set.asciiStyle = 0; g_set.asciiFont = 0;
        SendMessageW(a->edit, WM_CHAR, L' ', 0);
        std::wstring plain = PlainOf(*a);
        check(L"typing after ## converts", plain.substr(0, plain.find(L'\r')), L"Say  ");
        check(L"art follows on new lines", std::to_wstring(std::count(plain.begin(), plain.end(), L'\r')), L"7");
        CHARRANGE after{};
        SendMessageW(a->edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
        check(L"caret on the new line after the art", std::to_wstring(after.cpMin), std::to_wstring(static_cast<long>(plain.size())));
        Tab* c = NewTab(Kind::Text);
        StreamText(c->edit, L"#S8H2#Hi##", false);
        SendMessageW(c->edit, EM_SETSEL, 10, 10);
        SendMessageW(c->edit, WM_CHAR, L' ', 0);
        std::wstring coded = PlainOf(*c);
        check(L"#S8H2# code: hash style", coded.find(L'#') != std::wstring::npos && coded.find(L'\u2588') == std::wstring::npos ? L"hash" : coded, L"hash");
        g_set.asciiOn = was; g_set.asciiPlacement = keep; g_set.asciiHeight = height; g_set.asciiStyle = style; g_set.asciiFont = font;
    }

    log += failures ? L"RESULT: " + std::to_wstring(failures) + L" failure(s)\r\n" : L"RESULT: all passed\r\n";
    WriteAllBytes(logPath, Encode(log, Encoding::Utf8));
    for (auto& tab : g_tabs) SendMessageW(tab->edit, EM_SETMODIFY, FALSE, 0);
    return failures;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    LoadLibraryW(L"Msftedit.dll");
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    BufferedPaintInit();
    InitDarkModeApi();
    LoadSettings();

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainClass;
    wc.hIcon = MakeAppIcon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = MakeAppIcon(GetSystemMetrics(SM_CXSMICON));
    g_smallIcon = MakeAppIcon(GetSystemMetrics(SM_CXSMICON));  // title-bar icon at its real size
    RegisterClassExW(&wc);

    WNDCLASSEXW fc{sizeof fc};
    fc.lpfnWndProc = FindProc;
    fc.hInstance = inst;
    fc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    fc.lpszClassName = kFindClass;
    RegisterClassExW(&fc);
    RegisterSettingsClass(inst);
    flyout::Register(inst);
    dlg::Register(inst);
    g_pal = kLight;

    ACCEL accels[] = {
        {FCONTROL | FVIRTKEY, 'N', IDM_NEWTAB},
        {FCONTROL | FVIRTKEY, 'T', IDM_NEWTAB},
        {FCONTROL | FSHIFT | FVIRTKEY, 'N', IDM_NEWWINDOW},
        {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},
        {FCONTROL | FVIRTKEY, 'S', IDM_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_SAVEAS},
        {FCONTROL | FALT | FVIRTKEY, 'S', IDM_SAVEALL},
        {FCONTROL | FVIRTKEY, 'P', IDM_PRINT},
        {FCONTROL | FVIRTKEY, 'W', IDM_CLOSETAB},
        {FCONTROL | FSHIFT | FVIRTKEY, 'W', IDM_CLOSEWINDOW},
        {FCONTROL | FVIRTKEY, 'F', IDM_FIND},
        {FVIRTKEY, VK_F3, IDM_FINDNEXT},
        {FSHIFT | FVIRTKEY, VK_F3, IDM_FINDPREV},
        {FCONTROL | FVIRTKEY, 'H', IDM_REPLACE},
        {FCONTROL | FVIRTKEY, 'G', IDM_GOTO},
        {FVIRTKEY, VK_F5, IDM_TIMEDATE},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, IDM_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_ADD, IDM_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDM_ZOOMOUT},
        {FCONTROL | FVIRTKEY, VK_SUBTRACT, IDM_ZOOMOUT},
        {FCONTROL | FVIRTKEY, '0', IDM_ZOOMRESET},
        {FCONTROL | FVIRTKEY, VK_NUMPAD0, IDM_ZOOMRESET},
        {FCONTROL | FVIRTKEY, VK_TAB, IDM_NEXTTAB},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, IDM_PREVTAB},
        {FCONTROL | FVIRTKEY, 'B', IDM_BOLD},
        {FCONTROL | FVIRTKEY, 'I', IDM_ITALIC},
        {FCONTROL | FSHIFT | FVIRTKEY, 'X', IDM_STRIKE},
        {FCONTROL | FVIRTKEY, 'K', IDM_LINK},
        {FCONTROL | FVIRTKEY, VK_SPACE, IDM_CLEARFMT},
        {FCONTROL | FVIRTKEY, 'E', IDM_BING_AUTO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'A', IDM_ASCII_CONVERT},
        {FCONTROL | FVIRTKEY, 'Q', IDM_AI_BASE + 0},
        {FCONTROL | FVIRTKEY, 'D', IDM_AI_BASE + 1},
        {FCONTROL | FVIRTKEY, 'M', IDM_AI_BASE + 2},
    };
    g_accel = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

    UINT sysDpi = GetDpiForSystem();
    HWND hwnd = CreateWindowExW(0, kMainClass, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, MulDiv(960, static_cast<int>(sysDpi), 96),
                                MulDiv(680, static_cast<int>(sysDpi), 96), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--write-icon")) {
        bool ok = WriteIconFile(argv[2]);
        LocalFree(argv);
        DestroyWindow(hwnd);
        return ok ? 0 : 1;
    }
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--selftest")) {
        std::wstring logPath = argv[2];
        LocalFree(argv);
        int failures = RunSelfTest(logPath);
        DestroyWindow(hwnd);
        return failures;
    }

    RestorePlacement(show);
    bool openedFiles = false;
    for (int i = 1; argv && i < argc; ++i) {
        OpenArg(argv[i]);
        openedFiles = true;
    }
    if (argv) LocalFree(argv);
    if (!openedFiles && g_set.continueSession) RestoreSession();
    if (g_tabs.empty()) NewTab();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!g_settings && TranslateAcceleratorW(g_hwnd, g_accel, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyAcceleratorTable(g_accel);
    BufferedPaintUnInit();
    OleUninitialize();
    return static_cast<int>(m.wParam);
}
