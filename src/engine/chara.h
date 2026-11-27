/**
 * @file    engine/chara.h
 * @brief   The character handles: the Chara base every combatant shares, and
 *          the Player and Enemy bodies over it.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <array>
#include <optional>
#include <vector>

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

// World units for a position, radians for a YXZ euler rotation with yaw in the
// middle slot.
using Vec3 = std::array<f32, 3>;

// Bits of the status word. Player_RecalcParams clears kKelolon once the
// matching resist reaches kResistImmune, and the field party walk clears
// kKnockedOut as it restores a downed member to 1 HP.
enum class CharaStatus : u32 {
  kSleep = 0x001,
  kDizzy = 0x002,
  kPanic = 0x004,
  kParalyze = 0x008,
  kStun = 0x010,
  kUnknown20 = 0x020,
  kPoison = 0x040,
  kKelolon = 0x080,
  kStink = 0x100,
  kPetrify = 0x200,
  kKnockedOut = 0x400,
};

// A resistance at or above this value makes the matching status unappliable.
inline constexpr u32 kResistImmune = 100;

inline constexpr u32 StatusBit(CharaStatus bit) {
  return static_cast<u32>(bit);
}

inline bool HasStatus(u32 status_flags, CharaStatus bit) {
  return (status_flags & StatusBit(bit)) != 0;
}

inline const char *ToString(CharaStatus bit) {
  switch (bit) {
  case CharaStatus::kSleep:
    return "sleep";
  case CharaStatus::kDizzy:
    return "dizzy";
  case CharaStatus::kPanic:
    return "panic";
  case CharaStatus::kParalyze:
    return "paralyze";
  case CharaStatus::kStun:
    return "stun";
  case CharaStatus::kUnknown20:
    return "unknown20";
  case CharaStatus::kPoison:
    return "poison";
  case CharaStatus::kKelolon:
    return "kelolon";
  case CharaStatus::kStink:
    return "stink";
  case CharaStatus::kPetrify:
    return "petrify";
  case CharaStatus::kKnockedOut:
    return "knocked-out";
  }
  return "?";
}

// Every CharaStatus, in bit order, so a reader can name a whole flag word.
inline constexpr CharaStatus kCharaStatusBits[] = {
    CharaStatus::kSleep,    CharaStatus::kDizzy,      CharaStatus::kPanic,
    CharaStatus::kParalyze, CharaStatus::kStun,       CharaStatus::kUnknown20,
    CharaStatus::kPoison,   CharaStatus::kKelolon,    CharaStatus::kStink,
    CharaStatus::kPetrify,  CharaStatus::kKnockedOut,
};

// Index into the player's class records and bit index in its unlocked-class
// word. Player_RecalcParams walks 0..8 in this order.
enum class CharaClass : u32 {
  kWhiteMagic = 0,
  kBlackMagic = 1,
  kSupportMagic = 2,
  kBarrierMagic = 3,
  kSwordMaster = 4,
  kMonk = 5,
  kGuardian = 6,
  kAssassin = 7,
  kGeneralist = 8,
};

inline constexpr u32 kCharaClassCount = 9;

inline constexpr u32 ClassBit(CharaClass c) {
  return 1u << static_cast<u32>(c);
}

inline const char *ToString(CharaClass c) {
  switch (c) {
  case CharaClass::kWhiteMagic:
    return "white-magic";
  case CharaClass::kBlackMagic:
    return "black-magic";
  case CharaClass::kSupportMagic:
    return "support-magic";
  case CharaClass::kBarrierMagic:
    return "barrier-magic";
  case CharaClass::kSwordMaster:
    return "sword-master";
  case CharaClass::kMonk:
    return "monk";
  case CharaClass::kGuardian:
    return "guardian";
  case CharaClass::kAssassin:
    return "assassin";
  case CharaClass::kGeneralist:
    return "generalist";
  }
  return "?";
}

// Index into the status resist array. The engine orders these by itself, and
// it is not the CharaStatus bit order.
enum class CharaResist : u32 {
  kSleep = 0,
  kKelolon = 1,
  kDizzy = 2,
  kPoison = 3,
  kParalyze = 4,
  kPanic = 5,
  kStink = 6,
  kPetrify = 7,
  kStun = 8,
  kInstantDeath = 9,
  kWeaken = 10,
};

inline constexpr u32 kCharaResistCount = 11;

// Index into the element defense array. Light and dark sit apart from this
// run, in their own fields.
enum class CharaElement : u32 {
  kGeneral = 0,
  kFire = 1,
  kWater = 2,
  kEarth = 3,
  kWind = 4,
};

inline constexpr u32 kCharaElementCount = 5;

inline constexpr u32 kCharaKindEnemy = 2;

// Slot ids run in steps of ten, so the engine's own pc number is slotId/10 and
// its zero-based table index (slotId/10)-1.
inline constexpr u32 kSlotIdStride = 10;

// Player_CalcBattleParams adds one of these to each derived stat, in this
// fixed order.
enum class PermanentBonus : u32 {
  kMaxHP = 0,
  kMaxMP = 1,
  kAttack = 2,
  kMagicAttack = 3,
  kDefense = 4,
  kMagicDefense = 5,
  kAgility = 6,
};

inline constexpr u32 kPermanentBonusCount = 7;

inline constexpr u32 kCharaSkillSlotCount = 30;

// Accessory slots. Player_RecalcParams makes four or five live, and a load
// clamps to the full seven.
inline constexpr u32 kCharaEquipCount = 7;

// The five u16 slots a stat block carries, in the order the final assignments
// at the tail of Player_CalcBattleParams read them.
inline constexpr u32 kCharaBlockStatCount = 5;

// The six derived stats Player_RecalcParams computes.
struct CharaStats {
  u32 attack;
  u32 defense;
  u32 hitRate;
  u32 magicAttack;
  u32 magicDefense;
  u32 agility;
};

// Reads only the Chara base, leaving an enemy and a party member
// interchangeable here.
class Chara : public Object {
public:
  // Both effect containers key on type, and extra is always zero for a passive
  // entry, which has no third word.
  struct EffectEntry {
    u32 type;
    u32 value;
    u32 extra;
  };

  // One skill a class grants, as the shipped class table records it. namePtr
  // is a wide string the engine hands straight to d2anime.
  struct ClassSkill {
    u32 skillId;
    u32 effectType;
    u32 value;
    u32 namePtr;
  };

  Chara() = default;
  explicit Chara(u32 address) : Object(address) {}

  // The Chara whose CharaBattleParams sits at paramsAddress.
  static Chara FromParams(u32 paramsAddress);

  // Mirrors Chara_FindClassSkill: the class entry with this id, then the skill
  // with this id inside it. Empty when either misses.
  static std::optional<ClassSkill> FindClassSkill(CharaClass c, u32 skillId);

  u32 Kind() const;
  bool IsEnemy() const;
  u32 SlotId() const;    // 10/20/30/40/50, and 110 upward for added slots
  u32 SlotIndex() const; // (SlotId/kSlotIdStride)-1, or 0 below one full step
  u32 PartyOrder() const;

  u32 HP() const;
  u32 MaxHP() const;
  u32 MP() const;
  u32 MaxMP() const;
  bool IsAlive() const;

  // Mirror the in-engine writes with their clamps. False when the object does
  // not resolve or the store fails.
  bool SetHP(u32 v);
  bool SetMP(u32 v);

  CharaStats Stats() const;
  u32 StatusFlags() const;
  bool HasStatus(CharaStatus bit) const;
  bool SetStatusFlags(u32 v);

  // 0..kResistImmune, above which the matching status cannot be applied.
  u32 StatusResist(CharaResist which) const;
  bool SetStatusResist(CharaResist which, u32 v);
  u32 ElementDefense(CharaElement which) const;

  // Derived from the equipped skills, cleared and rebuilt by every
  // Player_RecalcParams.
  std::vector<EffectEntry> PassiveEffects() const;

  // Mutated during play. Chara_SetTimedEffect replaces any entry of the same
  // type.
  std::vector<EffectEntry> TimedEffects() const;

  // Counted down by Player_TickStatusEffects while the matching status bit is
  // set.
  u32 ParalyzeTurns() const;
  u32 StunTurns() const;
  bool SetParalyzeTurns(u32 v);
  bool SetStunTurns(u32 v);

  Vec3 Position() const;
  Vec3 Rotation() const;

  // Channel 0..2.
  f32 Ambient(u32 channel) const;
  bool SetAmbient(u32 channel, f32 v);
  f32 Alpha() const;
  bool SetAlpha(f32 v);
  u32 AlphaDirty() const;
  bool SetAlphaDirty(u32 v);
};

class Player : public Chara {
public:
  // One entry of a class record's equipped skill list: which class supplied
  // the skill, and its id within that class.
  struct SkillSlot {
    u32 classIndex;
    u32 skillId;
  };

  Player() = default;
  explicit Player(u32 address) : Chara(address) {}

  u32 Level() const;
  u32 Exp() const;
  u32 EquipCount() const;
  u32 Equipment(u32 slot) const;
  u32 UnlockedClasses() const; // ClassBit(CharaClass) per unlocked job
  bool SetUnlockedClasses(u32 mask);
  bool IsClassUnlocked(CharaClass c) const;
  u32 ClassRank(CharaClass c) const;
  u32 ClassSP(CharaClass c) const;
  bool SetClassSP(CharaClass c, u32 v);

  // The stat block every class record is raised against, index below
  // kCharaBlockStatCount.
  u32 BaseStat(u32 i) const;
  u32 ClassStat(CharaClass c, u32 i) const;

  // Equipped skills. ClassSlotCount is the stored count, which passive effects
  // can raise past kCharaSkillSlotCount, so clamp before indexing.
  u32 ClassSlotCount(CharaClass c) const;
  SkillSlot ClassSlot(CharaClass c, u32 i) const;

  // Clear, and the engine clamps or drops the class contribution entirely.
  u32 ClassGate() const;

  // Empty when the stored word does not name one of the nine jobs.
  std::optional<CharaClass> ActiveClass() const;

  u32 PermanentBonus(engine::PermanentBonus which) const;
  bool SetPermanentBonus(engine::PermanentBonus which, u32 v);

  // Petrified members are skipped by the camp leader cycle and by the field
  // party walk when it picks a replacement leader.
  bool CanLead() const;
};

class Enemy : public Chara {
public:
  Enemy() = default;
  explicit Enemy(u32 address) : Chara(address) {}

  // The em number, or 10000 + the bs number for a boss.
  u32 TypeId() const;
  u32 NextInGroup() const;
  static u32 NextInGroupOffset();
};

} // namespace bd::engine
