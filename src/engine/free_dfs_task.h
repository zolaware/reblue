/**
 * @file    engine/free_dfs_task.h
 * @brief   The close-up window task, FreeDfsTask, and the 2D vertices it draws.
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

struct BdPrim2DVertex {
  be_f32 x;
  be_f32 y;
  be_f32 u;
  be_f32 v;
  be_u32 color;
};
static_assert(sizeof(BdPrim2DVertex) == 0x14);
static_assert(offsetof(BdPrim2DVertex, u) == 0x08);
static_assert(offsetof(BdPrim2DVertex, color) == 0x10);

// The window as four points, read by the loop that copies them into the camera
// view and by the one that miters the frame.
struct CloseUpOutline {
  BdPrim2DVertex points[4];
};

// Only the fields the host reads. width and height are the aspect the view
// projection divides by.
struct FreeDfsTask_t {
  /* 0x000 */ u8 _pad000[0x1D0];
  /* 0x1D0 */ be_f32 width;
  /* 0x1D4 */ be_f32 height;
  /* 0x1D8 */ u8 _pad1D8[0x1E8 - 0x1D8];
  /* 0x1E8 */ CloseUpOutline outline;
};
static_assert(offsetof(FreeDfsTask_t, width) == 0x1D0);
static_assert(offsetof(FreeDfsTask_t, height) == 0x1D4);
static_assert(offsetof(FreeDfsTask_t, outline) == 0x1E8);

} // namespace bd::engine
