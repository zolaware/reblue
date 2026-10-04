/**
 * @file    engine/menus/shop_main_task.h
 * @brief   Shop::MainTask, the buy and sell screen, and the menu it hands the
 *          pad to in each of its states.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/d2anime/anime_menu.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/task.h"

namespace bd::engine {

// The twelve CSVs bdShopTaskConstruct loads, in the order it loads them.
enum class ShopLayout : u32 {
  Header = 0,
  Top = 1,
  MechatTop = 2,
  MedalTop = 3,
  Buy = 4,
  Sell = 5,
  Equipment = 6,
  BuyMechat = 7,
  YesNo = 8,
  Strings = 9,
  MedalHeader = 10,
  MedalExchange = 11,
};

class ShopMainTask : public Task {
public:
  ShopMainTask() = default;
  explicit ShopMainTask(u32 address) : Task(address) {}

  // The menu bound to the state the screen is in, empty for a state with none.
  AnimeMenu StateMenu() const;

  D2AnimeTask Layout(ShopLayout which) const;
};

} // namespace bd::engine
