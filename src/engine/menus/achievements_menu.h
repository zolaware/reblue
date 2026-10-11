/**
 * @file    engine/menus/achievements_menu.h
 * @brief   Achievement viewer - a read-only d2anime list hosted from the camp
 *          Encyclopedia. The config menu shows the same catalog in its own
 * list.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "engine/d2anime/d2anime.h"

#include <rex/types.h>

namespace bd::engine {

class AchievementsMenu {
public:
  enum class State {
    INTRO,   // S_dia_top_ach.csv playing, the list loaded but hidden
    INIT,    // waiting for task ready + menu discovery
    FADE_IN, // list up, alphas ramping, the strip already in place
    LIST,    // list active
    OUTRO,   // the same transition running backwards
    CLOSING, // done, the host tears us down
  };

  // Builds and loads both screens hidden, the way bdCampDiaryLoad brings up the
  // stock sixteen. Call it every frame: it takes one screen per call and
  // returns immediately once both exist. Without it the transition starts
  // loading only once the top screen has already been hidden, and the camp
  // frame sits empty for as long as the CSV and its eleven child anime take to
  // come back. Taking them one at a time keeps that cost off the frame the
  // Encyclopedia itself opens on.
  void Preload(const Task &parent);

  // parent is the host task, currently the camp Encyclopedia main task.
  // Nothing else about the host is assumed.
  void Create(const Task &parent);

  // Hides both screens and goes idle without unloading them. They belong to the
  // Items task, which outlives every Encyclopedia visit, so killing them here
  // only bought a reload, and one that runs while the player is walking
  // straight back in.
  void Close();

  // Host task already died and took this one with it: reset without reading or
  // writing engine memory that has since been freed.
  void Abandon();

  // Must run before the engine drives the task tree. See the note on the
  // definition.
  void Update();

  bool IsActive() const { return active_; }
  bool IsClosing() const { return state_ == State::CLOSING; }
  u32 TaskAddr() const { return task_.Address(); }

private:
  void Transition(State next);
  void StartOutro();
  bool IntroFinished();
  bool OutroFinished();
  bool DiscoverMenu();
  void PopulateNames();
  void RefreshVisuals();
  void UpdateRowDesc(int cursor);
  void HandleList();

  State state_ = State::INTRO;
  bool active_ = false;
  D2AnimeCursor cursor_;
  int intro_frames_ = 0;
  float intro_speed_ = 1.0f;
  // 0 while the strip is still walking, 1 once the list is fully up. Every
  // alpha on the right-hand pane is scaled by it.
  float fade_ = 0.0f;

  D2AnimeTask intro_;
  D2AnimeTask task_;
  AnimeMenu list_menu_;
};

// Serves the generated CSVs from the stock d2anime\camp\dia\ directory, so
// their relative references resolve like the disc's own siblings'. Never torn
// down: the Encyclopedia row and its Items pane twin are drawn from the same
// mount and outlive the viewer. Safe to call twice, and it has to be: re-adding
// a mount invalidates the VFS directory index, which costs a full walk to
// rebuild.
void RegisterAchievementsVFS();

} // namespace bd::engine
