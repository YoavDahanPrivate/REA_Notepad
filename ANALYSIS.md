# Windows Notepad 11.2607.14.0: analysis and clone checklist

Target: `C:\Program Files\WindowsApps\Microsoft.WindowsNotepad_11.2607.14.0_x64__8wekyb3d8bbwe`

Two sources fed this document:

1. **rea (static, read-only):** `inspect_pe_imports`, `inspect_pe_resources`, `inspect_native_exports`
   and UTF-16 string extraction from `NotepadXamlUI.dll`. No Ghidra/IDA, so no decompilation.
2. **UI Automation walk of the running app (read-only):** every menu, submenu, toolbar button,
   dropdown, the find/replace panel, the Go to dialog and the Settings page were opened and their
   controls listed with names, shortcuts and sizes. rea's own live-UI tools only support macOS
   apps, and its feature-tracing/reconstruction-ledger tools only work on JavaScript/web targets,
   so this pass was done with Windows UI Automation instead.

## Package layout

| File | Size | Role |
|---|---|---|
| `Notepad\Notepad.exe` | 3.3 MB | Native Win32 host: window, frame, file I/O, printing |
| `Notepad\NotepadXamlUI.dll` (+ `.winmd`) | 6.4 MB | WinUI 3 / XAML UI (menus, tabs, dialogs, settings) |
| `Notepad\riched20.dll`, `msptls.dll` | 4.0 / 2.0 MB | Private copy of RichEdit, the actual text surface |
| `NotepadExplorerCommand\…dll` | 0.3 MB | "Edit in Notepad" Explorer context-menu handler |
| `OleControls\OleControls.dll` | 0.3 MB | OLE helpers (embedded objects in formatted text) |

## Notepad.exe imports (what the host does)

- **Window frame:** `DwmExtendFrameIntoClientArea`, `DwmDefWindowProc`, `DwmSetWindowAttribute`,
  `DwmGetWindowAttribute`, `SetWindowRgn`, `GetDpiForWindow`, `GetSystemMetricsForDpi`.
  So the tab strip is drawn into a DWM-extended title bar, with system caption buttons.
- **Title-bar text:** `UxTheme!DrawThemeTextEx`, `OpenThemeData`, `GetThemeSysFont`.
- **XAML hosting:** `Microsoft.UI.Windowing.Core!ContentPreTranslateMessage`.
- **Printing:** `COMDLG32!PageSetupDlgW`, `PrintDlgExW`; `GDI32!StartDocW/StartPage/EndPage/AbortDoc`.
- **Files:** `SHAddToRecentDocs`, `DragAcceptFiles`/`DragQueryFileW`, `RegisterDragDrop`,
  `SHOpenFolderAndSelectItems` ("Show in folder"), `CommandLineToArgvW`.
- **Text:** `icu.dll` (Unicode/regex), `elscore.dll` (language services, spell check).

## Measured layout (100% scale)

- Tab strip: tabs at y 10..42, starting 40px in (app icon to their left), ~231px wide,
  close button 32x24 with 9px right margin, "+" 32x24 right after the last tab.
- Menu row: 33px. File/Edit/View on the left; the formatting toolbar (328px) centered;
  Writing tools, What's new, account and Settings on the right.
- Formatting toolbar: Headings 56, Lists 56, Bold 32, Italic 32, Strikethrough 32, Link 32,
  Table 56, Clear formatting 32 px. Shown for untitled, `.txt` and `.md` tabs; hidden for other
  file types (e.g. `.bat`).
- Editor text starts 16px in and ~14px below the menu row.
- Status bar: 32px; "Ln, Col" | "N characters", then columns starting 654 / 379 / 312 / 142px
  from the right edge: Plain text | 100% | Windows (CRLF) | UTF-8.
- Light colors: title bar 232, tabs/menu/status 248, editor 249, dividers 234-235.

## Per-control specs (round 3: every popup opened and screenshotted)

Screenshots of the real app (`docs\real\`) and the side-by-side comparison sheets were used
locally while matching the UI; they are not in the repository. All sizes at 100% scale.

| Control | Real Notepad (measured) | Clone |
|---|---|---|
| **Menu flyouts** (File/Edit/View, submenus, toolbar dropdowns, context menus) | WinUI MenuFlyout: rounded 8px, 1px border, shadow; rows 33px apart; text 14px at 14px (43px with check/icon column); shortcuts 12px gray in a left-aligned column 16px from the right; chevron for submenus; 3px separators; min width 230 for menu bar menus and submenus; hover = rounded gray fill; opens 2px below the button; moving across the menu bar switches menus | Own flyout engine (`src\flyout.cpp`) drawn to those numbers, with keyboard (arrows, Enter, Esc, Left/Right across the menu bar) and scrolling for long lists ✅ |
| **File menu** | New tab, New window, New Markdown tab, Open, Recent ▸, Save, Save as, Save all │ Page setup, Print │ Close tab, Close window, Exit | ✅ same items, order, separators, shortcuts |
| **Edit menu** | Undo │ Cut, Copy, Paste, Delete │ Clear formatting, Search with Bing │ Define with Bing │ Find, Find next, Find previous, Replace, Go to │ Select all, Time/Date │ Font | ✅ (Font opens Settings with the Font card, as in Notepad) |
| **View menu** | Zoom ▸ (Zoom in/out/Restore, Ctrl+Plus/Minus/0), ✓ Status bar, ✓ Word wrap, Markdown ▸ (Formatted/Syntax) | ✅ |
| **Formatting toolbar** | 328px centered: "H1"▾ 56, list▾ 56, B, I, S, link 32 each, 2×2 table▾ 56, clear-formatting 32; hidden for non-text file types | ✅ same widths and glyphs (table and clear-formatting icons drawn to match) |
| **Headings ▾** | 180px flyout centered 6px right of the button's left edge; Title, Subtitle, Heading, Subheading, Section, Subsection each drawn in its own size (≈32/28/24/20/17/16px, 55px rows), Body 14px (40px row); current style marked with an accent bar | ✅ |
| **Lists ▾** | 178px: Bulleted list, Numbered list, Increase indent, Decrease indent (indent items disabled outside a list) | ✅ |
| **Table ▾** | 200px: caption "Insert table", 5×5 grid of 30px rounded cells (6px gaps) that highlight as you hover, separator, "Insert table" (dialog), "Edit table ▸" (disabled outside a table) | ✅ grid picker with "C × R table" caption; Edit table: insert row above/below, insert column left/right, delete row/column/table |
| **Writing tools ▾** (right side) | 230px: Write [New] Ctrl+Q, Rewrite Ctrl+D, Summarize Ctrl+M, Make shorter, Make longer, Change tone ▸ (Formal, Casual, Inspirational, Humor), Change format ▸ (Paragraph, List, Business, Academic, Marketing, Poetry) | ✅ same menus; choosing one explains it needs Microsoft's Copilot service ⛔ (service) |
| **What's new** (megaphone with blue dot) | "New in Notepad" dialog: 3 entries on the left with "New" badges, illustration card with heading, text and "Start exploring" | ✅ same layout with our own illustration and wording |
| **Account** (avatar) | Flyout with the account's picture, name, email and Sign out | 🟡 same flyout shape showing "Not signed in" + Sign in, which explains it needs a Microsoft account |
| **Settings** (gear) | Full-window page: back arrow + icon + "Notepad" in the title bar, 40px "Settings" title, cards 542px wide (66px headers, icons, subtitles, expanders, toggles "On/Off", combo boxes), "About this app" column on the right | ✅ same page: App theme, Font (family/style/size + sample), Word wrap, Formatting, Opening files, When Notepad starts, Recent Files, Spell check (per file type), Autocorrect, Writing tools; About column with our own text |
| **Editor context menu** | Icons; Undo │ Insert table │ Define/Search with Bing │ Write, Rewrite, Custom rewrite ▸, Summarize │ Spelling ▸ │ Display and Unicode ▸ │ bottom bar Cut, Copy, Paste, Select all, Delete (icon + label) | ✅ layout and command bar; 🟡 Spelling ▸ has no suggestions, "Show Unicode control characters" disabled |
| **Tab context menu** | New tab │ Close tab, Close other tabs, Close tabs to the right | ✅ |
| **Find and replace** | 620×88 panel centered over the text; chevron, find box with search icon, ↓ ↑, options (sliders) and ✕ 40px apart; Replace row with Replace (88) and Replace all (90); options flyout opens above: ✓ Match case, ✓ Wrap around | ✅ |
| **Go to line** | ContentDialog: window dimmed, 360px card, 20px semibold title, "Line number" box with accent underline, gray footer with accent "Go to" and "Cancel" | ✅ (same dialog style also used for save prompts, links, tables and notices) |

## Feature checklist

✅ done · 🟡 partial · ⛔ left out on purpose

### Window and tabs
| Item | Clone |
|---|---|
| Tabs in the title bar, Mica backdrop, app icon, "+" button | ✅ |
| Modified dot / close button per tab, middle-click close | ✅ |
| Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+T | ✅ |
| Drag a tab out into a new window | ⛔ |

### File menu
| Item | Shortcut | Clone |
|---|---|---|
| New tab | Ctrl+N | ✅ |
| New window | Ctrl+Shift+N | ✅ |
| New Markdown tab | | ✅ |
| Open | Ctrl+O | ✅ (txt / md / all) |
| Recent ▸ (files, No recent files, Clear list) | | ✅ |
| Save / Save as / Save all | Ctrl+S / Ctrl+Shift+S / Ctrl+Alt+S | ✅ (asks before dropping formatting) |
| Page setup, Print | Ctrl+P | ✅ (prints formatting, always black text) |
| Close tab / Close window / Exit | Ctrl+W / Ctrl+Shift+W | ✅ |

### Edit menu
| Item | Shortcut | Clone |
|---|---|---|
| Undo, Cut, Copy, Paste, Delete | Ctrl+Z/X/C/V, Del | ✅ (paste is always plain text) |
| Clear formatting | Ctrl+Space | ✅ |
| Search with Bing / Define with Bing | Ctrl+E | ✅ (opens the browser) |
| Find, Find next, Find previous, Replace | Ctrl+F, F3, Shift+F3, Ctrl+H | ✅ |
| Go to | Ctrl+G | ✅ |
| Select all, Time/Date | Ctrl+A, F5 | ✅ |
| Font | | ✅ (classic font dialog) |

### View menu
| Item | Clone |
|---|---|
| Zoom ▸ Zoom in / Zoom out / Restore default zoom (Ctrl+Plus/Minus/0, Ctrl+wheel) | ✅ |
| Status bar, Word wrap (toggles) | ✅ |
| Markdown ▸ Formatted / Syntax | ✅ |

### Formatting toolbar
| Item | Shortcut | Clone |
|---|---|---|
| Headings ▾ Title, Subtitle, Heading, Subheading, Section, Subsection, Body | | ✅ (saved as `#`..`######`) |
| Lists ▾ Bulleted list, Numbered list, Increase indent, Decrease indent | | ✅ |
| Bold / Italic / Strikethrough (toggle state shown) | Ctrl+B / Ctrl+I / Ctrl+Shift+X | ✅ |
| Link (display text + address, Ctrl+click to open, warning for non-web links) | Ctrl+K | ✅ |
| Table ▾ Insert table, Edit table ▸ | | 🟡 insert, add row above/below, delete row/table; no column editing or grid picker |
| Clear formatting | Ctrl+Space | ✅ |
| Formatting stored as Markdown | | ✅ `.md` saves Markdown; `.txt` asks to save as `.md` or drop formatting |

### Find and replace panel
| Item | Clone |
|---|---|
| Find box, Search down / Search up, Close | ✅ |
| Expand to Replace, Replace, Replace all | ✅ (keeps formatting in formatted tabs) |
| More options ▾ Match case, Wrap around | ✅ |

### Dialogs and settings
| Item | Clone |
|---|---|
| Go to line dialog | ✅ |
| Settings: App theme (Light / Dark / Use system setting) | ✅ |
| Settings: Font family / style / size with sample | 🟡 classic font dialog instead of inline combo boxes |
| Settings: Word wrap, Formatting toggle | ✅ |
| Settings: Opening files (new tab / new window) | ✅ |
| Settings: When Notepad starts (continue previous session / start new) | ✅ |
| Settings: Recent files toggle | ✅ |
| Settings: Spell check | 🟡 RichEdit spell checking on/off; no per-extension list, no suggestion menu |
| Settings: Autocorrect | ⛔ |
| Settings: About, links, Send feedback, Help | 🟡 short About text only |
| Settings as a full page inside the window | 🟡 shown as a dialog |

### Editor context menu
| Item | Clone |
|---|---|
| Undo, Cut, Copy, Paste, Delete, Select all | ✅ |
| Search / Define with Bing | ✅ |
| Insert Unicode control character ▸ | ✅ |
| Right-to-left reading order, Show Unicode control characters | ⛔ |
| Spelling suggestions | ⛔ |

### Status bar
| Item | Clone |
|---|---|
| Ln/Col, character count (and "x of y" with a selection) | ✅ |
| Plain text / Markdown label | ✅ |
| Zoom, line ending, encoding (click to change) | ✅ |

### Microsoft services (left out on purpose)
Writing tools / Copilot (rewrite, summarize, generate, credits), What's new, account avatar,
Microsoft licence and privacy links.

## Rendering details (round 4, measured from pixels)

- **Text anti-aliasing:** WinUI draws UI text with grayscale anti-aliasing (no ClearType color
  fringes); the clone now uses grayscale for all UI fonts.
- **Colors:** status bar text RGB(93,92,93); tab, menu and toolbar glyphs RGB(26,26,26).
- **App icon:** 13x16 px blue pad (gradient #7DD9F4 to #43AFCF), four #057093 binding rings,
  three #057093 text lines, #EAEAEA page edge, #984B0A back cover. The clone draws its own icon
  to these measurements (Microsoft's icon file is not copied).
- **Toolbar:** "H1" text, list, B/I/S, link, 2x2 table, "Ab"+eraser clear-formatting glyphs.
- `docs\details-comparison.png` shows icon, toolbar and status bar zoomed side by side.
- **Layers over Mica (round 5):** on a focused window the tab strip is bare Mica, the selected tab
  is white at ~70% over it (measured 242,244,249 over 214,217,236), and the menu row and status
  bar are white at ~50% (that's why the status bar takes a warm tint from the wallpaper).
  Dividers are black at ~5%. The clone extends Mica over the tab strip, menu row and status bar
  and paints these as translucent layers (hover/pressed too), so focused and unfocused looks
  follow Windows. The selected tab's title is semibold, 10px from the tab edge.

Where rea helps here: it lists the icon and string resources inside Notepad.exe and the UI
module (sizes, names, which features exist). How things are drawn (anti-aliasing, colors,
stroke weight) isn't stored in any file rea can read, so those come from screenshots.

## Extra feature (not in Windows Notepad): ##text## ASCII art

Type `##some text##` and then any key (space, Enter, Tab, a letter...): `##some text##` is
removed from the line, the banner appears on new lines below it, and the cursor moves to a new
line after the art. Code: `src\ascii.cpp`. Styles: 3D shadow (default), Hollow 3D, Soft shadow,
Gradient fade, Made of your text, Fine dots (braille), Solid blocks, Hash marks.
See `docs\ascii-styles.png`.

Inline codes pick a style or height for one banner: `#S3#text##` (style 3 in the menu),
`#H3#text##` (height 3), `#S3H2#text##` or `#H2S3#text##` (both). The menus show the codes,
e.g. "(S1) 3D shadow", "(H2) Normal (6 lines)". Unknown numbers use the menu default.
Styles: S1 3D shadow, S2 Hollow 3D, S3 Soft shadow, S4 Gradient fade, S5 Made of your text,
S6 Fine dots, S7 Solid blocks, S8 Hash marks. Heights: H1 3, H2 6, H3 11, H4 16 lines.

- Letters: our own 5-row block font (A-Z, 0-9, common punctuation) drawn with a 3D shadow in
  box-drawing characters, the "ANSI shadow" look. Anything else (Hebrew, symbols) is drawn from a
  Windows font and converted to the same grid, so every language works.
- Edit > ASCII art: on/off, Convert ##text## on this line (Ctrl+Shift+A), Height (Compact 3 /
  Normal 6 (default) / Large 11 / Huge 16 lines), Style (3D shadow / Solid blocks / Hash marks),
  Letters (built-in or Segoe UI, Arial Black, Impact, Georgia, Comic Sans MS, Courier New),
  Alignment (left / centered), Placement (keep the sentence with art below / replace ##text##).
- Long phrases wrap onto several banners to fit the window width; art lines use Consolas in
  formatted tabs so they line up.

## How the clone is checked

`build\<arch>\Notepad--.exe --selftest <log>` runs Markdown round trips (parse → editor → Markdown),
the toolbar commands, link and table insertion and clear formatting against a hidden window and
writes PASS/FAIL lines. Opening a `.md` file only shows it formatted when that round trip is
lossless; anything the formatter doesn't support stays in Syntax view so it is never rewritten.

Screenshots: `docs\comparison.png` (layout vs real Notepad), `docs\formatting.png`
(`docs\sample.md` opened in formatted view).
