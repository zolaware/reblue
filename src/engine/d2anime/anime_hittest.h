/**
 * @file    engine/d2anime/anime_hittest.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include "engine/d2anime/anime_menu.h"

namespace bd::engine {

struct MenuCell {
  int row;
  int col;
  int index;
};

// Resolves a point in menu space to the entry under it. False when the point is
// outside the menu's extent or falls past the last entry.
bool MenuCellAt(const AnimeMenu &menu, f32 x, f32 y, MenuCell &out);

// The entry under the pointer and where the pointer sits inside it, in the row
// template's own coordinates: 0 at the row's left edge, itemW at its right.
// Those are the coordinates a row template states its element offsets in, so
// this resolves a click to the control it hit.
bool MenuCellPointerX(const AnimeMenu &menu, int &index, f32 &localX);

// The same reading for one named entry, ignoring how far above or below its
// row the pointer has wandered. What a drag reads once it has latched onto a
// row, since a 34px row is easy to slip off of while dragging along it.
bool MenuRowPointerX(const AnimeMenu &menu, int index, f32 &localX);

// The scrollbar AnimeMenu_DrawScrollbar puts beside a list that outgrows its
// window. Positions run along the track's own axis, so a vertical bar measures
// them down the screen and a horizontal one across it.
struct MenuScrollbar {
  bool vertical;
  f32 x, y, w, h;
  f32 trackStart, trackLen;
  f32 thumbStart, thumbLen;
};

// The bar as the engine draws it, given the page count
// AnimeMenu_GetScrollPageCount reports for the same menu. False when the list
// fits its window, which is when no bar is drawn.
bool MenuScrollbarAt(const AnimeMenu &menu, int pages, MenuScrollbar &out);

// -1 when the pointer sits above the menu's rows, +1 when below, and 0 when it
// is level with them or outside the list's own width. Holding the pointer in
// either band is what walks a list past the edge of its scroll window.
int CursorRowEdgeDirection(const AnimeMenu &menu);

// Converts the mouse's window pixel position onto the 1280x720 design canvas
// that a menu's geometry is expressed in. False when the mouse position or
// window size is unavailable.
bool CursorInMenuSpace(f32 &x, f32 &y);

} // namespace bd::engine
