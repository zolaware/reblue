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
#include "platform/platform.h"

#include <rex/ppc.h>
#include <rex/types.h>
#include <rex/ui/ui_event.h>

namespace bd::engine {

namespace {

// Where a row template draws its two cur_01 arrows, in the template's own
// coordinates. Each band spans both blink frames.
struct ArrowBands {
  f32 downX0, downX1;
  f32 upX0, upX1;
};

// L_shp_buy_itmbtn.csv and L_shp_sell_itmbtn.csv, behind the buy, sell and
// equipment lists.
constexpr ArrowBands kItemRow = {470.0f, 497.0f, 515.0f, 542.0f};
// L_shp_buy_mechattbtn.csv.
constexpr ArrowBands kMechatRow = {393.0f, 420.0f, 720.0f, 747.0f};

// The count moves on left and right, which a mouse has neither of, so a click
// on the arrows the row already draws is queued as the press the list reads.
// The engine keeps its own clamping, sound and redraw that way.
bool CountArrowClicked(const ShopMainTask &shop) {
  if (!MenuMouse::Get().PointerActive())
    return false;
  if (!platform::Mouse().IsButtonDown(rex::ui::MouseEvent::Button::kLeft))
    return false;

  AnimeMenu menu = shop.StateMenu();
  if (!menu)
    return false;

  int row = -1;
  f32 x = 0.0f;
  if (!menu.PointerRowX(row, x) || row != menu.CursorIndex())
    return false;

  const ArrowBands &bands =
      shop.Layout(ShopLayout::BuyMechat).IsVisible() ? kMechatRow : kItemRow;
  if (x >= bands.downX0 && x <= bands.downX1)
    InputActions::Get().Force(Action::NavLeft);
  else if (x >= bands.upX0 && x <= bands.upX1)
    InputActions::Get().Force(Action::NavRight);
  else
    return false;
  return true;
}

} // namespace

} // namespace bd::engine

// The list states poll confirm before the menu's own left and right, so a click
// on an arrow is a purchase unless the confirm it also is gets dropped here.
void bdShopListConfirmHook(PPCRegister &r3, PPCRegister &shop) {
  if (r3.u32 &&
      bd::engine::CountArrowClicked(bd::engine::ShopMainTask(shop.u32)))
    r3.u64 = 0;
}
