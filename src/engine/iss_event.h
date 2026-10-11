/**
 * @file    engine/iss_event.h
 * @brief   The engine's issEvent, one compiled .evt scene in playback, and the
 *          up to eight of them live at once.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <cstddef>
#include <string>

#include <rex/types.h>

#include "engine/task.h"

namespace bd::engine {

class IssEvent : public Task {
public:
  IssEvent() = default;
  explicit IssEvent(u32 address) : Task(address) {}

  // A pack event and a viewEvent can overlap.
  static size_t LiveCount();

  // The live events in slot order. Empty past LiveCount().
  static IssEvent LiveAt(size_t i);

  static bool AnyPlaying();

  i32 EventId() const; // eventNumber * 100 + sceneNumber, -1 when empty
  i32 EventNumber() const;
  i32 SceneNumber() const;
  std::string Prefix() const; // "ev" or "sv", empty when unknown

  bool Playing() const;
};

} // namespace bd::engine
