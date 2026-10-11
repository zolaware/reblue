/**
 * @file    engine/d2anime/command_select_task.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/command_select_task.h"

#include <cstddef>

#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_menu.h"

namespace bd::engine {

namespace {

// Width a scrollbar takes off the list, hard-coded in Visual__DrawGridItems and
// again in CommandSelectTask_RepositionCursor. Only a list that divides its
// window loses it, one with an item rect places rows itself.
constexpr f32 kScrollbarExtent = 24.0f;

struct CmdSelectWindow_t {
  /* 0x00 */ be_f32 x;
  /* 0x04 */ be_f32 y;
  /* 0x08 */ be_f32 w;
  /* 0x0C */ be_f32 h;
  /* 0x10 */ be_u32 color;
  /* 0x14 */ be_f32 inset; // 2 on descriptor-driven lists, 0 on the popup
  /* 0x18 */ be_u32 style;
};
static_assert(sizeof(CmdSelectWindow_t) == 0x1C);

// The highlight box one row is drawn in, relative to the window's origin.
// Allocated by CommandSelectTask_SetItemRect, so null on a list that divides
// its window instead.
struct CmdSelectItem_t {
  /* 0x00 */ be_f32 x;
  /* 0x04 */ be_f32 y;
  /* 0x08 */ be_f32 w;
  /* 0x0C */ be_f32 h;
  /* 0x10 */ be_u32 color;
  /* 0x14 */ u8 _pad014[0x08];
};
static_assert(sizeof(CmdSelectItem_t) == 0x1C);

struct CmdSelectStride_t {
  /* 0x00 */ be_f32 x;
  /* 0x04 */ be_f32 y;
};
static_assert(sizeof(CmdSelectStride_t) == 0x08);

// One option, padded out to the stride CommandSelectTask_RebuildVisibleItems
// copies at.
struct CmdSelectEntry_t {
  /* 0x00 */ u8 _pad000[0x80];
  /* 0x80 */ be_u32 id;      // what CommandSelectTask_GetSelection returns
  /* 0x84 */ be_u32 enabled; // zero refuses a confirm
  /* 0x88 */ u8 _pad088[0x58];
};
static_assert(offsetof(CmdSelectEntry_t, id) == 0x80);
static_assert(offsetof(CmdSelectEntry_t, enabled) == 0x84);
static_assert(sizeof(CmdSelectEntry_t) == 0xE0);

struct CommandSelectTask_t {
  /* 0x000 */ be_u32 vtable;
  /* 0x004 */ u8 _pad004[0x54];
  // The same word and bits AnimeMenu carries at 0x058.
  /* 0x058 */ be_u32 flags;
  /* 0x05C */ u8 _pad05C[0x0C];
  /* 0x068 */ be_f32 priority;
  /* 0x06C */ be_u32 window; // CmdSelectWindow_t*
  /* 0x070 */ be_u32 item;   // CmdSelectItem_t*, null on a divided list
  /* 0x074 */ be_u32 stride; // CmdSelectStride_t*, null on a divided list
  /* 0x078 */ be_u32 font;
  /* 0x07C */ be_u32 layerFront; // this list is the front one of its stack
  /* 0x080 */ u8 _pad080[0x08];
  // Shared with every sibling list, so the three battle lists carry one
  // cursor sprite between them.
  /* 0x088 */ be_u32 cursorTask;
  /* 0x08C */ be_u16 gridRows;
  /* 0x08E */ be_u16 gridCols;
  /* 0x090 */ u8 _pad090[0x04]; // entries vector base
  /* 0x094 */ mem::GuestVec<CmdSelectEntry_t> entries;
  /* 0x0A0 */ u8 _pad0A0[0x04]; // visible vector base
  // The page the scroll window is over, rebuilt every frame. cursorRow and
  // cursorCol index this, not entries.
  /* 0x0A4 */ mem::GuestVec<CmdSelectEntry_t> visible;
  /* 0x0B0 */ be_u32 cursorIndex; // into entries
  // In pages: an entry index is scrollOffset times the page's minor dimension
  // plus its place on the page.
  /* 0x0B4 */ be_u16 scrollOffset;
  /* 0x0B6 */ u8 _pad0B6[0x02];
  /* 0x0B8 */ be_u32 cursorRow; // within the page, always [0, gridRows)
  /* 0x0BC */ be_u32 cursorCol; // within the page, always [0, gridCols)
  /* 0x0C0 */ be_u32 orientation; // zero numbers the page across, then down
  /* 0x0C4 */ be_u32 inputEnabled;
  /* 0x0C8 */ be_u32 directionSources; // bit0 left stick, bit1 D-pad
  /* 0x0CC */ u8 inputBits;            // MenuInput, rebuilt each frame
  /* 0x0CD */ u8 _pad0CD[0x0F];
  /* 0x0DC */ be_u32 pageMode;
  /* 0x0E0 */ u8 _pad0E0[0x04];
  /* 0x0E4 */ be_u32 rightAlign;
  /* 0x0E8 */ u8 _pad0E8[0x38];
};
static_assert(offsetof(CommandSelectTask_t, flags) == 0x058);
static_assert(offsetof(CommandSelectTask_t, priority) == 0x068);
static_assert(offsetof(CommandSelectTask_t, window) == 0x06C);
static_assert(offsetof(CommandSelectTask_t, item) == 0x070);
static_assert(offsetof(CommandSelectTask_t, stride) == 0x074);
static_assert(offsetof(CommandSelectTask_t, font) == 0x078);
static_assert(offsetof(CommandSelectTask_t, layerFront) == 0x07C);
static_assert(offsetof(CommandSelectTask_t, cursorTask) == 0x088);
static_assert(offsetof(CommandSelectTask_t, gridRows) == 0x08C);
static_assert(offsetof(CommandSelectTask_t, gridCols) == 0x08E);
static_assert(offsetof(CommandSelectTask_t, entries) == 0x094);
static_assert(offsetof(CommandSelectTask_t, visible) == 0x0A4);
static_assert(offsetof(CommandSelectTask_t, cursorIndex) == 0x0B0);
static_assert(offsetof(CommandSelectTask_t, scrollOffset) == 0x0B4);
static_assert(offsetof(CommandSelectTask_t, cursorRow) == 0x0B8);
static_assert(offsetof(CommandSelectTask_t, cursorCol) == 0x0BC);
static_assert(offsetof(CommandSelectTask_t, orientation) == 0x0C0);
static_assert(offsetof(CommandSelectTask_t, inputEnabled) == 0x0C4);
static_assert(offsetof(CommandSelectTask_t, directionSources) == 0x0C8);
static_assert(offsetof(CommandSelectTask_t, inputBits) == 0x0CC);
static_assert(offsetof(CommandSelectTask_t, pageMode) == 0x0DC);
static_assert(offsetof(CommandSelectTask_t, rightAlign) == 0x0E4);
static_assert(sizeof(CommandSelectTask_t) == 0x120);

// Where one row of a list sits, resolved from whichever of the two layouts the
// list uses. Cells are laid at origin + index*stride and are cell-sized, which
// is narrower than the stride wherever a list leaves gaps.
struct CmdSelectGrid {
  f32 originX;
  f32 originY;
  f32 cellW;
  f32 cellH;
  f32 strideX;
  f32 strideY;
  int rows;
  int cols;
};

bool CmdSelectGridOf(const CommandSelectTask_t &task, CmdSelectGrid &out) {
  const int rows = int(u16(task.gridRows));
  const int cols = int(u16(task.gridCols));
  if (rows <= 0 || cols <= 0)
    return false;

  const auto *window = mem::try_at<const CmdSelectWindow_t>(u32(task.window));
  if (!window)
    return false;

  out.rows = rows;
  out.cols = cols;

  const auto *item = mem::try_at<const CmdSelectItem_t>(u32(task.item));
  const auto *stride = mem::try_at<const CmdSelectStride_t>(u32(task.stride));
  if (item && stride) {
    // The row box Visual__DrawGridItems highlights, which is the one on screen.
    // No inset here: the inset moves the cursor sprite, not the rows.
    out.originX = f32(window->x) + f32(item->x);
    out.originY = f32(window->y) + f32(item->y);
    out.cellW = item->w;
    out.cellH = item->h;
    out.strideX = stride->x;
    out.strideY = stride->y;
    return out.cellW > 0.0f && out.cellH > 0.0f;
  }

  // Otherwise the window is divided into the grid, which is how the
  // yes/no popup lays its two answers out.
  const f32 inset = window->inset;
  f32 innerW = f32(window->w) - inset * 2.0f;
  f32 innerH = f32(window->h) - inset * 2.0f;
  if (u32(task.pageMode) == 0 &&
      task.entries.size() > u32(rows) * u32(cols)) {
    if (u32(task.orientation) != 0)
      innerH -= kScrollbarExtent;
    else
      innerW -= kScrollbarExtent;
  }
  if (innerW <= 0.0f || innerH <= 0.0f)
    return false;

  out.originX = f32(window->x) + inset;
  out.originY = f32(window->y) + inset;
  out.cellW = innerW / f32(cols);
  out.cellH = innerH / f32(rows);
  out.strideX = out.cellW;
  out.strideY = out.cellH;
  return true;
}

} // namespace

bool CommandSelectTask::HasFocus() const {
  const auto *self = Self<CommandSelectTask_t>();
  if (!self)
    return false;
  const u32 f = Flags();
  if ((f & u32(MenuFlag::Active)) == 0)
    return false;
  if ((f & (u32(MenuFlag::Locked) | u32(MenuFlag::Animating))) != 0)
    return false;
  if (u32(self->inputEnabled) == 0)
    return false;
  // The entry test is CommandSelectTask_CheckConfirm's, and it is what keeps a
  // list built but never filled from claiming the pointer.
  return !self->entries.empty();
}

int CommandSelectTask::EntryCount() const {
  const auto *self = Self<CommandSelectTask_t>();
  return self ? static_cast<int>(self->entries.size()) : 0;
}

int CommandSelectTask::CursorIndex() const {
  const auto *self = Self<CommandSelectTask_t>();
  return self ? static_cast<int>(static_cast<u32>(self->cursorIndex)) : 0;
}

u8 CommandSelectTask::InputBits() const {
  const auto *self = Self<CommandSelectTask_t>();
  return self ? self->inputBits : 0;
}

bool CommandSelectTask::CellAt(f32 x, f32 y, int &index) const {
  const auto *self = Self<CommandSelectTask_t>();
  if (!self)
    return false;

  CmdSelectGrid grid{};
  if (!CmdSelectGridOf(*self, grid))
    return false;

  const f32 localX = x - grid.originX;
  const f32 localY = y - grid.originY;
  if (localX < 0.0f || localY < 0.0f)
    return false;

  // An axis with one cell on it has no stride, and the battle command list is
  // exactly that: five rows in one column, so CommandSelectTask_SetItemStride
  // leaves its x at zero. Dividing by a stride is correct only where there is
  // a second cell to reach.
  const auto cellOn = [](f32 local, f32 stride, int count) {
    return count > 1 && stride > 0.0f ? int(local / stride) : 0;
  };
  const int col = cellOn(localX, grid.strideX, grid.cols);
  const int row = cellOn(localY, grid.strideY, grid.rows);
  if (row >= grid.rows || col >= grid.cols)
    return false;

  // Inside the band but past the cell itself is a gap between rows.
  if (localX - f32(col) * grid.strideX > grid.cellW)
    return false;
  if (localY - f32(row) * grid.strideY > grid.cellH)
    return false;

  // Orientation picks how Visual__DrawGridItems numbers the page, so it has to
  // pick how the page is read back too.
  const bool columnMajor = u32(self->orientation) != 0;
  const int page = columnMajor ? col * grid.rows + row : row * grid.cols + col;
  if (page < 0 || page >= int(self->visible.size()))
    return false;

  // CommandSelectTask_RebuildVisibleItems fills the page starting one whole
  // page's minor dimension per scroll step, which is the same conversion
  // CommandSelectTask_RecalcCursorGrid inverts.
  const int pageStride = columnMajor ? grid.rows : grid.cols;
  const int absolute = int(u16(self->scrollOffset)) * pageStride + page;
  if (absolute >= int(self->entries.size()))
    return false;

  index = absolute;
  return true;
}

int CommandSelectTask::RowEdgeDirection() const {
  const auto *self = Self<CommandSelectTask_t>();
  if (!self)
    return 0;

  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return 0;

  const auto *window = mem::try_at<const CmdSelectWindow_t>(u32(self->window));
  if (!window)
    return 0;

  const f32 left = window->x;
  const f32 top = window->y;
  if (x < left || x > left + f32(window->w))
    return 0;
  if (y < top)
    return -1;
  if (y > top + f32(window->h))
    return 1;
  return 0;
}

int CommandSelectTask::RowStep() const {
  const auto *self = Self<CommandSelectTask_t>();
  if (!self)
    return 0;
  if (u32(self->orientation) != 0)
    return 1;
  const int cols = int(u16(self->gridCols));
  return cols > 0 ? cols : 0;
}

} // namespace bd::engine
