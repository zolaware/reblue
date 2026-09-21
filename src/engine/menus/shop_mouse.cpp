/**
 * @file    engine/menus/shop_mouse.cpp
 * @brief   Pointer support for the shop's per-row count arrows.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime.h"
#include "engine/input/action_state.h"
#include "engine/input/actions.h"
#include "engine/menus/shop_main_task.h"
#include "reblue_init.h"

#include <rex/hook.h>
#include <rex/types.h>

REX_EXTERN(__imp__Shop__MainTask__UpdateCount);

namespace bd::engine {

namespace {

// Where L_shp_buy_itmbtn.csv and its selling twin draw the two cur_01 arrows,
// in the row template's own coordinates. Each band spans both blink frames.
constexpr f32 kDownArrowX0 = 470.0f;
constexpr f32 kDownArrowX1 = 497.0f;
constexpr f32 kUpArrowX0 = 515.0f;
constexpr f32 kUpArrowX1 = 542.0f;

// The count moves on left and right, which a mouse has neither of, so a click
// on the arrows the row already draws is queued as the pad press the handler
// waits for. The engine keeps its own clamping, sound and redraw that way.
bool CountArrowClicked(const ShopMainTask &shop) {
  if (!MenuMouse::Get().PointerActive())
    return false;
  if (!CheckAction(Action::Confirm))
    return false;

  AnimeMenu menu = shop.StateMenu();
  if (!menu)
    return false;

  int row = -1;
  f32 x = 0.0f;
  if (!menu.PointerRowX(row, x) || row != menu.CursorIndex())
    return false;

  if (x >= kDownArrowX0 && x <= kDownArrowX1)
    InputActions::Get().Force(Action::NavLeft);
  else if (x >= kUpArrowX0 && x <= kUpArrowX1)
    InputActions::Get().Force(Action::NavRight);
  else
    return false;
  return true;
}

} // namespace

} // namespace bd::engine

// Skipping the handler eats the confirm edge the same click would otherwise
// also be, so an arrow click steps the count instead of buying.
REX_HOOK_RAW(Shop__MainTask__UpdateCount) {
  const bd::engine::ShopMainTask shop(ctx.r3.u32);
  if (bd::engine::CountArrowClicked(shop))
    return;
  __imp__Shop__MainTask__UpdateCount(ctx, base);
}
