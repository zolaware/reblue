/**
 * @file    bdengine/sysmes.h
 * @brief   Host-side wrapper for engine's native system message popups.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd {

/**
 * @brief Wraps the engine's native SelMesWinTask (yes/no confirmation popup).
 *        The popup renders as a translucent 9-slice panel with question text
 *        and two selectable answers. Input is handled by the engine.
 */
class SysMesConfirm {
public:
  /**
   * @brief Spawn a yes/no popup as a child of parentTask.
   *        Text strings are ASCII/Latin-1, converted to UTF-16BE internally.
   *        Up to 3 question lines; a1/a2 are the answer labels.
   *        Returns true if the task was created successfully.
   */
  bool Create(u32 parentTask, const char *q1, const char *q2 = "",
              const char *q3 = "", const char *a1 = "Yes",
              const char *a2 = "No", int defaultSel = 1);

  /**
   * @brief Returns true once the user has made a choice (confirm or cancel).
   */
  bool Poll() const;

  bool Confirmed() const;
  bool Cancelled() const;
  int SelectedAnswer() const;
  bool IsActive() const { return task_ != 0; }

  /**
   * @brief DEAD-flag the popup task and clear our reference.
   */
  void Kill();

private:
  u32 task_ = 0;
};

/**
 * @brief Wraps the engine's NormMesWinTask (message-only popup, no answers).
 *        Caller is responsible for detecting input and calling Kill() to dismiss.
 */
class SysMesNotice {
public:
  bool Create(u32 parentTask, const char *q1, const char *q2 = "",
              const char *q3 = "");
  void Kill();
  bool IsActive() const { return task_ != 0; }

private:
  u32 task_ = 0;
};

} // namespace bd
