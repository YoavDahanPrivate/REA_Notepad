#include "dialog.h"

#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>

#include "ui.h"

#pragma comment(lib, "msimg32.lib")

namespace dlg {
namespace {

constexpr wchar_t kSmokeClass[] = L"ReaNotepadSmoke";
constexpr wchar_t kDialogClass[] = L"ReaNotepadContentDialog";
constexpr wchar_t kTourClass[] = L"ReaNotepadWhatsNew";

COLORREF TopColor() { return g_dark ? RGB(43, 43, 43) : RGB(255, 255, 255); }
COLORREF FooterColor() { return g_dark ? RGB(32, 32, 32) : RGB(243, 243, 243); }

// Dimmed layer over the owner's client area while a dialog is open.
HWND CreateSmoke(HWND owner) {
    RECT rc;
    GetClientRect(owner, &rc);
    MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&rc), 2);
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kSmokeClass, L"", WS_POPUP, rc.left,
                             rc.top, rc.right - rc.left, rc.bottom - rc.top, owner, nullptr, g_inst, nullptr);
    SetLayeredWindowAttributes(h, 0, g_dark ? 110 : 64, LWA_ALPHA);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    return h;
}

void DrawButton(HDC dc, const RECT& r, const std::wstring& text, bool primary, bool hot) {
    if (primary) {
        COLORREF fill = g_pal.accent;
        if (hot) fill = g_dark ? RGB(80, 180, 230) : RGB(25, 110, 195);
        FillRound(dc, r, fill, S(8));
        DrawTextIn(dc, text, r, g_pal.accentText, g_uiFont, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    } else {
        FrameRound(dc, r, g_pal.inputBorder, hot ? g_pal.flyoutHover : TopColor(), S(8));
        DrawTextIn(dc, text, r, g_pal.text, g_uiFont, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
}

// ---------------------------------------------------------------------------
// ContentDialog

struct DialogState {
    Spec* spec = nullptr;
    std::vector<HWND> edits;
    std::vector<RECT> boxes;
    std::vector<std::pair<RECT, int>> buttons;  // rect, choice
    RECT bodyRect{};
    int footerTop = 0;
    int hot = -1;
    int result = Close;
    bool done = false;
    HBRUSH editBrush = nullptr;
};

DialogState* g_dlg = nullptr;

int Layout(DialogState& d, HWND h, bool create) {
    Spec& s = *d.spec;
    int w = S(s.width);
    int y = S(20) + S(32);
    HDC dc = GetDC(h);
    if (!s.body.empty()) {
        RECT br{S(24), y + S(8), w - S(24), y + S(8) + S(400)};
        HGDIOBJ old = SelectObject(dc, g_uiFont);
        DrawTextW(dc, s.body.c_str(), -1, &br, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        SelectObject(dc, old);
        d.bodyRect = br;
        y = br.bottom;
    }
    ReleaseDC(h, dc);
    d.boxes.clear();
    for (size_t i = 0; i < s.fields.size(); ++i) {
        y += S(14) + S(18) + S(6);
        RECT box{S(24), y, w - S(24), y + S(32)};
        d.boxes.push_back(box);
        if (create) {
            DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (s.fields[i].number ? ES_NUMBER : 0);
            HWND e = CreateWindowExW(0, L"EDIT", s.fields[i].value.c_str(), style, box.left + S(10),
                                     box.top + S(7), box.right - box.left - S(20), S(18), h, nullptr, g_inst, nullptr);
            SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), FALSE);
            SendMessageW(e, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(s.fields[i].placeholder.c_str()));
            d.edits.push_back(e);
        }
        y = box.bottom;
    }
    y += S(25);
    d.footerTop = y;
    d.buttons.clear();
    std::vector<std::pair<std::wstring, int>> labels;
    if (!s.primary.empty()) labels.push_back({s.primary, Primary});
    if (!s.secondary.empty()) labels.push_back({s.secondary, Secondary});
    if (!s.close.empty()) labels.push_back({s.close, Close});
    int n = static_cast<int>(labels.size());
    int bw = n ? (w - S(48) - S(8) * (n - 1)) / n : 0;
    for (int i = 0; i < n; ++i) {
        int x = S(24) + i * (bw + S(8));
        d.buttons.push_back({{x, y + S(24), x + bw, y + S(56)}, labels[i].second});
    }
    return y + S(80);
}

void Finish(DialogState& d, int choice) {
    for (size_t i = 0; i < d.edits.size(); ++i) {
        int len = GetWindowTextLengthW(d.edits[i]);
        std::wstring v(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(d.edits[i], v.data(), len + 1);
        v.resize(static_cast<size_t>(len));
        d.spec->fields[i].value = v;
    }
    d.result = choice;
    d.done = true;
}

LRESULT CALLBACK DialogProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    DialogState* d = g_dlg;
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(dc, bmp);
        FillSolid(dc, rc, TopColor());
        if (d) {
            FillSolid(dc, {0, d->footerTop, rc.right, rc.bottom}, FooterColor());
            FillSolid(dc, {0, d->footerTop, rc.right, d->footerTop + 1}, g_pal.cardBorder);
            DrawTextIn(dc, d->spec->title, {S(24), S(20), rc.right - S(24), S(52)}, g_pal.text, g_titleFont,
                       DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            if (!d->spec->body.empty())
                DrawTextIn(dc, d->spec->body, d->bodyRect, g_pal.text, g_uiFont, DT_WORDBREAK);
            HWND focus = GetFocus();
            for (size_t i = 0; i < d->boxes.size(); ++i) {
                const RECT& b = d->boxes[i];
                DrawTextIn(dc, d->spec->fields[i].label, {b.left, b.top - S(26), b.right, b.top - S(6)}, g_pal.text,
                           g_uiFont, DT_SINGLELINE | DT_BOTTOM);
                FrameRound(dc, b, g_pal.inputBorder, g_pal.input, S(8));
                if (i < d->edits.size() && d->edits[i] == focus)
                    FillSolid(dc, {b.left + S(1), b.bottom - S(2), b.right - S(1), b.bottom}, g_pal.accent);
            }
            for (size_t i = 0; i < d->buttons.size(); ++i)
                DrawButton(dc, d->buttons[i].first,
                           d->buttons[i].second == Primary ? d->spec->primary
                           : d->buttons[i].second == Secondary ? d->spec->secondary
                                                              : d->spec->close,
                           d->buttons[i].second == Primary, static_cast<int>(i) == d->hot);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, g_pal.text);
        SetBkColor(dc, g_pal.input);
        return reinterpret_cast<LRESULT>(d ? d->editBrush : GetStockObject(WHITE_BRUSH));
    }
    case WM_COMMAND:
        if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (d) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hot = -1;
            for (size_t i = 0; i < d->buttons.size(); ++i)
                if (PtInRect(&d->buttons[i].first, pt)) hot = static_cast<int>(i);
            if (hot != d->hot) {
                d->hot = hot;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (d && d->hot >= 0) {
            d->hot = -1;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (d) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (auto& b : d->buttons)
                if (PtInRect(&b.first, pt)) Finish(*d, b.second);
        }
        return 0;
    case WM_CLOSE:
        if (d) Finish(*d, Close);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------------------------------------------------------------------------
// What's new tour

struct TourEntry {
    const wchar_t* title;
    const wchar_t* blurb;
    bool isNew;
    const wchar_t* heading;
    const wchar_t* body;
    int highlight;  // toolbar slot highlighted in the illustration
};

const TourEntry kTour[3] = {
    {L"Your everyday editor, upgraded", L"Formatting and writing tools for quicker, tidier notes.", false,
     L"Your everyday editor, upgraded",
     L"The familiar Notepad, now with optional formatting and a calm space for quick edits and notes.", 2},
    {L"Writing tools", L"Write, rewrite and summarize text with AI help.", true, L"Writing tools",
     L"Draft, rewrite and summarize with Copilot. In this replica the menus are shown, but the service "
     L"itself needs a Microsoft account and isn't available.",
     8},
    {L"Lightweight formatting", L"Headings, lists, links and tables, saved as Markdown.", true,
     L"Lightweight formatting",
     L"Use the toolbar or Markdown syntax for headings, lists, bold, italic, links and tables. Formatted "
     L"notes are saved as plain .md files.",
     0},
};

struct TourState {
    int selected = 0;
    int hot = -1;  // 0..2 entries, 10 close, 11 start
    bool done = false;
};
TourState* g_tour = nullptr;

RECT TourEntryRect(int i) { return {S(16), S(80) + i * S(92), S(276), S(80) + i * S(92) + S(84)}; }
RECT TourClose(const RECT& rc) { return {rc.right - S(46), S(8), rc.right - S(10), S(40)}; }
RECT TourStart(const RECT& rc) { return {rc.right - S(158), rc.bottom - S(60), rc.right - S(28), rc.bottom - S(28)}; }

void DrawIllustration(HDC dc, RECT r, int highlight) {
    // Our own drawing of an editor window: gradient band, tab, toolbar row and text lines.
    TRIVERTEX v[2] = {{r.left, r.top, 0x6000, 0xB000, 0xFF00, 0}, {r.right, r.top + S(24), 0xB800, 0x9000, 0xF800, 0}};
    GRADIENT_RECT gr{0, 1};
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
    COLORREF paper = g_dark ? RGB(50, 50, 60) : RGB(243, 240, 252);
    FillSolid(dc, {r.left, r.top + S(24), r.right, r.bottom}, paper);
    FillRound(dc, {r.left + S(16), r.top + S(30), r.left + S(140), r.top + S(66)}, TopColor(), S(12));
    FillSolid(dc, {r.left, r.top + S(60), r.right, r.top + S(110)}, TopColor());
    static const wchar_t* tools[9] = {L"H1", glyph::List, glyph::Bold, glyph::Italic, glyph::Strike, glyph::Link,
                                      glyph::Grid, glyph::Eraser, glyph::PenLines};
    int x = r.left + S(110);
    for (int i = 0; i < 9; ++i) {
        RECT t{x, r.top + S(68), x + S(34), r.top + S(102)};
        if (i == highlight) FillRound(dc, t, g_pal.flyoutHover, S(8));
        DrawTextIn(dc, tools[i], t, g_pal.text, i == 0 ? g_semiboldFont : g_iconFont,
                   DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        x += S(38);
    }
    int y = r.top + S(132);
    COLORREF line = g_dark ? RGB(110, 110, 120) : RGB(200, 200, 210);
    FillRound(dc, {r.left + S(50), y, r.left + S(220), y + S(10)}, line, S(10));
    y += S(22);
    FillRound(dc, {r.left + S(46), y - S(4), r.left + S(394), y + S(14)}, g_pal.accentSoft, S(4));
    FillRound(dc, {r.left + S(50), y, r.left + S(390), y + S(10)}, line, S(10));
    y += S(22);
    FillRound(dc, {r.left + S(46), y - S(4), r.left + S(360), y + S(14)}, g_pal.accentSoft, S(4));
    FillRound(dc, {r.left + S(50), y, r.left + S(356), y + S(10)}, line, S(10));
    for (int i = 0; i < 5; ++i) {
        y += S(18);
        FillRound(dc, {r.left + S(52), y, r.left + S(60), y + S(8)}, line, S(8));
        FillRound(dc, {r.left + S(70), y, r.right - S(20) - (i % 2) * S(120), y + S(6)}, line, S(6));
    }
}

LRESULT CALLBACK TourProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    TourState* t = g_tour;
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(dc, bmp);
        FillSolid(dc, rc, g_pal.flyout);
        if (t) {
            RECT close = TourClose(rc);
            if (t->hot == 10) FillRound(dc, close, g_pal.flyoutHover, S(8));
            DrawTextIn(dc, glyph::Cancel, close, g_pal.text, g_iconFontSmall, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            DrawTextIn(dc, L"New in Notepad--", {S(24), S(40), rc.right, S(72)}, g_pal.text, g_titleFont,
                       DT_SINGLELINE | DT_VCENTER);
            for (int i = 0; i < 3; ++i) {
                RECT er = TourEntryRect(i);
                if (i == t->selected || i == t->hot) FillRound(dc, er, g_pal.flyoutHover, S(8));
                if (i == t->selected) {
                    int cy = (er.top + er.bottom) / 2;
                    FillRound(dc, {er.left, cy - S(16), er.left + S(3), cy + S(16)}, g_pal.accent, S(3));
                }
                HDC m2 = dc;
                HGDIOBJ o = SelectObject(m2, g_semiboldFont);
                int tw = TextWidth(m2, kTour[i].title);
                SelectObject(m2, o);
                DrawTextIn(dc, kTour[i].title, {er.left + S(16), er.top + S(10), er.right - S(8), er.top + S(32)},
                           g_pal.text, g_semiboldFont, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                if (kTour[i].isNew) {
                    int bx = std::min<int>(er.left + S(16) + tw + S(8), er.right - S(48));
                    RECT badge{bx, er.top + S(13), bx + S(40), er.top + S(29)};
                    FillRound(dc, badge, g_pal.accent, S(16));
                    DrawTextIn(dc, L"New", badge, g_pal.accentText, g_smallFont, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                }
                DrawTextIn(dc, kTour[i].blurb, {er.left + S(16), er.top + S(36), er.right - S(12), er.bottom - S(6)},
                           g_pal.textMuted, g_smallFont, DT_WORDBREAK);
            }
            RECT card{S(285), S(80), rc.right - S(13), rc.bottom - S(13)};
            FrameRound(dc, card, g_pal.cardBorder, TopColor(), S(12));
            RECT art{card.left + S(1), card.top + S(1), card.right - S(1), card.top + S(295)};
            HRGN clip = CreateRoundRectRgn(card.left + 1, card.top + 1, card.right, card.bottom, S(12), S(12));
            SelectClipRgn(dc, clip);
            DrawIllustration(dc, art, kTour[t->selected].highlight);
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);
            DrawTextIn(dc, kTour[t->selected].heading, {card.left + S(20), art.bottom + S(20), card.right - S(20),
                                                        art.bottom + S(46)},
                       g_pal.text, g_titleFont, DT_SINGLELINE | DT_VCENTER);
            DrawTextIn(dc, kTour[t->selected].body,
                       {card.left + S(20), art.bottom + S(56), card.right - S(20), art.bottom + S(110)}, g_pal.text,
                       g_uiFont, DT_WORDBREAK);
            DrawButton(dc, TourStart(rc), L"Start exploring", true, t->hot == 11);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (t) {
            RECT rc;
            GetClientRect(h, &rc);
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hot = -1;
            for (int i = 0; i < 3; ++i) {
                RECT er = TourEntryRect(i);
                if (PtInRect(&er, pt)) hot = i;
            }
            RECT c = TourClose(rc), s = TourStart(rc);
            if (PtInRect(&c, pt)) hot = 10;
            if (PtInRect(&s, pt)) hot = 11;
            if (hot != t->hot) {
                t->hot = hot;
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (t) {
            if (t->hot >= 0 && t->hot < 3) t->selected = t->hot;
            if (t->hot == 10 || t->hot == 11) t->done = true;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
        if (t) t->done = true;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

LRESULT CALLBACK SmokeProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, wp, lp);
}

HWND CenteredPopup(const wchar_t* cls, HWND owner, int w, int hgt) {
    RECT rc;
    GetClientRect(owner, &rc);
    MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&rc), 2);
    int x = (rc.left + rc.right - w) / 2, y = (rc.top + rc.bottom - hgt) / 2;
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME & 0, cls, L"", WS_POPUP, x, y, w, hgt, owner, nullptr, g_inst, nullptr);
    ApplyRoundedPopup(h);
    return h;
}

}  // namespace

void Register(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = SmokeProc;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kSmokeClass;
    RegisterClassExW(&wc);
    wc.hbrBackground = nullptr;
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = DialogProc;
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = TourProc;
    wc.lpszClassName = kTourClass;
    RegisterClassExW(&wc);
}

int Show(HWND owner, Spec& spec) {
    if (g_dlg) return Close;
    DialogState d;
    d.spec = &spec;
    d.editBrush = CreateSolidBrush(g_pal.input);
    g_dlg = &d;
    HWND smoke = CreateSmoke(owner);
    HWND h = CenteredPopup(kDialogClass, owner, S(spec.width), S(200));
    int height = Layout(d, h, true);
    RECT wr;
    GetWindowRect(h, &wr);
    RECT oc;
    GetClientRect(owner, &oc);
    MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&oc), 2);
    SetWindowPos(h, HWND_TOP, wr.left, (oc.top + oc.bottom - height) / 2, S(spec.width), height, 0);
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    if (!d.edits.empty()) {
        SetFocus(d.edits[0]);
        SendMessageW(d.edits[0], EM_SETSEL, 0, -1);
    } else {
        SetFocus(h);
    }
    MSG m{};
    while (!d.done) {
        if (!GetMessageW(&m, nullptr, 0, 0)) {
            PostQuitMessage(static_cast<int>(m.wParam));
            break;
        }
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_ESCAPE) {
                Finish(d, Close);
                continue;
            }
            if (m.wParam == VK_RETURN) {
                Finish(d, spec.primary.empty() ? Close : Primary);
                continue;
            }
            if (m.wParam == VK_TAB && d.edits.size() > 1) {
                auto it = std::find(d.edits.begin(), d.edits.end(), GetFocus());
                size_t i = it == d.edits.end() ? 0 : static_cast<size_t>(it - d.edits.begin());
                bool back = GetKeyState(VK_SHIFT) < 0;
                i = (i + (back ? d.edits.size() - 1 : 1)) % d.edits.size();
                SetFocus(d.edits[i]);
                SendMessageW(d.edits[i], EM_SETSEL, 0, -1);
                continue;
            }
        }
        if (m.message == WM_CHAR && (m.wParam == L'\r' || m.wParam == 27 || m.wParam == L'\t')) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(h);
    DestroyWindow(smoke);
    SetActiveWindow(owner);
    DeleteObject(d.editBrush);
    g_dlg = nullptr;
    return d.result;
}

void ShowWhatsNew(HWND owner) {
    if (g_tour) return;
    TourState t;
    g_tour = &t;
    HWND smoke = CreateSmoke(owner);
    HWND h = CenteredPopup(kTourClass, owner, S(770), S(550));
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    MSG m{};
    while (!t.done) {
        if (!GetMessageW(&m, nullptr, 0, 0)) {
            PostQuitMessage(static_cast<int>(m.wParam));
            break;
        }
        if (m.message == WM_KEYDOWN && (m.wParam == VK_ESCAPE || m.wParam == VK_RETURN)) {
            t.done = true;
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(h);
    DestroyWindow(smoke);
    SetActiveWindow(owner);
    g_tour = nullptr;
}

}  // namespace dlg
