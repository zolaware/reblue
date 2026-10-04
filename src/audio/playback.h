/**
 * @file    audio/playback.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

namespace bd::audio {

class Playback {
public:
  static Playback &Get();

  void SetPaused(bool paused);
};

} // namespace bd::audio
