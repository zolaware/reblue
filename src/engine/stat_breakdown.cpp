/**
 * @file    engine/stat_breakdown.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/stat_breakdown.h"

#include <algorithm>

#include "engine/config.h"
#include "engine/game_tables.h"

namespace bd::engine {

namespace {

// The five u16 slots of a stat block, in the order the final assignments at the
// tail of Player_CalcBattleParams read them.
enum class BlockStat : u32 {
  kAttack = 0,
  kAgility = 1,
  kMagicAttack = 2,
  kMagicDefense = 3,
  kDefense = 4,
};

struct StatWiring {
  const char *name;
  BlockStat block;
  PermanentBonus permanent;
  u32 CharaStats::*stored;
};

constexpr StatWiring kWiring[] = {
    {"attack", BlockStat::kAttack, PermanentBonus::kAttack,
     &CharaStats::attack},
    {"defense", BlockStat::kDefense, PermanentBonus::kDefense,
     &CharaStats::defense},
    {"magicAttack", BlockStat::kMagicAttack, PermanentBonus::kMagicAttack,
     &CharaStats::magicAttack},
    {"magicDefense", BlockStat::kMagicDefense, PermanentBonus::kMagicDefense,
     &CharaStats::magicDefense},
    {"agility", BlockStat::kAgility, PermanentBonus::kAgility,
     &CharaStats::agility},
};

std::optional<CharaClass> ToClass(u32 raw) {
  if (raw >= kCharaClassCount)
    return std::nullopt;
  return static_cast<CharaClass>(raw);
}

bool NewTablesActive() { return Config::Get().NewTables() != 0; }

} // namespace

StatBreakdown StatBreakdown::For(const Player &c) {
  StatBreakdown out;
  if (!c)
    return out;

  const CharaStats stored = c.Stats();
  const u32 unlocked = c.UnlockedClasses();
  const auto active = c.ActiveClass();

  for (const auto &w : kWiring) {
    const u32 index = static_cast<u32>(w.block);
    StatContribution sc{};
    sc.name = w.name;
    sc.final = stored.*(w.stored);
    sc.base = c.BaseStat(index);
    sc.permanent = c.PermanentBonus(w.permanent);
    if (active)
      sc.classActive = c.ClassStat(*active, index);
    for (u32 i = 0; i < kCharaClassCount; ++i) {
      if (!(unlocked & (1u << i)))
        continue;
      const u32 v = c.ClassStat(static_cast<CharaClass>(i), index);
      if (v > sc.classBest) {
        sc.classBest = v;
        sc.winner = static_cast<CharaClass>(i);
      }
    }
    out.stats_.push_back(sc);
  }

  if (active) {
    out.slotCount_ =
        std::min<u32>(c.ClassSlotCount(*active), kCharaSkillSlotCount);
    for (u32 i = 0; i < out.slotCount_; ++i) {
      const Player::SkillSlot slot = c.ClassSlot(*active, i);
      if (!slot.skillId)
        continue;
      EquippedSkill es{};
      es.slotIndex = i;
      es.sourceClass = ToClass(slot.classIndex);
      es.skillId = slot.skillId;
      if (es.sourceClass) {
        const auto def = Chara::FindClassSkill(*es.sourceClass, slot.skillId);
        if (def) {
          es.effectType = def->effectType;
          es.value = def->value;
          es.name = GameTables::Name(def->namePtr);
        }
      }
      out.skills_.push_back(es);
    }
  }

  const u32 worn = std::min<u32>(c.EquipCount(), kCharaEquipCount);
  for (u32 i = 0; i < worn; ++i) {
    EquippedItem it{};
    it.slotIndex = i;
    it.itemId = c.Equipment(i);
    if (!it.itemId)
      continue;
    it.name = GameTables::Get().PhenomeName(it.itemId);
    out.items_.push_back(it);
  }

  out.classGate_ = c.ClassGate() != 0;
  out.newTables_ = NewTablesActive();
  out.valid_ = true;
  return out;
}

} // namespace bd::engine
