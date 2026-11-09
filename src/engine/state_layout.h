/**
 * @file    engine/state_layout.h
 * @brief   The fixed addresses every engine handle starts from. A root only
 *          one file reads is declared in that file.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/kernel/memory_view.h>
#include <rex/types.h>

namespace bd::engine {

// Root globals (fixed VAs): the only addresses the readers start from.
// Everything past them is a chained dereference.
//
// The ones with no reader yet are kept because the addresses cost real work to
// find, not because anything walks them.
namespace addr {
REX_GUEST_GLOBAL(be_u32, kGameTask, 0x82DC97B0); // root field task
REX_GUEST_GLOBAL(be_u32, kScriptManTask,
                 0x82DC98E4); // field scene root (nulled by dtor)
REX_GUEST_GLOBAL(be_u32, kLoaderTask, 0x82DC97F4); // asset-slot manager
REX_GUEST_GLOBAL(be_u32, kSequenceControl,
                 0x827A7EC0); // top-level module dispatcher
REX_GUEST_GLOBAL(be_u32, kMindowsActivePanel,
                 0x827A7D68); // Mindows panel, NOT the camp menu
REX_GUEST_GLOBAL(be_u32, kMindowsHidden,
                 0x827A7D6C);              // 0 = overlay drawn, 1 = suppressed
inline constexpr u32 kConfig = 0x82DEC270; // bd::Config, a fixed object
REX_GUEST_GLOBAL(be_u32, kItemSaveData,
                 0x82DC9A7C); // -> inventory[512] + gold + flags
REX_GUEST_GLOBAL(be_u32, kFieldPlayerEntity,
                 0x82DC9B3C); // +124 active head, +120 roster head
REX_GUEST_GLOBAL(
    be_u32, kPlayRecord,
    0x82DC9AE0); // -> the 0xB48 play record, outliving any one battle
REX_GUEST_GLOBAL(
    be_u32, kVisualRender,
    0x82DC9848); // Visual::Render, which owns the world to screen projection
REX_GUEST_GLOBAL(be_u32, kBattleCameraCtl,
                 0x82DC999C); // BattleCameraTask (0xE0) => battle active
inline constexpr u32 kLanguageAvailable =
    0x827756F0; // u8[10] by locale id, bd_boot.ini [Language]
REX_GUEST_GLOBAL(be_u32, kBootDefaultLocale,
                 0x82775710); // bd_boot.ini [DefaultLanguage]
inline constexpr u32 kVoiceLanguages =
    0x82775714; // i32[] of locale ids, bd_boot.ini [Voice]
REX_GUEST_GLOBAL(be_i32, kVoiceLanguageCount, 0x82775794);
inline constexpr u32 kShadowLightView = 0x82DD6144;
inline constexpr u32 kRenderView = 0x82DE87E0;
inline constexpr u32 kShaderEye = 0x82DE8770;
inline constexpr u32 kCubeShadowLightView = 0x82776DA8;
inline constexpr u32 kProjectorMapInfos = 0x82DD6100;
inline constexpr u32 kProjectorMapInfosEnd = 0x82DD7170;
inline constexpr u32 kWaterBottomLightView = 0x82776F34;
inline constexpr u32 kReflectSlots = 0x82DD7170;
inline constexpr u32 kReflectSlotsEnd = 0x82DD7D78;
REX_GUEST_GLOBAL(be_u32, kCameraRenderVO, 0x82DBA92C);
REX_GUEST_GLOBAL(be_u32, kCameraViewList, 0x82DC9854);
REX_GUEST_GLOBAL(be_u32, kParticleModelPool, 0x82DC9BB0);
REX_GUEST_GLOBAL(be_u32, kParticleModelPoolCount, 0x82DC9BB8);
inline constexpr u32 kGameRequest =
    0x82DC97B8; // new-game/continue params (0x5484)
REX_GUEST_GLOBAL(be_u32, kCurrentAreaObject,
                 0x82DC9850); // mirror of GameTask+0xAC
REX_GUEST_GLOBAL(be_u32, kSaveDataTask,
                 0x82DC9B08); // save-flow UI task (NOT the stat store)
inline constexpr u32 kGrowthExpTable = 0x82DC9B44;  // [0] = table max level
REX_GUEST_GLOBAL(be_u32, kLevelUpFlag, 0x82DC9A94); // non-zero: leader leveled
inline constexpr u32 kBattleAICtl = 0x82DC99A0;     // AI/summon ctl (0x578)
REX_GUEST_GLOBAL(be_u32, kPreRestartTask,
                 0x82DC9A6C); // non-null once a party wipe restarts the game
inline constexpr u32 kBarrierFlagList =
    0x82DC4108; // bdFlagListLoad's elemental barrier flags and their colors
inline constexpr u32 kChestFlagList = 0x82DC4114; // treasure chest flags
} // namespace addr

} // namespace bd::engine
