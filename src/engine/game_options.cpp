/**
 * @file    engine/game_options.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/game_options.h"

#include <cstddef>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "core/global_config.h"
#include "core/logging.h"
#include "core/memory_helpers.h"
#include "core/settings.h" // kCvarGroup
#include "platform/platform.h"

REX_EXTERN(__imp__bdSaveBlockRestoreConfig);
REX_EXTERN(__imp__bdGameConfigInit);
REX_IMPORT(__imp__bdSoundStartWithVolume, SoundStartWithVolume, u32(u32, f64));

REXCVAR_DECLARE(i32, bd_opt_msg_speed);
REXCVAR_DECLARE(i32, bd_opt_msg_size);
REXCVAR_DECLARE(i32, bd_opt_voice_type);
REXCVAR_DECLARE(i32, bd_opt_ruby);
REXCVAR_DECLARE(i32, bd_opt_subtitles);
REXCVAR_DECLARE(i32, bd_opt_audio_hints);
REXCVAR_DECLARE(i32, bd_opt_battle_hints);
REXCVAR_DECLARE(i32, bd_opt_skip_events);
REXCVAR_DECLARE(i32, bd_opt_camera);
REXCVAR_DECLARE(i32, bd_opt_target_first);
REXCVAR_DECLARE(i32, bd_opt_ctl_normal_type);
REXCVAR_DECLARE(i32, bd_opt_ctl_mechatt_type);
REXCVAR_DECLARE(f64, bd_opt_music_volume);
REXCVAR_DECLARE(f64, bd_opt_se_volume);
REXCVAR_DECLARE(f64, bd_opt_brightness);
REXCVAR_DECLARE(f64, bd_opt_screen_pos_x);
REXCVAR_DECLARE(f64, bd_opt_screen_pos_y);

// Defaults are what bdGameConfigInit writes. The sliders run -1 to 1 with 0 at
// center, so a volume of 1 is full and a brightness of 0 is untouched.
REXCVAR_DEFINE_INT32(bd_opt_msg_speed, 2, kCvarGroup,
                     "Text display speed: 0 slow, 1 normal, 2 fast, 3 very "
                     "fast.");
REXCVAR_DEFINE_INT32(bd_opt_msg_size, 0, kCvarGroup,
                     "Text size: 0 small, 1 normal, 2 large. Absent on discs "
                     "that drop the row.");
REXCVAR_DEFINE_INT32(bd_opt_voice_type, 1, kCvarGroup,
                     "Voice track, 1-based into the disc's voice language "
                     "list. Absent on discs that drop the row.");
REXCVAR_DEFINE_INT32(bd_opt_ruby, 1, kCvarGroup,
                     "Furigana over kanji. Absent outside Japanese.");
REXCVAR_DEFINE_INT32(bd_opt_subtitles, 1, kCvarGroup,
                     "Event subtitles: 1 on, 0 off.");
REXCVAR_DEFINE_INT32(bd_opt_audio_hints, 1, kCvarGroup,
                     "Audio hints: 1 on, 0 off.");
REXCVAR_DEFINE_INT32(bd_opt_battle_hints, 1, kCvarGroup,
                     "Battle hints: 1 on, 0 off.");
REXCVAR_DEFINE_INT32(bd_opt_skip_events, 1, kCvarGroup,
                     "Let events be skipped: 1 on, 0 off.");
REXCVAR_DEFINE_INT32(bd_opt_camera, 0, kCvarGroup,
                     "Camera: 0 standard, 1 inverted.");
REXCVAR_DEFINE_INT32(bd_opt_target_first, 0, kCvarGroup,
                     "Battle target priority: 0 lowest HP, 1 left, 2 center, "
                     "3 right.");
REXCVAR_DEFINE_INT32(bd_opt_ctl_normal_type, 0, kCvarGroup,
                     "Field controller layout: 0 through 3 for types A to D.");
REXCVAR_DEFINE_INT32(bd_opt_ctl_mechatt_type, 0, kCvarGroup,
                     "Mechat shooting controller layout: 0 through 3 for types "
                     "A to D.");
REXCVAR_DEFINE_DOUBLE(bd_opt_music_volume, 1.0, kCvarGroup,
                      "Music volume, -1 silent through 1 full.");
REXCVAR_DEFINE_DOUBLE(bd_opt_se_volume, 1.0, kCvarGroup,
                      "Sound effect volume, -1 silent through 1 full. Drives "
                      "the voice bus alongside the effect bus.");
REXCVAR_DEFINE_DOUBLE(bd_opt_brightness, 0.0, kCvarGroup,
                      "Screen brightness, -1 through 1 with 0 untouched.");
REXCVAR_DEFINE_DOUBLE(bd_opt_screen_pos_x, 0.0, kCvarGroup,
                      "Screen position left to right, -1 through 1 with 0 "
                      "centered.");
REXCVAR_DEFINE_DOUBLE(bd_opt_screen_pos_y, 0.0, kCvarGroup,
                      "Screen position up and down, -1 through 1 with 0 "
                      "centered.");

namespace bd::engine {

namespace addr {
inline constexpr u32 kMsgSpeed = 0x82DEB160;
inline constexpr u32 kMsgSize = 0x82DEB164;
inline constexpr u32 kRuby = 0x82DEB168;
inline constexpr u32 kSubtitles = 0x82DEB16C;
inline constexpr u32 kBattleHints = 0x82DEB170;
inline constexpr u32 kSkipEvents = 0x82DEB174;
inline constexpr u32 kTargetFirst = 0x82DEB178;
inline constexpr u32 kCamera = 0x82DEB17C;
inline constexpr u32 kMusicVolume = 0x82DEB180;
inline constexpr u32 kSeVolume = 0x82DEB184;
inline constexpr u32 kVoiceVolume = 0x82DEB188;
inline constexpr u32 kBrightness = 0x82DEB190;
inline constexpr u32 kScreenPosX = 0x82DEB194;
inline constexpr u32 kScreenPosY = 0x82DEB198;
inline constexpr u32 kVoiceType = 0x82DC40D8;
inline constexpr u32 kAudioHints = 0x82DC40DC;
inline constexpr u32 kCtlNormalType = 0x82DC40E0;
inline constexpr u32 kCtlMechattType = 0x82DC40E4;
inline constexpr u32 kContentTask = 0x82DC9A80;
// Each holds a pointer to a bus name.
inline constexpr u32 kSoundBusDefault = 0x82774488;
inline constexpr u32 kSoundBusMusic = 0x8277448C;
inline constexpr u32 kSoundBusVoice = 0x82774490;
inline constexpr u32 kSeMixLevel = 0x827744DC;
inline constexpr u32 kVisualRender = 0x82DC9848;
} // namespace addr

namespace {

// The config tail of a save block, so the struct starts at kConfigBlockOffset
// rather than at the block itself.
struct SaveConfigBlock_t {
  /* 0x00 */ be_i32 msgSpeed;
  /* 0x04 */ be_i32 msgSize;
  /* 0x08 */ be_i32 ruby;
  /* 0x0C */ be_i32 subtitles;
  /* 0x10 */ be_i32 battleHints;
  /* 0x14 */ be_i32 skipEvents;
  /* 0x18 */ be_i32 targetFirst;
  /* 0x1C */ be_i32 camera;
  /* 0x20 */ be_f32 musicVolume;
  /* 0x24 */ be_f32 seVolume;
  /* 0x28 */ be_f32 voiceVolume;
  /* 0x2C */ be_i32 opaque;
  /* 0x30 */ be_f32 brightness;
  /* 0x34 */ be_f32 screenPosX;
  /* 0x38 */ be_f32 screenPosY;
};
static_assert(sizeof(SaveConfigBlock_t) == 0x3C);
static_assert(offsetof(SaveConfigBlock_t, targetFirst) == 0x18);
static_assert(offsetof(SaveConfigBlock_t, camera) == 0x1C);
static_assert(offsetof(SaveConfigBlock_t, opaque) == 0x2C);
static_assert(offsetof(SaveConfigBlock_t, brightness) == 0x30);

// Offsets from a block base, as bdSaveBlockRestoreConfig reads them.
constexpr u32 kConfigBlockOffset = 44876;
constexpr u32 kCtlNormalTypeOffset = 45484;
constexpr u32 kCtlMechattTypeOffset = 45488;
constexpr u32 kVoiceTypeOffset = 40792;
constexpr u32 kAudioHintsOffset = 40800;

// Two blocks live in the content task. The restore reads the second only when
// the selector is set, and bdSaveBlockCapture always writes the second.
constexpr u32 kWriteBlockOffset = 55208;
constexpr u32 kReadBlockOffset = 9528;
constexpr u32 kBlockSelector = 100896;

// Into g_pVisualRender, and into the global config for the camera.
constexpr u32 kRenderBrightness = 0x1B2C;
constexpr u32 kRenderBrightnessChannels = 3;
constexpr u32 kRenderScreenPosX = 0x1B44;
constexpr u32 kRenderScreenPosY = 0x1B48;

// A level runs -1 to 1 and reaches the mixer as (level + 1) scaled per bus.
constexpr f64 kSeBusScale = 0.75;
constexpr f64 kSeMixScale = 0.5;
constexpr f64 kVoiceBusScale = 0.65;
constexpr f64 kScreenPosXScale = 100.0;
constexpr f64 kScreenPosYScale = -100.0;

i32 LoadInt(u32 va) { return bd::mem::try_load<i32>(va); }

bool StoreInt(u32 va, i32 value) {
  return LoadInt(va) != value && bd::mem::try_store<i32>(va, value);
}

f64 LoadFloat(u32 va) { return static_cast<f64>(bd::mem::try_load<f32>(va)); }

bool StoreFloat(u32 va, f64 value) {
  const auto next = static_cast<f32>(value);
  return bd::mem::try_load<f32>(va) != next &&
         bd::mem::try_store<f32>(va, next);
}

// Set while a setter writes its own cvar, so the change callback does not push
// the value back into the guest before the setter has compared against it.
bool s_selfWrite = false;

template <typename T> bool WriteCvar(const char *name, T v) {
  s_selfWrite = true;
  const bool ok = rex::cvar::SetFlagByName(name, FormatCvar(v));
  s_selfWrite = false;
  return ok;
}

// Base of the block bdSaveBlockRestoreConfig will read, or 0 when the content
// task does not exist yet.
u32 ReadBlockBase() {
  const u32 task = bd::mem::try_load<u32>(addr::kContentTask);
  if (!task)
    return 0;
  const bool second = bd::mem::try_load<i32>(task + kBlockSelector) != 0;
  return task + (second ? kWriteBlockOffset : kReadBlockOffset);
}

// A volume global is inert until its bus is restarted with the level.
void ApplyMixer() {
  const f64 music = REXCVAR_GET(bd_opt_music_volume);
  const f64 se = REXCVAR_GET(bd_opt_se_volume);
  SoundStartWithVolume(bd::mem::try_load<u32>(addr::kSoundBusMusic),
                       music + 1.0);
  SoundStartWithVolume(bd::mem::try_load<u32>(addr::kSoundBusDefault),
                       (se + 1.0) * kSeBusScale);
  SoundStartWithVolume(bd::mem::try_load<u32>(addr::kSoundBusVoice),
                       (se + 1.0) * kVoiceBusScale);
  bd::mem::try_store<f32>(addr::kSeMixLevel,
                          static_cast<f32>((se + 1.0) * kSeMixScale));
}

// These copies are what gets read back, never the globals.
void ApplyMirrors() {
  if (auto *cfg = GetGlobalConfig())
    cfg->camRollInv = static_cast<u32>(REXCVAR_GET(bd_opt_camera));

  const u32 render = bd::mem::try_load<u32>(addr::kVisualRender);
  if (!render)
    return;

  const auto brightness = static_cast<f32>(REXCVAR_GET(bd_opt_brightness));
  for (u32 i = 0; i < kRenderBrightnessChannels; ++i)
    bd::mem::try_store<f32>(render + kRenderBrightness + i * sizeof(f32),
                            brightness);
  bd::mem::try_store<f32>(
      render + kRenderScreenPosX,
      static_cast<f32>(REXCVAR_GET(bd_opt_screen_pos_x) * kScreenPosXScale));
  bd::mem::try_store<f32>(
      render + kRenderScreenPosY,
      static_cast<f32>(REXCVAR_GET(bd_opt_screen_pos_y) * kScreenPosYScale));
}

// Pushes the global set into the guest globals. A no-op before the address
// space exists.
void AdoptCvars() {
  StoreInt(addr::kMsgSpeed, REXCVAR_GET(bd_opt_msg_speed));
  StoreInt(addr::kMsgSize, REXCVAR_GET(bd_opt_msg_size));
  StoreInt(addr::kVoiceType, REXCVAR_GET(bd_opt_voice_type));
  StoreInt(addr::kRuby, REXCVAR_GET(bd_opt_ruby));
  StoreInt(addr::kSubtitles, REXCVAR_GET(bd_opt_subtitles));
  StoreInt(addr::kAudioHints, REXCVAR_GET(bd_opt_audio_hints));
  StoreInt(addr::kBattleHints, REXCVAR_GET(bd_opt_battle_hints));
  StoreInt(addr::kSkipEvents, REXCVAR_GET(bd_opt_skip_events));
  StoreInt(addr::kCamera, REXCVAR_GET(bd_opt_camera));
  StoreInt(addr::kTargetFirst, REXCVAR_GET(bd_opt_target_first));
  StoreInt(addr::kCtlNormalType, REXCVAR_GET(bd_opt_ctl_normal_type));
  StoreInt(addr::kCtlMechattType, REXCVAR_GET(bd_opt_ctl_mechatt_type));

  const f64 se = REXCVAR_GET(bd_opt_se_volume);
  StoreFloat(addr::kMusicVolume, REXCVAR_GET(bd_opt_music_volume));
  StoreFloat(addr::kSeVolume, se);
  StoreFloat(addr::kVoiceVolume, se);
  StoreFloat(addr::kBrightness, REXCVAR_GET(bd_opt_brightness));
  StoreFloat(addr::kScreenPosX, REXCVAR_GET(bd_opt_screen_pos_x));
  StoreFloat(addr::kScreenPosY, REXCVAR_GET(bd_opt_screen_pos_y));
}

constexpr const char *kOptionCvars[] = {
    "bd_opt_msg_speed",        "bd_opt_msg_size",
    "bd_opt_voice_type",       "bd_opt_ruby",
    "bd_opt_subtitles",        "bd_opt_audio_hints",
    "bd_opt_battle_hints",     "bd_opt_skip_events",
    "bd_opt_camera",           "bd_opt_target_first",
    "bd_opt_ctl_normal_type",  "bd_opt_ctl_mechatt_type",
    "bd_opt_music_volume",     "bd_opt_se_volume",
    "bd_opt_brightness",       "bd_opt_screen_pos_x",
    "bd_opt_screen_pos_y"};

} // namespace

bool GameOptionsResolved() { return bd::mem::ready(); }

GameOptions &GameOptions::Get() {
  static GameOptions s;
  return s;
}

// Lays the global set over the block the guest is about to read, so whatever the
// save file carried is replaced before bdSaveBlockRestoreConfig consults it.
// Every mirror the restore performs stays the guest's.
void GameOptions::WriteBlock() {
  const u32 base = ReadBlockBase();
  if (!base)
    return;

  auto *cfg = bd::mem::try_at<SaveConfigBlock_t>(base + kConfigBlockOffset);
  if (!cfg)
    return;

  cfg->msgSpeed = REXCVAR_GET(bd_opt_msg_speed);
  cfg->msgSize = REXCVAR_GET(bd_opt_msg_size);
  cfg->ruby = REXCVAR_GET(bd_opt_ruby);
  cfg->subtitles = REXCVAR_GET(bd_opt_subtitles);
  cfg->battleHints = REXCVAR_GET(bd_opt_battle_hints);
  cfg->skipEvents = REXCVAR_GET(bd_opt_skip_events);
  cfg->targetFirst = REXCVAR_GET(bd_opt_target_first);
  cfg->camera = REXCVAR_GET(bd_opt_camera);

  const auto se = static_cast<f32>(REXCVAR_GET(bd_opt_se_volume));
  cfg->musicVolume = static_cast<f32>(REXCVAR_GET(bd_opt_music_volume));
  cfg->seVolume = se;
  cfg->voiceVolume = se;
  cfg->brightness = static_cast<f32>(REXCVAR_GET(bd_opt_brightness));
  cfg->screenPosX = static_cast<f32>(REXCVAR_GET(bd_opt_screen_pos_x));
  cfg->screenPosY = static_cast<f32>(REXCVAR_GET(bd_opt_screen_pos_y));

  bd::mem::try_store<i32>(base + kCtlNormalTypeOffset,
                          REXCVAR_GET(bd_opt_ctl_normal_type));
  bd::mem::try_store<i32>(base + kCtlMechattTypeOffset,
                          REXCVAR_GET(bd_opt_ctl_mechatt_type));
  bd::mem::try_store<i32>(base + kVoiceTypeOffset,
                          REXCVAR_GET(bd_opt_voice_type));
  bd::mem::try_store<i32>(base + kAudioHintsOffset,
                          REXCVAR_GET(bd_opt_audio_hints));
}

void GameOptions::AdoptVoiceType() {
  const i32 voice = VoiceType();
  if (voice != REXCVAR_GET(bd_opt_voice_type))
    dirty_ |= WriteCvar("bd_opt_voice_type", voice);
}

void GameOptions::Init() {
  // The guest has not booted yet, so this pushes nothing. It is the change
  // callback that matters here: a console or config file write reaches the
  // guest globals the same way a menu edit does.
  for (const char *name : kOptionCvars)
    rex::cvar::RegisterChangeCallback(
        name, [](std::string_view, std::string_view) {
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
  rex::cvar::SaveConfig(bd::platform::ConfigFilePath());
  dirty_ = false;
}

i32 GameOptions::MsgSpeed() const { return LoadInt(addr::kMsgSpeed); }
bool GameOptions::SetMsgSpeed(i32 v) {
  dirty_ |= WriteCvar("bd_opt_msg_speed", v);
  return StoreInt(addr::kMsgSpeed, v);
}

i32 GameOptions::MsgSize() const { return LoadInt(addr::kMsgSize); }
bool GameOptions::SetMsgSize(i32 v) {
  dirty_ |= WriteCvar("bd_opt_msg_size", v);
  return StoreInt(addr::kMsgSize, v);
}

i32 GameOptions::VoiceType() const { return LoadInt(addr::kVoiceType); }
bool GameOptions::SetVoiceType(i32 v) {
  dirty_ |= WriteCvar("bd_opt_voice_type", v);
  return StoreInt(addr::kVoiceType, v);
}

i32 GameOptions::Ruby() const { return LoadInt(addr::kRuby); }
bool GameOptions::SetRuby(i32 v) {
  dirty_ |= WriteCvar("bd_opt_ruby", v);
  return StoreInt(addr::kRuby, v);
}

i32 GameOptions::Subtitles() const { return LoadInt(addr::kSubtitles); }
bool GameOptions::SetSubtitles(i32 v) {
  dirty_ |= WriteCvar("bd_opt_subtitles", v);
  return StoreInt(addr::kSubtitles, v);
}

i32 GameOptions::AudioHints() const { return LoadInt(addr::kAudioHints); }
bool GameOptions::SetAudioHints(i32 v) {
  dirty_ |= WriteCvar("bd_opt_audio_hints", v);
  return StoreInt(addr::kAudioHints, v);
}

i32 GameOptions::BattleHints() const { return LoadInt(addr::kBattleHints); }
bool GameOptions::SetBattleHints(i32 v) {
  dirty_ |= WriteCvar("bd_opt_battle_hints", v);
  return StoreInt(addr::kBattleHints, v);
}

i32 GameOptions::SkipEvents() const { return LoadInt(addr::kSkipEvents); }
bool GameOptions::SetSkipEvents(i32 v) {
  dirty_ |= WriteCvar("bd_opt_skip_events", v);
  return StoreInt(addr::kSkipEvents, v);
}

i32 GameOptions::Camera() const { return LoadInt(addr::kCamera); }
bool GameOptions::SetCamera(i32 v) {
  dirty_ |= WriteCvar("bd_opt_camera", v);
  const bool changed = StoreInt(addr::kCamera, v);
  ApplyMirrors();
  return changed;
}

i32 GameOptions::TargetFirst() const { return LoadInt(addr::kTargetFirst); }
bool GameOptions::SetTargetFirst(i32 v) {
  dirty_ |= WriteCvar("bd_opt_target_first", v);
  return StoreInt(addr::kTargetFirst, v);
}

i32 GameOptions::CtlNormalType() const { return LoadInt(addr::kCtlNormalType); }
bool GameOptions::SetCtlNormalType(i32 v) {
  dirty_ |= WriteCvar("bd_opt_ctl_normal_type", v);
  return StoreInt(addr::kCtlNormalType, v);
}

i32 GameOptions::CtlMechattType() const {
  return LoadInt(addr::kCtlMechattType);
}
bool GameOptions::SetCtlMechattType(i32 v) {
  dirty_ |= WriteCvar("bd_opt_ctl_mechatt_type", v);
  return StoreInt(addr::kCtlMechattType, v);
}

f64 GameOptions::MusicVolume() const { return LoadFloat(addr::kMusicVolume); }
bool GameOptions::SetMusicVolume(f64 v) {
  dirty_ |= WriteCvar("bd_opt_music_volume", v);
  const bool changed = StoreFloat(addr::kMusicVolume, v);
  ApplyMixer();
  return changed;
}

f64 GameOptions::SeVolume() const { return LoadFloat(addr::kSeVolume); }
bool GameOptions::SetSeVolume(f64 v) {
  dirty_ |= WriteCvar("bd_opt_se_volume", v);
  const bool se = StoreFloat(addr::kSeVolume, v);
  const bool voice = StoreFloat(addr::kVoiceVolume, v);
  ApplyMixer();
  return se || voice;
}

f64 GameOptions::Brightness() const { return LoadFloat(addr::kBrightness); }
bool GameOptions::SetBrightness(f64 v) {
  dirty_ |= WriteCvar("bd_opt_brightness", v);
  const bool changed = StoreFloat(addr::kBrightness, v);
  ApplyMirrors();
  return changed;
}

f64 GameOptions::ScreenPosX() const { return LoadFloat(addr::kScreenPosX); }
bool GameOptions::SetScreenPosX(f64 v) {
  dirty_ |= WriteCvar("bd_opt_screen_pos_x", v);
  const bool changed = StoreFloat(addr::kScreenPosX, v);
  ApplyMirrors();
  return changed;
}

f64 GameOptions::ScreenPosY() const { return LoadFloat(addr::kScreenPosY); }
bool GameOptions::SetScreenPosY(f64 v) {
  dirty_ |= WriteCvar("bd_opt_screen_pos_y", v);
  const bool changed = StoreFloat(addr::kScreenPosY, v);
  ApplyMirrors();
  return changed;
}

} // namespace bd::engine

REX_HOOK_RAW(bdSaveBlockRestoreConfig) {
  bd::engine::GameOptions::Get().WriteBlock();
  __imp__bdSaveBlockRestoreConfig(ctx, base);
  bd::engine::GameOptions::Get().AdoptVoiceType();
}

// The original writes the engine's defaults over the whole set, on every
// return to the title as well as at boot.
REX_HOOK_RAW(bdGameConfigInit) {
  __imp__bdGameConfigInit(ctx, base);
  bd::engine::GameOptions::Get().Apply();
}
