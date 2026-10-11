/**
 * @file    engine/cutscene_pause.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/cutscene_pause.h"

#include "audio/audio.h"
#include "engine/game.h"
#include "engine/iss_event.h"

namespace bd::engine {

CutscenePause &CutscenePause::Get() {
  static CutscenePause instance;
  return instance;
}

bool CutscenePause::Available() const {
  return Active() || SofdecPlayer::Playing() || IssEvent::AnyPlaying();
}

void CutscenePause::Toggle() {
  if (Active())
    Resume();
  else
    Pause();
}

void CutscenePause::Poll() {
  if (Active() && !Holding())
    Resume();
}

void CutscenePause::Pause() {
  SofdecPlayer movie = Game::Get().SofdecPlayer();
  movie_ = movie && movie.Pause() ? movie : SofdecPlayer();
  const bool freeze = IssEvent::AnyPlaying();
  if (!movie_ && !freeze)
    return;
  frozen_.store(freeze, std::memory_order_relaxed);
  audio::Playback::Get().SetPaused(true);
  active_.store(true, std::memory_order_relaxed);
}

void CutscenePause::Resume() {
  movie_.Resume();
  movie_ = SofdecPlayer();
  frozen_.store(false, std::memory_order_relaxed);
  audio::Playback::Get().SetPaused(false);
  active_.store(false, std::memory_order_relaxed);
}

bool CutscenePause::Holding() const {
  return Frozen() || (movie_ && SofdecPlayer::Playing());
}

} // namespace bd::engine
