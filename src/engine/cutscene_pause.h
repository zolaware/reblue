/**
 * @file    engine/cutscene_pause.h
 * @brief   Holds a playing movie or event scene still.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>

#include "engine/sofdec_player.h"

namespace bd::engine {

class CutscenePause {
public:
  static CutscenePause &Get();

  bool Active() const { return active_.load(std::memory_order_relaxed); }

  bool Frozen() const { return frozen_.load(std::memory_order_relaxed); }

  bool Available() const;
  void Toggle();
  void Poll();

private:
  CutscenePause() = default;

  void Pause();
  void Resume();
  bool Holding() const;

  std::atomic<bool> active_{false};
  std::atomic<bool> frozen_{false};
  SofdecPlayer movie_;
};

} // namespace bd::engine
