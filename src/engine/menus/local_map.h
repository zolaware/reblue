/**
 * @file    engine/menus/local_map.h
 * @brief   The area map's own per-frame tick, which its screen cannot carry.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

namespace bd::engine {

// Polls the marker icon sheets once per engine frame. It cannot run from the
// world map screen's own update: closing that screen sets bit 2 of its task
// flags, and bdSceneTreeUpdate skips a task carrying it, so the screen is
// asleep for the whole time the player is in the field.
void AreaMapTick();

} // namespace bd::engine
