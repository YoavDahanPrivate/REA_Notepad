#include "ascii.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <map>

namespace ascii {
namespace {

using Grid = std::vector<std::vector<bool>>;  // [row][col], true = filled

// Our own 5-row block font. Glyphs are wider than tall because text cells are about
// twice as tall as they are wide.
const std::map<wchar_t, std::vector<const char*>>& Font() {
    static const std::map<wchar_t, std::vector<const char*>> f = {
        {L'A', {" #### ", "##  ##", "######", "##  ##", "##  ##"}},
        {L'B', {"##### ", "##  ##", "##### ", "##  ##", "##### "}},
        {L'C', {" #####", "##    ", "##    ", "##    ", " #####"}},
        {L'D', {"##### ", "##  ##", "##  ##", "##  ##", "##### "}},
        {L'E', {"######", "##    ", "##### ", "##    ", "######"}},
        {L'F', {"######", "##    ", "##### ", "##    ", "##    "}},
        {L'G', {" ##### ", "##     ", "##  ###", "##   ##", " ##### "}},
        {L'H', {"##  ##", "##  ##", "######", "##  ##", "##  ##"}},
        {L'I', {"##", "##", "##", "##", "##"}},
        {L'J', {"    ##", "    ##", "    ##", "##  ##", " #### "}},
        {L'K', {"##  ##", "## ## ", "####  ", "## ## ", "##  ##"}},
        {L'L', {"##    ", "##    ", "##    ", "##    ", "######"}},
        {L'M', {"##   ##", "### ###", "## # ##", "##   ##", "##   ##"}},
        {L'N', {"##   ##", "###  ##", "## # ##", "##  ###", "##   ##"}},
        {L'O', {" #### ", "##  ##", "##  ##", "##  ##", " #### "}},
        {L'P', {"##### ", "##  ##", "##### ", "##    ", "##    "}},
        {L'Q', {" #### ", "##  ##", "##  ##", "## ## ", " ## ##"}},
        {L'R', {"##### ", "##  ##", "##### ", "##  ##", "##  ##"}},
        {L'S', {" #####", "##    ", " #### ", "    ##", "##### "}},
        {L'T', {"######", "  ##  ", "  ##  ", "  ##  ", "  ##  "}},
        {L'U', {"##  ##", "##  ##", "##  ##", "##  ##", " #### "}},
        {L'V', {"##   ##", "##   ##", " ## ## ", " ## ## ", "  ###  "}},
        {L'W', {"##   ##", "##   ##", "## # ##", "### ###", "##   ##"}},
        {L'X', {"##  ##", " #### ", "  ##  ", " #### ", "##  ##"}},
        {L'Y', {"##  ##", " #### ", "  ##  ", "  ##  ", "  ##  "}},
        {L'Z', {"######", "   ## ", "  ##  ", " ##   ", "######"}},
        {L'0', {" ### ", "## ##", "## ##", "## ##", " ### "}},
        {L'1', {" ##", "###", " ##", " ##", " ##"}},
        {L'2', {"##### ", "    ##", " #### ", "##    ", "######"}},
        {L'3', {"##### ", "    ##", " #### ", "    ##", "##### "}},
        {L'4', {"##  ##", "##  ##", "######", "    ##", "    ##"}},
        {L'5', {"######", "##    ", "##### ", "    ##", "##### "}},
        {L'6', {" #### ", "##    ", "##### ", "##  ##", " #### "}},
        {L'7', {"######", "    ##", "   ## ", "  ##  ", "  ##  "}},
        {L'8', {" #### ", "##  ##", " #### ", "##  ##", " #### "}},
        {L'9', {" #### ", "##  ##", " #####", "    ##", " #### "}},
        {L'!', {"##", "##", "##", "  ", "##"}},
        {L'?', {"#### ", "   ##", " ### ", "     ", " ##  "}},
        {L'.', {"  ", "  ", "  ", "  ", "##"}},
        {L',', {"  ", "  ", "  ", "##", "# "}},
        {L':', {"  ", "##", "  ", "##", "  "}},
        {L'-', {"    ", "    ", "####", "    ", "    "}},
        {L'+', {"      ", "  ##  ", "######", "  ##  ", "      "}},
        {L'\'', {"##", "# ", "  ", "  ", "  "}},
        {L'/', {"    ##", "   ## ", "  ##  ", " ##   ", "##    "}},
        {L'_', {"      ", "      ", "      ", "      ", "######"}},
    };
    return f;
}

constexpr int kRows = 5;

bool BuiltInSupports(const std::wstring& upper) {
    for (wchar_t c : upper)
        if (c != L' ' && !Font().count(c)) return false;
    return true;
}

// Built-in letters, each pixel scaled to scale x scale cells.
Grid BuiltIn(const std::wstring& upper, int scale) {
    Grid g(static_cast<size_t>(kRows * scale));
    auto addCols = [&](const std::vector<const char*>* glyph, int width) {
        for (int x = 0; x < width; ++x)
            for (int r = 0; r < kRows; ++r) {
                bool on = false;
                if (glyph) {
                    const char* row = (*glyph)[r];
                    on = x < static_cast<int>(strlen(row)) && row[x] == '#';
                }
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) g[static_cast<size_t>(r * scale + sy)].push_back(on);
            }
    };
    bool first = true;
    for (wchar_t c : upper) {
        if (!first) addCols(nullptr, 1);  // letter spacing (the shadow fills it)
        first = false;
        if (c == L' ') {
            addCols(nullptr, 3);
            continue;
        }
        const auto& glyph = Font().at(c);
        int w = 0;
        for (const char* row : glyph) w = std::max(w, static_cast<int>(strlen(row)));
        addCols(&glyph, w);
    }
    return g;
}

// Any installed font: draw the text large, then sample it onto a grid of `rows` rows
// with cells half as wide as they are tall.
Grid Rasterize(const std::wstring& text, const std::wstring& face, int rows, double aspect = 0.5) {
    const int px = 160;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-px, 0, 0, 0, FW_BLACK, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face.c_str());
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    int w = sz.cx + px / 2, h = sz.cy + px / 4;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT all{0, 0, w, h};
    FillRect(dc, &all, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, px / 4, px / 8, text.c_str(), static_cast<int>(text.size()));
    GdiFlush();
    auto* p = static_cast<const DWORD*>(bits);
    auto ink = [&](int x, int y) { return (p[static_cast<size_t>(y) * w + x] & 0xFF) > 127; };
    int top = h, bottom = -1, left = w, right = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (ink(x, y)) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
    Grid g(static_cast<size_t>(rows));
    if (bottom >= top) {
        double cellH = static_cast<double>(bottom - top + 1) / rows, cellW = cellH * aspect;
        int cols = std::max(1, static_cast<int>((right - left + 1) / cellW + 0.5));
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                int y0 = top + static_cast<int>(r * cellH), y1 = top + static_cast<int>((r + 1) * cellH);
                int x0 = left + static_cast<int>(c * cellW), x1 = left + static_cast<int>((c + 1) * cellW);
                int on = 0, n = 0;
                for (int y = y0; y < std::min(y1, h); ++y)
                    for (int x = x0; x < std::min(x1, w); ++x) {
                        on += ink(x, y);
                        ++n;
                    }
                g[static_cast<size_t>(r)].push_back(n && on * 100 / n >= 45);
            }
    }
    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);
    return g;
}

bool At(const Grid& g, int r, int c) {
    return r >= 0 && c >= 0 && r < static_cast<int>(g.size()) && c < static_cast<int>(g[r].size()) && g[r][c];
}

// Solid blocks plus a shadow one cell right and below, drawn with box-drawing lines.
// fillChar lets the Hollow style draw the letters in a light shade instead.
std::vector<std::wstring> Shadow(const Grid& g, wchar_t fillChar = L'█') {
    int rows = static_cast<int>(g.size()), cols = rows ? static_cast<int>(g[0].size()) : 0;
    auto fill = [&](int r, int c) { return At(g, r, c); };
    auto shadow = [&](int r, int c) {
        return !fill(r, c) && (fill(r, c - 1) || fill(r - 1, c) || fill(r - 1, c - 1));
    };
    std::vector<std::wstring> out;
    for (int r = 0; r <= rows; ++r) {
        std::wstring line;
        for (int c = 0; c <= cols; ++c) {
            if (fill(r, c)) {
                line += fillChar;
            } else if (shadow(r, c)) {
                bool u = shadow(r - 1, c), d = shadow(r + 1, c), l = shadow(r, c - 1), rt = shadow(r, c + 1);
                wchar_t ch;
                if (u && d) ch = L'║';             // ║
                else if (l && rt) ch = L'═';       // ═
                else if (d && rt) ch = L'╔';       // ╔
                else if (d && l) ch = L'╗';        // ╗
                else if (u && l) ch = L'╝';        // ╝
                else if (u && rt) ch = L'╚';       // ╚
                else if (d) ch = fill(r, c - 1) ? L'╗' : L'╔';
                else if (u) ch = L'╝';
                else if (rt) ch = fill(r - 1, c) ? L'╚' : L'═';
                else if (l) ch = L'═';
                else ch = fill(r - 1, c) ? L'═' : L'║';
                line += ch;
            } else {
                line += L' ';
            }
        }
        out.push_back(line);
    }
    return out;
}

std::vector<std::wstring> Plain(const Grid& g, wchar_t on) {
    std::vector<std::wstring> out;
    for (const auto& row : g) {
        std::wstring line;
        for (bool b : row) line += b ? on : L' ';
        out.push_back(line);
    }
    return out;
}

// █ letters with a soft ▒ drop shadow one cell right and below.
std::vector<std::wstring> Soft(const Grid& g) {
    int rows = static_cast<int>(g.size()), cols = rows ? static_cast<int>(g[0].size()) : 0;
    std::vector<std::wstring> out;
    for (int r = 0; r <= rows; ++r) {
        std::wstring line;
        for (int c = 0; c <= cols; ++c)
            line += At(g, r, c) ? L'█' : At(g, r - 1, c - 1) ? L'▒' : L' ';
        out.push_back(line);
    }
    return out;
}

// Rows fade from full blocks at the top to light shade at the bottom.
std::vector<std::wstring> Gradient(const Grid& g) {
    static const wchar_t ramp[4] = {L'█', L'▓', L'▒', L'░'};
    std::vector<std::wstring> out;
    int rows = static_cast<int>(g.size());
    for (int r = 0; r < rows; ++r) {
        wchar_t ch = ramp[std::min(3, r * 4 / std::max(1, rows))];
        std::wstring line;
        for (bool b : g[r]) line += b ? ch : L' ';
        out.push_back(line);
    }
    return out;
}

// Letters drawn with the characters of the phrase itself, read left to right.
std::vector<std::wstring> Mosaic(const Grid& g, const std::wstring& text) {
    std::wstring src;
    for (wchar_t c : text)
        if (c != L' ') src += c;
    if (src.empty()) src = L"#";
    std::vector<std::wstring> out;
    size_t k = 0;
    for (const auto& row : g) {
        std::wstring line;
        for (bool b : row) line += b ? src[k++ % src.size()] : L' ';
        out.push_back(line);
    }
    return out;
}

// Braille: each character holds 2x4 dots, giving four times the vertical detail.
std::vector<std::wstring> Braille(const Grid& g) {
    static const int bit[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
    int rows = static_cast<int>(g.size()), cols = rows ? static_cast<int>(g[0].size()) : 0;
    std::vector<std::wstring> out;
    for (int r = 0; r < rows; r += 4) {
        std::wstring line;
        for (int c = 0; c < cols; c += 2) {
            int v = 0;
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    if (At(g, r + dy, c + dx)) v |= bit[dy][dx];
            // Blank cells use the empty braille pattern so every character has the same width.
            line += static_cast<wchar_t>(0x2800 + v);
        }
        line.erase(line.find_last_not_of(L'⠀') + 1);
        out.push_back(line);
    }
    return out;
}

// Two grid rows per text line using half blocks (Compact height).
std::vector<std::wstring> HalfBlocks(const Grid& g) {
    std::vector<std::wstring> out;
    for (size_t r = 0; r < g.size(); r += 2) {
        std::wstring line;
        for (size_t c = 0; c < g[r].size(); ++c) {
            bool top = g[r][c], bottom = r + 1 < g.size() && g[r + 1][c];
            line += top && bottom ? L'█' : top ? L'▀' : bottom ? L'▄' : L' ';
        }
        out.push_back(line);
    }
    return out;
}

void TrimRight(std::vector<std::wstring>& lines) {
    for (auto& l : lines) l.erase(l.find_last_not_of(L' ') + 1);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}

int Width(const std::vector<std::wstring>& lines) {
    size_t w = 0;
    for (const auto& l : lines) w = std::max(w, l.size());
    return static_cast<int>(w);
}

std::vector<std::wstring> RenderOne(const std::wstring& text, const Options& o) {
    std::wstring upper = text;
    for (wchar_t& c : upper) c = static_cast<wchar_t>(towupper(c));
    int scale = o.height == Height::Large ? 2 : o.height == Height::Huge ? 3 : 1;
    std::vector<std::wstring> lines;
    if (o.style == Style::Dots) {
        // Braille needs real curves, so always draw from a font (4 dots per text line).
        int textLines = LinesFor(o.height);
        lines = Braille(Rasterize(text, o.font.empty() ? L"Segoe UI" : o.font, textLines * 4, 1.0));
        TrimRight(lines);
        return lines;
    }
    Grid g;
    if (o.font.empty() && BuiltInSupports(upper)) {
        g = BuiltIn(upper, scale);
    } else {
        int rows = o.height == Height::Compact ? 6 : kRows * scale;
        g = Rasterize(text, o.font.empty() ? L"Segoe UI" : o.font, rows);
    }
    if (o.height == Height::Compact) {
        lines = HalfBlocks(g);
    } else {
        switch (o.style) {
        case Style::Shadow: lines = Shadow(g); break;
        case Style::Hollow: lines = Shadow(g, L'░'); break;
        case Style::Soft: lines = Soft(g); break;
        case Style::Gradient: lines = Gradient(g); break;
        case Style::Mosaic: lines = Mosaic(g, upper); break;
        case Style::Hash: lines = Plain(g, L'#'); break;
        default: lines = Plain(g, L'█'); break;
        }
    }
    TrimRight(lines);
    return lines;
}

}  // namespace

int LinesFor(Height h) {
    switch (h) {
    case Height::Compact: return 3;
    case Height::Large: return 11;
    case Height::Huge: return 16;
    default: return 6;
    }
}

std::vector<std::wstring> Render(const std::wstring& text, const Options& o) {
    // Split into words and pack as many as fit per banner.
    std::vector<std::wstring> words;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(L' ', i);
        if (j == std::wstring::npos) j = text.size();
        if (j > i) words.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    std::vector<std::vector<std::wstring>> banners;
    std::wstring cur;
    for (const auto& w : words) {
        std::wstring next = cur.empty() ? w : cur + L" " + w;
        if (!cur.empty() && o.maxColumns > 0 && Width(RenderOne(next, o)) > o.maxColumns) {
            banners.push_back(RenderOne(cur, o));
            cur = w;
        } else {
            cur = next;
        }
    }
    if (!cur.empty()) banners.push_back(RenderOne(cur, o));

    std::vector<std::wstring> out;
    for (size_t b = 0; b < banners.size(); ++b) {
        if (b) out.push_back(L"");
        int width = Width(banners[b]);
        int pad = o.center && o.maxColumns > width ? (o.maxColumns - width) / 2 : 0;
        for (const auto& l : banners[b]) out.push_back(l.empty() ? l : std::wstring(static_cast<size_t>(pad), L' ') + l);
    }
    return out;
}

}  // namespace ascii
