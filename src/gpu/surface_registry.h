/**
 * @file    gpu/surface_registry.h
 * @brief   One persistent host surface per RT/DS the engine creates.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::gpu {

struct GuestTexture;

class SurfaceRegistry {
public:
  static GuestTexture *Get(u32 width, u32 height, u32 guest_format,
                           u32 sample_count);

  static void Tick();
};

} // namespace bd::gpu
