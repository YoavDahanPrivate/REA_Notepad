#include "flyout.h"

#include <windowsx.h>

#include <algorithm>
#include <map>
#include <memory>

#include "ui.h"

namespace flyout {
namespace {

constexpr wchar_t kClass[] = L"ReaNotepadFlyout";
// Measured at 100%: rows 33px apart, 4px padding, separators 9px, shortcut column
// left-aligned 16px from the right edge, check/icon column pushes text to 43px.
constexpr int kRowH = 33;
constexpr int kPad = 4;
constexpr int kSepH = 3;
constexpr int kHeaderH = 28;
constexpr int kGridN = 5;
constexpr int kCell = 30;
constexpr int kCellGap = 6;
constexpr int kBarH = 56;
constexpr UINT_PTR kSubTimer = 1;

struct Level {
    HWND hwnd = nullptr;
    std::vector<Item> items;
    std::vector<RECT> rects;  // content coordinates
    int hot = -1;
    int width = 0, contentH = 0, viewH = 0, scroll = 0;
    int textX = 0, shortcutX = 0;
    int openSub = -1;
    int pendingSub = -1;
    int gridR = 0, gridC = 0;
    int barHot = -1;
    bool tracking = false;
};

struct State {
    HWND owner = nullptr;
    Options opt;
    std::vector<std::unique_ptr<Level>> levels;
    bool done = false;
    Result result;
    std::map<int, HFONT> previewFonts;
};

State* g_state = nullptr;

bool Selectable(const Item& it) {
    return (it.kind == Kind::Item || it.kind == Kind::StylePreview) && it.enabled;
}

HFONT PreviewFont(int px) {
    auto& fonts = g_state->previewFonts;
    auto it = fonts.find(px);
    if (it != fonts.end()) return it->second;
    HFONT f = MakeFont(S(px), g_uiFace);
    fonts[px] = f;
    return f;
}

int ItemHeight(const Item& it) {
    switch (it.kind) {
    case Kind::Separator: return S(kSepH);
    case Kind::Header: return S(kHeaderH);
    case Kind::StylePreview: return S(it.previewPx > 14 ? 55 : 40);  // measured: heading rows 55px, Body 40px
    case Kind::TableGrid: return S(kHeaderH + kGridN * kCell + (kGridN - 1) * kCellGap + 10);
    case Kind::CommandBar: return S(kBarH);
    default: return S(kRowH);
    }
}

void Measure(Level& L, int minWidth) {
    HDC dc = GetDC(nullptr);
    bool checkCol = false, iconCol = false, hasSub = false;
    int textW = 0, shortW = 0, fixedW = 0;
    for (const Item& it : L.items) {
        if (it.checkable || it.checked) checkCol = true;
        if (!it.icon.empty() && it.kind == Kind::Item) iconCol = true;
        if (!it.sub.empty() && it.kind == Kind::Item) hasSub = true;
        HGDIOBJ old = SelectObject(dc, it.kind == Kind::StylePreview ? PreviewFont(it.previewPx) : g_uiFont);
        int w = TextWidth(dc, it.text);
        SelectObject(dc, g_smallFont);
        if (!it.badge.empty()) w += S(8) + TextWidth(dc, it.badge) + S(14);
        if (it.kind == Kind::Item || it.kind == Kind::StylePreview) textW = std::max(textW, w);
        if (!it.shortcut.empty()) shortW = std::max(shortW, TextWidth(dc, it.shortcut));
        if (it.kind == Kind::Header) textW = std::max(textW, TextWidth(dc, it.text));
        if (it.kind == Kind::TableGrid) fixedW = std::max(fixedW, S(12) * 2 + kGridN * S(kCell) + (kGridN - 1) * S(kCellGap));
        if (it.kind == Kind::CommandBar) fixedW = std::max(fixedW, static_cast<int>(it.sub.size()) * S(68) + 2 * S(kPad));
        SelectObject(dc, old);
    }
    ReleaseDC(nullptr, dc);
    L.textX = S(checkCol || iconCol ? 43 : 14);
    // Chevrons sit in the right padding; WinUI doesn't widen the menu for them.
    int w = L.textX + textW + (shortW ? S(24) + shortW : 0) + S(17) + (hasSub && !shortW ? S(20) : 0);
    L.width = std::max({w, fixedW, minWidth, S(120)});
    L.shortcutX = L.width - S(16) - shortW;
    L.rects.clear();
    int y = S(kPad);
    for (const Item& it : L.items) {
        int h = ItemHeight(it);
        L.rects.push_back({0, y, L.width, y + h});
        y += h;
    }
    L.contentH = y + S(kPad);
}

RECT GridCell(const RECT& r, int row, int col) {
    int x0 = r.left + S(12), y0 = r.top + S(kHeaderH);
    int x = x0 + col * (S(kCell) + S(kCellGap)), y = y0 + row * (S(kCell) + S(kCellGap));
    return {x, y, x + S(kCell), y + S(kCell)};
}

RECT BarButton(const Level& L, const RECT& r, int i, int n) {
    int bw = (L.width - 2 * S(kPad)) / std::max(1, n);
    int x = S(kPad) + i * bw;
    return {x, r.top + S(4), x + bw, r.bottom - S(4)};
}

void Paint(Level& L, HDC hdc) {
    RECT client;
    GetClientRect(L.hwnd, &client);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillSolid(dc, client, g_pal.flyout);
    for (size_t i = 0; i < L.items.size(); ++i) {
        const Item& it = L.items[i];
        RECT r = L.rects[i];
        OffsetRect(&r, 0, -L.scroll);
        if (r.bottom < 0 || r.top > client.bottom) continue;
        COLORREF fg = it.enabled ? g_pal.text : g_pal.disabled;
        switch (it.kind) {
        case Kind::Separator: {
            int cy = (r.top + r.bottom) / 2;
            FillSolid(dc, {0, cy, L.width, cy + 1}, g_pal.flyoutBorder);
            break;
        }
        case Kind::Header:
            DrawTextIn(dc, it.text, {S(12), r.top, r.right - S(12), r.bottom}, g_pal.textMuted, g_smallFont,
                       DT_SINGLELINE | DT_VCENTER);
            break;
        case Kind::TableGrid: {
            wchar_t caption[64];
            if (L.gridR > 0)
                swprintf_s(caption, L"%d × %d table", L.gridC, L.gridR);
            else
                wcscpy_s(caption, it.text.c_str());
            DrawTextIn(dc, caption, {S(12), r.top + S(4), r.right - S(12), r.top + S(kHeaderH) - S(4)}, g_pal.textMuted,
                       g_smallFont, DT_SINGLELINE | DT_VCENTER);
            for (int row = 0; row < kGridN; ++row)
                for (int col = 0; col < kGridN; ++col) {
                    bool on = row < L.gridR && col < L.gridC;
                    FrameRound(dc, GridCell(r, row, col), on ? g_pal.accent : g_pal.inputBorder,
                               on ? g_pal.accentSoft : g_pal.input, S(8));
                }
            break;
        }
        case Kind::CommandBar: {
            int n = static_cast<int>(it.sub.size());
            for (int b = 0; b < n; ++b) {
                const Item& btn = it.sub[b];
                RECT br = BarButton(L, r, b, n);
                if (b == L.barHot && btn.enabled) {
                    RECT hl = br;
                    InflateRect(&hl, -S(2), 0);
                    FillRound(dc, hl, g_pal.flyoutHover, S(8));
                }
                if (b > 0) FillSolid(dc, {br.left, br.top + S(10), br.left + 1, br.bottom - S(10)}, g_pal.flyoutBorder);
                COLORREF c = btn.enabled ? g_pal.text : g_pal.disabled;
                DrawTextIn(dc, btn.icon, {br.left, br.top + S(4), br.right, br.top + S(28)}, c, g_iconFont,
                           DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                DrawTextIn(dc, btn.text, {br.left, br.top + S(28), br.right, br.bottom}, c, g_smallFont,
                           DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            break;
        }
        default: {
            RECT hl{r.left + S(kPad), r.top + S(2), r.right - S(kPad), r.bottom - S(2)};
            bool hot = static_cast<int>(i) == L.hot || static_cast<int>(i) == L.openSub;
            if ((hot && it.enabled) || it.current) FillRound(dc, hl, g_pal.flyoutHover, S(8));
            if (it.current) {
                int cy = (hl.top + hl.bottom) / 2;
                FillRound(dc, {hl.left, cy - S(8), hl.left + S(3), cy + S(8)}, g_pal.accent, S(3));
            }
            if (it.checked)
                DrawTextIn(dc, glyph::Check, {S(12), r.top, S(12) + S(16), r.bottom}, fg, g_iconFontSmall,
                           DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            else if (!it.icon.empty())
                DrawTextIn(dc, it.icon, {S(10), r.top, S(10) + S(20), r.bottom}, fg, g_iconFont,
                           DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            HFONT f = it.kind == Kind::StylePreview ? PreviewFont(it.previewPx) : g_uiFont;
            RECT tr{L.textX, r.top, r.right - S(16), r.bottom};
            DrawTextIn(dc, it.text, tr, fg, f, DT_SINGLELINE | DT_VCENTER);
            if (!it.badge.empty()) {
                HGDIOBJ o = SelectObject(dc, f);
                int tw = TextWidth(dc, it.text);
                SelectObject(dc, g_smallFont);
                int bw = TextWidth(dc, it.badge) + S(14);
                SelectObject(dc, o);
                int cy = (r.top + r.bottom) / 2;
                RECT badge{L.textX + tw + S(8), cy - S(8), L.textX + tw + S(8) + bw, cy + S(8)};
                FillRound(dc, badge, g_pal.accent, S(16));
                DrawTextIn(dc, it.badge, badge, g_pal.accentText, g_smallFont, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            if (!it.shortcut.empty())
                DrawTextIn(dc, it.shortcut, {L.shortcutX, r.top, r.right - S(8), r.bottom},
                           it.enabled ? g_pal.textMuted : g_pal.disabled, g_smallFont, DT_SINGLELINE | DT_VCENTER);
            if (!it.sub.empty())
                DrawTextIn(dc, glyph::ChevronRight, {r.right - S(32), r.top, r.right - S(14), r.bottom}, fg,
                           g_iconFontSmall, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
        }
        }
    }
    BitBlt(hdc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

int LevelOf(HWND h) {
    if (!g_state) return -1;
    for (size_t i = 0; i < g_state->levels.size(); ++i)
        if (g_state->levels[i]->hwnd == h) return static_cast<int>(i);
    return -1;
}

int HitItem(const Level& L, POINT pt) {
    pt.y += L.scroll;
    for (size_t i = 0; i < L.rects.size(); ++i)
        if (PtInRect(&L.rects[i], pt)) return static_cast<int>(i);
    return -1;
}

void Finish(UINT id, int rows = 0, int cols = 0) {
    g_state->result.id = id;
    g_state->result.gridRows = rows;
    g_state->result.gridCols = cols;
    g_state->done = true;
}

void CloseFrom(size_t level) {
    while (g_state->levels.size() > level) {
        DestroyWindow(g_state->levels.back()->hwnd);
        g_state->levels.pop_back();
    }
    if (level > 0 && level <= g_state->levels.size()) {
        g_state->levels[level - 1]->openSub = -1;
        InvalidateRect(g_state->levels[level - 1]->hwnd, nullptr, FALSE);
    }
}

HWND CreateLevel(Level& L, int x, int y, bool flipX, int anchorLeft) {
    HMONITOR mon = MonitorFromPoint({x, y}, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork;
    int maxH = static_cast<int>(wa.bottom - wa.top) - S(16);
    if (g_state->opt.maxHeight) maxH = std::min(maxH, g_state->opt.maxHeight);
    L.viewH = std::min(L.contentH, maxH);
    if (x + L.width > wa.right) x = flipX ? anchorLeft - L.width : wa.right - L.width;
    if (x < wa.left) x = wa.left;
    if (y + L.viewH > wa.bottom) y = std::max<int>(wa.top, wa.bottom - L.viewH);
    L.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, x, y, L.width,
                             L.viewH, g_state->owner, nullptr, g_inst, nullptr);
    ApplyRoundedPopup(L.hwnd);
    ShowWindow(L.hwnd, SW_SHOWNOACTIVATE);
    return L.hwnd;
}

void OpenSub(size_t level, int item, bool keyboard) {
    CloseFrom(level + 1);
    Level& p = *g_state->levels[level];
    const Item& it = p.items[item];
    if (it.sub.empty() || !it.enabled) return;
    p.openSub = item;
    auto child = std::make_unique<Level>();
    child->items = it.sub;
    Measure(*child, S(230));  // WinUI submenus measured at >= 230px
    RECT wr;
    GetWindowRect(p.hwnd, &wr);
    int top = wr.top + p.rects[item].top - p.scroll - S(kPad);
    Level* raw = child.get();
    g_state->levels.push_back(std::move(child));
    CreateLevel(*raw, wr.right - S(2), top, true, wr.left + S(2));
    if (keyboard)
        for (size_t i = 0; i < raw->items.size(); ++i)
            if (Selectable(raw->items[i])) {
                raw->hot = static_cast<int>(i);
                break;
            }
    InvalidateRect(p.hwnd, nullptr, FALSE);
}

void Activate(size_t level, int item, POINT pt) {
    Level& L = *g_state->levels[level];
    if (item < 0) return;
    const Item& it = L.items[item];
    if (it.kind == Kind::TableGrid) {
        if (L.gridR > 0) Finish(it.id, L.gridR, L.gridC);
        return;
    }
    if (it.kind == Kind::CommandBar) {
        int n = static_cast<int>(it.sub.size());
        for (int b = 0; b < n; ++b) {
            RECT br = BarButton(L, L.rects[item], b, n);
            pt.y += L.scroll;
            if (PtInRect(&br, pt) && it.sub[b].enabled) Finish(it.sub[b].id);
            pt.y -= L.scroll;
        }
        return;
    }
    if (!Selectable(it)) return;
    if (!it.sub.empty()) {
        OpenSub(level, item, false);
        return;
    }
    Finish(it.id);
}

void MoveHot(Level& L, int dir) {
    int n = static_cast<int>(L.items.size());
    if (!n) return;
    int i = L.hot < 0 ? (dir > 0 ? -1 : n) : L.hot;
    for (int step = 0; step < n; ++step) {
        i += dir;
        if (i >= n) i = 0;
        if (i < 0) i = n - 1;
        if (Selectable(L.items[i])) {
            L.hot = i;
            // Keep the hot row visible when scrolling.
            const RECT& r = L.rects[i];
            if (r.top - L.scroll < 0) L.scroll = r.top - S(kPad);
            if (r.bottom - L.scroll > L.viewH) L.scroll = r.bottom - L.viewH + S(kPad);
            L.scroll = std::clamp(L.scroll, 0, std::max(0, L.contentH - L.viewH));
            InvalidateRect(L.hwnd, nullptr, FALSE);
            return;
        }
    }
}

void HandleKey(UINT vk) {
    auto& levels = g_state->levels;
    size_t level = levels.size() - 1;
    Level& L = *levels[level];
    switch (vk) {
    case VK_DOWN: MoveHot(L, 1); break;
    case VK_UP: MoveHot(L, -1); break;
    case VK_HOME: L.hot = -1; MoveHot(L, 1); break;
    case VK_END: L.hot = 0; MoveHot(L, -1); break;
    case VK_RIGHT:
        if (L.hot >= 0 && !L.items[L.hot].sub.empty() && L.items[L.hot].kind == Kind::Item)
            OpenSub(level, L.hot, true);
        else if (g_state->opt.menuBarIndex >= 0) {
            g_state->result.switchTo = g_state->opt.menuBarIndex + 1;
            g_state->done = true;
        }
        break;
    case VK_LEFT:
        if (level > 0)
            CloseFrom(level);
        else if (g_state->opt.menuBarIndex >= 0) {
            g_state->result.switchTo = g_state->opt.menuBarIndex - 1;
            g_state->done = true;
        }
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (L.hot >= 0) {
            if (!L.items[L.hot].sub.empty())
                OpenSub(level, L.hot, true);
            else if (Selectable(L.items[L.hot]))
                Finish(L.items[L.hot].id);
        }
        break;
    case VK_ESCAPE:
        if (level > 0)
            CloseFrom(level);
        else
            g_state->done = true;
        break;
    case VK_MENU:
    case VK_F10:
    case VK_TAB:
        g_state->done = true;
        break;
    }
}

LRESULT CALLBACK FlyoutProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    int li = LevelOf(h);
    if (li < 0) return DefWindowProcW(h, m, wp, lp);
    Level& L = *g_state->levels[li];
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        Paint(L, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!L.tracking) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            L.tracking = true;
        }
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hit = HitItem(L, pt);
        int gr = 0, gc = 0, bar = -1;
        if (hit >= 0 && L.items[hit].kind == Kind::TableGrid) {
            POINT cp{pt.x, pt.y + L.scroll};
            for (int row = 0; row < kGridN; ++row)
                for (int col = 0; col < kGridN; ++col) {
                    RECT c = GridCell(L.rects[hit], row, col);
                    InflateRect(&c, S(kCellGap) / 2, S(kCellGap) / 2);
                    if (PtInRect(&c, cp)) {
                        gr = row + 1;
                        gc = col + 1;
                    }
                }
        }
        if (hit >= 0 && L.items[hit].kind == Kind::CommandBar) {
            int n = static_cast<int>(L.items[hit].sub.size());
            POINT cp{pt.x, pt.y + L.scroll};
            for (int b = 0; b < n; ++b) {
                RECT br = BarButton(L, L.rects[hit], b, n);
                if (PtInRect(&br, cp)) bar = b;
            }
        }
        int hot = hit >= 0 && Selectable(L.items[hit]) ? hit : -1;
        if (hot != L.hot || gr != L.gridR || gc != L.gridC || bar != L.barHot) {
            L.hot = hot;
            L.gridR = gr;
            L.gridC = gc;
            L.barHot = bar;
            InvalidateRect(h, nullptr, FALSE);
        }
        // Open or close submenus after a short hover, like WinUI.
        int want = hot >= 0 && !L.items[hot].sub.empty() ? hot : -1;
        if (want != L.openSub && want != L.pendingSub) {
            L.pendingSub = want;
            SetTimer(h, kSubTimer, 250, nullptr);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        L.tracking = false;
        if (L.openSub < 0 && (L.hot >= 0 || L.gridR || L.barHot >= 0)) {
            L.hot = -1;
            L.gridR = L.gridC = 0;
            L.barHot = -1;
            InvalidateRect(h, nullptr, FALSE);
        }
        L.pendingSub = L.openSub;
        KillTimer(h, kSubTimer);
        return 0;
    case WM_TIMER:
        if (wp == kSubTimer) {
            KillTimer(h, kSubTimer);
            if (L.pendingSub >= 0)
                OpenSub(static_cast<size_t>(li), L.pendingSub, false);
            else
                CloseFrom(static_cast<size_t>(li) + 1);
        }
        return 0;
    case WM_LBUTTONUP: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        Activate(static_cast<size_t>(li), HitItem(L, pt), pt);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

void Register(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = FlyoutProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
}

Result Show(HWND owner, const std::vector<Item>& items, RECT anchor, const Options& options) {
    if (g_state || items.empty()) return {};
    State st;
    st.owner = owner;
    st.opt = options;
    g_state = &st;

    auto top = std::make_unique<Level>();
    top->items = items;
    Measure(*top, options.minWidth);
    Level* L = top.get();
    st.levels.push_back(std::move(top));
    int x = anchor.left;
    if (anchor.left == anchor.right) x -= L->width / 2;  // a zero-width anchor means "centered here"
    int y = options.above ? anchor.top - std::min(L->contentH, options.maxHeight ? options.maxHeight : L->contentH) -
                                S(2)
                          : anchor.bottom + S(2);
    CreateLevel(*L, x, y, false, x);
    if (options.keyboard) MoveHot(*L, 1);

    HWND root = GetAncestor(owner, GA_ROOT);
    MSG m{};
    while (!st.done) {
        if (!GetMessageW(&m, nullptr, 0, 0)) {
            PostQuitMessage(static_cast<int>(m.wParam));
            break;
        }
        UINT msg = m.message;
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            HandleKey(static_cast<UINT>(m.wParam));
            continue;
        }
        if (msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_CHAR || msg == WM_SYSCHAR) continue;
        if (msg == WM_MOUSEWHEEL) {
            POINT pt;
            GetCursorPos(&pt);
            int li = LevelOf(WindowFromPoint(pt));
            if (li >= 0) {
                Level& W = *st.levels[li];
                W.scroll = std::clamp(W.scroll - GET_WHEEL_DELTA_WPARAM(m.wParam) / 2, 0,
                                      std::max(0, W.contentH - W.viewH));
                InvalidateRect(W.hwnd, nullptr, FALSE);
            }
            continue;
        }
        bool inFlyout = LevelOf(m.hwnd) >= 0;
        bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN ||
                    msg == WM_NCLBUTTONDOWN || msg == WM_NCRBUTTONDOWN;
        if (down && !inFlyout) {
            POINT pt;
            GetCursorPos(&pt);
            int idx = options.menuBarHit ? options.menuBarHit(pt) : -1;
            if (idx >= 0 && idx != options.menuBarIndex) st.result.switchTo = idx;
            st.done = true;  // light dismiss eats the click
            continue;
        }
        if (msg == WM_MOUSEMOVE && !inFlyout && options.menuBarHit) {
            POINT pt;
            GetCursorPos(&pt);
            int idx = options.menuBarHit(pt);
            if (idx >= 0 && idx != options.menuBarIndex) {
                st.result.switchTo = idx;
                st.done = true;
                continue;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
        if (GetForegroundWindow() != root) st.done = true;
    }
    CloseFrom(0);
    for (auto& f : st.previewFonts) DeleteObject(f.second);
    g_state = nullptr;
    return st.result;
}

}  // namespace flyout
