/**
 * @file    audio/playback.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license BSD 3-Clause, see LICENSE
 */
#include "audio/playback.h"

#include <rex/runtime.h>

#include <rex/ui/title_app.h>

namespace bd::audio {

Playback &Playback::Get() {
  static Playback instance;
  return instance;
}

void Playback::SetPaused(bool paused) {
  rex::ui::InvokeFunction([paused] {
    if (auto *runtime = rex::GetProcessRuntime())
      runtime->SetAudioPaused(paused);
  });
}

} // namespace bd::audio
