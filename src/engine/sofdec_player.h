/**
 * @file    engine/sofdec_player.h
 * @brief   The mwPly wrapper a CRI::Sofdec::PlayTask owns, which is what
 *          plays a prerendered .sfd movie.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

class SofdecPlayer : public Object {
public:
  SofdecPlayer() = default;
  explicit SofdecPlayer(u32 address) : Object(address) {}

  static bool Playing();

  i32 Status() const;
  bool Paused() const;

  bool Pause();
  bool Resume();
};

} // namespace bd::engine
