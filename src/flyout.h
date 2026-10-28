#pragma once

// WinUI-style flyout menus drawn from scratch: rounded popup, 32px rows, check and
// icon columns, right-aligned shortcuts, "New" badges, submenus, scrolling, keyboard
// navigation and hover-switching between menu bar menus. Measured from Notepad 11.2607.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace flyout {

enum class Kind {
    Item,
    Separator,
    Header,          // small gray caption
    StylePreview,    // item drawn in its own font size (heading styles)
    TableGrid,       // 5x5 hover grid for inserting a table
    CommandBar,      // row of icon+label buttons (context menu bottom row)
};

struct Item {
    Kind kind = Kind::Item;
    UINT id = 0;
    std::wstring text, shortcut, icon, badge;
    bool enabled = true;
    bool checked = false;
    bool checkable = false;  // reserves the check column for the whole menu
    bool current = false;    // accent bar marking the current choice
    int previewPx = 0;       // StylePreview font size at 100%
    std::vector<Item> sub;   // submenu items, or CommandBar buttons
};

struct Result {
    UINT id = 0;          // chosen command, 0 when dismissed
    int gridRows = 0;     // TableGrid choice
    int gridCols = 0;
    int switchTo = -1;    // menu bar index the user moved to while open
};

struct Options {
    bool keyboard = false;  // opened from the keyboard: highlight the first item
    int minWidth = 0;
    int maxHeight = 0;      // 0 = fit the screen
    bool above = false;     // open above the anchor (status bar)
    // Menu bar hover switching: given a screen point, return the menu bar index under it, or -1.
    std::function<int(POINT)> menuBarHit;
    int menuBarIndex = -1;
};

// Shows a flyout under anchor (screen coordinates) and runs it modally.
Result Show(HWND owner, const std::vector<Item>& items, RECT anchor, const Options& options = {});

void Register(HINSTANCE inst);

}  // namespace flyout
