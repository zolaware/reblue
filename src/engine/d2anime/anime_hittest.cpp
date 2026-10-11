/**
 * @file    engine/d2anime/anime_hittest.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/d2anime/anime_hittest.h"

#include <rex/types.h>

#include "gpu/gpu.h"
#include "platform/platform.h"

namespace bd::engine {

namespace {

// AnimeMenu_CalcItemPosition spreads the extent over the cell count rather than
// packing cells at their own size, so a grid's stride is wider than its item.
f32 Stride(f32 extent, f32 item, int count) {
  if (count <= 1)
    return item;
  return item + (extent - item * f32(count)) / f32(count - 1);
}


// AnimeMenu_DrawScrollbar's own numbers: the track sits fifteen pixels clear of
// the list, is ten wide, and never draws a thumb shorter than that.
constexpr f32 kScrollbarGap = 15.0f;
constexpr f32 kScrollbarWidth = 10.0f;

} // namespace

bool MenuCellAt(const AnimeMenu &menu, f32 x, f32 y, MenuCell &out) {
  const int rows = int(menu.GridRows());
  const int cols = int(menu.GridCols());
  if (rows <= 0 || cols <= 0)
    return false;

  // PosX/PosY, not OriginX/OriginY. AnimeMenu_CalcChildTemplatePos places each
  // drawn template at stride*index plus pos, while CalcItemPosition reports the
  // cursor anchor, which sits a fixed offset away. The boxes on screen are the
  // ones a pointer has to hit.
  const f32 boxX = menu.PosX();
  const f32 boxY = menu.PosY();
  const f32 itemW = menu.ItemW();
  const f32 itemH = menu.ItemH();
  const f32 rowStride = Stride(menu.ExtentH(), itemH, rows);
  const f32 colStride = Stride(menu.ExtentW(), itemW, cols);

  const f32 localX = x - boxX;
  const f32 localY = y - boxY;
  if (localX < 0.0f || localY < 0.0f)
    return false;

  const int col = colStride > 0.0f ? int(localX / colStride) : 0;
  const int row = rowStride > 0.0f ? int(localY / rowStride) : 0;
  if (row >= rows || col >= cols)
    return false;

  // Inside the band but past the cell itself is a gap between cells, not a hit.
  if (localX - f32(col) * colStride > itemW)
    return false;
  if (localY - f32(row) * rowStride > itemH)
    return false;

  // Orientation picks the index order the cursor walks in, so it has to pick
  // the order the hit test reads back in too.
  const u32 orientation = menu.Orientation();
  const u32 scrollOffset = menu.ScrollOffset();
  const int slot = orientation == 0 ? row * cols + col : col * rows + row;
  const int index =
      int(scrollOffset) * (orientation == 0 ? cols : rows) + slot;
  if (index < 0 || index >= menu.EntryCount())
    return false;

  out.row = row;
  out.col = col;
  out.index = index;
  return true;
}

bool MenuCellPointerX(const AnimeMenu &menu, int &index, f32 &localX) {
  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return false;

  MenuCell cell{};
  if (!MenuCellAt(menu, x, y, cell))
    return false;

  const f32 itemW = menu.ItemW();
  if (itemW <= 0.0f)
    return false;

  const int cols = int(menu.GridCols());
  const f32 colStride = Stride(menu.ExtentW(), itemW, cols);
  index = cell.index;
  localX = x - menu.PosX() - f32(cell.col) * colStride;
  return true;
}

bool MenuRowPointerX(const AnimeMenu &menu, int index, f32 &localX) {
  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return false;

  const f32 itemW = menu.ItemW();
  const int cols = int(menu.GridCols());
  const int rows = int(menu.GridRows());
  if (itemW <= 0.0f || cols <= 0 || rows <= 0 || index < 0)
    return false;

  // The index is absolute and the grid is a window onto the list, so the
  // column is the scrolled slot's, the same way MenuCellAt reads one back.
  const bool rowMajor = menu.Orientation() == 0;
  const int slot = index - int(menu.ScrollOffset()) * (rowMajor ? cols : rows);
  if (slot < 0)
    return false;
  const int col = rowMajor ? slot % cols : slot / rows;
  if (col >= cols)
    return false;
  const f32 colStride = Stride(menu.ExtentW(), itemW, cols);
  localX = x - menu.PosX() - f32(col) * colStride;
  return true;
}

bool MenuScrollbarAt(const AnimeMenu &menu, int pages, MenuScrollbar &out) {
  const int rows = int(menu.GridRows());
  const int cols = int(menu.GridCols());
  if (rows <= 0 || cols <= 0 || pages <= 1)
    return false;

  // The draw's own gate: a list that fits its window has no bar to grab.
  if (menu.EntryCount() <= rows * cols)
    return false;

  // Orientation picks which way the window scrolls, and the bar lies along
  // that axis on the far side of the list.
  const bool vertical = menu.Orientation() == 0;
  const int visible = vertical ? rows : cols;
  out.vertical = vertical;
  if (vertical) {
    out.x = menu.PosX() + menu.ExtentW() + kScrollbarGap;
    out.y = menu.PosY();
    out.w = kScrollbarWidth;
    out.h = menu.ExtentH();
    out.trackStart = out.y;
    out.trackLen = out.h;
  } else {
    out.x = menu.PosX();
    out.y = menu.PosY() + menu.ExtentH() + kScrollbarGap;
    out.w = menu.ExtentW();
    out.h = kScrollbarWidth;
    out.trackStart = out.x;
    out.trackLen = out.w;
  }

  out.thumbLen = out.trackLen / (f32(pages) / f32(visible) + 1.0f);
  if (out.thumbLen < kScrollbarWidth)
    out.thumbLen = kScrollbarWidth;
  out.thumbStart = out.trackStart + f32(menu.ScrollOffset()) / f32(pages - 1) *
                                        (out.trackLen - out.thumbLen);
  return true;
}

int CursorRowEdgeDirection(const AnimeMenu &menu) {
  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return 0;

  const f32 left = menu.PosX();
  const f32 top = menu.PosY();
  if (x < left || x > left + menu.ExtentW())
    return 0;
  if (y < top)
    return -1;
  if (y > top + menu.ExtentH())
    return 1;
  return 0;
}

bool CursorInMenuSpace(f32 &x, f32 &y) {
  f32 wx = 0.0f;
  f32 wy = 0.0f;
  if (!bd::platform::Mouse().Position(wx, wy))
    return false;

  f32 winW = 0.0f;
  f32 winH = 0.0f;
  if (!bd::platform::Mouse().WindowSize(winW, winH) || winW <= 0.0f ||
      winH <= 0.0f)
    return false;

  // The window is not the picture. Present blits the composite into the
  // aspect-fit rect of Output::RenderAspect and leaves bars around it, so a
  // pointer has to come back through the same rect the picture went out
  // through. Identical at 16:9, where the fit is the whole window.
  u32 fitW = u32(winW);
  u32 fitH = u32(winH);
  i32 offX = 0;
  i32 offY = 0;
  bd::gpu::Output::ComputeFit(u32(winW), u32(winH),
                                   bd::gpu::Output::RenderAspect(), fitW, fitH,
                                   offX, offY);
  if (!fitW || !fitH)
    return false;

  // Design canvas units across the picture, before the 2D layer's own fit.
  x = (wx - f32(offX)) * bd::gpu::kDesignCanvasWidth / f32(fitW);
  y = (wy - f32(offY)) * bd::gpu::kDesignCanvasHeight / f32(fitH);

  // FitDesignCanvasVertices scales every 2D draw about the canvas center to
  // reach a render rect of another ratio, so undo that scale to return to
  // the coordinates the menu was authored in. Both scales are 1 at 16:9.
  const f32 scaleX = bd::gpu::Output::DesignScaleX();
  const f32 scaleY = bd::gpu::Output::DesignScaleY();
  if (scaleX <= 0.0f || scaleY <= 0.0f)
    return false;
  constexpr f32 kCenterX = bd::gpu::kDesignCanvasWidth * 0.5f;
  constexpr f32 kCenterY = bd::gpu::kDesignCanvasHeight * 0.5f;
  x = kCenterX + (x - kCenterX) / scaleX;
  y = kCenterY + (y - kCenterY) / scaleY;
  return true;
}

} // namespace bd::engine
