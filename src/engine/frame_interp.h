/**
 * @file    engine/frame_interp.h
 * @brief   Tick start entry point for the fps unlock's render interpolation.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

namespace bd::engine {

// Call once per bdMainGameStep iteration, before the guest's logic block:
// advances the tick clock and releases anything the interpolator deferred.
void OnGuestGameStep();

bool SparseFrame();

} // namespace bd::engine
