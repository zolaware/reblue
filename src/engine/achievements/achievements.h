/**
 * @file    engine/achievements/achievements.h
 * @brief   reblue-authored achievements, unlocked from engine events.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

class Achievements {
public:
  // Subscribes every condition to the events that can satisfy it. Called once
  // at startup.
  static void Init();

  // Repeatable: the SDK clears the store when it loads the title's own XDBF
  // catalog at kernel boot, so readers assert the catalog rather than trust an
  // earlier registration.
  static void Register();

  // Awards every achievement through the same path a condition would, so the
  // store, the internal done flag and the viewer all stay in step. Returns how
  // many were still locked. For the cheat menu; nothing in the game calls it.
  static u32 UnlockAll();

  // Re-locks everything. The manager exposes no clear, so this rewrites the
  // per-profile unlock store and asks it to reload. Returns how many were
  // still unlocked afterwards: non-zero means the reload merged rather than
  // replaced and the reset needs a restart to show.
  static u32 LockAll();
};

} // namespace bd::engine
