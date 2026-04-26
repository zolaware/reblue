/**
 * @file        bdengine/global_config.h
 *
 * @brief       Overlay struct for the game's bd::Config singleton at 0x82DEC270.
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

#include <rex/types.h>
#include <rex/system/kernel_state.h>

namespace bd {

// Guest address of the config singleton
inline constexpr u32 kGlobalConfigAddr    = 0x82DEC270;

// Guest address of the Mindows visibility flag (separate from config singleton).
// 0 = show config overlay, 1 = hidden (default).
inline constexpr u32 kMindowsHiddenAddr   = 0x827A7D6C;

// All 11 tool entry bits (Design, StageSelect, BattleViewer, BattleMotion,
// BattleCamera, Vibration, MotCmd, Sound, Achievement, MsgTest1, MsgTest2).
inline constexpr u32 kAllToolEntryBits    = 0x7FF;

// Guest address of the runtime speed multiplier array.
// Index 2 (offset +8) is the battle/scene speed written by bdSceneSpeedUpdate.
inline constexpr u32 kSpeedMultipliersAddr = 0x82DDA878;
inline constexpr u32 kBattleSpeedOffset    = 0x8;  // flt_82DDA880 = array[2]

struct GlobalConfig {
    be_u32 vtable;               // +0x000
    u8 _pad004[0x108];                 // +0x004

    // Debug menu startup flags (from bd_config.ini).
    be_u32 debugMenuBoot;        // +0x10C  [DEBUG_MENU_BOOT]
    be_u32 debugMenuBuild;       // +0x110  [DEBUG_MENU_BUILD]
    be_u32 debugMenuMemory;      // +0x114  [DEBUG_MENU_MEMORY]

    // Sequence holder enables.
    be_u32 mainMenu;             // +0x118  [MAIN_MENU]
    be_u32 userMenu;             // +0x11C  [USER_MENU]
    be_u32 toolMenu;             // +0x120  [TOOL_MENU]
    be_u32 toolEntryBits;        // +0x124  [TOOL_ENTRY] bitfield

    u8 _pad128[0x188];                // +0x128

    // Mindows config overlay.
    be_u32 debugMindows;         // +0x2B0  [DebugMindows], enables Mindows text rendering

    // Debug input toggles, exposed in Mindows under CONFIG/DEBUG/Input.
    // bdGameSettingsInit defaults both to 1; bdDiscContentLoad zeros them when
    // debugMindows == 0. We force them to 0 in ApplyDebugConfig so devmode
    // gets the overlay without the gameplay-altering pad/keyboard debug paths.
    be_u32 debugInputKey;        // +0x2B4  Mindows "Key"
    be_u32 debugInputPad;        // +0x2B8  Mindows "Pad"

    u8 _pad2BC[0x44];                  // +0x2BC

    // Debug rendering.
    be_u32 debugLabels;          // +0x300  g_bDebugLabels
    u8 _pad304[0x8];                   // +0x304
    be_f32    gameSpeed;            // +0x30C  g_fGameSpeed (used in 60fps patch)
};

static_assert(offsetof(GlobalConfig, debugMenuBoot)   == 0x10C);
static_assert(offsetof(GlobalConfig, debugMenuBuild)  == 0x110);
static_assert(offsetof(GlobalConfig, debugMenuMemory) == 0x114);
static_assert(offsetof(GlobalConfig, mainMenu)        == 0x118);
static_assert(offsetof(GlobalConfig, userMenu)        == 0x11C);
static_assert(offsetof(GlobalConfig, toolMenu)        == 0x120);
static_assert(offsetof(GlobalConfig, toolEntryBits)   == 0x124);
static_assert(offsetof(GlobalConfig, debugMindows)    == 0x2B0);
static_assert(offsetof(GlobalConfig, debugInputKey)   == 0x2B4);
static_assert(offsetof(GlobalConfig, debugInputPad)   == 0x2B8);
static_assert(offsetof(GlobalConfig, debugLabels)     == 0x300);
static_assert(offsetof(GlobalConfig, gameSpeed)       == 0x30C);

/**
 * @brief Returns a host pointer to the config singleton, or nullptr if kernel memory
 *        is not yet available. Safe to call before kernel init (returns nullptr).
 */
inline GlobalConfig* GetGlobalConfig() {
    auto* ks = rex::system::kernel_state();
    if (!ks || !ks->memory()) return nullptr;
    return reinterpret_cast<GlobalConfig*>(ks->memory()->virtual_membase() + kGlobalConfigAddr);
}

/**
 * @brief Returns a host pointer to the Mindows hidden flag (be_u32),
 *        or nullptr if kernel memory is not yet available. Safe to call before kernel init.
 */
inline be_u32* GetMindowsHiddenFlag() {
    auto* ks = rex::system::kernel_state();
    if (!ks || !ks->memory()) return nullptr;
    return reinterpret_cast<be_u32*>(ks->memory()->virtual_membase() + kMindowsHiddenAddr);
}

}  // namespace bd
