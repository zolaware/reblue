/**
 * @file    engine/chara.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/chara.h"

#include <algorithm>
#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace addr {
inline constexpr u32 kCharaClassTable = 0x82DC9B40; // g_pCharaClassTable
} // namespace addr

namespace {

// Effect types are carried as raw numbers rather than an enum: the set comes
// from the dev comments in the shipped data tables rather than from engine
// code, so it is advisory and open-ended. effect_names.h labels them.
//
// Type 11 is the one known contradiction. Player_ApplyPassiveEffect handles it
// as a structural type the recompute consumes, Player_RecalcParams adds its
// value to a non-active class's skill slot count, and the SkEf comment names a
// paralysis barrier. A barrier that widens the slot count fits all three, but
// that is unconfirmed in-engine.
//
// An element of CharaBattleParams_t::passiveEffects, rebuilt from the equipped
// skills on every Player_RecalcParams.
struct CharaPassiveEffect_t {
  /* 0x00 */ be_u32 type;
  /* 0x04 */ be_u32 value;
};
static_assert(sizeof(CharaPassiveEffect_t) == 0x08);

// An element of CharaBattleParams_t::timedEffects. Chara_SetTimedEffect erases
// any entry of the same type before pushing a replacement, so type is a key.
// The third word is written from that function's third argument and its
// meaning is not yet confirmed, so it is not named a turn count.
struct CharaTimedEffect_t {
  /* 0x00 */ be_u32 type;
  /* 0x04 */ be_u32 value;
  /* 0x08 */ be_u32 extra;
};
static_assert(sizeof(CharaTimedEffect_t) == 0x0C);

// Chara + 0x16FC. What battle math reads and equipment feeds: the resistances,
// the derived stats, live HP/MP and the status word. Player_RecalcParams
// (0x823B2270) takes its address as its second argument, and bdEventFindEntry
// searches the effect vector at its tail.
struct CharaBattleParams_t {
  /* 0x00 */ u8 _pad00[0x20];
  /* 0x20 */ u8 statusResist[kCharaResistCount]; // by CharaResist
  /* 0x2B */ u8 _pad2B[0x01];
  /* 0x2C */ be_u16 elementDefense[kCharaElementCount]; // by CharaElement
  /* 0x36 */ u8 _pad36[0x02];
  /* 0x38 */ be_u32 attack;
  /* 0x3C */ be_u32 defense;
  /* 0x40 */ be_u32 hitRate;
  /* 0x44 */ u8 _pad44[0x04];
  /* 0x48 */ be_u32 magicAttack;
  /* 0x4C */ be_u32 magicDefense;
  /* 0x50 */ u8 _pad50[0x04];
  /* 0x54 */ be_u32 agility;
  /* 0x58 */ u8 _pad58[0x07];
  /* 0x5F */ u8 slowResist;
  /* 0x60 */ u8 stopResist;
  /* 0x61 */ u8 _pad61[0x01];
  /* 0x62 */ be_u16 lightDefense;
  /* 0x64 */ be_u16 darkDefense;
  /* 0x66 */ u8 _pad66[0x06];
  /* 0x6C */ be_u32 curHP;
  /* 0x70 */ be_u32 curMP;
  /* 0x74 */ u8 _pad74[0x10];
  /* 0x84 */ be_u32 paralyzeTurns;
  /* 0x88 */ be_u32 stunTurns;
  /* 0x8C */ u8 _pad8C[0x0C];
  /* 0x98 */ be_u32 statusFlags; // CharaStatus bits
  /* 0x9C */ u8 _pad9C[0x14];
  /* 0xB0 */ be_u32 maxHP;
  /* 0xB4 */ be_u32 maxMP;
  /* 0xB8 */ u8 _padB8[0x18];
  // Both containers carry a leading word before the pointer triple, so the
  // vectors sit past the base addresses the engine passes around. Engine code
  // hands a container helper params+0xD0 or params+0xE0 and the helper reads
  // begin at +4.
  /* 0xD0 */ be_u32 _timedHeader;
  /* 0xD4 */ mem::GuestVec<CharaTimedEffect_t> timedEffects;
  /* 0xE0 */ be_u32 _passiveHeader;
  /* 0xE4 */ mem::GuestVec<CharaPassiveEffect_t> passiveEffects;
};
static_assert(sizeof(CharaBattleParams_t) == 0xF0);
static_assert(offsetof(CharaBattleParams_t, statusResist) == 0x20);
static_assert(offsetof(CharaBattleParams_t, elementDefense) == 0x2C);
static_assert(offsetof(CharaBattleParams_t, attack) == 0x38);
static_assert(offsetof(CharaBattleParams_t, agility) == 0x54);
static_assert(offsetof(CharaBattleParams_t, curHP) == 0x6C);
static_assert(offsetof(CharaBattleParams_t, curMP) == 0x70);
static_assert(offsetof(CharaBattleParams_t, statusFlags) == 0x98);
static_assert(offsetof(CharaBattleParams_t, maxHP) == 0xB0);
static_assert(offsetof(CharaBattleParams_t, maxMP) == 0xB4);
static_assert(offsetof(CharaBattleParams_t, paralyzeTurns) == 0x84);
static_assert(offsetof(CharaBattleParams_t, stunTurns) == 0x88);
static_assert(offsetof(CharaBattleParams_t, timedEffects) == 0xD4);
static_assert(offsetof(CharaBattleParams_t, passiveEffects) == 0xE4);

// One equipped skill slot: which class supplied the skill, and its id within
// that class.
struct CharaSkillSlot_t {
  /* 0x00 */ be_u32 classIndex;
  /* 0x04 */ be_u32 skillId;
};
static_assert(sizeof(CharaSkillSlot_t) == 0x08);

// The block at Chara+0x1AF0 and each of the nine records in
// PlayerChara_t::classes are the same type: Chara_CopyStatBlock copies one
// onto the other field for field. The player body spends on level and exp the
// two words a class record spends on rank and sp.
struct CharaStatBlock_t {
  /* 0x000 */ be_u16 stats[kCharaBlockStatCount];
  /* 0x00A */ u8 _pad00A[0x06];
  /* 0x010 */ be_u32 rank; // level, on the player body
  /* 0x014 */ be_u32 sp;   // exp, on the player body
  /* 0x018 */ be_u32 extraBonus;
  /* 0x01C */ be_u16 stats2[kCharaBlockStatCount];
  /* 0x026 */ u8 _pad026[0x02];
  /* 0x028 */ be_u32 equipment[kCharaEquipCount];
  /* 0x044 */ be_u32 equipCount;
  /* 0x048 */ u8 _pad048[0x1C];
  /* 0x064 */ CharaSkillSlot_t slots[kCharaSkillSlotCount];
  /* 0x154 */ be_u32 slotCount;
  /* 0x158 */ u8 resistBonus[kCharaResistCount];
  /* 0x163 */ u8 _pad163[0x05];
};
static_assert(sizeof(CharaStatBlock_t) == 0x168);
static_assert(offsetof(CharaStatBlock_t, rank) == 0x010);
static_assert(offsetof(CharaStatBlock_t, sp) == 0x014);
static_assert(offsetof(CharaStatBlock_t, equipment) == 0x028);
static_assert(offsetof(CharaStatBlock_t, equipCount) == 0x044);
static_assert(offsetof(CharaStatBlock_t, slots) == 0x064);
static_assert(offsetof(CharaStatBlock_t, slotCount) == 0x154);
static_assert(offsetof(CharaStatBlock_t, resistBonus) == 0x158);

// One of the nine job records in PlayerChara_t::classes.
using CharaClassRecord_t = CharaStatBlock_t;
static_assert(sizeof(CharaClassRecord_t) == 0x168);
static_assert(offsetof(CharaClassRecord_t, rank) == 0x10);
static_assert(offsetof(CharaClassRecord_t, sp) == 0x14);

constexpr u32 kCharaAmbientChannels = 3;

// Task + 0x70. The vtable'd character object Chara__ctor (0x822B7BD0) builds,
// shared by party members and enemies: CharaVO carries the model and world
// transform, params the combat state. Everything past it belongs to whichever
// body extends this base.
struct Chara_t {
  /* 0x0000 */ be_u32 vtable;
  /* 0x0004 */ u8 _pad0004[0x0B50]; // CharaVO +0x08, ChrPresetAnim +0x15A4
  /* 0x0B54 */ be_f32 ambient[kCharaAmbientChannels];
  /* 0x0B60 */ u8 _pad0B60[0x70];
  /* 0x0BD0 */ be_f32 alpha;
  /* 0x0BD4 */ u8 _pad0BD4[0x10];
  /* 0x0BE4 */ be_u32 alphaDirty;
  /* 0x0BE8 */ u8 _pad0BE8[0x9F0];
  /* 0x15D8 */ be_f32 position[3];
  /* 0x15E4 */ be_f32 rotation[3];
  /* 0x15F0 */ u8 _pad15F0[0xCC];
  // Position in the marching order, and the camp formation grid's only storage:
  // the Rank screen derives row from partyOrder / activeCount and column from
  // 4 - partyOrder % activeCount. Never renumber it flat.
  /* 0x16BC */ be_u32 partyOrder;
  /* 0x16C0 */ u8 _pad16C0[0x3C];
  /* 0x16FC */ CharaBattleParams_t params;
  /* 0x17EC */ u8 _pad17EC[0x34];
  /* 0x1820 */ be_u32 kind; // Chara__ctor's first argument
  /* 0x1824 */ be_u32
      slotId; // 10/20/30/40/50, the engine spells pc%02d from slotId/10
  /* 0x1828 */ u8 _pad1828[0x2C8];
};
static_assert(sizeof(Chara_t) == 0x1AF0);
static_assert(offsetof(Chara_t, ambient) == 0x0B54);
static_assert(offsetof(Chara_t, alpha) == 0x0BD0);
static_assert(offsetof(Chara_t, alphaDirty) == 0x0BE4);
static_assert(offsetof(Chara_t, position) == 0x15D8);
static_assert(offsetof(Chara_t, rotation) == 0x15E4);
static_assert(offsetof(Chara_t, partyOrder) == 0x16BC);
static_assert(offsetof(Chara_t, params) == 0x16FC);
static_assert(offsetof(Chara_t, kind) == 0x1820);
static_assert(offsetof(Chara_t, slotId) == 0x1824);

// Chara extended for a party member.
struct PlayerChara_t {
  /* 0x0000 */ Chara_t base;
  /* 0x1AF0 */ be_u16 baseStats[kCharaBlockStatCount];
  /* 0x1AFA */ u8 _pad1AFA[0x06];
  /* 0x1B00 */ be_u32 level;
  /* 0x1B04 */ be_u32 exp;
  /* 0x1B08 */ u8 _pad1B08[0x10];
  /* 0x1B18 */ be_u32
      equipment[kCharaEquipCount]; // item ids, equipCount of them live
  /* 0x1B34 */ be_u32
      equipCount; // Player_RecalcParams sets 4 or 5, load clamps to 7
  /* 0x1B38 */ be_u32 unlockedClasses;    // ClassBit(CharaClass)
  /* 0x1B3C */ be_u32 classGate;          // gates the class recompute
  /* 0x1B40 */ be_u32 activeClass;        // CharaClass
  /* 0x1B44 */ be_u32 activeClassLatched; // copy Player_RecalcParams stamps
  /* 0x1B48 */ u8 _pad1B48[0x10];
  /* 0x1B58 */ CharaClassRecord_t classes[kCharaClassCount];
  /* 0x2800 */ be_u32 permanentBonus[kPermanentBonusCount];
  /* 0x281C */ u8 _pad281C[0x0C];
};
static_assert(sizeof(PlayerChara_t) == 0x2828);
static_assert(offsetof(PlayerChara_t, baseStats) == 0x1AF0);
static_assert(offsetof(PlayerChara_t, level) == 0x1B00);
static_assert(offsetof(PlayerChara_t, exp) == 0x1B04);
static_assert(offsetof(PlayerChara_t, equipment) == 0x1B18);
static_assert(offsetof(PlayerChara_t, equipCount) == 0x1B34);
static_assert(offsetof(PlayerChara_t, unlockedClasses) == 0x1B38);
static_assert(offsetof(PlayerChara_t, classGate) == 0x1B3C);
static_assert(offsetof(PlayerChara_t, activeClass) == 0x1B40);
static_assert(offsetof(PlayerChara_t, classes) == 0x1B58);
static_assert(offsetof(PlayerChara_t, permanentBonus) == 0x2800);

// Chara extended for a battle enemy. typeId takes the word a party member
// spends on equipCount and nextInGroup on the one that gates its class
// recompute, making these two bodies alternatives over the same bytes rather
// than one struct.
//
// An enemy carries two chain links, and they thread two different lists off the
// same EnePtyTask. The one in _pad1B38 belongs to the spawn list at group+0xF8,
// which the field build and teardown own. nextInGroup belongs to the list at
// group+0xFC, which is the one the engine's own battle walks read, so anything
// reachable from the battle manager takes nextInGroup.
struct EnemyChara_t {
  /* 0x0000 */ Chara_t base;
  /* 0x1AF0 */ u8 _pad1AF0[0x44];
  /* 0x1B34 */ be_u32 typeId; // em number, or 10000 + bs number for a boss
  /* 0x1B38 */ u8 _pad1B38[0x04];
  /* 0x1B3C */ be_u32 nextInGroup;
};
static_assert(offsetof(EnemyChara_t, typeId) == 0x1B34);
static_assert(offsetof(EnemyChara_t, nextInGroup) == 0x1B3C);
static_assert(offsetof(EnemyChara_t, typeId) ==
              offsetof(PlayerChara_t, equipCount));

// One skill a class grants. Chara_FindClassSkill finds a class entry by
// classId then scans its skills for a skillId.
struct CharaSkillDef_t {
  /* 0x00 */ be_u32 skillId;
  /* 0x04 */ be_u32 _unk04;
  /* 0x08 */ be_u32 effectType;
  /* 0x0C */ be_u32 value;
  // Camp_Shadow_BindSkillClassWindow feeds this to the d2anime wide-string
  // setter, so a skill names itself rather than being looked up in a table.
  /* 0x10 */ be_u32 namePtr;
  /* 0x14 */ u8 _pad14[0x04];
};
static_assert(sizeof(CharaSkillDef_t) == 0x18);
static_assert(offsetof(CharaSkillDef_t, namePtr) == 0x10);

constexpr u32 kClassSkillCount = 15;
constexpr u32 kClassInnateCount = 3;

// One entry of the table g_pCharaClassTable points at.
struct CharaClassTableEntry_t {
  /* 0x000 */ u8 _pad000[0x10];
  /* 0x010 */ be_u32 classId;
  /* 0x014 */ u8 _pad014[0x3C];
  /* 0x050 */ CharaSkillDef_t skills[kClassSkillCount];
  /* 0x1B8 */ CharaSkillDef_t innate[kClassInnateCount];
  /* 0x200 */ u8 _pad200[0x30];
};
static_assert(sizeof(CharaClassTableEntry_t) == 0x230);
static_assert(offsetof(CharaClassTableEntry_t, classId) == 0x010);
static_assert(offsetof(CharaClassTableEntry_t, skills) == 0x050);
static_assert(offsetof(CharaClassTableEntry_t, innate) == 0x1B8);

// A ceiling on a vector walk. A larger count means the pointer triple was read
// while the engine was reallocating, not that a character has that many
// effects.
constexpr u32 kEffectWalkCap = 256;

Chara::EffectEntry ToEntry(const CharaPassiveEffect_t &e) {
  return {e.type, e.value, 0};
}

Chara::EffectEntry ToEntry(const CharaTimedEffect_t &e) {
  return {e.type, e.value, e.extra};
}

template <class T>
std::vector<Chara::EffectEntry> ReadEffects(const mem::GuestVec<T> &vec) {
  std::vector<Chara::EffectEntry> out;
  const u32 first = vec.first;
  const u32 last = vec.last;
  if (!first || last <= first)
    return out;
  const u32 n = std::min<u32>((last - first) / sizeof(T), kEffectWalkCap);
  out.reserve(n);
  for (u32 i = 0; i < n; ++i)
    out.push_back(ToEntry(vec[i]));
  return out;
}

} // namespace

Chara Chara::FromParams(u32 paramsAddress) {
  return paramsAddress ? Chara(paramsAddress - offsetof(Chara_t, params))
                       : Chara();
}

std::optional<Chara::ClassSkill> Chara::FindClassSkill(CharaClass c,
                                                       u32 skillId) {
  const u32 table = mem::try_load<u32>(addr::kCharaClassTable);
  if (!table)
    return std::nullopt;
  const u32 classId = static_cast<u32>(c);
  for (u32 i = 0; i < kCharaClassCount; ++i) {
    const auto *entry = mem::try_at<const CharaClassTableEntry_t>(
        table + i * sizeof(CharaClassTableEntry_t));
    if (!entry || static_cast<u32>(entry->classId) != classId)
      continue;
    for (const CharaSkillDef_t &def : entry->skills)
      if (static_cast<u32>(def.skillId) == skillId)
        return ClassSkill{static_cast<u32>(def.skillId),
                          static_cast<u32>(def.effectType),
                          static_cast<u32>(def.value),
                          static_cast<u32>(def.namePtr)};
    return std::nullopt;
  }
  return std::nullopt;
}

u32 Chara::Kind() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->kind) : 0;
}

bool Chara::IsEnemy() const { return Kind() == kCharaKindEnemy; }

u32 Chara::SlotId() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->slotId) : 0;
}

u32 Chara::SlotIndex() const {
  const u32 slot = SlotId();
  return slot >= kSlotIdStride ? slot / kSlotIdStride - 1 : 0;
}

u32 Chara::PartyOrder() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->partyOrder) : 0;
}

u32 Chara::HP() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.curHP) : 0;
}

u32 Chara::MaxHP() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.maxHP) : 0;
}

u32 Chara::MP() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.curMP) : 0;
}

u32 Chara::MaxMP() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.maxMP) : 0;
}

bool Chara::IsAlive() const { return HP() > 0; }

bool Chara::SetHP(u32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  const u32 max = self->params.maxHP;
  self->params.curHP = max > 0 ? std::min(v, max) : v;
  return true;
}

bool Chara::SetMP(u32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  const u32 max = self->params.maxMP;
  self->params.curMP = max > 0 ? std::min(v, max) : v;
  return true;
}

CharaStats Chara::Stats() const {
  const auto *self = Self<Chara_t>();
  if (!self)
    return {};
  const CharaBattleParams_t &params = self->params;
  return {
      params.attack,
      params.defense,
      params.hitRate,
      params.magicAttack,
      params.magicDefense,
      params.agility,
  };
}

u32 Chara::StatusFlags() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.statusFlags) : 0;
}

bool Chara::SetStatusFlags(u32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->params.statusFlags = v;
  return true;
}

bool Chara::HasStatus(CharaStatus bit) const {
  return engine::HasStatus(StatusFlags(), bit);
}

u32 Chara::StatusResist(CharaResist which) const {
  const u32 i = static_cast<u32>(which);
  if (i >= kCharaResistCount)
    return 0;
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.statusResist[i]) : 0;
}

bool Chara::SetStatusResist(CharaResist which, u32 v) {
  const u32 i = static_cast<u32>(which);
  if (i >= kCharaResistCount)
    return false;
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->params.statusResist[i] = static_cast<u8>(v);
  return true;
}

u32 Chara::ElementDefense(CharaElement which) const {
  const u32 i = static_cast<u32>(which);
  if (i >= kCharaElementCount)
    return 0;
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.elementDefense[i]) : 0;
}

std::vector<Chara::EffectEntry> Chara::PassiveEffects() const {
  const Chara_t *self = Self<Chara_t>();
  return self ? ReadEffects(self->params.passiveEffects)
              : std::vector<EffectEntry>{};
}

std::vector<Chara::EffectEntry> Chara::TimedEffects() const {
  const Chara_t *self = Self<Chara_t>();
  return self ? ReadEffects(self->params.timedEffects)
              : std::vector<EffectEntry>{};
}

u32 Chara::ParalyzeTurns() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.paralyzeTurns) : 0;
}

u32 Chara::StunTurns() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->params.stunTurns) : 0;
}

bool Chara::SetParalyzeTurns(u32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->params.paralyzeTurns = v;
  return true;
}

bool Chara::SetStunTurns(u32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->params.stunTurns = v;
  return true;
}

Vec3 Chara::Position() const {
  Vec3 v{};
  const auto *self = Self<Chara_t>();
  if (!self)
    return v;
  for (u32 i = 0; i < v.size(); ++i)
    v[i] = self->position[i];
  return v;
}

Vec3 Chara::Rotation() const {
  Vec3 v{};
  const auto *self = Self<Chara_t>();
  if (!self)
    return v;
  for (u32 i = 0; i < v.size(); ++i)
    v[i] = self->rotation[i];
  return v;
}

f32 Chara::Ambient(u32 channel) const {
  if (channel >= kCharaAmbientChannels)
    return 0.0f;
  const auto *self = Self<Chara_t>();
  return self ? static_cast<f32>(self->ambient[channel]) : 0.0f;
}

bool Chara::SetAmbient(u32 channel, f32 v) {
  if (channel >= kCharaAmbientChannels)
    return false;
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->ambient[channel] = v;
  return true;
}

f32 Chara::Alpha() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<f32>(self->alpha) : 0.0f;
}

bool Chara::SetAlpha(f32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->alpha = v;
  return true;
}

u32 Chara::AlphaDirty() const {
  const auto *self = Self<Chara_t>();
  return self ? static_cast<u32>(self->alphaDirty) : 0;
}

bool Chara::SetAlphaDirty(u32 v) {
  auto *self = Self<Chara_t>();
  if (!self)
    return false;
  self->alphaDirty = v;
  return true;
}

u32 Player::Level() const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->level) : 0;
}

u32 Player::Exp() const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->exp) : 0;
}

u32 Player::EquipCount() const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->equipCount) : 0;
}

u32 Player::Equipment(u32 slot) const {
  if (slot >= kCharaEquipCount)
    return 0;
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->equipment[slot]) : 0;
}

u32 Player::UnlockedClasses() const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->unlockedClasses) : 0;
}

bool Player::SetUnlockedClasses(u32 mask) {
  auto *self = Self<PlayerChara_t>();
  if (!self)
    return false;
  self->unlockedClasses = mask;
  return true;
}

bool Player::IsClassUnlocked(CharaClass c) const {
  return (UnlockedClasses() & ClassBit(c)) != 0;
}

u32 Player::ClassRank(CharaClass c) const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->classes[static_cast<u32>(c)].rank) : 0;
}

u32 Player::ClassSP(CharaClass c) const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->classes[static_cast<u32>(c)].sp) : 0;
}

bool Player::SetClassSP(CharaClass c, u32 v) {
  const u32 i = static_cast<u32>(c);
  if (i >= kCharaClassCount)
    return false;
  auto *self = Self<PlayerChara_t>();
  if (!self)
    return false;
  self->classes[i].sp = v;
  return true;
}

u32 Player::BaseStat(u32 i) const {
  if (i >= kCharaBlockStatCount)
    return 0;
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->baseStats[i]) : 0;
}

u32 Player::ClassStat(CharaClass c, u32 i) const {
  if (i >= kCharaBlockStatCount)
    return 0;
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->classes[static_cast<u32>(c)].stats[i])
              : 0;
}

u32 Player::ClassSlotCount(CharaClass c) const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->classes[static_cast<u32>(c)].slotCount)
              : 0;
}

Player::SkillSlot Player::ClassSlot(CharaClass c, u32 i) const {
  if (i >= kCharaSkillSlotCount)
    return {};
  const auto *self = Self<PlayerChara_t>();
  if (!self)
    return {};
  const CharaSkillSlot_t &slot = self->classes[static_cast<u32>(c)].slots[i];
  return {static_cast<u32>(slot.classIndex), static_cast<u32>(slot.skillId)};
}

u32 Player::ClassGate() const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->classGate) : 0;
}

std::optional<CharaClass> Player::ActiveClass() const {
  const auto *self = Self<PlayerChara_t>();
  const u32 raw = self ? static_cast<u32>(self->activeClass) : 0;
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

u32 Player::PermanentBonus(engine::PermanentBonus which) const {
  const auto *self = Self<PlayerChara_t>();
  return self ? static_cast<u32>(self->permanentBonus[static_cast<u32>(which)])
              : 0;
}

bool Player::SetPermanentBonus(engine::PermanentBonus which, u32 v) {
  const u32 i = static_cast<u32>(which);
  if (i >= kPermanentBonusCount)
    return false;
  auto *self = Self<PlayerChara_t>();
  if (!self)
    return false;
  self->permanentBonus[i] = v;
  return true;
}

bool Player::CanLead() const { return !HasStatus(CharaStatus::kPetrify); }

u32 Enemy::TypeId() const {
  const auto *self = Self<EnemyChara_t>();
  return self ? static_cast<u32>(self->typeId) : 0;
}

u32 Enemy::NextInGroup() const {
  const auto *self = Self<EnemyChara_t>();
  return self ? static_cast<u32>(self->nextInGroup) : 0;
}

u32 Enemy::NextInGroupOffset() {
  return static_cast<u32>(offsetof(EnemyChara_t, nextInGroup));
}

} // namespace bd::engine
