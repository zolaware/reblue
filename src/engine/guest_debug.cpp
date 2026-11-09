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
#include "core/cvar_table.h"
#include "core/logging.h"
#include "core/memory_helpers.h"
#include "core/settings.h"
#include "engine/engine.h"
#include "engine/settings.h"
#include "platform/keyboard_bridge.h"
#include "platform/platform.h"

#include <atomic>
#include <charconv>

#include <rex/ppc/hook.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/midasm.h>
#include <rex/types.h>

namespace bd::engine {
namespace {

// The Alt+key chars bdPadLogDebugHandler acts on.
constexpr u16 kPadLogStopKey = 0x1B;
constexpr u16 kPadLogRecordKey = 0x70;
constexpr u16 kPadLogPlayKey = 0x71;
constexpr u16 kPadLogSaveKey = 0x72;
constexpr u16 kPadLogLoadKey = 0x73;
constexpr u32 kPadLogMaxSlot = 999;
constexpr u8 kPadLogModifiers = 0x4;

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
  REXBD_INFO("Mindows overlay {}", hidden ? "shown" : "hidden");
}

namespace {

// A console command queues its key here and the handler presses it on the
// engine thread. The low half is the key, the high half is the slot plus one,
// or zero to keep the current slot.
std::atomic<u32> g_padLogRequest{0};

void RequestPadLog(u16 key, std::string_view args = {}) {
  if (!bd::Settings::Get().Devmode()) {
    REXBD_WARN("pad log commands need devmode");
    return;
  }
  const auto first = args.find_first_not_of(" \t");
  args = first == std::string_view::npos
             ? std::string_view{}
             : args.substr(first, args.find_last_not_of(" \t") - first + 1);
  u32 slotField = 0;
  if (!args.empty()) {
    u32 slot = 0;
    const auto [end, ec] =
        std::from_chars(args.data(), args.data() + args.size(), slot);
    if (ec != std::errc() || end != args.data() + args.size() ||
        slot > kPadLogMaxSlot) {
      REXBD_WARN("pad log slot must be 0 to {}", kPadLogMaxSlot);
      return;
    }
    slotField = slot + 1;
  }
  g_padLogRequest.store(slotField << 16 | key);
}

void PressRequestedPadLogKey() {
  const u32 request = g_padLogRequest.exchange(0);
  if (!request)
    return;
  if (const u32 slotField = request >> 16)
    Config::Get().SetPadLogSlot(slotField - 1);
  auto *entry = bd::mem::try_at<bd::platform::GuestKeyEntry>(
      bd::platform::kGuestKeyBuffer.GetAddress());
  if (!entry)
    return;
  entry->charCode = static_cast<u16>(request);
  entry->vkCode = 0;
  entry->modifiers = kPadLogModifiers;
}

} // namespace

} // namespace bd::engine

void bd::commands::PadLogRecord(std::string_view) {
  bd::engine::RequestPadLog(bd::engine::kPadLogRecordKey);
}

void bd::commands::PadLogStop(std::string_view) {
  bd::engine::RequestPadLog(bd::engine::kPadLogStopKey);
}

void bd::commands::PadLogSave(std::string_view args) {
  bd::engine::RequestPadLog(bd::engine::kPadLogSaveKey, args);
}

void bd::commands::PadLogLoad(std::string_view args) {
  bd::engine::RequestPadLog(bd::engine::kPadLogLoadKey, args);
}

void bd::commands::PadLogPlay(std::string_view) {
  bd::engine::RequestPadLog(bd::engine::kPadLogPlayKey);
}

// The engine runs this only while debugMindows is set, which devmode does.
REX_DETOUR(bdPadLogDebugHandler, void, ()) {
  bd::engine::PressRequestedPadLogKey();
  original();
}

// Fires after bdGameSettingsInit writes defaults.
REX_MIDASM_HOOK(bdPostConfigInitHook, ()) {
  bd::Settings::Get().SetDevmodeApplier(bd::engine::ApplyDebugConfig);
  bd::engine::Settings::Get().ApplyCameraSpeed();
  REXBD_INFO("engine debug config applied (devmode={})",
          bd::Settings::Get().Devmode());
}
REX_MIDASM_AT(bdPostConfigInitHook, 0x820CE730, after);

// The engine key buffer only feeds debug systems, so the poll is skipped outside
// devmode to keep stray keystrokes out of the engine. Ctrl+Alt+M toggles the
// Mindows overlay, and while it is hidden the bridge forwards nothing.
REX_MIDASM_HOOK(bdKeyboardPollHook, ()) {
  bd::audio::ApplyAudioDebugPokes();
  const bool devmode = bd::Settings::Get().Devmode();
  if (devmode && bd::platform::PollMindowsHotkey())
    bd::engine::Game::Get().ToggleMindows();
  const auto &game = bd::engine::Game::Get();
  bd::platform::HoldPadsForMindows(devmode && game.IsReady() &&
                                   !game.MindowsHidden());
  if (devmode)
    bd::platform::PollKeyboardToGuest();
}
REX_MIDASM_AT(bdKeyboardPollHook, 0x82126B04, before);

// MapManTask's sound trigger debug draw walks trigger polygon lists borrowed
// from the field scene with no liveness guard of its own, so it takes the same
// guard the matching update has.
REX_MIDASM_RETURN_IF(MapManTask__DebugDrawTriggersSkip, ()) {
  const auto &game = bd::engine::Game::Get();
  return !game.ScriptManTask() || !game.FieldPlayerEntity();
}
REX_MIDASM_AT(MapManTask__DebugDrawTriggersSkip, 0x823A4000, before);
