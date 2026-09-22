/**
 * @file    engine/field_camera.h
 * @brief   Raw mouse look on the field camera.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

struct PendingLook {
  f32 yaw = 0.0f;
  f32 pitch = 0.0f;
  f32 pivot[3] = {};
};

bool PendingMouseLook(u64 tick, f32 alpha, PendingLook &out);

} // namespace bd::engine
