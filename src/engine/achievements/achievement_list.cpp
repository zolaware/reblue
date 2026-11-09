/**
 * @file    engine/achievements/achievement_list.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/achievements/achievement_list.h"

#include <algorithm>
#include <utility>

#include <rex/kernel/title_runtime.h>
#include <rex/xam/achievement_manager.h>

#include "core/logging.h"
#include "engine/achievements/achievements.h"
#include "platform/host_ui.h"

namespace bd::engine {

namespace {

std::vector<AchievementRow> s_rows;

} // namespace

const std::vector<AchievementRow> &RefreshAchievementList() {
  Achievements::Register();
  s_rows.clear();

  auto *rt = bd::platform::Title();
  if (!rt) {
    REXBD_WARN("achv no runtime, achievement list is empty");
    return s_rows;
  }
  auto &mgr = rt->GetAchievementManager();

  for (const auto &a : mgr.ListAchievements()) {
    AchievementRow row;
    row.id = a.id;
    row.label = a.label;
    row.unlocked = mgr.IsUnlocked(a.id);
    row.unlockedAt = row.unlocked ? mgr.GetUnlockTime(a.id).value_or(0) : 0;
    row.desc = row.unlocked ? a.description : a.unachieved_description;
    row.gamerscore = a.gamerscore;
    s_rows.push_back(std::move(row));
  }

  // Newest unlock first, then everything still locked. Stable, so the locked
  // tail keeps registration order: the title's XDBF catalog, then the reblue
  // achievements registered when the save loads.
  std::stable_sort(s_rows.begin(), s_rows.end(),
                   [](const AchievementRow &a, const AchievementRow &b) {
                     if (a.unlocked != b.unlocked)
                       return a.unlocked;
                     return a.unlocked && a.unlockedAt > b.unlockedAt;
                   });

  REXBD_DEBUG("achv catalog refreshed: {} achievements", s_rows.size());
  return s_rows;
}

const std::vector<AchievementRow> &GetAchievementList() { return s_rows; }

AchievementSummary GetAchievementSummary() {
  AchievementSummary s;
  s.total = static_cast<int>(s_rows.size());
  for (const auto &row : s_rows) {
    s.totalScore += static_cast<int>(row.gamerscore);
    if (row.unlocked) {
      ++s.unlocked;
      s.earnedScore += static_cast<int>(row.gamerscore);
    }
  }
  return s;
}

} // namespace bd::engine
