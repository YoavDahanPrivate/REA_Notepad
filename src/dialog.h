#pragma once

// WinUI ContentDialog lookalike: the owner window is dimmed, a rounded card shows a
// 20px semibold title, optional body text and text boxes, and a gray footer band with
// an accent primary button. Used for Go to line, links, tables, save prompts and notices.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace dlg {

struct Field {
    std::wstring label, value, placeholder;
    bool number = false;
};

struct Spec {
    std::wstring title, body;
    std::vector<Field> fields;
    std::wstring primary, secondary, close;  // empty = no button
    int width = 360;
};

enum Choice { Close = 0, Primary = 1, Secondary = 2 };

// Shows the dialog modally; fields receive the edited values.
int Show(HWND owner, Spec& spec);

// "New in Notepad" style feature tour dialog.
void ShowWhatsNew(HWND owner);

void Register(HINSTANCE inst);

}  // namespace dlg
