/**
 * @file    engine/game_options.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/game_options.h"

#include <cstddef>
#include <string>

#include <vector>

#include <rex/ppc/hook.h>
#include <rex/kernel/memory_view.h>
#include <rex/kernel/title_runtime.h>

#include "core/logging.h"
#include "core/guest_global.h"
#include "core/memory_helpers.h"
#include "core/cvar_table.h"
#include "core/shared_globals.h"
#include "core/settings.h"
#include "engine/config.h"
#include "engine/save/save_types.h"
#include "engine/visual_render.h"
#include "platform/platform.h"
#include "platform/host_ui.h"

REX_IMPORT(__imp__bdSoundStartWithVolume, SoundStartWithVolume, u32(u32, f64));

namespace bd::engine {

namespace addr {
REX_GUEST_GLOBAL(be_i32, kMsgSpeed, 0x82DEB160);
REX_GUEST_GLOBAL(be_i32, kMsgSize, 0x82DEB164);
REX_GUEST_GLOBAL(be_i32, kRuby, 0x82DEB168);
REX_GUEST_GLOBAL(be_i32, kSubtitles, 0x82DEB16C);
REX_GUEST_GLOBAL(be_i32, kBattleHints, 0x82DEB170);
REX_GUEST_GLOBAL(be_i32, kSkipEvents, 0x82DEB174);
REX_GUEST_GLOBAL(be_i32, kTargetFirst, 0x82DEB178);
REX_GUEST_GLOBAL(be_i32, kCamera, 0x82DEB17C);
REX_GUEST_GLOBAL(be_f32, kMusicVolume, 0x82DEB180);
REX_GUEST_GLOBAL(be_f32, kSeVolume, 0x82DEB184);
REX_GUEST_GLOBAL(be_f32, kVoiceVolume, 0x82DEB188);
REX_GUEST_GLOBAL(be_f32, kBrightness, 0x82DEB190);
REX_GUEST_GLOBAL(be_f32, kScreenPosX, 0x82DEB194);
REX_GUEST_GLOBAL(be_f32, kScreenPosY, 0x82DEB198);
REX_GUEST_GLOBAL(be_i32, kAudioHints, 0x82DC40DC);
REX_GUEST_GLOBAL(be_i32, kCtlNormalType, 0x82DC40E0);
REX_GUEST_GLOBAL(be_i32, kCtlMechattType, 0x82DC40E4);
REX_GUEST_GLOBAL(be_u32, kContentTask, 0x82DC9A80);
// Each holds a pointer to a bus name.
REX_GUEST_GLOBAL(be_u32, kSoundBusDefault, 0x82774488);
REX_GUEST_GLOBAL(be_u32, kSoundBusMusic, 0x8277448C);
REX_GUEST_GLOBAL(be_u32, kSoundBusVoice, 0x82774490);
REX_GUEST_GLOBAL(be_f32, kSeMixLevel, 0x827744DC);
} // namespace addr

namespace {

bd::SettingWatches g_callbacks;

constexpr i32 kCtlTypeA = 0;

// A level runs -1 to 1 and reaches the mixer as (level + 1) scaled per bus.
constexpr f64 kSeBusScale = 0.75;
constexpr f64 kSeMixScale = 0.5;
constexpr f64 kVoiceBusScale = 0.65;
constexpr f64 kScreenPosXScale = 100.0;
constexpr f64 kScreenPosYScale = -100.0;

template <u32 Address> i32 LoadInt(rex::kernel::GuestGlobal<be_i32, Address> global) {
  return bd::mem::global_load(global);
}

template <u32 Address>
bool StoreInt(rex::kernel::GuestGlobal<be_i32, Address> global, i32 value) {
  return LoadInt(global) != value && bd::mem::global_store(global, value);
}

template <u32 Address>
f64 LoadFloat(rex::kernel::GuestGlobal<be_f32, Address> global) {
  return static_cast<f64>(bd::mem::global_load(global));
}

template <u32 Address>
bool StoreFloat(rex::kernel::GuestGlobal<be_f32, Address> global, f64 value) {
  const auto next = static_cast<f32>(value);
  return bd::mem::global_load(global) != next &&
         bd::mem::global_store(global, next);
}

// Set while a setter writes its own cvar, so the change callback does not push
// the value back into the engine before the setter has compared against it.
bool s_selfWrite = false;

template <typename T> bool WriteCvar(const char *name, T v) {
  s_selfWrite = true;
  const bool ok = bd::CvarSet(name, FormatCvar(v));
  s_selfWrite = false;
  return ok;
}

// Base of the block bdSaveBlockRestoreConfig will read, or 0 when the content
// task does not exist yet.
u32 ReadBlockBase() {
  const u32 task = bd::mem::global_load(addr::kContentTask);
  if (!task)
    return 0;
  const auto *content = bd::mem::try_at<const ContentTask>(task);
  if (!content)
    return 0;
  const bool second = i32(content->blockSelector) != 0;
  return task + static_cast<u32>(second ? offsetof(ContentTask, writeBlock)
                                        : offsetof(ContentTask, readBlock));
}

// A volume global is inert until its bus is restarted with the level.
void ApplyMixer() {
  const f64 music = bd::cvars::Get().opt_music_volume.Get();
  const f64 se = bd::cvars::Get().opt_se_volume.Get();
  SoundStartWithVolume(bd::mem::global_load(addr::kSoundBusMusic),
                       music + 1.0);
  SoundStartWithVolume(bd::mem::global_load(addr::kSoundBusDefault),
                       (se + 1.0) * kSeBusScale);
  SoundStartWithVolume(bd::mem::global_load(addr::kSoundBusVoice),
                       (se + 1.0) * kVoiceBusScale);
  bd::mem::global_store(addr::kSeMixLevel,
                        static_cast<f32>((se + 1.0) * kSeMixScale));
}

// These copies are what gets read back, never the globals.
void ApplyMirrors() {
  Config::Get().SetCamRollInv(static_cast<u32>(bd::cvars::Get().opt_camera.Get()));

  VisualRender render = VisualRender::Get();
  if (!render)
    return;

  const auto brightness = static_cast<f32>(bd::cvars::Get().opt_brightness.Get());
  for (u32 i = 0; i < VisualRender::kBrightnessChannels; ++i)
    render.SetBrightness(i, brightness);
  render.SetScreenPosX(
      static_cast<f32>(bd::cvars::Get().opt_screen_pos_x.Get() * kScreenPosXScale));
  render.SetScreenPosY(
      static_cast<f32>(bd::cvars::Get().opt_screen_pos_y.Get() * kScreenPosYScale));
}

// Pushes the global set into the engine globals. A no-op before the address
// space exists.
void AdoptCvars() {
  StoreInt(addr::kMsgSpeed, bd::cvars::Get().opt_msg_speed.Get());
  StoreInt(addr::kMsgSize, bd::cvars::Get().opt_msg_size.Get());
  StoreInt(globals::kVoiceType, bd::cvars::Get().opt_voice_type.Get());
  StoreInt(addr::kRuby, bd::cvars::Get().opt_ruby.Get());
  StoreInt(addr::kSubtitles, bd::cvars::Get().opt_subtitles.Get());
  StoreInt(addr::kAudioHints, bd::cvars::Get().opt_audio_hints.Get());
  StoreInt(addr::kBattleHints, bd::cvars::Get().opt_battle_hints.Get());
  StoreInt(addr::kSkipEvents, bd::cvars::Get().opt_skip_events.Get());
  StoreInt(addr::kCamera, bd::cvars::Get().opt_camera.Get());
  StoreInt(addr::kTargetFirst, bd::cvars::Get().opt_target_first.Get());
  StoreInt(addr::kCtlNormalType, kCtlTypeA);
  StoreInt(addr::kCtlMechattType, kCtlTypeA);

  const f64 se = bd::cvars::Get().opt_se_volume.Get();
  StoreFloat(addr::kMusicVolume, bd::cvars::Get().opt_music_volume.Get());
  StoreFloat(addr::kSeVolume, se);
  StoreFloat(addr::kVoiceVolume, se);
  StoreFloat(addr::kBrightness, bd::cvars::Get().opt_brightness.Get());
  StoreFloat(addr::kScreenPosX, bd::cvars::Get().opt_screen_pos_x.Get());
  StoreFloat(addr::kScreenPosY, bd::cvars::Get().opt_screen_pos_y.Get());
}

constexpr const char *kOptionCvars[] = {
    "bd.opt_msg_speed",    "bd.opt_msg_size",     "bd.opt_voice_type",
    "bd.opt_ruby",         "bd.opt_subtitles",    "bd.opt_audio_hints",
    "bd.opt_battle_hints", "bd.opt_skip_events",  "bd.opt_camera",
    "bd.opt_target_first", "bd.opt_music_volume", "bd.opt_se_volume",
    "bd.opt_brightness",   "bd.opt_screen_pos_x", "bd.opt_screen_pos_y"};

} // namespace

bool GameOptionsResolved() {
  const rex::kernel::TitleRuntime *title = bd::platform::Title();
  return title && title->IsRunning();
}

GameOptions &GameOptions::Get() {
  static GameOptions s;
  return s;
}

// Lays the global set over the block the engine is about to read, so whatever the
// save file carried is replaced before bdSaveBlockRestoreConfig consults it.
// Every mirror the restore performs stays the engine's.
void GameOptions::WriteBlock() {
  const u32 base = ReadBlockBase();
  if (!base)
    return;

  auto *block = bd::mem::try_at<SaveBlock_t>(base);
  if (!block)
    return;
  SaveConfigBlock_t *cfg = &block->config;

  cfg->msgSpeed = bd::cvars::Get().opt_msg_speed.Get();
  cfg->msgSize = bd::cvars::Get().opt_msg_size.Get();
  cfg->ruby = bd::cvars::Get().opt_ruby.Get();
  cfg->subtitles = bd::cvars::Get().opt_subtitles.Get();
  cfg->battleHints = bd::cvars::Get().opt_battle_hints.Get();
  cfg->skipEvents = bd::cvars::Get().opt_skip_events.Get();
  cfg->targetFirst = bd::cvars::Get().opt_target_first.Get();
  cfg->camera = bd::cvars::Get().opt_camera.Get();

  const auto se = static_cast<f32>(bd::cvars::Get().opt_se_volume.Get());
  cfg->musicVolume = static_cast<f32>(bd::cvars::Get().opt_music_volume.Get());
  cfg->seVolume = se;
  cfg->voiceVolume = se;
  cfg->brightness = static_cast<f32>(bd::cvars::Get().opt_brightness.Get());
  cfg->screenPosX = static_cast<f32>(bd::cvars::Get().opt_screen_pos_x.Get());
  cfg->screenPosY = static_cast<f32>(bd::cvars::Get().opt_screen_pos_y.Get());

  block->ctlNormalType = kCtlTypeA;
  block->ctlMechattType = kCtlTypeA;
  block->fltVoice = bd::cvars::Get().opt_audio_hints.Get();
}

void GameOptions::AdoptVoiceType() {
  const i32 voice = VoiceType();
  if (voice != bd::cvars::Get().opt_voice_type.Get())
    dirty_ |= WriteCvar("bd.opt_voice_type", voice);
}

void GameOptions::Init() {
  // The engine has not booted yet, so this pushes nothing. It is the change
  // callback that matters here: a console or config file write reaches the
  // engine globals the same way a menu edit does.
  for (const char *name : kOptionCvars)
    bd::CvarWatch(g_callbacks, name,
                  [](std::string_view, std::string_view) {
          if (!s_selfWrite)
            AdoptCvars();
        });
}

void GameOptions::Apply() {
  AdoptCvars();
  ApplyMixer();
  ApplyMirrors();
}

void GameOptions::Flush() {
  if (!dirty_)
    return;
  if (auto saved = bd::SaveConfig(bd::platform::ConfigFilePath()); !saved)
    REXBD_WARN("config save failed: {}", saved.error().message);
  dirty_ = false;
}

i32 GameOptions::MsgSpeed() const { return LoadInt(addr::kMsgSpeed); }
bool GameOptions::SetMsgSpeed(i32 v) {
  dirty_ |= WriteCvar("bd.opt_msg_speed", v);
  return StoreInt(addr::kMsgSpeed, v);
}

i32 GameOptions::MsgSize() const { return LoadInt(addr::kMsgSize); }
bool GameOptions::SetMsgSize(i32 v) {
  dirty_ |= WriteCvar("bd.opt_msg_size", v);
  return StoreInt(addr::kMsgSize, v);
}

i32 GameOptions::VoiceType() const { return LoadInt(globals::kVoiceType); }
bool GameOptions::SetVoiceType(i32 v) {
  dirty_ |= WriteCvar("bd.opt_voice_type", v);
  return StoreInt(globals::kVoiceType, v);
}
bool GameOptions::SetEngineVoiceType(i32 v) {
  return StoreInt(globals::kVoiceType, v);
}

i32 GameOptions::Ruby() const { return LoadInt(addr::kRuby); }
bool GameOptions::SetRuby(i32 v) {
  dirty_ |= WriteCvar("bd.opt_ruby", v);
  return StoreInt(addr::kRuby, v);
}

i32 GameOptions::Subtitles() const { return LoadInt(addr::kSubtitles); }
bool GameOptions::SetSubtitles(i32 v) {
  dirty_ |= WriteCvar("bd.opt_subtitles", v);
  return StoreInt(addr::kSubtitles, v);
}

i32 GameOptions::AudioHints() const { return LoadInt(addr::kAudioHints); }
bool GameOptions::SetAudioHints(i32 v) {
  dirty_ |= WriteCvar("bd.opt_audio_hints", v);
  return StoreInt(addr::kAudioHints, v);
}

i32 GameOptions::BattleHints() const { return LoadInt(addr::kBattleHints); }
bool GameOptions::SetBattleHints(i32 v) {
  dirty_ |= WriteCvar("bd.opt_battle_hints", v);
  return StoreInt(addr::kBattleHints, v);
}

i32 GameOptions::SkipEvents() const { return LoadInt(addr::kSkipEvents); }
bool GameOptions::SetSkipEvents(i32 v) {
  dirty_ |= WriteCvar("bd.opt_skip_events", v);
  return StoreInt(addr::kSkipEvents, v);
}

i32 GameOptions::Camera() const { return LoadInt(addr::kCamera); }
bool GameOptions::SetCamera(i32 v) {
  dirty_ |= WriteCvar("bd.opt_camera", v);
  const bool changed = StoreInt(addr::kCamera, v);
  ApplyMirrors();
  return changed;
}

i32 GameOptions::TargetFirst() const { return LoadInt(addr::kTargetFirst); }
bool GameOptions::SetTargetFirst(i32 v) {
  dirty_ |= WriteCvar("bd.opt_target_first", v);
  return StoreInt(addr::kTargetFirst, v);
}

f64 GameOptions::MusicVolume() const { return LoadFloat(addr::kMusicVolume); }
bool GameOptions::SetMusicVolume(f64 v) {
  dirty_ |= WriteCvar("bd.opt_music_volume", v);
  const bool changed = StoreFloat(addr::kMusicVolume, v);
  ApplyMixer();
  return changed;
}

f64 GameOptions::SeVolume() const { return LoadFloat(addr::kSeVolume); }
bool GameOptions::SetSeVolume(f64 v) {
  dirty_ |= WriteCvar("bd.opt_se_volume", v);
  const bool se = StoreFloat(addr::kSeVolume, v);
  const bool voice = StoreFloat(addr::kVoiceVolume, v);
  ApplyMixer();
  return se || voice;
}

f64 GameOptions::Brightness() const { return LoadFloat(addr::kBrightness); }
bool GameOptions::SetBrightness(f64 v) {
  dirty_ |= WriteCvar("bd.opt_brightness", v);
  const bool changed = StoreFloat(addr::kBrightness, v);
  ApplyMirrors();
  return changed;
}

f64 GameOptions::ScreenPosX() const { return LoadFloat(addr::kScreenPosX); }
bool GameOptions::SetScreenPosX(f64 v) {
  dirty_ |= WriteCvar("bd.opt_screen_pos_x", v);
  const bool changed = StoreFloat(addr::kScreenPosX, v);
  ApplyMirrors();
  return changed;
}

f64 GameOptions::ScreenPosY() const { return LoadFloat(addr::kScreenPosY); }
bool GameOptions::SetScreenPosY(f64 v) {
  dirty_ |= WriteCvar("bd.opt_screen_pos_y", v);
  const bool changed = StoreFloat(addr::kScreenPosY, v);
  ApplyMirrors();
  return changed;
}

} // namespace bd::engine

REX_DETOUR(bdSaveBlockRestoreConfig, void, ()) {
  bd::engine::GameOptions::Get().WriteBlock();
  original();
  bd::engine::GameOptions::Get().AdoptVoiceType();
}

// The original writes the engine's defaults over the whole set, on every
// return to the title as well as at boot.
REX_DETOUR(bdGameConfigInit, void, (i32 mode)) {
  original(mode);
  bd::engine::GameOptions::Get().Apply();
}
