#include "markdown.h"

#include <cwctype>

namespace md {
namespace {

const std::wstring kEscapable = L"\\*~[]#|-+.)";

bool IsEscapable(wchar_t c) { return kEscapable.find(c) != std::wstring::npos; }

std::vector<std::wstring> SplitLines(const std::wstring& s) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r' || c == L'\n') {
            lines.push_back(cur);
            cur.clear();
            if (c == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') ++i;
        } else {
            cur.push_back(c);
        }
    }
    lines.push_back(cur);
    return lines;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L' ');
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L' ');
    return s.substr(a, b - a + 1);
}

struct Attr {
    bool bold = false, italic = false, strike = false;
    std::wstring url;
    bool operator==(const Attr& o) const {
        return bold == o.bold && italic == o.italic && strike == o.strike && url == o.url;
    }
    bool Plain() const { return !bold && !italic && !strike && url.empty(); }
};

// Inline Markdown -> plain text + spans.
void ParseInline(const std::wstring& s, std::wstring& text, std::vector<Span>& spans) {
    std::vector<Attr> attrs;
    Attr cur;
    auto put = [&](wchar_t c, const Attr& a) {
        text.push_back(c);
        attrs.push_back(a);
    };
    size_t n = s.size();
    for (size_t i = 0; i < n;) {
        wchar_t c = s[i];
        if (c == L'\\' && i + 1 < n && IsEscapable(s[i + 1])) {
            put(s[i + 1], cur);
            i += 2;
        } else if (c == L'*' && i + 1 < n && s[i + 1] == L'*') {
            cur.bold = !cur.bold;
            i += 2;
        } else if (c == L'~' && i + 1 < n && s[i + 1] == L'~') {
            cur.strike = !cur.strike;
            i += 2;
        } else if (c == L'*') {
            cur.italic = !cur.italic;
            ++i;
        } else if (c == L'[') {
            size_t mid = s.find(L"](", i + 1);
            size_t close = mid == std::wstring::npos ? std::wstring::npos : s.find(L')', mid + 2);
            if (mid != std::wstring::npos && close != std::wstring::npos) {
                std::wstring inner = s.substr(i + 1, mid - i - 1);
                Attr linkAttr = cur;
                linkAttr.url = s.substr(mid + 2, close - mid - 2);
                // Formatting markers inside the link text.
                std::wstring innerText;
                std::vector<Span> innerSpans;
                ParseInline(inner, innerText, innerSpans);
                for (size_t k = 0; k < innerText.size(); ++k) {
                    Attr a = linkAttr;
                    for (const Span& sp : innerSpans) {
                        if (k >= sp.start && k < sp.end) {
                            a.bold = a.bold != sp.bold;
                            a.italic = a.italic != sp.italic;
                            a.strike = a.strike != sp.strike;
                        }
                    }
                    put(innerText[k], a);
                }
                i = close + 1;
            } else {
                put(c, cur);
                ++i;
            }
        } else {
            put(c, cur);
            ++i;
        }
    }
    // Compress per-character attributes into spans.
    for (size_t i = 0; i < attrs.size();) {
        size_t j = i;
        while (j < attrs.size() && attrs[j] == attrs[i]) ++j;
        if (!attrs[i].Plain()) {
            Span sp;
            sp.start = i;
            sp.end = j;
            sp.bold = attrs[i].bold;
            sp.italic = attrs[i].italic;
            sp.strike = attrs[i].strike;
            sp.url = attrs[i].url;
            spans.push_back(sp);
        }
        i = j;
    }
}

bool IsSeparatorRow(const std::wstring& line, size_t& cells) {
    std::wstring t = Trim(line);
    if (t.size() < 2 || t.front() != L'|' || t.back() != L'|') return false;
    cells = 0;
    size_t i = 1;
    while (i < t.size()) {
        size_t j = t.find(L'|', i);
        std::wstring cell = Trim(t.substr(i, j - i));
        if (cell.size() < 3 || cell.find_first_not_of(L'-') != std::wstring::npos) return false;
        ++cells;
        i = j + 1;
    }
    return cells > 0;
}

std::vector<std::wstring> SplitRow(const std::wstring& line) {
    std::vector<std::wstring> cells;
    std::wstring t = Trim(line);
    if (t.empty() || t.front() != L'|') return cells;
    std::wstring cur;
    for (size_t i = 1; i < t.size(); ++i) {
        wchar_t c = t[i];
        if (c == L'\\' && i + 1 < t.size() && t[i + 1] == L'|') {
            cur.push_back(L'|');
            ++i;
        } else if (c == L'|') {
            cells.push_back(Trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    return cells;
}

bool ParseListPrefix(const std::wstring& line, Para& p, std::wstring& rest) {
    size_t sp = line.find_first_not_of(L' ');
    if (sp == std::wstring::npos || sp % 2 != 0) return false;
    size_t i = sp;
    wchar_t c = line[i];
    if ((c == L'-' || c == L'*' || c == L'+') && i + 1 < line.size() && line[i + 1] == L' ') {
        p.kind = Block::Bullet;
        p.marker = c;
        p.level = static_cast<int>(sp / 2);
        rest = line.substr(i + 2);
        return true;
    }
    size_t d = i;
    while (d < line.size() && iswdigit(line[d])) ++d;
    if (d > i && d + 1 < line.size() && (line[d] == L'.' || line[d] == L')') && line[d + 1] == L' ') {
        p.kind = Block::Numbered;
        p.marker = line[d];
        p.level = static_cast<int>(sp / 2);
        rest = line.substr(d + 2);
        return true;
    }
    return false;
}

// Plain text + spans -> inline Markdown.
std::wstring SerializeInline(const std::wstring& text, const std::vector<Span>& spans, bool inTable) {
    std::vector<Attr> attrs(text.size());
    for (const Span& sp : spans) {
        for (size_t k = sp.start; k < sp.end && k < text.size(); ++k) {
            attrs[k].bold = sp.bold;
            attrs[k].italic = sp.italic;
            attrs[k].strike = sp.strike;
            attrs[k].url = sp.url;
        }
    }
    std::wstring out;
    Attr open;  // currently open markers (url unused)
    auto close = [&] {
        if (open.strike) out += L"~~";
        if (open.italic) out += L"*";
        if (open.bold) out += L"**";
        open = Attr{};
    };
    auto openAs = [&](const Attr& a) {
        if (a.bold) out += L"**";
        if (a.italic) out += L"*";
        if (a.strike) out += L"~~";
        open.bold = a.bold;
        open.italic = a.italic;
        open.strike = a.strike;
    };
    bool hasLinkPattern = text.find(L"](") != std::wstring::npos;
    for (const Span& sp : spans)
        if (!sp.url.empty()) hasLinkPattern = true;
    std::wstring curUrl;
    for (size_t k = 0; k < text.size(); ++k) {
        const Attr& a = attrs[k];
        if (a.url != curUrl) {
            close();
            if (!curUrl.empty()) out += L"](" + curUrl + L")";
            curUrl = a.url;
            if (!curUrl.empty()) out += L"[";
        }
        if (a.bold != open.bold || a.italic != open.italic || a.strike != open.strike) {
            close();
            openAs(a);
        }
        wchar_t c = text[k];
        bool sameNext = k + 1 < text.size() && attrs[k + 1] == a;
        switch (c) {
        case L'*':
            out += L"\\*";
            break;
        case L'~':
            if ((k > 0 && text[k - 1] == L'~') || (k + 1 < text.size() && text[k + 1] == L'~'))
                out += L"\\~";
            else
                out += c;
            break;
        case L'[':
            out += hasLinkPattern ? L"\\[" : L"[";
            break;
        case L'|':
            out += inTable ? L"\\|" : L"|";
            break;
        case L'\\': {
            // Keep "C:\Work" readable: only escape when the next output char could be read as an escape.
            bool risky = k + 1 < text.size() ? !sameNext || IsEscapable(text[k + 1]) : !a.Plain();
            out += risky ? L"\\\\" : L"\\";
            break;
        }
        default:
            out += c;
        }
    }
    close();
    if (!curUrl.empty()) out += L"](" + curUrl + L")";
    return out;
}

// Escape a body paragraph's leading characters that would otherwise start a block.
std::wstring EscapeBlockStart(const std::wstring& inl) {
    size_t sp = inl.find_first_not_of(L' ');
    if (sp == std::wstring::npos) return inl;
    wchar_t c = inl[sp];
    if (c == L'#') {
        size_t h = sp;
        while (h < inl.size() && inl[h] == L'#') ++h;
        if (sp == 0 && h - sp <= 6 && h < inl.size() && inl[h] == L' ') return L"\\" + inl;
        return inl;
    }
    if ((c == L'-' || c == L'+') && sp + 1 < inl.size() && inl[sp + 1] == L' ')
        return inl.substr(0, sp) + L"\\" + inl.substr(sp);
    if (c == L'|' && sp == 0) return L"\\" + inl;
    size_t d = sp;
    while (d < inl.size() && iswdigit(inl[d])) ++d;
    if (d > sp && d + 1 < inl.size() && (inl[d] == L'.' || inl[d] == L')') && inl[d + 1] == L' ')
        return inl.substr(0, d) + L"\\" + inl.substr(d);
    return inl;
}

}  // namespace

std::wstring NormalizeBreaks(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') continue;
        out.push_back(s[i] == L'\n' ? L'\r' : s[i]);
    }
    return out;
}

std::vector<Para> Parse(const std::wstring& markdown) {
    std::vector<Para> doc;
    std::vector<std::wstring> lines = SplitLines(markdown);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring& line = lines[i];
        size_t sepCells = 0;
        if (!line.empty() && line[0] == L'|' && i + 1 < lines.size() && IsSeparatorRow(lines[i + 1], sepCells) &&
            SplitRow(line).size() == sepCells) {
            Para header;
            header.kind = Block::TableRow;
            header.cells = SplitRow(line);
            doc.push_back(header);
            i += 2;
            while (i < lines.size() && !lines[i].empty() && lines[i][0] == L'|') {
                Para row;
                row.kind = Block::TableRow;
                row.cells = SplitRow(lines[i]);
                row.cells.resize(sepCells);
                doc.push_back(row);
                ++i;
            }
            --i;
            continue;
        }
        Para p;
        std::wstring rest = line;
        size_t h = 0;
        while (h < line.size() && line[h] == L'#') ++h;
        if (h >= 1 && h <= 6 && h < line.size() && line[h] == L' ') {
            p.kind = Block::Heading;
            p.level = static_cast<int>(h);
            rest = line.substr(h + 1);
        } else if (!ParseListPrefix(line, p, rest)) {
            rest = line;
        }
        ParseInline(rest, p.text, p.spans);
        doc.push_back(p);
    }
    return doc;
}

std::wstring Serialize(const std::vector<Para>& doc) {
    std::wstring out;
    int counters[9] = {};
    for (size_t i = 0; i < doc.size(); ++i) {
        const Para& p = doc[i];
        if (i) out += L'\r';
        if (p.kind != Block::Numbered && p.kind != Block::Bullet) {
            for (int& c : counters) c = 0;
        }
        switch (p.kind) {
        case Block::TableRow: {
            bool header = i == 0 || doc[i - 1].kind != Block::TableRow;
            std::wstring row = L"|";
            for (const auto& cell : p.cells) row += L" " + SerializeInline(cell, {}, true) + L" |";
            out += row;
            if (header) {
                out += L"\r|";
                for (size_t c = 0; c < p.cells.size(); ++c) out += L" --- |";
            }
            break;
        }
        case Block::Heading:
            out += std::wstring(static_cast<size_t>(p.level), L'#') + L" " + SerializeInline(p.text, p.spans, false);
            break;
        case Block::Bullet:
        case Block::Numbered: {
            int lvl = p.level < 0 ? 0 : (p.level > 8 ? 8 : p.level);
            for (int d = lvl + 1; d < 9; ++d) counters[d] = 0;
            std::wstring prefix(static_cast<size_t>(lvl) * 2, L' ');
            if (p.kind == Block::Bullet) {
                counters[lvl] = 0;
                prefix += p.marker;
            } else {
                prefix += std::to_wstring(++counters[lvl]) + p.marker;
            }
            out += prefix + L" " + SerializeInline(p.text, p.spans, false);
            break;
        }
        default:
            out += EscapeBlockStart(SerializeInline(p.text, p.spans, false));
        }
    }
    return out;
}

bool HasFormatting(const std::vector<Para>& doc) {
    for (const Para& p : doc)
        if (p.kind != Block::Body || !p.spans.empty()) return true;
    return false;
}

}  // namespace md
