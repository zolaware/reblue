/**
 * @file    engine/menus/shop_main_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/menus/shop_main_task.h"

#include <cstddef>
#include <iterator>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

struct ShopMainTask_t {
  /* 0x000 */ u8 _pad000[0x06C];
  /* 0x06C */ be_u32 state;
  /* 0x070 */ u8 _pad070[0x088 - 0x070];
  /* 0x088 */ be_u32 layouts[12];
  // Shop::MainTask::Init binds all ten at once, and its handlers reach a menu
  // by indexing this table with the state at 0x06C.
  /* 0x0B8 */ be_u32 stateMenus[10];
};
static_assert(offsetof(ShopMainTask_t, state) == 0x06C);
static_assert(offsetof(ShopMainTask_t, layouts) == 0x088);
static_assert(offsetof(ShopMainTask_t, stateMenus) == 0x0B8);

} // namespace

AnimeMenu ShopMainTask::StateMenu() const {
  const auto *self = Self<ShopMainTask_t>();
  if (!self)
    return AnimeMenu();
  const u32 state = self->state;
  if (state >= std::size(self->stateMenus))
    return AnimeMenu();
  return AnimeMenu(self->stateMenus[state]);
}

D2AnimeTask ShopMainTask::Layout(ShopLayout which) const {
  const auto *self = Self<ShopMainTask_t>();
  const u32 i = static_cast<u32>(which);
  if (!self || i >= std::size(self->layouts))
    return D2AnimeTask();
  return D2AnimeTask(self->layouts[i]);
}

} // namespace bd::engine
