/**
 * @file    audio/playback.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license BSD 3-Clause, see LICENSE
 */
#include "audio/playback.h"

#include <rex/kernel/title_runtime.h>

#include "platform/host_ui.h"

namespace bd::audio {

Playback &Playback::Get() {
  static Playback instance;
  return instance;
}

void Playback::SetPaused(bool paused) {
  rex::ui::IUISystem *ui = bd::platform::UI();
  if (!ui)
    return;
  ui->PostTask([this, paused] {
    if (rex::kernel::TitleRuntime *title = title_.load())
      title->SetAudioPaused(paused);
  });
}

} // namespace bd::audio
