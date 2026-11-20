#include "pacman.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <queue>
#include <random>
#include <string>

#include "sound.h"

namespace pacman {
namespace {

constexpr wchar_t kClass[] = L"ReaNotepadMaze";
constexpr UINT_PTR kTimer = 1;
constexpr int kTickMs = 33;  // ~30 ticks a second
constexpr int W = 19, H = 19;
constexpr int kTunnelRow = 9;

// An original maze. '#' wall, '.' dot, 'o' power dot, '-' ghost-house door, 'G' ghost house,
// 'P' player start, ' ' empty. Row 9 wraps around through the side tunnels.
const char* const kMaze[H] = {
    "###################",
    "#o..#.........#..o#",
    "#.#.#.#######.#.#.#",
    "#.#.............#.#",
    "#.###.###.###.###.#",
    "#.....#.....#.....#",
    "###.#.#.###.#.#.###",
    "#...#.........#...#",
    "#.#.#.###-###.#.#.#",
    "....#.#GGGGG#.#....",
    "#.#.#.#######.#.#.#",
    "#.#.............#.#",
    "#.###.###.###.###.#",
    "#.....#..P..#.....#",
    "###.#.#.###.#.#.###",
    "#o..#.........#..o#",
    "#.#####.###.#####.#",
    "#.................#",
    "###################",
};

enum Dir { Up, Left, Down, Right, None };
const int DX[4] = {0, -1, 0, 1}, DY[4] = {-1, 0, 1, 0};
int Reverse(int d) { return d == None ? None : (d + 2) % 4; }

enum class GS { House, Leaving, Chase, Frightened, Eyes };

struct Ghost {
    int x = 0, y = 0, dir = Left;
    GS st = GS::House;
    int release = 0;  // ticks before leaving the house
    int moveTick = 0;
    int cornerX = 0, cornerY = 0;
    COLORREF color = 0;
};

struct Game {
    HWND hwnd = nullptr;
    Look look{};
    std::function<void()> onExit;
    HBITMAP snapshot = nullptr;
    HFONT font = nullptr;  // own copy, sized so the maze fits
    bool intro = true;
    int introOffset = 0, introStep = 16;
    int charW = 8, lineH = 16, ox = 0, oy = 0;

    char cells[H][W + 1]{};
    int pellets = 0, eaten = 0;
    int px = 9, py = 13, pdir = Left, want = Left, pTick = 0, mouth = 0;
    Ghost ghosts[4];
    int score = 0, hi = 0, lives = 3, level = 1;
    int frightTicks = 0, eatChain = 0;
    int modeTicks = 0;
    bool scatter = true;
    int readyTicks = 0, deathTicks = 0, levelTicks = 0;
    bool over = false, paused = false, chompAlt = false;
    int fruitTicks = 0, fruitsShown = 0;
    int popupTicks = 0, popupX = 0, popupY = 0, popupScore = 0;
    int tick = 0, houseTicks = 0;
    std::mt19937 rng{std::random_device{}()};
    bool dark = false;
};

Game* g_game = nullptr;

bool IsWall(const Game& g, int x, int y) {
    if (y < 0 || y >= H) return false;
    if (x < 0 || x >= W) return false;
    return g.cells[y][x] == '#' || g.cells[y][x] == '-';
}

bool Open(const Game& g, int x, int y, bool door) {
    if (y < 0 || y >= H) return false;
    if (x < 0 || x >= W) return y == kTunnelRow;
    char c = g.cells[y][x];
    if (c == '#') return false;
    if (c == '-') return door;
    return true;
}

void Wrap(int& x) {
    if (x < 0) x = W - 1;
    if (x >= W) x = 0;
}

// First step of a shortest path (through the door) from (x,y) to (tx,ty).
int PathDir(const Game& g, int x, int y, int tx, int ty) {
    int first[H][W];
    for (auto& r : first) std::fill(std::begin(r), std::end(r), -2);
    std::queue<std::pair<int, int>> q;
    first[y][x] = -1;
    q.push({x, y});
    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        if (cx == tx && cy == ty) return first[cy][cx] < 0 ? None : first[cy][cx];
        for (int d = 0; d < 4; ++d) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!Open(g, nx, ny, true)) continue;
            Wrap(nx);
            if (first[ny][nx] != -2) continue;
            first[ny][nx] = first[cy][cx] < 0 ? d : first[cy][cx];
            q.push({nx, ny});
        }
    }
    return None;
}

void ResetActors(Game& g) {
    g.px = 9;
    g.py = 13;
    g.pdir = g.want = Left;
    g.mouth = 0;
    const int startX[4] = {9, 9, 8, 10}, startY[4] = {7, 9, 9, 9};
    const int release[4] = {0, 0, 90, 180};
    const int cx[4] = {W - 2, 1, W - 2, 1}, cy[4] = {0, 0, H - 1, H - 1};
    // Ghost colors that read on both the light and the dark editor background.
    const COLORREF light[4] = {RGB(214, 40, 40), RGB(214, 70, 160), RGB(0, 150, 180), RGB(220, 120, 0)};
    const COLORREF dark[4] = {RGB(255, 90, 90), RGB(255, 140, 210), RGB(80, 220, 240), RGB(255, 175, 70)};
    for (int i = 0; i < 4; ++i) {
        Ghost& gh = g.ghosts[i];
        gh.x = startX[i];
        gh.y = startY[i];
        gh.dir = i == 0 ? Left : Up;
        gh.st = i == 0 ? GS::Chase : GS::House;
        gh.release = std::max(0, release[i] - (g.level - 1) * 30);
        gh.moveTick = 0;
        gh.cornerX = cx[i];
        gh.cornerY = cy[i];
        gh.color = g.dark ? dark[i] : light[i];
    }
    g.frightTicks = 0;
    g.houseTicks = 0;
    g.modeTicks = 0;
    g.scatter = true;
    g.readyTicks = 60;
}

void ResetMaze(Game& g) {
    g.pellets = 0;
    g.eaten = 0;
    g.fruitTicks = 0;
    g.fruitsShown = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            char c = kMaze[y][x];
            if (c == 'P' || c == 'G') c = ' ';
            if (c == '.' || c == 'o') ++g.pellets;
            g.cells[y][x] = c;
        }
        g.cells[y][W] = 0;
    }
}

void NewGame(Game& g) {
    g.score = 0;
    g.lives = 3;
    g.level = 1;
    g.over = false;
    g.paused = false;
    g.deathTicks = g.levelTicks = 0;
    ResetMaze(g);
    ResetActors(g);
    g.readyTicks = 75;
    sfx::StopAll();
    sfx::Play(sfx::MazeStart);
}

void Popup(Game& g, int x, int y, int points) {
    g.popupX = x;
    g.popupY = y;
    g.popupScore = points;
    g.popupTicks = 30;
}

void Collide(Game& g) {
    for (auto& gh : g.ghosts) {
        if (gh.x != g.px || gh.y != g.py) continue;
        if (gh.st == GS::Frightened) {
            int points = 200 << std::min(g.eatChain, 3);
            ++g.eatChain;
            g.score += points;
            gh.st = GS::Eyes;
            Popup(g, gh.x, gh.y, points);
            sfx::Play(sfx::MazeEatGhost);
        } else if (gh.st == GS::Chase || gh.st == GS::Leaving) {
            g.deathTicks = 75;
            sfx::StopAll();
            sfx::Play(sfx::MazeDeath);
            return;
        }
    }
}

void MoveGhost(Game& g, Ghost& gh) {
    int interval = gh.st == GS::Eyes ? 2 : gh.st == GS::Frightened ? 8 : gh.st == GS::Leaving ? 4 : g.level >= 3 ? 4 : 5;
    if (gh.st == GS::Chase && gh.y == kTunnelRow && (gh.x < 4 || gh.x > W - 5)) interval += 3;  // slow in the tunnel
    if (++gh.moveTick < interval) return;
    gh.moveTick = 0;
    if (gh.st == GS::House) {
        if (g.houseTicks >= gh.release) gh.st = GS::Leaving;
        return;
    }
    int d = None;
    if (gh.st == GS::Leaving) {
        d = PathDir(g, gh.x, gh.y, 9, 7);
    } else if (gh.st == GS::Eyes) {
        d = PathDir(g, gh.x, gh.y, 9, 9);
    } else {
        int options[4], n = 0;
        for (int k = 0; k < 4; ++k)
            if (k != Reverse(gh.dir) && Open(g, gh.x + DX[k], gh.y + DY[k], false)) options[n++] = k;
        if (!n) options[n++] = Reverse(gh.dir);
        if (gh.st == GS::Frightened) {
            d = options[g.rng() % static_cast<unsigned>(n)];
        } else {
            // Each ghost has its own idea of where to go.
            int tx = g.px, ty = g.py;
            const Ghost& lead = g.ghosts[0];
            if (g.scatter) {
                tx = gh.cornerX;
                ty = gh.cornerY;
            } else if (&gh == &g.ghosts[1] && g.pdir != None) {
                tx = g.px + 4 * DX[g.pdir];
                ty = g.py + 4 * DY[g.pdir];
            } else if (&gh == &g.ghosts[2] && g.pdir != None) {
                tx = 2 * (g.px + 2 * DX[g.pdir]) - lead.x;
                ty = 2 * (g.py + 2 * DY[g.pdir]) - lead.y;
            } else if (&gh == &g.ghosts[3]) {
                int dx = gh.x - g.px, dy = gh.y - g.py;
                if (dx * dx + dy * dy < 36) {
                    tx = gh.cornerX;
                    ty = gh.cornerY;
                }
            }
            long best = -1;
            for (int i = 0; i < n; ++i) {
                int nx = gh.x + DX[options[i]] - tx, ny = gh.y + DY[options[i]] - ty;
                long dist = static_cast<long>(nx) * nx + static_cast<long>(ny) * ny;
                if (best < 0 || dist < best) {
                    best = dist;
                    d = options[i];
                }
            }
        }
    }
    if (d == None) {
        if (gh.st == GS::Leaving) gh.st = g.frightTicks ? GS::Frightened : GS::Chase;
        if (gh.st == GS::Eyes) gh.st = GS::Leaving;
        return;
    }
    gh.dir = d;
    gh.x += DX[d];
    gh.y += DY[d];
    Wrap(gh.x);
    if (gh.st == GS::Leaving && gh.x == 9 && gh.y == 7) gh.st = GS::Chase;
    if (gh.st == GS::Eyes && gh.x == 9 && gh.y == 9) gh.st = GS::Leaving;
}

void Step(Game& g) {
    ++g.tick;
    if (g.popupTicks) --g.popupTicks;
    if (g.levelTicks) {  // maze flashes after it is cleared
        if (--g.levelTicks == 0) {
            ++g.level;
            ResetMaze(g);
            ResetActors(g);
        }
        return;
    }
    if (g.deathTicks) {
        if (--g.deathTicks == 0) {
            if (--g.lives <= 0) {
                g.over = true;
                g.hi = std::max(g.hi, g.score);
            } else {
                ResetActors(g);
            }
        }
        return;
    }
    if (g.readyTicks) {
        --g.readyTicks;
        return;
    }
    ++g.houseTicks;
    // Scatter for 7 s, chase for 20 s, and again; ghosts turn around when the mode changes.
    if (!g.frightTicks && ++g.modeTicks >= (g.scatter ? 210 : 600)) {
        g.modeTicks = 0;
        g.scatter = !g.scatter;
        for (auto& gh : g.ghosts)
            if (gh.st == GS::Chase) gh.dir = Reverse(gh.dir);
    }
    if (g.frightTicks && --g.frightTicks == 0) {
        for (auto& gh : g.ghosts)
            if (gh.st == GS::Frightened) gh.st = GS::Chase;
        sfx::StopLoop(sfx::MazePower);
    }
    if (g.frightTicks)
        sfx::Loop(sfx::MazePower), sfx::StopLoop(sfx::MazeSiren);
    else
        sfx::Loop(sfx::MazeSiren);

    if (++g.pTick >= (g.level >= 3 ? 3 : 4)) {
        g.pTick = 0;
        if (g.want != None && Open(g, g.px + DX[g.want], g.py + DY[g.want], false)) g.pdir = g.want;
        if (g.pdir != None && Open(g, g.px + DX[g.pdir], g.py + DY[g.pdir], false)) {
            g.px += DX[g.pdir];
            g.py += DY[g.pdir];
            Wrap(g.px);
            g.mouth ^= 1;
        }
        char& c = g.cells[g.py][g.px];
        if (c == '.' || c == 'o') {
            bool power = c == 'o';
            c = ' ';
            --g.pellets;
            ++g.eaten;
            g.score += power ? 50 : 10;
            sfx::Play(g.chompAlt ? sfx::MazeChompB : sfx::MazeChompA);
            g.chompAlt = !g.chompAlt;
            if (power) {
                g.frightTicks = std::max(90, 240 - 30 * (g.level - 1));
                g.eatChain = 0;
                for (auto& gh : g.ghosts)
                    if (gh.st == GS::Chase) {
                        gh.st = GS::Frightened;
                        gh.dir = Reverse(gh.dir);
                    }
            }
            if ((g.eaten == 60 || g.eaten == 140) && g.fruitsShown < 2) {
                ++g.fruitsShown;
                g.fruitTicks = 300;
            }
        }
        if (g.fruitTicks && g.px == 9 && g.py == 11) {
            g.fruitTicks = 0;
            g.score += 100 * g.level;
            Popup(g, 9, 11, 100 * g.level);
            sfx::Play(sfx::MazeFruit);
        }
        Collide(g);
        if (g.deathTicks) return;
    }
    if (g.fruitTicks) --g.fruitTicks;
    for (auto& gh : g.ghosts) {
        MoveGhost(g, gh);
    }
    Collide(g);
    if (!g.deathTicks && g.pellets == 0) {
        g.levelTicks = 60;
        sfx::StopAll();
        sfx::Play(sfx::MazeLevel);
    }
    g.hi = std::max(g.hi, g.score);
}

// Double-line wall glyph from which neighbours are walls too.
wchar_t WallGlyph(const Game& g, int x, int y, bool& joinRight) {
    bool u = IsWall(g, x, y - 1), d = IsWall(g, x, y + 1), l = IsWall(g, x - 1, y), r = IsWall(g, x + 1, y);
    joinRight = r;
    if (g.cells[y][x] == '-') return L'\x2500';  // the door is a thin line
    int m = (u ? 1 : 0) | (r ? 2 : 0) | (d ? 4 : 0) | (l ? 8 : 0);
    static const wchar_t glyphs[16] = {
        L'\x25AA', L'\x2551', L'\x2550', L'\x255A', L'\x2551', L'\x2551', L'\x2554', L'\x2560',
        L'\x2550', L'\x255D', L'\x2550', L'\x2569', L'\x2557', L'\x2563', L'\x2566', L'\x256C',
    };
    return glyphs[m];
}

void Text(HDC dc, const Game& g, int col, int row, const std::wstring& s, COLORREF c) {
    SetTextColor(dc, c);
    TextOutW(dc, g.ox + col * g.charW, g.oy + row * g.lineH, s.c_str(), static_cast<int>(s.size()));
}

void Cell(HDC dc, const Game& g, int x, int y, wchar_t ch, COLORREF c) { Text(dc, g, x * 2, y, std::wstring(1, ch), c); }

void Paint(Game& g, HDC hdc) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ ob = SelectObject(dc, bmp);
    SetDCBrushColor(dc, g.look.background);
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (g.intro) {
        if (g.snapshot) {
            HDC sdc = CreateCompatibleDC(hdc);
            HGDIOBJ os = SelectObject(sdc, g.snapshot);
            BitBlt(dc, 0, 0, rc.right, rc.bottom - g.introOffset, sdc, 0, g.introOffset, SRCCOPY);
            SelectObject(sdc, os);
            DeleteDC(sdc);
        }
    } else {
        HGDIOBJ of = SelectObject(dc, g.font);
        SetBkMode(dc, TRANSPARENT);
        wchar_t hud[160];
        swprintf_s(hud, L"SCORE %06d   HI %06d   LEVEL %d   %s", g.score, g.hi, g.level,
                   std::wstring(static_cast<size_t>(std::max(0, g.lives - 1)), L'C').c_str());
        Text(dc, g, 0, -2, hud, g.look.muted);
        bool flash = g.levelTicks && (g.levelTicks / 8) % 2;
        COLORREF wall = flash ? g.look.text : g.look.accent;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                char c = g.cells[y][x];
                if (c == '#' || c == '-') {
                    bool joinRight = false;
                    wchar_t w = WallGlyph(g, x, y, joinRight);
                    std::wstring s(1, w);
                    if (joinRight && c == '#') s += IsWall(g, x + 1, y) && g.cells[y][x + 1] == '-' ? L'\x2500' : L'\x2550';
                    if (c == '-') s += L'\x2500';
                    Text(dc, g, x * 2, y, s, c == '-' ? g.look.muted : wall);
                } else if (c == '.') {
                    Cell(dc, g, x, y, L'\x00B7', g.look.text);
                } else if (c == 'o' && (g.tick / 8) % 2 == 0) {
                    Cell(dc, g, x, y, L'\x25CF', g.look.text);
                }
            }
        if (g.fruitTicks) Cell(dc, g, 9, 11, L'\x2666', RGB(220, 40, 60));
        COLORREF player = g.dark ? RGB(255, 214, 10) : RGB(205, 140, 0);
        if (!g.over) {
            wchar_t pc = L'O';
            if (g.deathTicks) {
                const wchar_t shrink[] = L"*+x\x00B7 ";
                int k = std::clamp((75 - g.deathTicks) / 12, 0, 5);
                pc = k == 0 ? L'O' : shrink[k - 1];
            } else if (g.mouth && g.pdir != None) {
                const wchar_t open[4] = {L'V', L'>', L'^', L'<'};
                pc = open[g.pdir];
            } else if (g.pdir == Right) {
                pc = L'C';
            }
            Cell(dc, g, g.px, g.py, pc, player);
        }
        if (!g.deathTicks && !g.levelTicks && !g.over) {
            for (const auto& gh : g.ghosts) {
                wchar_t ch = L'\x03A9';  // a ghost-like omega
                COLORREF c = gh.color;
                if (gh.st == GS::Frightened) {
                    ch = L'\x03C9';
                    c = g.frightTicks < 60 && (g.tick / 6) % 2 ? g.look.text : RGB(40, 80, 230);
                } else if (gh.st == GS::Eyes) {
                    ch = L'"';
                    c = g.look.muted;
                }
                Cell(dc, g, gh.x, gh.y, ch, c);
            }
        }
        if (g.popupTicks) Text(dc, g, g.popupX * 2 - 1, g.popupY, std::to_wstring(g.popupScore), RGB(40, 80, 230));
        auto center = [&](const std::wstring& s, COLORREF c) { Text(dc, g, W - static_cast<int>(s.size()) / 2, 11, s, c); };
        if (g.over)
            center(L" GAME  OVER - Enter to play again ", RGB(214, 40, 40));
        else if (g.paused)
            center(L" PAUSED ", g.look.text);
        else if (g.readyTicks)
            center(L"READY!", player);
        std::wstring help = std::wstring(L"Arrows/WASD move   P pause   ") +
                            (sfx::Muted() ? L"M sound on" : L"M sound off") + L"   Esc back to your text";
        Text(dc, g, W - static_cast<int>(help.size()) / 2, H + 1, help, g.look.muted);
        SelectObject(dc, of);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// Picks a Consolas size that fits the maze plus the score and help lines, no bigger than the editor's.
void Measure(Game& g) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    LOGFONTW lf{};
    GetObjectW(g.look.font, sizeof lf, &lf);
    int h = std::abs(lf.lfHeight ? lf.lfHeight : 16);
    HDC dc = GetDC(g.hwnd);
    for (;; --h) {
        if (g.font) DeleteObject(g.font);
        g.font = CreateFontW(-h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        HGDIOBJ of = SelectObject(dc, g.font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, of);
        g.charW = std::max<int>(1, tm.tmAveCharWidth);
        g.lineH = std::max<int>(1, tm.tmHeight);
        if (h <= 6 || ((H + 4) * g.lineH <= rc.bottom && (2 * W + 2) * g.charW <= rc.right)) break;
    }
    ReleaseDC(g.hwnd, dc);
    g.ox = std::max(0, (static_cast<int>(rc.right) - 2 * W * g.charW) / 2);
    g.oy = std::max(2 * g.lineH, (static_cast<int>(rc.bottom) - (H + 2) * g.lineH) / 2 + g.lineH);
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
            // The old text is pushed up a line at a time, as if Enter were held down.
            g->introOffset += g->introStep;
            RECT rc;
            GetClientRect(h, &rc);
            if (g->introOffset >= rc.bottom) {
                g->intro = false;
                Measure(*g);
                NewGame(*g);
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
        if (wp == 'M') sfx::SetMuted(!sfx::Muted());
        if (g->intro) return 0;
        switch (wp) {
        case VK_UP: case 'W': g->want = Up; break;
        case VK_LEFT: case 'A': g->want = Left; break;
        case VK_DOWN: case 'S': g->want = Down; break;
        case VK_RIGHT: case 'D': g->want = Right; break;
        case 'P':
            if (!g->over) {
                g->paused = !g->paused;
                if (g->paused) sfx::StopAll();
            }
            break;
        case VK_RETURN:
            if (g->over) NewGame(*g);
            break;
        }
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;
    case WM_KILLFOCUS:
        if (g && !g->over && !g->intro) g->paused = true;
        sfx::StopAll();
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        return 0;
    case WM_SIZE:
        if (g && !g->intro) Measure(*g);
        return 0;
    case WM_NCDESTROY:
        if (g) {
            if (g->snapshot) DeleteObject(g->snapshot);
            if (g->font) DeleteObject(g->font);
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
    int lum = GetRValue(look.background) * 3 + GetGValue(look.background) * 6 + GetBValue(look.background);
    g_game->dark = lum < 1280;
    g_game->hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, area.left, area.top,
                                   area.right - area.left, area.bottom - area.top, parent, nullptr,
                                   reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetWindowPos(g_game->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Measure(*g_game);
    g_game->introStep = g_game->lineH;
    ResetMaze(*g_game);
    SetFocus(g_game->hwnd);
    SetTimer(g_game->hwnd, kTimer, kTickMs, nullptr);
}

void Reposition(RECT area) {
    if (!g_game) return;
    SetWindowPos(g_game->hwnd, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                 SWP_NOACTIVATE);
}

}  // namespace pacman
