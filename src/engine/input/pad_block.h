/**
 * @file    engine/input/pad_block.h
 * @brief   The pad state block the engine polls into and reads input from.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>

#include <rex/types.h>

namespace bd::engine {

// Only the words the input layer reads or writes. The size is left open.
struct PadBlock_t {
  /* 0x00 */ u8 connected;
  /* 0x01 */ u8 connectEdge;
  /* 0x02 */ u8 disconnectEdge;
  /* 0x03 */ u8 _pad003[0x0C - 0x03];
  /* 0x0C */ be_u32 held;
  /* 0x10 */ be_u32 pressed;
  /* 0x14 */ be_u32 released;
  /* 0x18 */ be_u32 repeat;
  /* 0x1C */ be_f32 analog[4];
};
static_assert(offsetof(PadBlock_t, connected) == 0x00);
static_assert(offsetof(PadBlock_t, connectEdge) == 0x01);
static_assert(offsetof(PadBlock_t, disconnectEdge) == 0x02);
static_assert(offsetof(PadBlock_t, held) == 0x0C);
static_assert(offsetof(PadBlock_t, pressed) == 0x10);
static_assert(offsetof(PadBlock_t, released) == 0x14);
static_assert(offsetof(PadBlock_t, repeat) == 0x18);
static_assert(offsetof(PadBlock_t, analog) == 0x1C);

} // namespace bd::engine
