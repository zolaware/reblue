/**
 * @file    engine/stat_breakdown.h
 * @brief   Where a party member's derived stats come from: the base block, the
 *          best-of class bonus, and the permanent bonus block.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <rex/types.h>

#include "engine/chara.h"

namespace bd::engine {

// One derived stat, split into the sources Player_CalcBattleParams sums. final
// is what the engine actually stored, so a mismatch against the sum shows
// rather than hides.
//
// Two class candidates, because the engine has two paths. It seeds a scratch
// block from the latched class, raises it field by field to the maximum over
// every unlocked class, then in the config[89] branch calls
// Chara_CopyStatBlock, which refreshes that block from the latched class and
// so discards the maximum for these five stats while keeping it for the
// resist bytes. The other branch keeps the maximum. Recording both lets the
// sum check say which path ran instead of guessing.
struct StatContribution {
  const char *name;
  u32 final;
  u32 base;
  u32 classActive; // the latched active class's value
  u32 classBest;   // maximum over every unlocked class
  u32 permanent;
  // Which class supplied classBest. Empty when no class contributed.
  std::optional<CharaClass> winner;
};

// One occupied skill slot of the active class record, resolved through the
// class table to the effect it contributes. The name comes off the class table
// record itself, since a skill is not in the accessory table.
struct EquippedSkill {
  u32 slotIndex;
  std::optional<CharaClass> sourceClass;
  u32 skillId;
  std::string name; // empty when the class table has no record for it
  u32 effectType;
  u32 value;
};

// One worn accessory. These are the ids bdPhenomeTableFindById takes, so the
// name is a table lookup here.
struct EquippedItem {
  u32 slotIndex;
  u32 itemId;
  std::string name; // empty when the table lookup misses
};

// Computed on construction: the walk touches hundreds of fields and a torn
// read across them would produce a breakdown that never existed. Rebuild it
// per frame.
class StatBreakdown {
public:
  static StatBreakdown For(const Player &c);

  explicit operator bool() const { return valid_; }

  const std::vector<StatContribution> &Contributions() const { return stats_; }
  const std::vector<EquippedSkill> &Skills() const { return skills_; }
  const std::vector<EquippedItem> &Items() const { return items_; }

  // Which of the two Player_CalcBattleParams formulas, and which shipped data
  // tables, are live.
  bool UsesNewTables() const { return newTables_; }

  // The word gating the class recompute. Clear, and the engine clamps or drops
  // the class contribution entirely.
  bool ClassBonusActive() const { return classGate_; }

  u32 SlotCount() const { return slotCount_; }

private:
  bool valid_ = false;
  bool newTables_ = false;
  bool classGate_ = false;
  u32 slotCount_ = 0;
  std::vector<StatContribution> stats_;
  std::vector<EquippedSkill> skills_;
  std::vector<EquippedItem> items_;
};

} // namespace bd::engine
