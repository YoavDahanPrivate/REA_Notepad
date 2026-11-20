#pragma once

// Easter egg: an ASCII Space Invaders that plays in a window laid over the editor.
// The game never touches the document; it draws on its own child window and calls
// onExit when the player presses Esc.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

namespace invaders {

struct Look {
    HFONT font;  // monospace
    COLORREF background, text, accent, muted;
};

void Register(HINSTANCE inst);

// Covers `area` (client coordinates of parent). `snapshot` (may be null, taken over by the
// game) is a picture of the editor that scrolls away line by line before play starts.
void Start(HWND parent, RECT area, HBITMAP snapshot, const Look& look, std::function<void()> onExit);
bool Running();
void Stop();  // same as the player pressing Esc
void Reposition(RECT area);

}  // namespace invaders
