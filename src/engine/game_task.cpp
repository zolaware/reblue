/**
 * @file    engine/game_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/game_task.h"

#include <cstddef>

#include "engine/chara.h"

namespace bd::engine {

namespace {

struct GameTask_t {
  /* 0x00 */ u8 _pad00[0x6C];
  /* 0x6C */ be_u32 unlockedSlots; // bit i unlocks slot (i + 1) * kSlotIdStride
  /* 0x70 */ u8 _pad70[0x8C - 0x70];
  /* 0x8C */ be_u32 shutdownFlag;
};
static_assert(offsetof(GameTask_t, unlockedSlots) == 0x6C);
static_assert(offsetof(GameTask_t, shutdownFlag) == 0x8C);

} // namespace

u32 GameTask::UnlockedSlots() const {
  const auto *self = Self<GameTask_t>();
  return self ? static_cast<u32>(self->unlockedSlots) : 0;
}

u32 GameTask::ShutdownFlag() const {
  const auto *self = Self<GameTask_t>();
  return self ? static_cast<u32>(self->shutdownFlag) : 0;
}

bool GameTask::IsSlotUnlocked(u32 slotId) const {
  if (slotId < kSlotIdStride)
    return false;
  const u32 bit = slotId / kSlotIdStride - 1;
  return bit < 32 && (UnlockedSlots() & (1u << bit)) != 0;
}

} // namespace bd::engine
