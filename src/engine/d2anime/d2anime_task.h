/**
 * @file    engine/d2anime/d2anime_task.h
 * @brief   The engine's D2AnimeTask, one parsed CSV layout and its timeline.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#pragma once

#include <cstddef>
#include <string_view>

#include <rex/types.h>

#include "engine/d2anime/anime_data.h"
#include "engine/d2anime/anime_menu.h"
#include "engine/task.h"

namespace bd::engine {

class D2AnimeTask : public Task {
public:
  // How a loaded task first shows. The engine's own screen loaders start their
  // CSVs hidden and reveal them once parsed, and Load follows suit: WhenReady
  // shows the task on the tick its parse completes, Held leaves the reveal to
  // the owner.
  enum class Reveal { WhenReady, Held };

  D2AnimeTask() = default;
  explicit D2AnimeTask(u32 address) : Task(address) {}

  static D2AnimeTask Load(const Task &parent, const char *csvPath,
                          Reveal reveal = Reveal::WhenReady);

  // Reveals every WhenReady task whose parse has finished. Once per engine
  // tick.
  static void Tick();

  void SetVisibleAndPlay(bool visible);
  bool IsVisible() const;

  void SetVisible(bool visible);

  // Propagated to the child menu's active flag at load.
  u32 AutoPlay() const;

  // 1 through 3 while the parse is still running.
  u32 LoadState() const;

  // Zero is one-shot, and the timeline then reports finished.
  u32 LoopFlag() const;

  // Ping-pongs between Draw and PostUpdate.
  u32 DrawDirty() const;

  // Timeline controls, the pair Camp::Diary::MainTask's Exit uses to run a
  // transition screen backwards: seek to its last frame, then play at a
  // negative rate. SetAnimTime recurses into child anime, so a plain write to
  // the frame would leave every nested d2anime running forwards.
  void SetAnimTime(f32 frame);
  void SetAnimSpeed(f32 speed);
  f32 AnimTime() const;
  f32 AnimSpeed() const;  // negative plays the timeline backwards
  f32 AnimLength() const; // -1 for an anime the CSV gives no end

  // The timeline reached that end. Only an anime with a length ever reports
  // it, so a caller driving an endless one needs a bound of its own.
  bool IsAnimFinished() const;

  void Kill();

  bool IsReady() const;

  size_t MenuCount() const;
  AnimeMenu MenuAt(size_t i) const;

  // Find a menu by its CSV-defined name (e.g. "SltSection", "ModList").
  AnimeMenu FindMenu(const char *name) const;

  engine::AnimeData AnimeData() const;

  void SetFloat(const char *name, double value);
  // Engine tokens (window types, texture paths) only. User-facing text goes
  // through SetText, which does not pass through the Shift-JIS widener.
  void SetString(const char *name, const char *value);
  void SetText(const char *name, std::string_view utf8);
};

} // namespace bd::engine
