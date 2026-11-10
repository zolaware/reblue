/**
 * @file    audio/playback.h
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <atomic>

namespace rex::kernel {
class TitleRuntime;
}

namespace bd::audio {

class Playback {
public:
  static Playback &Get();

  void Bind(rex::kernel::TitleRuntime *title) { title_.store(title); }

  // Posts to the UI thread, which is the one the runtime pauses audio on.
  void SetPaused(bool paused);

private:
  std::atomic<rex::kernel::TitleRuntime *> title_{nullptr};
};

} // namespace bd::audio
