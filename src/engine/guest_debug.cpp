/**
 * @file    engine/guest_debug.cpp
 * @brief   Guest debug tooling: devmode config overlay (Mindows), keyboard
 *          bridge, sound trigger draw teardown guard.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "audio/audio.h"
#include "core/global_config.h"
#include "core/logging.h"
#include "core/memory_helpers.h"
#include "core/settings.h"
#include "engine/engine.h"
#include "engine/settings.h"
#include "platform/platform.h"

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/types.h>

namespace bd::engine {
namespace {

void ApplyDebugConfig() {
  auto *cfg = GetGlobalConfig();
  if (!cfg)
    return;

  cfg->hddCache = 0u;

  // The guest zeroes these only when debugMindows==0, which the overlay sets.
  cfg->debugInputKey = 0u;
  cfg->debugInputPad = 0u;

  const bool dev = bd::Settings::Get().Devmode();
  const u32 v = dev ? 1u : 0u;

  cfg->debugMenuBoot = v;
  cfg->debugMenuBuild = v;
  cfg->debugMenuMemory = v;
  cfg->debugLabels = v;
  cfg->mainMenu = v;
  cfg->userMenu = v;
  cfg->toolMenu = v;
  cfg->toolEntryBits = dev ? kAllToolEntryBits : 0u;

  cfg->debugMindows = v;
  if (auto *flag = GetMindowsHiddenFlag())
    *flag = dev ? 0u : 1u;
}

void ToggleMindows() {
  if (!bd::Settings::Get().Devmode())
    return;

  auto *flag = GetMindowsHiddenFlag();
  if (flag) {
    u32 cur = *flag;
    *flag = cur ^ 1u;
    BD_INFO("Mindows overlay {}", cur ? "shown" : "hidden");
  }
}

} // namespace
} // namespace bd::engine

// Fires after bdGameSettingsInit writes defaults.
void bdPostConfigInitHook() {
  bd::Settings::Get().SetDevmodeApplier(bd::engine::ApplyDebugConfig);
  bd::engine::Settings::Get().ApplyCameraSpeed();
  BD_INFO("guest debug config applied (devmode={})",
          bd::Settings::Get().Devmode());
}

// The guest key buffer only feeds debug systems, so the poll is skipped outside
// devmode to keep stray keystrokes out of the guest. Ctrl+Alt+M toggles the
// Mindows overlay, and while it is hidden the bridge forwards nothing.
void bdKeyboardPollHook() {
  bd::audio::ApplyAudioDebugPokes();
  if (!bd::Settings::Get().Devmode())
    return;
  if (bd::platform::PollMindowsHotkey())
    bd::engine::ToggleMindows();
  bd::platform::PollKeyboardToGuest();
}

// MapManTask's sound trigger debug draw walks trigger polygon lists borrowed
// from the field scene with no liveness guard of its own, so it takes the same
// guard the matching update has.
REX_EXTERN(__imp__MapManTask__DebugDrawTriggers);
REX_HOOK_RAW(MapManTask__DebugDrawTriggers) {
  if (!bd::mem::load<u32>(bd::engine::addr::kFieldSceneCtl) ||
      !bd::mem::load<u32>(bd::engine::addr::kFieldPlayerEntity))
    return;
  __imp__MapManTask__DebugDrawTriggers(ctx, base);
}
