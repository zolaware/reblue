/**
 * @file    engine/character.h
 * @brief   Live handles over the character list node: the Chara base every
 *          combatant shares, and the party member and enemy bodies over it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <optional>
#include <vector>

#include <rex/types.h>

#include "engine/chara_types.h"

namespace bd::engine {

class Party;
class Roster;
class Battle;

// The six derived stats Player_RecalcParams computes into CharaBattleParams_t.
struct CharaStats {
  u32 attack;
  u32 defense;
  u32 hitRate;
  u32 magicAttack;
  u32 magicDefense;
  u32 agility;
};

// A handle, not a copy: every accessor re-reads guest memory through
// bd::mem::try_load, so a node freed between two calls yields defaults rather
// than faulting the host. Reads only the Chara base, leaving an enemy and a
// party member interchangeable here. No virtuals, so narrowing a
// PlayableCharacter to a Character is how code that does not care which it has
// gets written.
class Character {
public:
  Character() = default;

  explicit operator bool() const { return ea_ != 0; }
  u32 Address() const { return ea_; }

  u32 HP() const;
  u32 MaxHP() const;
  u32 MP() const;
  u32 MaxMP() const;
  bool IsAlive() const;

  CharaStats Stats() const;

  u32 StatusFlags() const;
  bool HasStatus(CharaStatus bit) const;

  // 0..kResistImmune, above which the matching status cannot be applied.
  u32 StatusResist(CharaResist which) const;
  u32 ElementDefense(CharaElement which) const;

  // Both effect containers key on type, so a raw type id is the identity a
  // caller matches on. extra is always zero for a passive entry, which has no
  // third word.
  struct EffectEntry {
    u32 type;
    u32 value;
    u32 extra;
  };

  // Derived from the equipped skills, cleared and rebuilt by every
  // Player_RecalcParams.
  std::vector<EffectEntry> PassiveEffects() const;

  // Mutated during play. Chara_SetTimedEffect replaces any entry of the
  // same type.
  std::vector<EffectEntry> TimedEffects() const;

  // Counted down by Player_TickStatusEffects while the matching status
  // bit is set.
  u32 ParalyzeTurns() const;
  u32 StunTurns() const;

  // Mirror the in-engine writes with their clamps. These sit on Character
  // rather than PlayableCharacter because every field they touch is in the
  // Chara base an enemy shares.
  bool SetHP(u32 v);
  bool SetMP(u32 v);
  bool SetStatusFlags(u32 v);
  bool SetStatusResist(CharaResist which, u32 v);
  bool SetParalyzeTurns(u32 v);
  bool SetStunTurns(u32 v);

  // A handle over an arbitrary node EA, for tooling that enumerates subjects
  // itself rather than receiving them from Party, Roster or Battle.
  static Character FromAddress(u32 ea) { return Character(ea); }

protected:
  explicit Character(u32 ea) : ea_(ea) {}
  u32 ea_ = 0;
};

class PlayableCharacter : public Character {
public:
  PlayableCharacter() = default;

  u32 SlotId() const;    // 10/20/30/40/50, and 110 upward for added slots
  u32 SlotIndex() const; // (SlotId/kSlotIdStride)-1, or 0 below one full step
  u32 UniqueId() const;
  u32 Level() const;
  u32 Exp() const;

  u32 UnlockedClasses() const; // ClassBit(CharaClass) per unlocked job
  bool IsClassUnlocked(CharaClass c) const;
  u32 ClassRank(CharaClass c) const;
  u32 ClassSP(CharaClass c) const;

  // Empty when the guest word does not name one of the nine jobs.
  std::optional<CharaClass> ActiveClass() const;

  // Petrified members are skipped by the camp leader cycle and by the field
  // party walk when it picks a replacement leader.
  bool CanLead() const;

  bool SetExp(u32 v);
  bool SetUnlockedClasses(u32 mask);
  bool SetClassSP(CharaClass c, u32 v);

  // Player_CalcBattleParams adds one of these to each derived stat after it
  // recomputes, so a write here survives the recompute that would clobber a
  // direct write to the stat itself.
  u32 StatBonus(PermanentBonus which) const;
  bool SetStatBonus(PermanentBonus which, u32 v);

  // As Character::FromAddress, for callers that already know the node is a
  // party member rather than an enemy.
  static PlayableCharacter FromAddress(u32 ea) { return PlayableCharacter(ea); }

private:
  friend class Party;
  friend class Roster;
  friend class Battle;
  // The event catalog in engine/events.h. Each struct constructs its handle in
  // its own constructor so a publisher never names the handle type.
  friend struct PlayerSpawned;
  friend struct PlayerDied;
  friend struct PartyMemberAdded;
  friend struct PartyLeaderChanged;
  explicit PlayableCharacter(u32 ea) : Character(ea) {}
};

class Enemy : public Character {
public:
  Enemy() = default;

  // The em number, or 10000 + the bs number for a boss.
  u32 TypeId() const;

private:
  friend class Battle;
  friend struct EnemySpawned;
  friend struct EnemyKilled;
  explicit Enemy(u32 ea) : Character(ea) {}
};

} // namespace bd::engine
