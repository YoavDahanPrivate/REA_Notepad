#pragma once

// ASCII art banners for the ##text## feature. Letters come from our own 5-row block
// font (A-Z, 0-9, common punctuation) or, for anything else (other scripts, symbols, or
// when a font is chosen), from rasterizing an installed Windows font. Styles:
//   Shadow - solid blocks with a 3D shadow drawn in box-drawing characters
//   Solid  - solid blocks only
//   Hash   - '#' characters

#include <string>
#include <vector>

namespace ascii {

enum class Style {
    Shadow = 0,    // █ letters with a 3D box-drawing shadow (default)
    Solid = 1,     // █ only
    Hash = 2,      // # only
    Hollow = 3,    // ░ letters inside the 3D box-drawing frame
    Soft = 4,      // █ letters with a ▒ drop shadow
    Gradient = 5,  // rows fade █ ▓ ▒ ░ from top to bottom
    Mosaic = 6,    // letters built from the characters of the phrase itself
    Dots = 7,      // fine detail with braille dots (2x4 dots per character)
};
constexpr int kStyleCount = 8;
enum class Height { Compact = 0, Normal = 1, Large = 2, Huge = 3 };  // 3, 6, 11, 16 lines

struct Options {
    Style style = Style::Shadow;
    Height height = Height::Normal;
    std::wstring font;   // empty = built-in block letters (falls back to a font for other characters)
    bool center = false;
    int maxColumns = 0;  // 0 = no limit; otherwise words wrap onto several banners
};

// Returns the art lines (no trailing spaces).
std::vector<std::wstring> Render(const std::wstring& text, const Options& options);

int LinesFor(Height h);

}  // namespace ascii
