/**
 * @file    engine/cheats.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/cheats.h"

#include <algorithm>
#include <vector>
#include <iterator>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/stack.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "core/settings.h" // kCvarGroup, FormatCvar
#include "engine/events.h"
#include "engine/game.h"
#include "engine/achievements/achievements.h"
#include "engine/game_tables.h"

// The guest's own award-EXP entry point, at 0x823B3CD0. It adds to the running
// total at chara+0x1B04 and then walks its own level-up loop until the surplus
// is spent, refusing to pass the max level table[0] names. Handing it the bonus
// beats storing the total ourselves: the levels land on the battle that earned
// them instead of the one after, which is what a bare write leaves behind.
REX_IMPORT(__imp__Player_AddExp, GuestAddExp, u32(u32, u32));

REXCVAR_DECLARE(bool, bd_cheat_diag);
REXCVAR_DECLARE(bool, bd_cheat_invincible);
REXCVAR_DECLARE(bool, bd_cheat_infinite_mp);
REXCVAR_DECLARE(bool, bd_cheat_infinite_gold);
REXCVAR_DECLARE(bool, bd_cheat_status_immune);
REXCVAR_DECLARE(bool, bd_cheat_unlock_classes);
REXCVAR_DECLARE(bool, bd_cheat_one_hit_kill);
REXCVAR_DECLARE(i32, bd_cheat_attack_mult);
REXCVAR_DECLARE(i32, bd_cheat_magic_attack_mult);
REXCVAR_DECLARE(i32, bd_cheat_magic_defence_mult);
REXCVAR_DECLARE(i32, bd_cheat_agility_mult);
REXCVAR_DECLARE(i32, bd_cheat_medals_mult);
REXCVAR_DECLARE(bool, bd_cheat_infinite_medals);
REXCVAR_DECLARE(bool, bd_cheat_infinite_items);
REXCVAR_DECLARE(bool, bd_cheat_give_all_items);
REXCVAR_DECLARE(bool, bd_cheat_unlock_achievements);
REXCVAR_DECLARE(bool, bd_cheat_reset_achievements);
REXCVAR_DECLARE(bool, bd_cheat_give_heal);
REXCVAR_DECLARE(bool, bd_cheat_give_usable);
REXCVAR_DECLARE(bool, bd_cheat_give_spellbook);
REXCVAR_DECLARE(bool, bd_cheat_give_arm);
REXCVAR_DECLARE(bool, bd_cheat_give_finger);
REXCVAR_DECLARE(bool, bd_cheat_give_ear);
REXCVAR_DECLARE(bool, bd_cheat_give_neck);
REXCVAR_DECLARE(bool, bd_cheat_give_chest);
REXCVAR_DECLARE(bool, bd_cheat_give_valuable);

REXCVAR_DECLARE(i32, bd_cheat_defence_mult);
REXCVAR_DECLARE(i32, bd_cheat_stat_bonus);
REXCVAR_DECLARE(i32, bd_cheat_exp_mult);
REXCVAR_DECLARE(i32, bd_cheat_sp_mult);
REXCVAR_DECLARE(i32, bd_cheat_gold_mult);

REXCVAR_DEFINE_BOOL(bd_cheat_diag, false, kCvarGroup,
                    "Log a line whenever a cheat acts, naming what it changed. "
                    "For checking a cheat fires; off by default.");

REXCVAR_DEFINE_BOOL(bd_cheat_invincible, false, kCvarGroup,
                    "Refill every living party member's HP each logic step. A "
                    "member already knocked out stays down.");

REXCVAR_DEFINE_BOOL(bd_cheat_infinite_mp, false, kCvarGroup,
                    "Refill every living party member's MP each logic step.");

REXCVAR_DEFINE_BOOL(bd_cheat_infinite_gold, false, kCvarGroup,
                    "Hold gold at the 99,999,999 ceiling the game clamps to.");

REXCVAR_DEFINE_BOOL(bd_cheat_status_immune, false, kCvarGroup,
                    "Hold every status resistance at the immune threshold and "
                    "clear any ailment already applied.");

REXCVAR_DEFINE_BOOL(bd_cheat_unlock_classes, false, kCvarGroup,
                    "Keep all nine classes unlocked on every roster member.");

REXCVAR_DEFINE_BOOL(bd_cheat_one_hit_kill, false, kCvarGroup,
                    "Hold every living enemy at 1 HP while a battle is up, so "
                    "the next hit that lands kills it.");

REXCVAR_DEFINE_INT32(bd_cheat_attack_mult, 1, kCvarGroup,
                     "Multiply physical attack as the guest finishes each stat "
                     "recompute. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_magic_attack_mult, 1, kCvarGroup,
                     "Multiply magic attack as the guest finishes each stat "
                     "recompute. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_defence_mult, 1, kCvarGroup,
                     "Multiply physical defense as the guest finishes each "
                     "stat recompute. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_magic_defence_mult, 1, kCvarGroup,
                     "Multiply magic defense as the guest finishes each stat "
                     "recompute. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_agility_mult, 1, kCvarGroup,
                     "Multiply agility, which drives turn order, as the guest "
                     "finishes each stat recompute. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_stat_bonus, 0, kCvarGroup,
                     "Flat addition held in every permanent stat bonus slot: "
                     "max HP/MP, attack, magic attack, defense, magic defense "
                     "and agility. 0 is off.");

REXCVAR_DEFINE_INT32(bd_cheat_exp_mult, 1, kCvarGroup,
                     "Multiply the EXP a battle awards. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_sp_mult, 1, kCvarGroup,
                     "Multiply the class SP a battle awards. 1 leaves it "
                     "alone.");

REXCVAR_DEFINE_INT32(bd_cheat_gold_mult, 1, kCvarGroup,
                     "Multiply the gold a battle awards. 1 leaves it alone.");

REXCVAR_DEFINE_INT32(bd_cheat_medals_mult, 1, kCvarGroup,
                     "Multiply the ancient medals a battle awards. 1 leaves "
                     "them alone.");

REXCVAR_DEFINE_BOOL(bd_cheat_infinite_medals, false, kCvarGroup,
                    "Hold ancient medals at the 9999 ceiling the game clamps "
                    "to.");

REXCVAR_DEFINE_BOOL(bd_cheat_infinite_items, false, kCvarGroup,
                    "Hold every occupied inventory slot at a full stack, so "
                    "nothing is spent by using or selling it.");

REXCVAR_DEFINE_BOOL(bd_cheat_give_all_items, false, kCvarGroup,
                    "Fill the inventory with one full stack of every item the "
                    "record table knows. Runs once, then clears itself.");

REXCVAR_DEFINE_BOOL(bd_cheat_unlock_achievements, false, kCvarGroup,
                    "Award every re:Blue achievement through the same path a "
                    "condition would. Runs once, then clears itself.");

REXCVAR_DEFINE_BOOL(bd_cheat_reset_achievements, false, kCvarGroup,
                    "Re-lock every achievement by clearing the profile's "
                    "unlock store. Runs once, then clears itself.");

REXCVAR_DEFINE_BOOL(bd_cheat_give_heal, false, kCvarGroup,
                    "Grant one of every heal record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_usable, false, kCvarGroup,
                    "Grant one of every usable record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_spellbook, false, kCvarGroup,
                    "Grant one of every spellbook record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_arm, false, kCvarGroup,
                    "Grant one of every arm record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_finger, false, kCvarGroup,
                    "Grant one of every finger record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_ear, false, kCvarGroup,
                    "Grant one of every ear record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_neck, false, kCvarGroup,
                    "Grant one of every neck record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_chest, false, kCvarGroup,
                    "Grant one of every chest record. Runs once, then clears "
                    "itself.");
REXCVAR_DEFINE_BOOL(bd_cheat_give_valuable, false, kCvarGroup,
                    "Grant one of every valuable record. Runs once, then clears "
                    "itself.");

namespace bd::engine {
namespace {

// What Inventory::SetGold clamps to, so pinning here writes what the guest
// would have stored rather than a value it has to correct.
constexpr u32 kGoldMax = 99999999u;

// What Inventory::SetAt clamps a stack to, so pinning here writes a count the
// guest would have stored itself.
constexpr u32 kItemStackMax = 99u;

// Ceiling on the id sweep for "give all". Well past the ids the shipped item
// table uses, and the walk stops early once the inventory is full anyway.
constexpr u32 kMaxItemId = 4095u;

// Logic steps a fired grant stays lit before disarming: about half a second at
// the fixed 30Hz simulation rate. Long enough to see, short enough that the
// row is plainly a button rather than a setting.
constexpr u32 kGrantLitSteps = 15;

// Each category grant is its own cvar so the menu has a row to bind to.
struct GrantEntry {
  ItemCategory cat;
  const char *cvar;
};
constexpr GrantEntry kGrantEntries[] = {
    {ItemCategory::kHeal, "bd_cheat_give_heal"},
    {ItemCategory::kUsable, "bd_cheat_give_usable"},
    {ItemCategory::kSpellbook, "bd_cheat_give_spellbook"},
    {ItemCategory::kArm, "bd_cheat_give_arm"},
    {ItemCategory::kFinger, "bd_cheat_give_finger"},
    {ItemCategory::kEar, "bd_cheat_give_ear"},
    {ItemCategory::kNeck, "bd_cheat_give_neck"},
    {ItemCategory::kChest, "bd_cheat_give_chest"},
    {ItemCategory::kValuable, "bd_cheat_give_valuable"},
};

// EXP and class SP are read back as signed by the screens that print them, so
// a top-up allowed to run to the u32 ceiling shows up as a negative number.
// This sits far above anything a playthrough reaches and an order of magnitude
// below where the sign bit turns over.
constexpr u32 kProgressMax = 99999999u;

// Every class bit set.
constexpr u32 kAllClasses = (1u << kCharaClassCount) - 1u;

// Steps the reward award keeps retrying after the battle-end edge, at the
// fixed 30Hz simulation rate. Two seconds covers a guest that banks its
// rewards after the edge without reaching far enough to catch a chest opened
// on the way out.
constexpr u32 kSettleSteps = 60;

// Multipliers are offered as a short menu, but a console write can put
// anything in the cvar, so both ends are held to something survivable.
constexpr i32 kMultMin = 1;
constexpr i32 kMultMax = 99;
constexpr i32 kStatBonusMax = 99999;

// A derived stat, at its offset inside the params block the guest just wrote.
// Reads back what it computed and stores the product.
//
// The ceiling is far above any value the game produces and far below the point
// a u32 wraps, so a stat the menus print as signed can never come out negative
// even if this is handed a block it did not expect.
constexpr u32 kStatCeiling = 999999u;

bool ScaleStat(u32 paramsEA, size_t field, i32 mult) {
  const u32 va = paramsEA + static_cast<u32>(field);
  const u32 cur = mem::try_load<u32>(va);
  if (cur == 0)
    return false;
  const u64 scaled = static_cast<u64>(cur) * static_cast<u64>(mult);
  return mem::try_store<u32>(
      va, static_cast<u32>(std::min<u64>(scaled, u64(kStatCeiling))));
}

// now + (mult-1)*(now-before), saturated. The guest has already paid the
// base award, so only the remainder is owed.
u32 TopUp(u32 before, u32 now, i32 mult, u32 ceiling) {
  if (mult <= 1 || now <= before)
    return now;
  const u64 extra =
      static_cast<u64>(now - before) * static_cast<u64>(mult - 1);
  return static_cast<u32>(std::min<u64>(static_cast<u64>(now) + extra,
                                        static_cast<u64>(ceiling)));
}

} // namespace

bool CheatDiagEnabled() { return REXCVAR_GET(bd_cheat_diag); }

Cheats &Cheats::Get() {
  static Cheats c;
  return c;
}

// ---- cvar adoption ----

void Cheats::AdoptInvincible() {
  invincible_ = REXCVAR_GET(bd_cheat_invincible);
  toldInvincible_ = false;
}
void Cheats::AdoptInfiniteMP() {
  infiniteMP_ = REXCVAR_GET(bd_cheat_infinite_mp);
  toldInfiniteMP_ = false;
}
void Cheats::AdoptInfiniteGold() {
  infiniteGold_ = REXCVAR_GET(bd_cheat_infinite_gold);
  toldInfiniteGold_ = false;
}
void Cheats::AdoptStatusImmune() {
  statusImmune_ = REXCVAR_GET(bd_cheat_status_immune);
  toldStatusImmune_ = false;
}
void Cheats::AdoptUnlockClasses() {
  unlockClasses_ = REXCVAR_GET(bd_cheat_unlock_classes);
  toldUnlockClasses_ = false;
}
void Cheats::AdoptOneHitKill() {
  oneHitKill_ = REXCVAR_GET(bd_cheat_one_hit_kill);
}
void Cheats::AdoptAttackMult() {
  attackMult_ = std::clamp(REXCVAR_GET(bd_cheat_attack_mult), kMultMin, kMultMax);
}
void Cheats::AdoptMagicAttackMult() {
  magicAttackMult_ =
      std::clamp(REXCVAR_GET(bd_cheat_magic_attack_mult), kMultMin, kMultMax);
}
void Cheats::AdoptDefenceMult() {
  defenceMult_ =
      std::clamp(REXCVAR_GET(bd_cheat_defence_mult), kMultMin, kMultMax);
}
void Cheats::AdoptMagicDefenceMult() {
  magicDefenceMult_ =
      std::clamp(REXCVAR_GET(bd_cheat_magic_defence_mult), kMultMin, kMultMax);
}
void Cheats::AdoptAgilityMult() {
  agilityMult_ =
      std::clamp(REXCVAR_GET(bd_cheat_agility_mult), kMultMin, kMultMax);
}
void Cheats::AdoptStatBonus() {
  statBonus_ = std::clamp(REXCVAR_GET(bd_cheat_stat_bonus), 0, kStatBonusMax);
  toldStatBonus_ = false;
}
void Cheats::AdoptExpMult() {
  expMult_ = std::clamp(REXCVAR_GET(bd_cheat_exp_mult), kMultMin, kMultMax);
}
void Cheats::AdoptSpMult() {
  spMult_ = std::clamp(REXCVAR_GET(bd_cheat_sp_mult), kMultMin, kMultMax);
}
void Cheats::AdoptMedalsMult() {
  medalsMult_ =
      std::clamp(REXCVAR_GET(bd_cheat_medals_mult), kMultMin, kMultMax);
}
void Cheats::AdoptInfiniteMedals() {
  infiniteMedals_ = REXCVAR_GET(bd_cheat_infinite_medals);
  toldInfiniteMedals_ = false;
}
void Cheats::AdoptInfiniteItems() {
  infiniteItems_ = REXCVAR_GET(bd_cheat_infinite_items);
  toldInfiniteItems_ = false;
}
void Cheats::AdoptGiveAllItems() {
  giveAllItems_ = REXCVAR_GET(bd_cheat_give_all_items);
  // Armed here, run from Apply: the setting can change at the title, long
  // before there is any save data to write into.
  if (giveAllItems_)
    pendingGiveAll_ = true;
}
void Cheats::AdoptGrants() {
  // Armed here, run from Apply, for the same reason GiveAllItems is: the row
  // can be set at the title with no save data to write into yet.
  for (u32 i = 0; i < std::size(kGrantEntries); ++i) {
    bool on = false;
    if (rex::cvar::GetFlagByName(kGrantEntries[i].cvar) == "true")
      on = true;
    if (on)
      pendingGrants_ |= (1u << i);
  }
}

bool Cheats::Grant(ItemCategory c) const {
  for (const auto &e : kGrantEntries)
    if (e.cat == c)
      return rex::cvar::GetFlagByName(e.cvar) == "true";
  return false;
}

bool Cheats::SetGrant(ItemCategory c, bool v) {
  for (const auto &e : kGrantEntries)
    if (e.cat == c)
      return rex::cvar::SetFlagByName(e.cvar, FormatCvar(v));
  return false;
}

void Cheats::AdoptUnlockAchievements() {
  unlockAchievements_ = REXCVAR_GET(bd_cheat_unlock_achievements);
  if (unlockAchievements_)
    pendingAchievements_ = true;
}

bool Cheats::SetUnlockAchievements(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_unlock_achievements",
                                  FormatCvar(v));
}

void Cheats::AdoptResetAchievements() {
  resetAchievements_ = REXCVAR_GET(bd_cheat_reset_achievements);
  if (resetAchievements_)
    pendingResetAchv_ = true;
}

bool Cheats::SetResetAchievements(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_reset_achievements", FormatCvar(v));
}

void Cheats::AdoptGoldMult() {
  goldMult_ = std::clamp(REXCVAR_GET(bd_cheat_gold_mult), kMultMin, kMultMax);
}

bool Cheats::SetInvincible(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_invincible", FormatCvar(v));
}
bool Cheats::SetInfiniteMP(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_infinite_mp", FormatCvar(v));
}
bool Cheats::SetInfiniteGold(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_infinite_gold", FormatCvar(v));
}
bool Cheats::SetStatusImmune(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_status_immune", FormatCvar(v));
}
bool Cheats::SetUnlockClasses(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_unlock_classes", FormatCvar(v));
}
bool Cheats::SetOneHitKill(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_one_hit_kill", FormatCvar(v));
}
bool Cheats::SetAttackMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_attack_mult", FormatCvar(v));
}
bool Cheats::SetMagicAttackMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_magic_attack_mult", FormatCvar(v));
}
bool Cheats::SetDefenceMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_defence_mult", FormatCvar(v));
}
bool Cheats::SetMagicDefenceMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_magic_defence_mult", FormatCvar(v));
}
bool Cheats::SetAgilityMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_agility_mult", FormatCvar(v));
}
bool Cheats::SetStatBonus(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_stat_bonus", FormatCvar(v));
}
bool Cheats::SetExpMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_exp_mult", FormatCvar(v));
}
bool Cheats::SetSpMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_sp_mult", FormatCvar(v));
}
bool Cheats::SetGoldMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_gold_mult", FormatCvar(v));
}
bool Cheats::SetMedalsMult(i32 v) {
  return rex::cvar::SetFlagByName("bd_cheat_medals_mult", FormatCvar(v));
}
bool Cheats::SetInfiniteMedals(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_infinite_medals", FormatCvar(v));
}
bool Cheats::SetInfiniteItems(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_infinite_items", FormatCvar(v));
}
bool Cheats::SetGiveAllItems(bool v) {
  return rex::cvar::SetFlagByName("bd_cheat_give_all_items", FormatCvar(v));
}

void Cheats::AdoptCvars() {
  AdoptInvincible();
  AdoptInfiniteMP();
  AdoptInfiniteGold();
  AdoptStatusImmune();
  AdoptUnlockClasses();
  AdoptOneHitKill();
  AdoptAttackMult();
  AdoptMagicAttackMult();
  AdoptDefenceMult();
  AdoptMagicDefenceMult();
  AdoptAgilityMult();
  AdoptStatBonus();
  AdoptExpMult();
  AdoptSpMult();
  AdoptGoldMult();
  AdoptMedalsMult();
  AdoptInfiniteMedals();
  AdoptInfiniteItems();
  AdoptGiveAllItems();
  AdoptGrants();
  AdoptUnlockAchievements();
  AdoptResetAchievements();
}

void Cheats::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Cheats::*adopt)()) {
    rex::cvar::RegisterChangeCallback(
        name, [adopt](std::string_view, std::string_view) {
          (Cheats::Get().*adopt)();
        });
  };
  reg("bd_cheat_invincible", &Cheats::AdoptInvincible);
  reg("bd_cheat_infinite_mp", &Cheats::AdoptInfiniteMP);
  reg("bd_cheat_infinite_gold", &Cheats::AdoptInfiniteGold);
  reg("bd_cheat_status_immune", &Cheats::AdoptStatusImmune);
  reg("bd_cheat_unlock_classes", &Cheats::AdoptUnlockClasses);
  reg("bd_cheat_one_hit_kill", &Cheats::AdoptOneHitKill);
  reg("bd_cheat_attack_mult", &Cheats::AdoptAttackMult);
  reg("bd_cheat_magic_attack_mult", &Cheats::AdoptMagicAttackMult);
  reg("bd_cheat_defence_mult", &Cheats::AdoptDefenceMult);
  reg("bd_cheat_magic_defence_mult", &Cheats::AdoptMagicDefenceMult);
  reg("bd_cheat_agility_mult", &Cheats::AdoptAgilityMult);
  reg("bd_cheat_stat_bonus", &Cheats::AdoptStatBonus);
  reg("bd_cheat_exp_mult", &Cheats::AdoptExpMult);
  reg("bd_cheat_sp_mult", &Cheats::AdoptSpMult);
  reg("bd_cheat_gold_mult", &Cheats::AdoptGoldMult);
  reg("bd_cheat_medals_mult", &Cheats::AdoptMedalsMult);
  reg("bd_cheat_infinite_medals", &Cheats::AdoptInfiniteMedals);
  reg("bd_cheat_infinite_items", &Cheats::AdoptInfiniteItems);
  reg("bd_cheat_give_all_items", &Cheats::AdoptGiveAllItems);
  reg("bd_cheat_unlock_achievements", &Cheats::AdoptUnlockAchievements);
  reg("bd_cheat_reset_achievements", &Cheats::AdoptResetAchievements);
  reg("bd_cheat_give_heal", &Cheats::AdoptGrants);
  reg("bd_cheat_give_usable", &Cheats::AdoptGrants);
  reg("bd_cheat_give_spellbook", &Cheats::AdoptGrants);
  reg("bd_cheat_give_arm", &Cheats::AdoptGrants);
  reg("bd_cheat_give_finger", &Cheats::AdoptGrants);
  reg("bd_cheat_give_ear", &Cheats::AdoptGrants);
  reg("bd_cheat_give_neck", &Cheats::AdoptGrants);
  reg("bd_cheat_give_chest", &Cheats::AdoptGrants);
  reg("bd_cheat_give_valuable", &Cheats::AdoptGrants);

  // Subscriptions are permanent and callback-scoped, so the handles the bus
  // hands over are not kept: the snapshot holds scalars and node addresses.
  Events::Subscribe<BattleStarted>(
      [](const BattleStarted &) { Cheats::Get().OnBattleStarted(); });
  Events::Subscribe<BattleEnded>(
      [](const BattleEnded &) { Cheats::Get().OnBattleEnded(); });
}

// ---- per-step pins ----

bool Cheats::AnyPin() const {
  return invincible_ || infiniteMP_ || infiniteGold_ || statusImmune_ ||
         unlockClasses_ || oneHitKill_ || infiniteMedals_ || infiniteItems_ ||
         statBonus_ > 0;
}

void Cheats::Apply() {
  const auto &g = Game::Get();
  // A loader slot mid-state is a half-built save: the party walk cannot tell a
  // stale node from a live one there, and every accessor below would be
  // reading whatever the heap last held.
  if (!g.IsReady() || g.IsLoading())
    return;

  // The settle window runs even with every pin off, because a reward
  // multiplier is not a pin and owes its remainder regardless.
  if (settleSteps_ > 0) {
    --settleSteps_;
    if (AwardRewards())
      settleSteps_ = 0;
  }

  // Ahead of the AnyPin gate: this is a one-shot action, not a pin, so it must
  // still run on a frame where every pin happens to be off. Sitting below that
  // early return is why selecting it alone did nothing and left the row On.
  if (pendingAchievements_) {
    pendingAchievements_ = false;
    Achievements::UnlockAll();
    litGrants_ |= (1u << 30);
    litSteps_ = kGrantLitSteps;
  }

  if (pendingResetAchv_) {
    pendingResetAchv_ = false;
    Achievements::LockAll();
    litGrants_ |= (1u << 29);
    litSteps_ = kGrantLitSteps;
  }

  if (pendingGiveAll_) {
    pendingGiveAll_ = false;
    FillInventory(ItemCategory::kNone);
    litGrants_ |= (1u << 31);
    litSteps_ = kGrantLitSteps;
  }

  if (pendingGrants_) {
    const u32 pending = pendingGrants_;
    pendingGrants_ = 0;
    for (u32 i = 0; i < std::size(kGrantEntries); ++i) {
      if (!(pending & (1u << i)))
        continue;
      FillInventory(kGrantEntries[i].cat);
      litGrants_ |= (1u << i);
    }
    litSteps_ = kGrantLitSteps;
  }

  // The disarm the press has been waiting on. Deferred rather than immediate
  // so the button reads as pressed for a beat; still automatic, because a row
  // left on would fire again from the profile on the next launch and overwrite
  // the bag every time the game starts.
  if (litSteps_ > 0 && --litSteps_ == 0 && litGrants_) {
    for (u32 i = 0; i < std::size(kGrantEntries); ++i)
      if (litGrants_ & (1u << i))
        SetGrant(kGrantEntries[i].cat, false);
    if (litGrants_ & (1u << 31))
      SetGiveAllItems(false);
    if (litGrants_ & (1u << 30))
      SetUnlockAchievements(false);
    if (litGrants_ & (1u << 29))
      SetResetAchievements(false);
    litGrants_ = 0;
  }

  if (!AnyPin())
    return;

  if (invincible_ || infiniteMP_ || statusImmune_) {
    auto party = g.Party();
    if (party) {
      const size_t n = party.Size();
      for (size_t i = 0; i < n; ++i) {
        auto c = party.At(i);
        // Reviving is the game's own flow. Topping a knocked-out member's HP
        // up from here leaves the KO status bit set behind it, and the two
        // disagreeing is worse than the member staying down.
        if (!c || !c.IsAlive())
          continue;
        if (invincible_ && c.HP() < c.MaxHP()) {
          const u32 from = c.HP();
          if (c.SetHP(c.MaxHP()) && !toldInvincible_) {
            toldInvincible_ = true;
            BD_CHEAT_DIAG("[cheat-diag] invincible: slot {} HP {} -> {}", c.SlotId(),
                    from, c.MaxHP());
          }
        }
        if (infiniteMP_ && c.MP() < c.MaxMP()) {
          const u32 from = c.MP();
          if (c.SetMP(c.MaxMP()) && !toldInfiniteMP_) {
            toldInfiniteMP_ = true;
            BD_CHEAT_DIAG("[cheat-diag] infinite_mp: slot {} MP {} -> {}", c.SlotId(),
                    from, c.MaxMP());
          }
        }
        if (statusImmune_) {
          for (u32 r = 0; r < kCharaResistCount; ++r)
            c.SetStatusResist(static_cast<CharaResist>(r), kResistImmune);
          // Resistance stops the next application; it does nothing about one
          // already on the books, and the two turn counters tick under their
          // own status bits rather than under the flag word.
          if (c.StatusFlags() != 0) {
            const u32 had = c.StatusFlags();
            c.SetStatusFlags(0);
            if (!toldStatusImmune_) {
              toldStatusImmune_ = true;
              BD_CHEAT_DIAG("[cheat-diag] status_immune: slot {} cleared 0x{:X}",
                      c.SlotId(), had);
            }
          }
          if (c.ParalyzeTurns() != 0)
            c.SetParalyzeTurns(0);
          if (c.StunTurns() != 0)
            c.SetStunTurns(0);
        }
      }
    }
  }

  if (unlockClasses_ || statBonus_ > 0) {
    auto roster = g.Roster();
    if (roster) {
      const size_t n = roster.Size();
      const u32 bonus = static_cast<u32>(statBonus_);
      for (size_t i = 0; i < n; ++i) {
        auto c = roster.At(i);
        if (!c)
          continue;
        if (unlockClasses_ && c.UnlockedClasses() != kAllClasses) {
          const u32 had = c.UnlockedClasses();
          if (c.SetUnlockedClasses(kAllClasses) && !toldUnlockClasses_) {
            toldUnlockClasses_ = true;
            BD_CHEAT_DIAG("[cheat-diag] unlock_classes: slot {} 0x{:X} -> 0x{:X}",
                    c.SlotId(), had, kAllClasses);
          }
        }
        if (statBonus_ > 0) {
          for (u32 b = 0; b < kPermanentBonusCount; ++b) {
            const auto which = static_cast<PermanentBonus>(b);
            if (c.StatBonus(which) < bonus) {
              const u32 had = c.StatBonus(which);
              if (c.SetStatBonus(which, bonus) && !toldStatBonus_) {
                toldStatBonus_ = true;
                BD_CHEAT_DIAG("[cheat-diag] stat_bonus: slot {} bonus[{}] {} -> {}",
                        c.SlotId(), b, had, bonus);
              }
            }
          }
        }
      }
    }
  }

  if (oneHitKill_) {
    auto b = g.Battle();
    // EnemyAt needs the manager root, which bdBattleSceneUpdate captures once
    // per step and which is only trusted inside the step that produced it.
    if (b.IsActive() && b.HasManager()) {
      const size_t n = b.EnemyCount();
      for (size_t i = 0; i < n; ++i) {
        auto e = b.EnemyAt(i);
        // 1 rather than 0: the guest's own death path is what clears the
        // actor, awards the battle and ends the encounter, so the kill has to
        // come from a hit landing rather than from this write.
        if (e && e.IsAlive() && e.HP() > 1)
          e.SetHP(1);
      }
    }
  }

  if (infiniteItems_) {
    auto inv = g.Inventory();
    if (inv) {
      // Topped up every step rather than restored on use, so a stack never
      // reaches zero in the first place: the guest clears a slot's id the
      // moment its count runs out, and a cleared slot has nothing left to
      // identify the item by.
      const size_t slots = inv.SlotCount();
      for (size_t s = 0; s < slots; ++s) {
        const auto it = inv.At(s);
        if (it.id == 0 || it.count >= kItemStackMax)
          continue;
        const u32 had = it.count;
        if (inv.SetAt(s, it.id, kItemStackMax) && !toldInfiniteItems_) {
          toldInfiniteItems_ = true;
          BD_CHEAT_DIAG("[cheat-diag] infinite_items: slot {} item {} {} -> {}", s,
                  it.id, had, kItemStackMax);
        }
      }
    }
  }

  if (infiniteMedals_) {
    auto inv = g.Inventory();
    if (inv && inv.Medals() < Inventory::kMedalsMax) {
      const u32 from = inv.Medals();
      if (inv.SetMedals(Inventory::kMedalsMax) && !toldInfiniteMedals_) {
        toldInfiniteMedals_ = true;
        BD_CHEAT_DIAG("[cheat-diag] infinite_medals: {} -> {}", from,
                Inventory::kMedalsMax);
      }
    }
  }

  if (infiniteGold_) {
    auto inv = g.Inventory();
    if (inv && inv.Gold() < kGoldMax) {
      const u32 from = inv.Gold();
      if (inv.SetGold(kGoldMax) && !toldInfiniteGold_) {
        toldInfiniteGold_ = true;
        BD_CHEAT_DIAG("[cheat-diag] infinite_gold: {} -> {}", from, kGoldMax);
      }
    }
  }
}

// ---- derived stats ----

// Walks the id space rather than a list: the record table is the only account
// of which ids exist, and ItemRecord answers zero for the gaps. Stops at the
// last slot, so a table larger than the inventory fills it and no more.
void Cheats::FillInventory(ItemCategory only) {
  auto inv = Game::Get().Inventory();
  if (!inv)
    return;
  const size_t slots = inv.SlotCount();
  auto &tables = GameTables::Get();

  // Adds rather than replaces. Writing from slot 0 meant a second grant sat on
  // top of the first, which is not what a row that says "+1" should do to a
  // bag you already filled.
  std::vector<bool> present(kItemCategoryMaxId + 2, false);
  std::vector<size_t> freeSlots;
  for (size_t s = 0; s < slots; ++s) {
    const auto held = inv.At(s);
    if (held.id == 0)
      freeSlots.push_back(s);
    else if (held.id <= kItemCategoryMaxId)
      present[held.id] = true;
  }

  size_t next = 0;
  u32 added = 0;
  u32 had = 0;
  bool full = false;
  for (u32 id = 1; id <= kMaxItemId; ++id) {
    if (!tables.PhenomeRecord(id))
      continue;
    const ItemCategory cat = CategoryOf(id);
    // kNone covers both ids the sheet never classified and the ones it marks
    // abolished, which are the entries that read "Worthless Junk" in game.
    if (cat == ItemCategory::kNone)
      continue;
    if (only != ItemCategory::kNone && cat != only)
      continue;
    if (id <= kItemCategoryMaxId && present[id]) {
      ++had;
      continue;
    }
    if (next >= freeSlots.size()) {
      full = true;
      break;
    }
    // One each, not a full stack: the row says "+1" and should mean it.
    // Infinite Items is the separate switch for holding stacks at 99.
    if (inv.SetAt(freeSlots[next], id, 1)) {
      ++next;
      ++added;
    }
  }

  BD_CHEAT_DIAG("[cheat-diag] give_items(cat={}): +{} new, {} already held, {} free "
          "slot(s) left{}",
          static_cast<u32>(only), added, had,
          freeSlots.size() - next, full ? " [FULL: no free slots]" : "");
}

void Cheats::ScaleBattleParams(u32 paramsEA) {
  if (!paramsEA)
    return;
  if (attackMult_ > 1)
    ScaleStat(paramsEA, offsetof(CharaBattleParams_t, attack), attackMult_);
  if (magicAttackMult_ > 1)
    ScaleStat(paramsEA, offsetof(CharaBattleParams_t, magicAttack),
              magicAttackMult_);
  if (defenceMult_ > 1)
    ScaleStat(paramsEA, offsetof(CharaBattleParams_t, defense), defenceMult_);
  if (magicDefenceMult_ > 1)
    ScaleStat(paramsEA, offsetof(CharaBattleParams_t, magicDefense),
              magicDefenceMult_);
  if (agilityMult_ > 1)
    ScaleStat(paramsEA, offsetof(CharaBattleParams_t, agility), agilityMult_);
}

// ---- per-fight rewards ----

void Cheats::OnBattleStarted() {
  armed_ = false;
  settleSteps_ = 0;
  preBattle_.clear();

  if (expMult_ <= 1 && spMult_ <= 1 && goldMult_ <= 1 && medalsMult_ <= 1)
    return;

  const auto &g = Game::Get();
  if (!g.IsReady())
    return;

  auto roster = g.Roster();
  if (roster) {
    const size_t n = roster.Size();
    preBattle_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      auto c = roster.At(i);
      if (!c)
        continue;
      MemberSnapshot m;
      m.nodeEA = c.Address();
      m.exp = c.Exp();
      // Every class, not just the active one: accessories in this game can
      // spread SP across classes, and a per-class diff catches that for free.
      for (u32 k = 0; k < kCharaClassCount; ++k)
        m.sp[k] = c.ClassSP(static_cast<CharaClass>(k));
      preBattle_.push_back(m);
    }
  }
  preGold_ = g.Inventory().Gold();
  preMedals_ = g.Inventory().Medals();
  armed_ = true;
}

void Cheats::OnBattleEnded() {
  if (!armed_)
    return;
  // The award usually lands before this edge, which publishes from the battle
  // camera destructor. When it has not, the settle window in Apply() retries.
  if (!AwardRewards())
    settleSteps_ = kSettleSteps;
}

bool Cheats::AwardRewards() {
  if (!armed_)
    return false;

  const auto &g = Game::Get();
  if (!g.IsReady() || g.IsLoading())
    return false;

  bool paid = false;

  if (expMult_ > 1 || spMult_ > 1) {
    auto roster = g.Roster();
    if (roster) {
      const size_t n = roster.Size();
      for (size_t i = 0; i < n; ++i) {
        auto c = roster.At(i);
        if (!c)
          continue;
        const u32 ea = c.Address();
        // Match on the node rather than the index: a battle can add a member,
        // which would shift every position after it.
        const auto it =
            std::find_if(preBattle_.begin(), preBattle_.end(),
                         [ea](const MemberSnapshot &m) { return m.nodeEA == ea; });
        if (it == preBattle_.end())
          continue;

        if (expMult_ > 1) {
          const u32 now = c.Exp();
          // Only the remainder: the guest has already paid the base award.
          const u64 bonus64 =
              now > it->exp
                  ? static_cast<u64>(now - it->exp) * u64(expMult_ - 1)
                  : 0ull;
          const u32 bonus =
              static_cast<u32>(std::min<u64>(bonus64, u64(kProgressMax)));
          if (bonus > 0) {
            // Handles are node-relative; the guest wants the chara itself.
            const u32 chara = c.Address() + kNodeChara;
            const u32 before = now;
            u32 ret = 0;
            {
              rex::ppc::stack_guard guard;
              ret = GuestAddExp(chara, bonus);
            }
            const u32 after = c.Exp();
            BD_CHEAT_DIAG("[cheat-diag] exp slot {} chara=0x{:08X} snap={} now={} "
                    "bonus={} -> after={} ret={}",
                    c.SlotId(), chara, it->exp, before, bonus, after, ret);
            // Re-read rather than assume: the call may have levelled, and the
            // snapshot has to move with it or the settle window pays twice.
            it->exp = after;
            paid = true;
          }
        }
        if (spMult_ > 1) {
          for (u32 k = 0; k < kCharaClassCount; ++k) {
            const auto cls = static_cast<CharaClass>(k);
            const u32 now = c.ClassSP(cls);
            const u32 want = TopUp(it->sp[k], now, spMult_, kProgressMax);
            if (want != now && c.SetClassSP(cls, want)) {
              BD_CHEAT_DIAG("[cheat-diag] sp slot {} class {} snap={} now={} -> "
                      "after={}",
                      c.SlotId(), k, it->sp[k], now, want);
              it->sp[k] = want;
              paid = true;
            }
          }
        }
      }
    }
  }

  if (medalsMult_ > 1) {
    auto inv = g.Inventory();
    if (inv) {
      const u32 now = inv.Medals();
      const u32 want =
          TopUp(preMedals_, now, medalsMult_, Inventory::kMedalsMax);
      if (want != now && inv.SetMedals(want)) {
        BD_CHEAT_DIAG("[cheat-diag] medals snap={} now={} -> after={}", preMedals_,
                now, want);
        preMedals_ = want;
        paid = true;
      }
    }
  }

  if (goldMult_ > 1) {
    auto inv = g.Inventory();
    if (inv) {
      const u32 now = inv.Gold();
      const u32 want = TopUp(preGold_, now, goldMult_, kGoldMax);
      if (want != now && inv.SetGold(want)) {
        BD_CHEAT_DIAG("[cheat-diag] gold snap={} now={} -> after={}", preGold_, now,
                want);
        preGold_ = want;
        paid = true;
      }
    }
  }

  if (paid)
    armed_ = false;
  return paid;
}

} // namespace bd::engine
