/**
 * @file    engine/character.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license   BSD 3-Clause License
 */
#include "engine/character.h"

#include <algorithm>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

// Handles hold the list node EA, so every field VA is node-relative. Chara and
// both bodies over it start at the same place, so one rebase serves all three.
constexpr u32 CharaField(size_t off) {
  return kNodeChara + static_cast<u32>(off);
}

constexpr u32 ParamField(size_t off) {
  return CharaField(offsetof(Chara_t, params) + off);
}

constexpr u32 ClassField(CharaClass c, size_t off) {
  return CharaField(offsetof(PlayerChara_t, classes) +
                    static_cast<size_t>(c) * sizeof(CharaClassRecord_t) + off);
}

// A ceiling on a guest vector walk. A larger count means the pointer triple was
// read while the guest was reallocating, not that a character has that many
// effects.
constexpr u32 kEffectWalkCap = 256;

// Both element types lead with the same two words, so one walk serves both
// containers.
static_assert(offsetof(CharaPassiveEffect_t, type) ==
              offsetof(CharaTimedEffect_t, type));
static_assert(offsetof(CharaPassiveEffect_t, value) ==
              offsetof(CharaTimedEffect_t, value));

// Scalar loads throughout. try_at validates alignment but not size, so pulling
// a whole element struct through it can over-read past a region boundary that a
// per-word load would have rejected.
std::vector<Character::EffectEntry> ReadEffects(u32 ea, u32 off, u32 stride,
                                                bool hasExtra) {
  std::vector<Character::EffectEntry> out;
  if (!ea)
    return out;
  using Triple = mem::GuestVec<u32>;
  const u32 base = ea + off;
  const u32 first = mem::try_load<u32>(base + offsetof(Triple, first));
  const u32 last = mem::try_load<u32>(base + offsetof(Triple, last));
  if (!first || last <= first)
    return out;
  const u32 n = std::min<u32>((last - first) / stride, kEffectWalkCap);
  out.reserve(n);
  for (u32 i = 0; i < n; ++i) {
    const u32 rec = first + i * stride;
    if (!mem::try_at<const be_u32>(rec))
      break;
    Character::EffectEntry e{};
    e.type = mem::try_load<u32>(rec + offsetof(CharaTimedEffect_t, type));
    e.value = mem::try_load<u32>(rec + offsetof(CharaTimedEffect_t, value));
    if (hasExtra)
      e.extra = mem::try_load<u32>(rec + offsetof(CharaTimedEffect_t, extra));
    out.push_back(e);
  }
  return out;
}

} // namespace

u32 Character::HP() const {
  return mem::try_field<u32>(ea_,
                             ParamField(offsetof(CharaBattleParams_t, curHP)));
}

u32 Character::MaxHP() const {
  return mem::try_field<u32>(ea_,
                             ParamField(offsetof(CharaBattleParams_t, maxHP)));
}

u32 Character::MP() const {
  return mem::try_field<u32>(ea_,
                             ParamField(offsetof(CharaBattleParams_t, curMP)));
}

u32 Character::MaxMP() const {
  return mem::try_field<u32>(ea_,
                             ParamField(offsetof(CharaBattleParams_t, maxMP)));
}

bool Character::IsAlive() const { return HP() > 0; }

CharaStats Character::Stats() const {
  return {
      mem::try_field<u32>(ea_,
                          ParamField(offsetof(CharaBattleParams_t, attack))),
      mem::try_field<u32>(ea_,
                          ParamField(offsetof(CharaBattleParams_t, defense))),
      mem::try_field<u32>(ea_,
                          ParamField(offsetof(CharaBattleParams_t, hitRate))),
      mem::try_field<u32>(
          ea_, ParamField(offsetof(CharaBattleParams_t, magicAttack))),
      mem::try_field<u32>(
          ea_, ParamField(offsetof(CharaBattleParams_t, magicDefense))),
      mem::try_field<u32>(ea_,
                          ParamField(offsetof(CharaBattleParams_t, agility))),
  };
}

u32 Character::StatusFlags() const {
  return mem::try_field<u32>(
      ea_, ParamField(offsetof(CharaBattleParams_t, statusFlags)));
}

bool Character::HasStatus(CharaStatus bit) const {
  return engine::HasStatus(StatusFlags(), bit);
}

u32 Character::StatusResist(CharaResist which) const {
  const u32 i = static_cast<u32>(which);
  if (!ea_ || i >= kCharaResistCount)
    return 0;
  const u8 *p = bd::mem::try_at<const u8>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, statusResist) + i));
  return p ? *p : 0;
}

u32 Character::ElementDefense(CharaElement which) const {
  const u32 i = static_cast<u32>(which);
  if (i >= kCharaElementCount)
    return 0;
  return mem::try_field<u16>(
      ea_, ParamField(offsetof(CharaBattleParams_t, elementDefense) +
                      i * sizeof(be_u16)));
}

std::vector<Character::EffectEntry> Character::PassiveEffects() const {
  return ReadEffects(ea_,
                     ParamField(offsetof(CharaBattleParams_t, passiveEffects)),
                     sizeof(CharaPassiveEffect_t), false);
}

std::vector<Character::EffectEntry> Character::TimedEffects() const {
  return ReadEffects(ea_,
                     ParamField(offsetof(CharaBattleParams_t, timedEffects)),
                     sizeof(CharaTimedEffect_t), true);
}

u32 Character::ParalyzeTurns() const {
  return mem::try_field<u32>(
      ea_, ParamField(offsetof(CharaBattleParams_t, paralyzeTurns)));
}

u32 Character::StunTurns() const {
  return mem::try_field<u32>(
      ea_, ParamField(offsetof(CharaBattleParams_t, stunTurns)));
}

u32 PlayableCharacter::SlotId() const {
  return mem::try_field<u32>(ea_, CharaField(offsetof(Chara_t, slotId)));
}

u32 PlayableCharacter::SlotIndex() const {
  const u32 slot = SlotId();
  return slot >= kSlotIdStride ? slot / kSlotIdStride - 1 : 0;
}

u32 PlayableCharacter::UniqueId() const {
  return mem::try_field<u32>(ea_, offsetof(PlyTask_t, uniqueId));
}

u32 PlayableCharacter::Level() const {
  return mem::try_field<u32>(ea_, CharaField(offsetof(PlayerChara_t, level)));
}

u32 PlayableCharacter::Exp() const {
  return mem::try_field<u32>(ea_, CharaField(offsetof(PlayerChara_t, exp)));
}

u32 PlayableCharacter::UnlockedClasses() const {
  return mem::try_field<u32>(
      ea_, CharaField(offsetof(PlayerChara_t, unlockedClasses)));
}

bool PlayableCharacter::IsClassUnlocked(CharaClass c) const {
  return (UnlockedClasses() & ClassBit(c)) != 0;
}

u32 PlayableCharacter::ClassRank(CharaClass c) const {
  return mem::try_field<u32>(ea_,
                             ClassField(c, offsetof(CharaClassRecord_t, rank)));
}

u32 PlayableCharacter::ClassSP(CharaClass c) const {
  return mem::try_field<u32>(ea_,
                             ClassField(c, offsetof(CharaClassRecord_t, sp)));
}

std::optional<CharaClass> PlayableCharacter::ActiveClass() const {
  const u32 raw = mem::try_field<u32>(
      ea_, CharaField(offsetof(PlayerChara_t, activeClass)));
  switch (raw) {
  case 0:
    return CharaClass::kWhiteMagic;
  case 1:
    return CharaClass::kBlackMagic;
  case 2:
    return CharaClass::kSupportMagic;
  case 3:
    return CharaClass::kBarrierMagic;
  case 4:
    return CharaClass::kSwordMaster;
  case 5:
    return CharaClass::kMonk;
  case 6:
    return CharaClass::kGuardian;
  case 7:
    return CharaClass::kAssassin;
  case 8:
    return CharaClass::kGeneralist;
  default:
    return std::nullopt;
  }
}

bool PlayableCharacter::CanLead() const {
  return !HasStatus(CharaStatus::kPetrify);
}

bool Character::SetHP(u32 v) {
  if (!ea_)
    return false;
  const u32 max = MaxHP();
  return bd::mem::try_store<u32>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, curHP)),
      max > 0 ? std::min(v, max) : v);
}

bool Character::SetMP(u32 v) {
  if (!ea_)
    return false;
  const u32 max = MaxMP();
  return bd::mem::try_store<u32>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, curMP)),
      max > 0 ? std::min(v, max) : v);
}

bool Character::SetStatusFlags(u32 v) {
  if (!ea_) return false;
  return bd::mem::try_store<u32>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, statusFlags)), v);
}

bool Character::SetStatusResist(CharaResist which, u32 v) {
  const u32 i = static_cast<u32>(which);
  if (!ea_ || i >= kCharaResistCount) return false;
  // One byte per status, so there is nothing to byte-swap. This goes through
  // try_at rather than try_store, whose be<T> wrapper has no single-byte form:
  // rex spells be_u8 as a plain u8 and never instantiates be<u8>.
  auto *p = bd::mem::try_at<u8>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, statusResist) + i));
  if (!p) return false;
  *p = static_cast<u8>(std::min<u32>(v, 255));
  return true;
}

bool Character::SetParalyzeTurns(u32 v) {
  if (!ea_) return false;
  return bd::mem::try_store<u32>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, paralyzeTurns)), v);
}

bool Character::SetStunTurns(u32 v) {
  if (!ea_) return false;
  return bd::mem::try_store<u32>(
      ea_ + ParamField(offsetof(CharaBattleParams_t, stunTurns)), v);
}

bool PlayableCharacter::SetExp(u32 v) {
  if (!ea_) return false;
  return bd::mem::try_store<u32>(ea_ + CharaField(offsetof(PlayerChara_t, exp)), v);
}

bool PlayableCharacter::SetUnlockedClasses(u32 mask) {
  if (!ea_) return false;
  return bd::mem::try_store<u32>(
      ea_ + CharaField(offsetof(PlayerChara_t, unlockedClasses)), mask);
}

bool PlayableCharacter::SetClassSP(CharaClass c, u32 v) {
  if (!ea_ || static_cast<u32>(c) >= kCharaClassCount) return false;
  return bd::mem::try_store<u32>(
      ea_ + ClassField(c, offsetof(CharaClassRecord_t, sp)), v);
}

u32 PlayableCharacter::StatBonus(PermanentBonus which) const {
  const u32 i = static_cast<u32>(which);
  if (i >= kPermanentBonusCount) return 0;
  return mem::try_field<u32>(
      ea_, CharaField(offsetof(PlayerChara_t, permanentBonus) + i * sizeof(be_u32)));
}

bool PlayableCharacter::SetStatBonus(PermanentBonus which, u32 v) {
  const u32 i = static_cast<u32>(which);
  if (!ea_ || i >= kPermanentBonusCount) return false;
  return bd::mem::try_store<u32>(
      ea_ + CharaField(offsetof(PlayerChara_t, permanentBonus) + i * sizeof(be_u32)), v);
}

u32 Enemy::TypeId() const {
  return mem::try_field<u32>(ea_, CharaField(offsetof(EnemyChara_t, typeId)));
}

} // namespace bd::engine
