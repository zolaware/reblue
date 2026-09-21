/**
 * @file    engine/guest_debug.cpp
 * @brief   Engine debug tooling: devmode config overlay (Mindows), keyboard
 *          bridge, sound trigger draw teardown guard.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "audio/audio.h"
#include "core/logging.h"
#include "core/settings.h"
#include "engine/engine.h"
#include "engine/settings.h"
#include "platform/platform.h"

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/types.h>

namespace bd::engine {
namespace {

// All 11 tool entry bits (Design, StageSelect, BattleViewer, BattleMotion,
// BattleCamera, Vibration, MotCmd, Sound, Achievement, MsgTest1, MsgTest2).
constexpr u32 kAllToolEntryBits = 0x7FF;

void ApplyDebugConfig() {
  Config cfg = Config::Get();
  if (!cfg)
    return;

  cfg.SetHddCache(0u);

  // The engine zeroes these only when debugMindows==0, which the overlay sets.
  cfg.SetDebugInputKey(0u);
  cfg.SetDebugInputPad(0u);

  const bool dev = bd::Settings::Get().Devmode();
  const u32 v = dev ? 1u : 0u;

  cfg.SetDebugMenuBoot(v);
  cfg.SetDebugMenuBuild(v);
  cfg.SetDebugMenuMemory(v);
  cfg.SetDebugLabels(v);
  cfg.SetMainMenu(v);
  cfg.SetUserMenu(v);
  cfg.SetToolMenu(v);
  cfg.SetToolEntryBits(dev ? kAllToolEntryBits : 0u);

  cfg.SetDebugMindows(v);
  Game::Get().SetMindowsHidden(!dev);
}

} // namespace

void Game::ToggleMindows() {
  if (!bd::Settings::Get().Devmode())
    return;

  const bool hidden = MindowsHidden();
  SetMindowsHidden(!hidden);
  BD_INFO("Mindows overlay {}", hidden ? "shown" : "hidden");
}

} // namespace bd::engine

// Fires after bdGameSettingsInit writes defaults.
void bdPostConfigInitHook() {
  bd::Settings::Get().SetDevmodeApplier(bd::engine::ApplyDebugConfig);
  bd::engine::Settings::Get().ApplyCameraSpeed();
  BD_INFO("engine debug config applied (devmode={})",
          bd::Settings::Get().Devmode());
}

// The engine key buffer only feeds debug systems, so the poll is skipped outside
// devmode to keep stray keystrokes out of the engine. Ctrl+Alt+M toggles the
// Mindows overlay, and while it is hidden the bridge forwards nothing.
void bdKeyboardPollHook() {
  bd::audio::ApplyAudioDebugPokes();
  if (!bd::Settings::Get().Devmode())
    return;
  if (bd::platform::PollMindowsHotkey())
    bd::engine::Game::Get().ToggleMindows();
  bd::platform::PollKeyboardToGuest();
}

// MapManTask's sound trigger debug draw walks trigger polygon lists borrowed
// from the field scene with no liveness guard of its own, so it takes the same
// guard the matching update has.
REX_EXTERN(__imp__MapManTask__DebugDrawTriggers);
REX_HOOK_RAW(MapManTask__DebugDrawTriggers) {
  const auto &game = bd::engine::Game::Get();
  if (!game.ScriptManTask() || !game.FieldPlayerEntity())
    return;
  __imp__MapManTask__DebugDrawTriggers(ctx, base);
}
