/**
 * @file    engine/menus/title_task.h
 * @brief   The title screen task: its row cursor, its state machine and the
 *          child screen it hands the frame to.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/task.h"

namespace bd::engine {

class TitleTask : public Task {
public:
  TitleTask() = default;
  explicit TitleTask(u32 address) : Task(address) {}

  u32 State() const;
  void SetState(u32 state);

  u32 Cursor() const;
  void SetCursor(u32 cursor);

  u32 DiscSetting() const;
  void SetDiscSetting(u32 setting);

  u32 VoicePick() const;
  void SetVoicePick(u32 pick);

  bool HasSaveData() const;
  bool IsXboxLive() const;

  void SetNextSeqId(u32 id);

  bool HasChild() const;

  // Drops the child screen and puts the task's own name back, which is what
  // the dispatcher reads once the child is gone.
  void ClearChild();
};

} // namespace bd::engine
