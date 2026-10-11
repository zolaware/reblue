/**
 * @file    engine/d2anime/anime_menu.h
 * @brief       The engine's AnimeMenu, the CSV template list widget behind
 *              every camp and config screen.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <cstddef>
#include <functional>

#include <rex/types.h>

#include "engine/d2anime/anime_data.h"
#include "engine/task.h"

namespace bd::engine {

class D2AnimeTask;

// Bits AnimeMenu_BuildInputBitmask leaves in a menu's input bitmask, and the
// only input a menu's owner reads: it rebuilds them every frame with the
// repeat behavior the owner expects, and drops diagonals.
enum class MenuInput : u8 {
  Up = 0x01,
  Down = 0x02,
  Left = 0x04,
  Right = 0x08,
  LT = 0x10,
  RT = 0x20,
  LB = 0x40,
  RB = 0x80,
};

// Task flag bits, as AnimeMenu_CheckConfirmInput tests them.
enum class MenuFlag : u32 {
  Animating = 0x1,
  Active = 0x2,
  Locked = 0x4,
};

class AnimeMenu : public Task {
public:
  AnimeMenu() = default;
  explicit AnimeMenu(u32 address) : Task(address) {}

  // The engine's own answer to which menu is taking input, copied from
  // AnimeMenu_CheckConfirmInput at 0x8217F128. AnimeMenu_BuildInputBitmask at
  // 0x8217FBE0 leads with the same test and writes an empty bitmask without
  // it, so a menu that fails here cannot move its cursor however visible.
  //
  // Screens raise the active flag on the list they hand the pad to and drop it
  // on every other menu they leave on screen: Magic at 0x822EE710, Status at
  // 0x822F46E8. Visible cannot stand in, since both pass 1 to
  // AnimeMenu__SetVisibleAndPlay for the focused list and the unfocused one
  // alike, and party strips carry 0 there for their whole life while drawn.
  bool HasFocus() const;

  // The CSV menu row's x,y,w,h,pri. PosX/PosY are where the engine actually
  // tiles the child templates, which AnimeMenu_CalcChildTemplatePos reads as
  // stride*index + pos, so they are the origin a hit test wants. OriginX and
  // OriginY below are not.
  f32 PosX() const;
  f32 PosY() const;
  f32 ExtentW() const;
  f32 ExtentH() const;
  // AnimeMenu_CalcItemPosition derives the row stride from the extent and the
  // row count, so growing a grid by a row means raising the extent too, or
  // every existing row shifts.
  void SetExtentH(f32 v);
  f32 Priority() const;

  // The CSV's StartCurX/StartCurY, the cursor sprite anchor.
  // CalcItemPosition returns positions in that space, which is offset from
  // where the templates draw, so hit testing uses PosX/PosY instead.
  f32 OriginX() const;
  f32 OriginY() const;
  f32 ItemW() const;
  f32 ItemH() const;

  // AnimeMenu_Update shows the cursor sprite only with both of these set, and
  // AnimeMenu_ctor starts them at 1. The first is the menu's own visible flag,
  // which AnimeMenu__SetVisibleAndPlay writes and UpdateTemplateVisuals reads
  // before it puts a row template back up, so a screen leaving a list on
  // screen unfocused has to reach for the second one: nothing in the game
  // writes it, leaving it the cursor's own switch.
  bool IsVisible() const;
  bool CursorShown() const;

  // The cursor arrow alone, for an active menu whose rows sit somewhere other
  // than where its grid says they do. SetActive(true) shows it again.
  void SetCursorShown(bool shown);

  // Each menu owns its own cursor sprite. This is not a link to a parent.
  Task CursorTask() const;

  // Geometry always reads the grid as rows and columns. Orientation only swaps
  // which one strides the scroll window in RebuildVisibleItems.
  u32 GridRows() const;
  void SetGridRows(u32 rows);
  u32 GridCols() const;

  u32 Orientation() const;

  // Sixteen bits wide, not thirty-two: AnimeMenu_Update loads it with
  // lhz r30, 0xD0(r31) at 0x8217E32C.
  u16 ScrollOffset() const;
  void SetScrollOffset(u16 offset);

  u32 ActiveFlag() const;
  void SetActiveFlag(u32 flag);

  u8 InputBits() const;
  void SetInputBits(u8 bits);

  // Paged lists stop a full window short of the end rather than on the last
  // entry, keeping a partial final page from scrolling past itself.
  u32 PagedMaxIndex() const;
  bool ScrollbarEnabled() const;
  // The cursor past either end comes back around.
  bool WrapEnabled() const;

  // SelectAll paints every item EnableWndType, DeselectAll paints every item
  // DisableWndType, which hides the cursor frame. Both need HasWndType, set
  // when the templates carry a WndType string variable.
  u32 SelectAll() const;
  u32 DeselectAll() const;
  void SetDeselectAll(u32 v);
  bool HasWndType() const;

  size_t TemplateCount() const;
  D2AnimeTask TemplateAt(size_t i) const;

  u32 EnableColor() const;
  u32 DisableColor() const;

  // Set to 1 triggers RebuildVisibleItems.
  u32 NeedsRebuild() const;
  void SetNeedsRebuild(u32 v);

  // Gates the LT/RT/LB/RB page inputs.
  u32 ShoulderPageJump() const;

  void SetActive(bool active);

  // Adds/removes templates from the scene tree, and hides overlapping menus
  // that otherwise crash.
  void SetVisibleAndPlay(bool visible);

  void AttachCursor();

  int CursorIndex() const;

  void SetCursorIndex(int index);

  // The row under the pointer and where the pointer sits along it, in the row
  // template's own coordinates. False when the pointer is over no row, which
  // is also what a pad press reports.
  bool PointerRowX(int &index, f32 &x) const;

  // The same reading for a row already chosen, whatever the pointer's height.
  // A drag latches onto its row this way rather than losing it the moment the
  // pointer slips off a 34px band.
  bool RowPointerX(int index, f32 &x) const;

  void ForEachTemplate(std::function<void(int, AnimeData)> cb) const;

  // Absolute entry index shown in a visible slot. Template slots are a scroll
  // window over the entry list, so slot i displays entry ItemDataIndex(i).
  // Returns 'slot' when the item data vector is not populated.
  int ItemDataIndex(int slot) const;

  // Whether the slot's row draws in EnableColor rather than DisableColor.
  bool ItemDataEnabled(int slot) const;

  // ForEachTemplate with the entry index behind each slot resolved. The index
  // can be past the end of the list, as a screen writing blanks into the
  // trailing slots needs to see.
  using RowFn = std::function<void(int slot, int index, AnimeData varBag)>;
  void ForEachSlot(const RowFn &fn) const;

  // ForEachSlot restricted to the slots showing a real entry, so a scrolled
  // long list tints and checks the rows it is actually drawing.
  void ForEachRow(size_t count, const RowFn &fn) const;

  void SetItemEnabled(int index, bool enabled);

  // A row whose enabled flag drives both its tint and which of the two
  // checkbox cells draws. Every list that marks rows on or off shares it.
  void SetToggleRow(int slot, AnimeData varBag, bool enabled, u32 color);

  void AddEntryData(int index, bool enabled);

  // How many entries the widget has. The cursor's upper bound and the drawn
  // cell count both come off this, so it is what makes a row reachable.
  int EntryCount() const;

  // Rows the grid is laid out for, which the CSV fixes and a screen adding a
  // row of its own has to raise.
  int RowCount() const;

  // Appends a row to the grid. The engine derives the row stride from the
  // extent and the row count, so the extent grows by one stride to leave the
  // rows already there where they are.
  void GrowGridByOneRow();

  // AddEntryData over a whole list. A CSV widget declares defaultItem=0, which
  // leaves it with no entries at all, so a screen has to seed them itself or
  // the engine renders into a null target. Seeding is also what lets a list
  // longer than its window scroll rather than lose its tail.
  void SeedEntries(size_t count, bool enabled = true);
};

// One menu's cursor, remembered across frames. A list screen rebuilds its
// detail panel and row description only when the cursor moves somewhere new,
// while the row visuals go in every frame regardless.
class D2AnimeCursor {
public:
  // A list whose entry vector the engine may not have built reports a cursor
  // no count can be checked against, so it passes kUnbounded and takes it.
  static constexpr size_t kUnbounded = 0;

  void Poll(const AnimeMenu &menu, size_t count,
            const std::function<void(int index)> &fn);
  void Poll(const AnimeMenu &menu, const std::function<void(int index)> &fn) {
    Poll(menu, kUnbounded, fn);
  }

  // Makes the next Poll report wherever the cursor is, for a screen changing
  // state: the panels below it belong to the old list.
  void Reset() { last_ = -1; }

private:
  int last_ = -1;
};

} // namespace bd::engine
