/**
 * @file    audio/playback.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license BSD 3-Clause, see LICENSE
 */
#include "audio/playback.h"

#include <rex/audio/audio_system.h>
#include <rex/runtime.h>

#include "core/ui_thread.h"

namespace bd::audio {

namespace {

rex::audio::AudioSystem *System() {
  auto *runtime = rex::Runtime::instance();
  return runtime ? static_cast<rex::audio::AudioSystem *>(runtime->audio_system())
                 : nullptr;
}

} // namespace

Playback &Playback::Get() {
  static Playback instance;
  return instance;
}

void Playback::SetPaused(bool paused) {
  bd::RunOnUIThread([paused] {
    auto *system = System();
    if (!system || system->is_paused() == paused)
      return;
    if (paused)
      system->Pause();
    else
      system->Resume();
  });
}

} // namespace bd::audio
