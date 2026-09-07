/**
 * @file    core/global_config.h
 *
 * @brief       Overlay struct for the game's bd::Config singleton at
 * 0x82DEC270.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "core/memory_helpers.h"

namespace bd {

inline constexpr u32 kGlobalConfigAddr = 0x82DEC270;

// Mindows visibility flag (separate from the config singleton): 0 = overlay
// shown, 1 = hidden.
inline constexpr u32 kMindowsHiddenAddr = 0x827A7D6C;

// All 11 tool entry bits (Design, StageSelect, BattleViewer, BattleMotion,
// BattleCamera, Vibration, MotCmd, Sound, Achievement, MsgTest1, MsgTest2).
inline constexpr u32 kAllToolEntryBits = 0x7FF;

struct GlobalConfig {
  be_u32 vtable;     // +0x000
  u8 _pad004[0x108]; // +0x004

  // Debug menu startup flags (from bd_config.ini).
  be_u32 debugMenuBoot;   // +0x10C  [DEBUG_MENU_BOOT]
  be_u32 debugMenuBuild;  // +0x110  [DEBUG_MENU_BUILD]
  be_u32 debugMenuMemory; // +0x114  [DEBUG_MENU_MEMORY]

  // Sequence holder enables.
  be_u32 mainMenu;      // +0x118  [MAIN_MENU]
  be_u32 userMenu;      // +0x11C  [USER_MENU]
  be_u32 toolMenu;      // +0x120  [TOOL_MENU]
  be_u32 toolEntryBits; // +0x124  [TOOL_ENTRY] bitfield

  u8 _pad128[0x28]; // +0x128

  be_u32 camRollInv; // +0x150  [CamRollInv]
  be_f32 camRollSpd; // +0x154  [CamRollSpd]

  u8 _pad158[0x14]; // +0x158

  // HDD content pack cache toggle. Gates loading packs from the mounted utility
  // drive in the async pack request pump (read at 0x82128BBC).
  // bdDiscContentLoad leaves it nonzero only when XMountUtilityDrive succeeds.
  // Write 0 to force off.
  be_u32 hddCache; // +0x16C  [HDD_Cache]

  u8 _pad170[0x140]; // +0x170

  // Mindows config overlay.
  be_u32 debugMindows; // +0x2B0  [DebugMindows], enables Mindows text rendering

  // Debug input toggles, exposed in Mindows under CONFIG/DEBUG/Input.
  // bdGameSettingsInit defaults both to 1. bdDiscContentLoad zeros them when
  // debugMindows == 0. ApplyDebugConfig forces them to 0 so devmode gets the
  // overlay without the gameplay-altering pad/keyboard debug paths.
  be_u32 debugInputKey; // +0x2B4  Mindows "Key"
  be_u32 debugInputPad; // +0x2B8  Mindows "Pad"

  u8 _pad2BC[0x44]; // +0x2BC

  // Debug rendering.
  be_u32 debugLabels; // +0x300  g_bDebugLabels
  u8 _pad304[0x8];    // +0x304
  be_f32 gameSpeed;   // +0x30C  g_fGameSpeed (used in 60fps patch)
};

static_assert(offsetof(GlobalConfig, debugMenuBoot) == 0x10C);
static_assert(offsetof(GlobalConfig, debugMenuBuild) == 0x110);
static_assert(offsetof(GlobalConfig, debugMenuMemory) == 0x114);
static_assert(offsetof(GlobalConfig, mainMenu) == 0x118);
static_assert(offsetof(GlobalConfig, userMenu) == 0x11C);
static_assert(offsetof(GlobalConfig, toolMenu) == 0x120);
static_assert(offsetof(GlobalConfig, toolEntryBits) == 0x124);
static_assert(offsetof(GlobalConfig, camRollInv) == 0x150);
static_assert(offsetof(GlobalConfig, camRollSpd) == 0x154);
static_assert(offsetof(GlobalConfig, hddCache) == 0x16C);
static_assert(offsetof(GlobalConfig, debugMindows) == 0x2B0);
static_assert(offsetof(GlobalConfig, debugInputKey) == 0x2B4);
static_assert(offsetof(GlobalConfig, debugInputPad) == 0x2B8);
static_assert(offsetof(GlobalConfig, debugLabels) == 0x300);
static_assert(offsetof(GlobalConfig, gameSpeed) == 0x30C);

// Host pointer to the config singleton, or nullptr before kernel memory exists.
inline GlobalConfig *GetGlobalConfig() {
  return mem::try_at<GlobalConfig>(kGlobalConfigAddr);
}

// Host pointer to the Mindows hidden flag, or nullptr before kernel memory
// exists. Checked, since the keyboard listener reads it from the UI thread.
inline be_u32 *GetMindowsHiddenFlag() {
  return mem::try_at<be_u32>(kMindowsHiddenAddr);
}

} // namespace bd
