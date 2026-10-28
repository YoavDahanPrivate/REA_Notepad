#pragma once

// A small Markdown model covering what Notepad's formatting toolbar can produce:
// headings, bulleted/numbered lists with nesting, bold, italic, strikethrough,
// links and pipe tables. Each line is one paragraph; lines are joined with '\r'.
// Anything else stays literal text, and callers check round trips so unsupported
// Markdown is never silently rewritten.

#include <string>
#include <vector>

namespace md {

enum class Block { Body, Heading, Bullet, Numbered, TableRow };

struct Span {
    size_t start = 0, end = 0;  // offsets into Para::text
    bool bold = false, italic = false, strike = false;
    std::wstring url;  // non-empty for links
};

struct Para {
    Block kind = Block::Body;
    int level = 0;          // heading 1..6, list nesting 0..8
    wchar_t marker = L'-';  // bullet marker (- * +) or number delimiter (. ))
    std::wstring text;
    std::vector<Span> spans;  // formatted parts of text, ordered, non-overlapping
    std::vector<std::wstring> cells;  // TableRow only
};

std::vector<Para> Parse(const std::wstring& markdown);
std::wstring Serialize(const std::vector<Para>& doc);
bool HasFormatting(const std::vector<Para>& doc);

// Line endings normalized to '\r' so Parse/Serialize round trips can be compared.
std::wstring NormalizeBreaks(const std::wstring& s);

}  // namespace md
