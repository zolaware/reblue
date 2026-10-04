/**
 * @file    engine/menus/update_prompt.h
 * @brief   The update check and its offers, in front of the engine's own
 *          downloadable-content load.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include <rex/types.h>

#include "engine/d2anime/d2anime.h"
#include "engine/task.h"

namespace bd::engine {

// Engine thread only: the windows it puts the answers in are engine tasks, and
// the engine polls them for input as it does its own screens.
class UpdatePrompt {
public:
  static UpdatePrompt &Get();

  // Once per title tick. True for as long as the title must wait here.
  bool Hold(const Task &parent);

  // The host's own readout and prompt stand down while this owns the answer.
  bool Active() const;

private:
  UpdatePrompt() = default;
  UpdatePrompt(const UpdatePrompt &) = delete;
  UpdatePrompt &operator=(const UpdatePrompt &) = delete;

  enum class Phase {
    kIdle,
    kLoading, // the CSV that carries the windows' variables
    kChecking,
    kAppOffer,
    kAppWorking,
    kAppFailed,
    kAppStaged,
    kContentOffer,
    kContentWorking,
    kDone,
  };

  bool Release();
  bool EnterOffers();
  bool EnterContentOffer();
  void ShowCheckLine();

  std::string app_version_;
  u64 app_bytes_ = 0;
  std::chrono::steady_clock::time_point deadline_{};
  D2AnimeTask host_;
  SysMesNotice notice_;
  SysMesConfirm confirm_;
  std::atomic<Phase> phase_{Phase::kIdle};
};

} // namespace bd::engine
