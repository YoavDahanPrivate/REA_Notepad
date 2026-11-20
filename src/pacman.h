#pragma once

// Easter egg: an ASCII maze-chase game ("go pacman now") that plays in a window laid over the
// editor. Like the invaders game it never touches the document and calls onExit on Esc.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>

#include "invaders.h"  // Look

namespace pacman {

using Look = invaders::Look;

void Register(HINSTANCE inst);
void Start(HWND parent, RECT area, HBITMAP snapshot, const Look& look, std::function<void()> onExit);
bool Running();
void Stop();
void Reposition(RECT area);

}  // namespace pacman
