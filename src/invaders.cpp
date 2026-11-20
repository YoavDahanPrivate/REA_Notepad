#include "invaders.h"
#include "sound.h"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace invaders {
namespace {

constexpr wchar_t kClass[] = L"ReaNotepadInvaders";
constexpr UINT_PTR kTimer = 1;
constexpr int kTickMs = 33;

struct Bullet {
    int x, y;
};

struct Shield {
    int x, y;
    std::wstring rows[2];
};

struct Game {
    HWND hwnd = nullptr;
    Look look{};
    std::function<void()> onExit;
    HBITMAP snapshot = nullptr;
    int introOffset = 0;  // pixels the old text has scrolled up
    bool intro = true;
    int charW = 8, lineH = 16, cols = 40, rows = 20;

    // Play state
    std::mt19937 rng{std::random_device{}()};
    int alienRows = 5, alienCols = 8;
    std::vector<bool> alive;
    int ax = 0, ay = 0, dir = 1, frame = 0, stepTick = 0;
    int px = 0, moveTick = 0;
    std::vector<Bullet> shots, bombs;
    std::vector<Shield> shields;
    int ufoX = -100, ufoDir = 0;
    int score = 0, hiScore = 0, lives = 3, level = 1;
    int hitFlash = 0;
    bool paused = false, over = false, won = false;
    int tick = 0;
    int march = 0;  // which note of the fleet's four-note step plays next
};

Game* g_game = nullptr;

const wchar_t* AlienSprite(int row, int frame) {
    static const wchar_t* sprites[3][2] = {{L"{@}", L"}@{"}, {L"<O>", L">O<"}, {L"/o\\", L"\\o/"}};
    int type = row == 0 ? 0 : row < 3 ? 1 : 2;
    return sprites[type][frame];
}

int AlienPoints(int row) { return row == 0 ? 30 : row < 3 ? 20 : 10; }
int PlayerRow(const Game& g) { return g.rows - 3; }
int AlienX(const Game& g, int c) { return g.ax + c * 5; }
int AlienY(const Game& g, int r) { return g.ay + r * 2; }

void NewWave(Game& g, bool resetAll) {
    g.alienCols = std::clamp((g.cols - 12) / 5, 3, 10);
    g.alienRows = std::clamp((g.rows - 12) / 3, 2, 5);
    g.alive.assign(static_cast<size_t>(g.alienRows * g.alienCols), true);
    g.ax = 2;
    g.ay = 3 + std::min(g.level - 1, 4);
    g.dir = 1;
    g.shots.clear();
    g.bombs.clear();
    g.ufoDir = 0;
    if (resetAll) {
        g.score = 0;
        g.lives = 3;
        g.level = 1;
        g.ay = 3;
    }
    g.px = g.cols / 2 - 1;
    // Four bunkers spread across the field, two rows each.
    g.shields.clear();
    int n = std::clamp(g.cols / 16, 2, 4);
    for (int i = 0; i < n; ++i) {
        Shield s;
        s.x = (i + 1) * g.cols / (n + 1) - 3;
        s.y = PlayerRow(g) - 4;
        s.rows[0] = L"#####";
        s.rows[1] = L"## ##";
        g.shields.push_back(s);
    }
    g.over = g.won = false;
}

bool HitShield(Game& g, int x, int y) {
    for (auto& s : g.shields) {
        int r = y - s.y, c = x - s.x;
        if (r < 0 || r > 1 || c < 0 || c >= static_cast<int>(s.rows[r].size())) continue;
        wchar_t& ch = s.rows[r][c];
        if (ch == L' ') continue;
        ch = ch == L'#' ? L'+' : L' ';  // two hits per block
        return true;
    }
    return false;
}

void Step(Game& g) {
    ++g.tick;
    if (g.hitFlash) --g.hitFlash;
    // Movement: arrows or A/D, held down.
    bool left = GetKeyState(VK_LEFT) < 0 || GetKeyState('A') < 0;
    bool right = GetKeyState(VK_RIGHT) < 0 || GetKeyState('D') < 0;
    if (++g.moveTick >= 2) {
        g.moveTick = 0;
        if (left) g.px = std::max(0, g.px - 1);
        if (right) g.px = std::min(g.cols - 3, g.px + 1);
    }
    // Player shots fly one row per tick.
    for (auto& b : g.shots) --b.y;
    for (size_t i = 0; i < g.shots.size();) {
        Bullet& b = g.shots[i];
        bool gone = b.y < 1 || HitShield(g, b.x, b.y);
        if (!gone && g.ufoDir && b.y == 2 && b.x >= g.ufoX && b.x < g.ufoX + 3) {
            g.score += 100 + 50 * static_cast<int>(g.rng() % 4);
            g.ufoDir = 0;
            sfx::StopLoop(sfx::InvUfo);
            sfx::Play(sfx::InvUfoHit);
            gone = true;
        }
        for (int r = 0; r < g.alienRows && !gone; ++r)
            for (int c = 0; c < g.alienCols && !gone; ++c) {
                size_t k = static_cast<size_t>(r * g.alienCols + c);
                if (!g.alive[k] || b.y != AlienY(g, r) || b.x < AlienX(g, c) || b.x >= AlienX(g, c) + 3) continue;
                g.alive[k] = false;
                sfx::Play(sfx::InvAlienHit);
                g.score += AlienPoints(r);
                gone = true;
            }
        if (gone)
            g.shots.erase(g.shots.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
    int left0 = g.cols, right0 = -1, bottom = -1, count = 0;
    for (int r = 0; r < g.alienRows; ++r)
        for (int c = 0; c < g.alienCols; ++c)
            if (g.alive[static_cast<size_t>(r * g.alienCols + c)]) {
                left0 = std::min(left0, AlienX(g, c));
                right0 = std::max(right0, AlienX(g, c) + 2);
                bottom = std::max(bottom, AlienY(g, r));
                ++count;
            }
    if (!count) {  // wave cleared
        ++g.level;
        sfx::StopLoop(sfx::InvUfo);
        sfx::Play(sfx::InvWave);
        g.score += 500;
        NewWave(g, false);
        return;
    }
    // The march speeds up as the fleet shrinks.
    int total = g.alienRows * g.alienCols;
    int interval = std::max(1, 2 + count * 14 / total - std::min(g.level - 1, 3));
    if (++g.stepTick >= interval) {
        g.stepTick = 0;
        g.frame ^= 1;
        sfx::Play(static_cast<sfx::Id>(sfx::InvMarch0 + g.march));
        g.march = (g.march + 1) % 4;
        if (left0 + g.dir < 0 || right0 + g.dir >= g.cols) {
            g.dir = -g.dir;
            ++g.ay;
        } else {
            g.ax += g.dir;
        }
    }
    if (bottom >= PlayerRow(g) - 1) {
        g.over = true;
        sfx::StopLoop(sfx::InvUfo);
        sfx::Play(sfx::InvPlayerHit);
        return;
    }
    // Aliens drop bombs from the lowest alien of a random column.
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (chance(g.rng) < 0.04 + 0.01 * g.level && g.bombs.size() < static_cast<size_t>(2 + g.level)) {
        int c = static_cast<int>(g.rng() % static_cast<unsigned>(g.alienCols));
        for (int r = g.alienRows - 1; r >= 0; --r)
            if (g.alive[static_cast<size_t>(r * g.alienCols + c)]) {
                g.bombs.push_back({AlienX(g, c) + 1, AlienY(g, r) + 1});
                break;
            }
    }
    if (g.tick % 2 == 0) {
        for (auto& b : g.bombs) ++b.y;
        for (size_t i = 0; i < g.bombs.size();) {
            Bullet& b = g.bombs[i];
            bool gone = b.y >= g.rows - 1 || HitShield(g, b.x, b.y);
            if (!gone && b.y == PlayerRow(g) && b.x >= g.px && b.x < g.px + 3) {
                gone = true;
                g.hitFlash = 12;
                sfx::Play(sfx::InvPlayerHit);
                if (--g.lives <= 0) g.over = true;
                g.bombs.clear();
                break;
            }
            if (gone)
                g.bombs.erase(g.bombs.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
    }
    // Mystery ship.
    if (!g.ufoDir && chance(g.rng) < 0.004) {
        g.ufoDir = g.rng() % 2 ? 1 : -1;
        g.ufoX = g.ufoDir > 0 ? -3 : g.cols;
    }
    if (g.ufoDir && g.tick % 2 == 0) {
        g.ufoX += g.ufoDir;
        if (g.ufoX < -3 || g.ufoX > g.cols) g.ufoDir = 0;
    }
    if (g.ufoDir && !g.over)
        sfx::Loop(sfx::InvUfo);
    else
        sfx::StopLoop(sfx::InvUfo);
    g.hiScore = std::max(g.hiScore, g.score);
}

void Put(std::vector<std::wstring>& scr, int x, int y, const std::wstring& s) {
    if (y < 0 || y >= static_cast<int>(scr.size())) return;
    for (size_t i = 0; i < s.size(); ++i) {
        int cx = x + static_cast<int>(i);
        if (cx >= 0 && cx < static_cast<int>(scr[y].size())) scr[y][cx] = s[i];
    }
}

void Paint(Game& g, HDC hdc) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(dc, bmp);
    SetDCBrushColor(dc, g.look.background);
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (g.intro && g.snapshot) {
        // The old text scrolls up as if new lines were being added below it.
        HDC sdc = CreateCompatibleDC(hdc);
        HGDIOBJ os = SelectObject(sdc, g.snapshot);
        BitBlt(dc, 0, -g.introOffset, rc.right, rc.bottom, sdc, 0, 0, SRCCOPY);
        SelectObject(sdc, os);
        DeleteDC(sdc);
    } else if (!g.intro) {
        std::vector<std::wstring> scr(static_cast<size_t>(g.rows), std::wstring(static_cast<size_t>(g.cols), L' '));
        wchar_t hud[128];
        swprintf_s(hud, L" SCORE %06d   HI %06d   LEVEL %d   LIVES %s", g.score, g.hiScore, g.level,
                   std::wstring(static_cast<size_t>(std::max(0, g.lives)), L'^').c_str());
        Put(scr, 0, 0, hud);
        if (g.ufoDir) Put(scr, g.ufoX, 2, L"<=>");
        for (int r = 0; r < g.alienRows; ++r)
            for (int c = 0; c < g.alienCols; ++c)
                if (g.alive[static_cast<size_t>(r * g.alienCols + c)])
                    Put(scr, AlienX(g, c), AlienY(g, r), AlienSprite(r, g.frame));
        for (const auto& s : g.shields) {
            Put(scr, s.x, s.y, s.rows[0]);
            Put(scr, s.x, s.y + 1, s.rows[1]);
        }
        for (const auto& b : g.shots) Put(scr, b.x, b.y, L"|");
        for (const auto& b : g.bombs) Put(scr, b.x, b.y, L"!");
        if (!(g.hitFlash && g.hitFlash % 4 < 2)) Put(scr, g.px, PlayerRow(g), L"/A\\");
        Put(scr, 0, PlayerRow(g) + 1, std::wstring(static_cast<size_t>(g.cols), L'_'));
        Put(scr, 1, g.rows - 1, sfx::Muted() ? L"Left/Right move   Space fire   P pause   M sound on   Esc back to your text"
                                           : L"Left/Right move   Space fire   P pause   M sound off   Esc back to your text");
        if (g.over || g.paused) {
            std::wstring msg = g.over ? L"  G A M E   O V E R  -  Enter to play again  " : L"  P A U S E D  ";
            int y = g.rows / 2, x = std::max(0, (g.cols - static_cast<int>(msg.size())) / 2);
            Put(scr, x, y - 1, std::wstring(msg.size(), L' '));
            Put(scr, x, y, msg);
            Put(scr, x, y + 1, std::wstring(msg.size(), L' '));
        }
        HGDIOBJ of = SelectObject(dc, g.look.font);
        SetBkMode(dc, TRANSPARENT);
        for (int y = 0; y < g.rows; ++y) {
            SetTextColor(dc, y == 0 || y == g.rows - 1 ? g.look.muted : g.look.text);
            TextOutW(dc, 0, y * g.lineH, scr[static_cast<size_t>(y)].c_str(), g.cols);
        }
        // Player and mystery ship in the accent color.
        SetTextColor(dc, g.look.accent);
        if (!(g.hitFlash && g.hitFlash % 4 < 2)) TextOutW(dc, g.px * g.charW, PlayerRow(g) * g.lineH, L"/A\\", 3);
        if (g.ufoDir) TextOutW(dc, g.ufoX * g.charW, 2 * g.lineH, L"<=>", 3);
        SelectObject(dc, of);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void Measure(Game& g) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    HDC dc = GetDC(g.hwnd);
    HGDIOBJ of = SelectObject(dc, g.look.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g.hwnd, dc);
    g.charW = std::max<int>(1, tm.tmAveCharWidth);
    g.lineH = std::max<int>(1, tm.tmHeight);
    g.cols = std::max(20, static_cast<int>(rc.right / g.charW));
    g.rows = std::max(14, static_cast<int>(rc.bottom / g.lineH));
}

void Exit() {
    Game* g = g_game;
    if (!g) return;
    KillTimer(g->hwnd, kTimer);
    sfx::StopAll();
    auto onExit = g->onExit;
    DestroyWindow(g->hwnd);  // WM_NCDESTROY frees the game
    if (onExit) onExit();
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Game* g = g_game;
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g) Paint(*g, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_TIMER:
        if (!g) return 0;
        if (g->intro) {
            g->introOffset += g->lineH;
            RECT rc;
            GetClientRect(h, &rc);
            if (g->introOffset >= rc.bottom) {
                g->intro = false;
                Measure(*g);
                NewWave(*g, true);
            }
        } else if (!g->paused && !g->over) {
            Step(*g);
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (!g) return 0;
        if (wp == VK_ESCAPE) {
            Exit();
            return 0;
        }
        if (g->intro) return 0;
        if (wp == VK_SPACE && !g->over && !g->paused && g->shots.size() < 2) {
            g->shots.push_back({g->px + 1, PlayerRow(*g) - 1});
            sfx::Play(sfx::InvShot);
        }
        if (wp == 'P') {
            g->paused = !g->paused;
            if (g->paused) sfx::StopAll();
        }
        if (wp == 'M') sfx::SetMuted(!sfx::Muted());
        if (wp == VK_RETURN && g->over) NewWave(*g, true);
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;
    case WM_KILLFOCUS:
        if (g && !g->over) g->paused = true;
        sfx::StopAll();
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        return 0;
    case WM_SIZE:
        if (g && !g->intro) {
            Measure(*g);
            NewWave(*g, false);
        }
        return 0;
    case WM_NCDESTROY:
        if (g) {
            if (g->snapshot) DeleteObject(g->snapshot);
            delete g;
            g_game = nullptr;
        }
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

void Register(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
}

bool Running() { return g_game != nullptr; }

void Stop() { Exit(); }

void Start(HWND parent, RECT area, HBITMAP snapshot, const Look& look, std::function<void()> onExit) {
    if (g_game) {
        if (snapshot) DeleteObject(snapshot);
        return;
    }
    g_game = new Game;
    g_game->look = look;
    g_game->onExit = std::move(onExit);
    g_game->snapshot = snapshot;
    g_game->hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, area.left, area.top,
                                   area.right - area.left, area.bottom - area.top, parent, nullptr,
                                   reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetWindowPos(g_game->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Measure(*g_game);
    g_game->hiScore = 0;
    SetFocus(g_game->hwnd);
    SetTimer(g_game->hwnd, kTimer, kTickMs, nullptr);
}

void Reposition(RECT area) {
    if (!g_game) return;
    SetWindowPos(g_game->hwnd, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                 SWP_NOACTIVATE);
}

}  // namespace invaders
