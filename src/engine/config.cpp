/**
 * @file    engine/config.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/config.h"

#include <cstddef>

#include "engine/state_layout.h"

namespace bd::engine {

namespace {

struct Config_t {
  /* 0x000 */ be_u32 vtable;
  /* 0x004 */ u8 _pad004[0x108];

  /* 0x10C */ be_u32 debugMenuBoot;
  /* 0x110 */ be_u32 debugMenuBuild;
  /* 0x114 */ be_u32 debugMenuMemory;

  /* 0x118 */ be_u32 mainMenu;
  /* 0x11C */ be_u32 userMenu;
  /* 0x120 */ be_u32 toolMenu;
  /* 0x124 */ be_u32 toolEntryBits;

  /* 0x128 */ u8 _pad128[0x28];

  /* 0x150 */ be_u32 camRollInv;
  /* 0x154 */ be_f32 camRollSpd;

  /* 0x158 */ u8 _pad158[0x0C];

  // Selects both the alternate formula in Player_CalcBattleParams and the _new
  // variants of every shipped data table.
  /* 0x164 */ be_u32 newTables;

  /* 0x168 */ u8 _pad168[0x04];

  // HDD content pack cache toggle. Gates loading packs from the mounted utility
  // drive in the async pack request pump (read at 0x82128BBC).
  // bdDiscContentLoad leaves it nonzero only when XMountUtilityDrive succeeds.
  // Write 0 to force off.
  /* 0x16C */ be_u32 hddCache;

  /* 0x170 */ u8 _pad170[0x08];

  // bdGetCurrentMapPath substitutes the default control row when this is set
  // and the player is on control type A.
  /* 0x178 */ be_u32 altMap;

  /* 0x17C */ u8 _pad17C[0x2B0 - 0x17C];

  /* 0x2B0 */ be_u32 debugMindows; // enables Mindows text rendering

  // Debug input toggles, exposed in Mindows under CONFIG/DEBUG/Input.
  // bdGameSettingsInit defaults both to 1. bdDiscContentLoad zeros them when
  // debugMindows == 0. ApplyDebugConfig forces them to 0 so devmode gets the
  // overlay without the gameplay-altering pad/keyboard debug paths.
  /* 0x2B4 */ be_u32 debugInputKey; // Mindows "Key"
  /* 0x2B8 */ be_u32 debugInputPad; // Mindows "Pad"

  /* 0x2BC */ u8 _pad2BC[0x44];

  /* 0x300 */ be_u32 debugLabels; // g_bDebugLabels
  /* 0x304 */ u8 _pad304[0x8];
  /* 0x30C */ be_f32 gameSpeed; // g_fGameSpeed
};

static_assert(offsetof(Config_t, debugMenuBoot) == 0x10C);
static_assert(offsetof(Config_t, debugMenuBuild) == 0x110);
static_assert(offsetof(Config_t, debugMenuMemory) == 0x114);
static_assert(offsetof(Config_t, mainMenu) == 0x118);
static_assert(offsetof(Config_t, userMenu) == 0x11C);
static_assert(offsetof(Config_t, toolMenu) == 0x120);
static_assert(offsetof(Config_t, toolEntryBits) == 0x124);
static_assert(offsetof(Config_t, camRollInv) == 0x150);
static_assert(offsetof(Config_t, camRollSpd) == 0x154);
static_assert(offsetof(Config_t, newTables) == 0x164);
static_assert(offsetof(Config_t, hddCache) == 0x16C);
static_assert(offsetof(Config_t, altMap) == 0x178);
static_assert(offsetof(Config_t, debugMindows) == 0x2B0);
static_assert(offsetof(Config_t, debugInputKey) == 0x2B4);
static_assert(offsetof(Config_t, debugInputPad) == 0x2B8);
static_assert(offsetof(Config_t, debugLabels) == 0x300);
static_assert(offsetof(Config_t, gameSpeed) == 0x30C);

} // namespace

Config Config::Get() { return Config(addr::kConfig); }

u32 Config::NewTables() const {
  const auto *self = Self<Config_t>();
  return self ? static_cast<u32>(self->newTables) : 0;
}

bool Config::CamRollInv() const {
  const auto *self = Self<Config_t>();
  return self && static_cast<u32>(self->camRollInv) != 0;
}

bool Config::SetHddCache(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->hddCache = v;
  return true;
}

bool Config::SetDebugInputKey(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugInputKey = v;
  return true;
}

bool Config::SetDebugInputPad(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugInputPad = v;
  return true;
}

bool Config::SetDebugMenuBoot(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugMenuBoot = v;
  return true;
}

bool Config::SetDebugMenuBuild(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugMenuBuild = v;
  return true;
}

bool Config::SetDebugMenuMemory(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugMenuMemory = v;
  return true;
}

bool Config::SetDebugLabels(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugLabels = v;
  return true;
}

bool Config::SetMainMenu(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->mainMenu = v;
  return true;
}

bool Config::SetUserMenu(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->userMenu = v;
  return true;
}

bool Config::SetToolMenu(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->toolMenu = v;
  return true;
}

bool Config::SetToolEntryBits(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->toolEntryBits = v;
  return true;
}

bool Config::SetDebugMindows(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->debugMindows = v;
  return true;
}

bool Config::SetCamRollInv(u32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->camRollInv = v;
  return true;
}

bool Config::SetCamRollSpd(f32 v) {
  auto *self = Self<Config_t>();
  if (!self)
    return false;
  self->camRollSpd = v;
  return true;
}

} // namespace bd::engine
