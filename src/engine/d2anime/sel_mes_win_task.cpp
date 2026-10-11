/**
 * @file    engine/d2anime/sel_mes_win_task.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/sel_mes_win_task.h"

#include <cstddef>

#include <rex/types.h>

namespace bd::engine {

namespace {

struct SelMesWinTask_t {
  /* 0x000 */ u8 _pad000[0xDC8];
  /* 0xDC8 */ be_u32 confirmed;
  /* 0xDCC */ be_u32 canceled;
  /* 0xDD0 */ be_u32 commandSelect;
};
static_assert(offsetof(SelMesWinTask_t, confirmed) == 0xDC8);
static_assert(offsetof(SelMesWinTask_t, canceled) == 0xDCC);
static_assert(offsetof(SelMesWinTask_t, commandSelect) == 0xDD0);

} // namespace

bool SelMesWinTask::Confirmed() const {
  const auto *self = Self<SelMesWinTask_t>();
  return self && static_cast<u32>(self->confirmed) != 0;
}

bool SelMesWinTask::Canceled() const {
  const auto *self = Self<SelMesWinTask_t>();
  return self && static_cast<u32>(self->canceled) != 0;
}

CommandSelectTask SelMesWinTask::CommandSelect() const {
  const auto *self = Self<SelMesWinTask_t>();
  return CommandSelectTask(self ? static_cast<u32>(self->commandSelect) : 0);
}

} // namespace bd::engine
