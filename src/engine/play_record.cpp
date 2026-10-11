/**
 * @file    engine/play_record.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/play_record.h"

#include <cstddef>

namespace bd::engine {

namespace {

struct PlayRecord_t {
  /* 0x000 */ u8 _pad000[0xA58];
  /* 0xA58 */ be_u32 surroundWins;
  /* 0xA5C */ u8 _padA5C[0xA6C - 0xA5C];
  /* 0xA6C */ be_u32 battlesWon;
  /* 0xA70 */ be_u32 escapes;
  /* 0xA74 */ u8 _padA74[0xB48 - 0xA74];
};
static_assert(offsetof(PlayRecord_t, surroundWins) == 0xA58);
static_assert(offsetof(PlayRecord_t, battlesWon) == 0xA6C);
static_assert(offsetof(PlayRecord_t, escapes) == 0xA70);
static_assert(sizeof(PlayRecord_t) == 0xB48);

} // namespace

u32 PlayRecord::BattlesWon() const {
  const auto *self = Self<PlayRecord_t>();
  return self ? static_cast<u32>(self->battlesWon) : 0;
}

u32 PlayRecord::Escapes() const {
  const auto *self = Self<PlayRecord_t>();
  return self ? static_cast<u32>(self->escapes) : 0;
}

u32 PlayRecord::SurroundWins() const {
  const auto *self = Self<PlayRecord_t>();
  return self ? static_cast<u32>(self->surroundWins) : 0;
}

} // namespace bd::engine
