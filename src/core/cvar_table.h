/**
 * @file    core/cvar_table.h
 * @brief   re:Blue cvars, the bd scope table rex::Init registers, and the
 *          registry accessors the settings objects write through.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/cvar/registry.h>

#include "core/build_info.h"
#include "gpu/settings.h"

namespace bd {

// The registry rex::Init serves. Set once by ReblueApp after rex::Init, before
// any settings object adopts its values.
rex::cvar::ICvarRegistry &Cvars();
void SetCvars(rex::cvar::ICvarRegistry *registry);

inline bool CvarSet(std::string_view name, std::string_view value) {
  return Cvars().Set(name, value).has_value();
}

inline std::string CvarGet(std::string_view name) {
  return Cvars().Get(name).value_or(std::string());
}

inline bool CvarIsSet(std::string_view name) {
  return Cvars().GetSource(name) != rex::cvar::CvarSource::kDefault;
}

inline double CvarDouble(std::string_view name, double fallback) {
  const std::string text = CvarGet(name);
  double out = fallback;
  auto r = std::from_chars(text.data(), text.data() + text.size(), out);
  return r.ec == std::errc() ? out : fallback;
}

inline int CvarInt(std::string_view name, int fallback) {
  const std::string text = CvarGet(name);
  int out = fallback;
  auto r = std::from_chars(text.data(), text.data() + text.size(), out);
  return r.ec == std::errc() ? out : fallback;
}

inline void CvarWatch(std::vector<rex::cvar::CallbackHandle> &handles,
                      std::string_view name, rex::cvar::ChangeCallback fn) {
  if (auto handle = Cvars().OnChange(name, std::move(fn)))
    handles.push_back(std::move(*handle));
}

} // namespace bd

namespace bd::commands {

void GameMode(std::string_view args);
void GameStage(std::string_view args);
void GameCutscene(std::string_view args);
void GameLanguage(std::string_view args);
void GameParty(std::string_view args);
void GameRoster(std::string_view args);
void GameEffects(std::string_view args);
void GameTableName(std::string_view args);
void GameStats(std::string_view args);
void GameCharacter(std::string_view args);
void GameGold(std::string_view args);
void GameInventory(std::string_view args);
void GameBattle(std::string_view args);
void GameSetHP(std::string_view args);
void GameSetMP(std::string_view args);
void GameSetGold(std::string_view args);
void GameSetItem(std::string_view args);
void GameOptionsDump(std::string_view args);
void GameGimmicks(std::string_view args);
void GameGimmicksByType(std::string_view args);
void GameGimmickPoints(std::string_view args);
void GameEvents(std::string_view args);
void InputBinds(std::string_view args);
void InputSourcesDump(std::string_view args);
void InputActionsDump(std::string_view args);
void InputMap(std::string_view args);

} // namespace bd::commands

namespace bd::cvars {

inline bool IsFinite(std::string_view v) {
  double d = 0;
  auto r = std::from_chars(v.data(), v.data() + v.size(), d);
  return r.ec == std::errc() && r.ptr == v.data() + v.size() &&
         std::isfinite(d);
}

inline bool IsSuperSampling(std::string_view v) {
  int n = 0;
  auto r = std::from_chars(v.data(), v.data() + v.size(), n);
  return r.ec == std::errc() && (n == 1 || n == 2);
}

inline bool IsMSAA(std::string_view v) {
  int n = 0;
  auto r = std::from_chars(v.data(), v.data() + v.size(), n);
  return r.ec == std::errc() && (n == 0 || n == 2 || n == 4 || n == 8);
}

inline bool IsShadowDimension(std::string_view v) {
  int n = 0;
  auto r = std::from_chars(v.data(), v.data() + v.size(), n);
  return r.ec == std::errc() &&
         (n == 512 || n == 1024 || n == 2048 || n == 4096 || n == 8192);
}

REXCVAR_STRING(bd, profile, "default",
               "Active profile name. Selects <install_root>/profiles/<name> as "
               "the user data location for config, saves, and DLC toggles. Set "
               "by the launcher.")
    .SetLifecycle(rex::cvar::Lifecycle::kInitOnly)
    .SetNoSave();
REXCVAR_INT32(bd, config_version, 0,
              "Config schema version this file was last written by");
REXCVAR_BOOL(bd, no_installer, false,
             "Skip the first-run installer, error out if no install is "
             "configured");

REXCVAR_BOOL(bd, devmode, false,
             "Developer mode: debug menu boot, Mindows overlay (F11) and debug "
             "keyboard input.");
REXCVAR_STRING(bd, language, "auto",
               "UI text language: auto, us, jp, de, fr, es, it, kr, tw, cn, "
               "po. Requires restart.")
    .SetLifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_BOOL(bd, i18n_keys, false,
             "Show catalog keys instead of translated text, so a string on "
             "screen names the entry that produced it.");
REXCVAR_STRING(bd, lang_path, "lang",
               "Folder searched for a localization.toml of UI text overrides. "
               "Relative to the app data folder.");
REXCVAR_BOOL(bd, profiler, false,
             "Start the Tracy profiler at boot so a viewer can attach. Read at "
             "startup only, and inert in playtest builds, which compile the "
             "profiler out.");
REXCVAR_INT32(bd, shutdown_timeout_ms, 1500,
              "Budget for the ordered shutdown (guest quiesce + GPU drain) "
              "before the process is killed outright.")
    .SetRange(100, 30000);
REXCVAR_BOOL(bd, update_check, true,
             "Ask the update endpoint at startup: offer a newer re:Blue "
             "release, and fetch the content packs it points at.");
REXCVAR_STRING(bd, update_base, REBLUE_UPDATE_BASE,
               "Update endpoint the startup check asks, without a trailing "
               "slash. Empty disables the check.");
REXCVAR_STRING(bd, update_channel, REBLUE_UPDATE_CHANNEL,
               "Which builds the check offers: stable or nightly. Names the "
               "manifest read under bd.update_base.");
REXCVAR_STRING(bd, saves_path, "",
               "Save-game directory. Empty = the active profile's saves "
               "folder.");
REXCVAR_STRING(bd, cache_path, "",
               "Transient cache directory (PSO capture). Empty = "
               "<exe_dir>/cache.");

REXCVAR_BOOL(bd, pso_precache, true,
             "Precompile pipelines during loads instead of at first draw.");
REXCVAR_BOOL(bd, geometry_gpu_upload, true,
             "Place static geometry in the GPU_UPLOAD heap when the device "
             "supports it. Off uses UPLOAD instead, costing the write-combine "
             "win on AMD. Requires restart.");
REXCVAR_BOOL(bd, dred, true,
             "Record D3D12 auto-breadcrumbs and page-fault allocations so a "
             "lost device names the op and resource it died on. Costs a little "
             "GPU time per op. Requires restart.");
REXCVAR_BOOL(bd, scene_color_r11g11b10, false,
             "Store the HDR scene as R11G11B10_FLOAT instead of "
             "R16G16B16A16_FLOAT: half the VRAM per scene surface, no alpha "
             "channel, 6/6/5-bit mantissas. Requires restart.");
REXCVAR_INT32(bd, anisotropy, bd::gpu::kDefaultSettings.anisotropy,
              "Anisotropic texture filtering level.")
    .SetRange(0, 16);
REXCVAR_INT32(bd, supersampling, bd::gpu::kDefaultSettings.superSampling,
              "Render the scene at 2x the output resolution and downsample. 1 "
              "= off, 2 = on.")
    .SetRange(1, 2)
    .SetValidator(IsSuperSampling);
REXCVAR_INT32(bd, msaa, bd::gpu::kDefaultSettings.msaa,
              "MSAA sample count for the 3D scene: 0 = off, 2, 4, 8. Clamped "
              "to device support.")
    .SetRange(0, 8)
    .SetValidator(IsMSAA);
REXCVAR_INT32(bd, post_quality,
              static_cast<int32_t>(bd::gpu::kDefaultSettings.postQuality),
              "Resolution the bloom and depth-of-field chain runs at: 0 = half "
              "the scene, 1 = the scene, 2 = the widened bloom target the "
              "native renderer builds.")
    .SetRange(static_cast<int32_t>(bd::gpu::PostQuality::Low),
           static_cast<int32_t>(bd::gpu::PostQuality::High));
REXCVAR_INT32(bd, reflection_quality,
              static_cast<int32_t>(bd::gpu::kDefaultSettings.reflectionQuality),
              "How far a planar reflection grows past the size BD authored: 0 "
              "= not at all, 1 = with the render resolution, 2 = with "
              "supersampling too.")
    .SetRange(static_cast<int32_t>(bd::gpu::ReflectionQuality::Low),
           static_cast<int32_t>(bd::gpu::ReflectionQuality::High));
REXCVAR_BOOL(bd, ntsc_filter, false,
             "Restore BD's analog-TV scanline filter. Every shipped "
             "db_posteffect record disables it, so it only shows up in the "
             "Battle Viewer, where it strobes the whole screen.");
REXCVAR_DOUBLE(bd, dof_strength, 1.0,
               "Depth-of-field intensity, 1.0 = the game's own blur, 0 = off.")
    .SetRange(0.0, 1.0)
    .SetValidator(IsFinite);
REXCVAR_INT32(bd, shadow_dimension, bd::gpu::kDefaultSettings.shadowDimension,
              "Sun shadow-map resolution in pixels. Only "
              "512/1024/2048/4096/8192.")
    .SetRange(512, 8192)
    .SetValidator(IsShadowDimension);
REXCVAR_DOUBLE(bd, shadow_distance, bd::gpu::kDefaultSettings.shadowDistance,
               "Sun shadow draw-distance multiplier (1.0 = X360 native).")
    .SetRange(1.0, 4.0)
    .SetValidator(IsFinite);
REXCVAR_INT32(bd, aspect_ratio, static_cast<int32_t>(bd::gpu::AspectMode::Auto),
              "Output aspect ratio: 0 = 16:9, 1 = 4:3, 2 = 16:10, 3 = 21:9, 4 "
              "= 32:9, 5 = match the display, 6 = fill the display and "
              "stretch.")
    .SetRange(0, static_cast<int32_t>(bd::gpu::AspectMode::Stretch));
REXCVAR_INT32(bd, fov_offset, 0,
              "Horizontal degrees added to the game's own field of view at "
              "16:9, which the menu counts off 45. 0 keeps how the game frames "
              "itself. Battle and event scenes hold the game's own value, since "
              "their effects are drawn to span it.")
    .SetRange(0, 75);
REXCVAR_BOOL(bd, vsync, true, "Vertical sync.");

REXCVAR_DOUBLE(bd, audio_gain, 1.0, "Master gain of the audio output.")
    .SetRange(0.0, 4.0)
    .SetValidator(IsFinite);
REXCVAR_DOUBLE(bd, audio_center, 1.0,
               "Center channel level on a surround device.")
    .SetRange(0.0, 2.0)
    .SetValidator(IsFinite);
REXCVAR_DOUBLE(bd, audio_surround, 1.0,
               "Rear channel level on a surround device.")
    .SetRange(0.0, 2.0)
    .SetValidator(IsFinite);
REXCVAR_DOUBLE(bd, audio_lfe, 0.0, "LFE channel level on a surround device.")
    .SetRange(0.0, 2.0)
    .SetValidator(IsFinite);
REXCVAR_INT32(bd, audio_qframes, 16,
              "Audio pacing queue depth in 256-sample frames. Requires "
              "restart.")
    .SetRange(4, 64)
    .SetLifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_INT32(bd, audio_debug, 0,
              "Engine cue-monitor overlay: 0 off, 1 all cue slots, 2 active "
              "cues only. Needs bd.devmode.")
    .SetRange(0, 2);
REXCVAR_INT32(bd, audio_peak, 0,
              "Engine output peak-meter overlay: 0 off, 1 meters, 2 meters "
              "with dB readout. Needs bd.devmode.")
    .SetRange(0, 2);
REXCVAR_BOOL(bd, audio_log, false,
             "Log cue playback and voice pitch changes.");
REXCVAR_DOUBLE(bd, audio_limit, 8.0,
               "Peak the final mix may reach before the output guard mutes it: "
               "0 disables the guard.")
    .SetRange(0.0, 1024.0)
    .SetValidator(IsFinite);

REXCVAR_BOOL(bd, mod_log, false,
             "Log guest file accesses to logs/file_access_summary.csv and "
             "per-session logs under logs/mod_access_logs/.");
REXCVAR_INT32(bd, mod_log_keep, 10,
              "Previous-session mod access logs kept at boot.");
REXCVAR_INT32(bd, mod_log_max_mb, 64,
              "Per-session mod access log size cap in MB. 0 = unlimited.");
REXCVAR_BOOL(bd, disc_prefetch, true,
             "Open every loose file under pack, snd_memory and snd_memory_jp "
             "once at boot on a background thread so the OS caches their "
             "metadata and the game's first opens do not stall on a cold "
             "disk.");

REXCVAR_INT32(bd, fps_limit, 0,
              "Frame-rate cap: 0 = unlimited, above 30 the fixed 30Hz "
              "simulation is interpolated.");
REXCVAR_BOOL(bd, save_anywhere, false,
             "Keep the camp menu's Save entry usable away from save points. "
             "May cause softlocks and other oddities.");
REXCVAR_BOOL(bd, disable_tutorials, false,
             "Skip the tutorial pages. The story letters, the warp map and the "
             "loading animation still play.");
REXCVAR_BOOL(bd, map_gimmick_markers, false,
             "Mark the area map with the search points, chests and barriers "
             "the current map still has untouched.");
REXCVAR_INT32(bd, hud_mode, 0,
              "Field HUD: 0 always on, 1 fades out while idle and returns on "
              "any face or shoulder button, 2 never drawn.");
REXCVAR_DOUBLE(bd, hud_fade_delay, 5.0,
               "Seconds of no button before the idle field HUD starts fading.");
REXCVAR_BOOL(bd, mouse_input, true,
             "Mouse input: raw mouse look in the field with the pointer "
             "locked, and the drawn cursor in menus.");
REXCVAR_BOOL(bd, mouse_cursor_sfx, true,
             "Play the cursor-move sound effect on a mouse-driven row change.");
REXCVAR_INT32(bd, mouse_cursor_opacity, 80,
              "Opacity of the drawn mouse pointer, in percent.");
REXCVAR_INT32(bd, glyph_set, 0,
              "Button prompt glyphs: 0 = follow the input device, 1 = "
              "controller, 2 = keyboard.");
REXCVAR_INT32(bd, glyph_pad, -1,
              "Controller the prompt glyphs draw: -1 = follow the connected "
              "pad, 0 = Xbox 360, 1 = Xbox, 2 = PlayStation, 3 = Switch, 4 = "
              "Steam Deck.");
REXCVAR_BOOL(bd, vibration, true,
             "Pad rumble. Off stops the motors and leaves the rest of the pad as it is.");
REXCVAR_DOUBLE(bd, camera_speed, 1.2,
               "Field camera stick speed, scaling both axes. 1.2 is the value "
               "the disc ships.");

REXCVAR_INT32(bd, opt_msg_speed, 2,
              "Text display speed: 0 slow, 1 normal, 2 fast, 3 very fast.");
REXCVAR_INT32(bd, opt_msg_size, 0,
              "Text size: 0 small, 1 normal, 2 large. Absent on discs that "
              "drop the row.");
REXCVAR_INT32(bd, opt_voice_type, 1,
              "Voice track, 1-based into the disc's voice language list. "
              "Absent on discs that drop the row.");
REXCVAR_INT32(bd, opt_ruby, 1, "Furigana over kanji. Absent outside Japanese.");
REXCVAR_INT32(bd, opt_subtitles, 1, "Event subtitles: 1 on, 0 off.");
REXCVAR_INT32(bd, opt_audio_hints, 1, "Audio hints: 1 on, 0 off.");
REXCVAR_INT32(bd, opt_battle_hints, 1, "Battle hints: 1 on, 0 off.");
REXCVAR_INT32(bd, opt_skip_events, 1, "Let events be skipped: 1 on, 0 off.");
REXCVAR_INT32(bd, opt_camera, 0, "Camera: 0 standard, 1 inverted.");
REXCVAR_INT32(bd, opt_target_first, 0,
              "Battle target priority: 0 lowest HP, 1 left, 2 center, 3 "
              "right.");
REXCVAR_INT32(bd, opt_ctl_normal_type, 0,
              "Field controller layout: 0 through 3 for types A to D.");
REXCVAR_INT32(bd, opt_ctl_mechatt_type, 0,
              "Mechat shooting controller layout: 0 through 3 for types A to "
              "D.");
REXCVAR_DOUBLE(bd, opt_music_volume, 1.0,
               "Music volume, -1 silent through 1 full.");
REXCVAR_DOUBLE(bd, opt_se_volume, 1.0,
               "Sound effect volume, -1 silent through 1 full. Drives the "
               "voice bus alongside the effect bus.");
REXCVAR_DOUBLE(bd, opt_brightness, 0.0,
               "Screen brightness, -1 through 1 with 0 untouched.");
REXCVAR_DOUBLE(bd, opt_screen_pos_x, 0.0,
               "Screen position left to right, -1 through 1 with 0 centered.");
REXCVAR_DOUBLE(bd, opt_screen_pos_y, 0.0,
               "Screen position up and down, -1 through 1 with 0 centered.");

REXCVAR_BOOL(bd, input_bindings, true,
             "Route input through reblue's binding layer");

REXCVAR_COMMAND_ARGS(bd, game_mode, bd::commands::GameMode,
                     "Dump engine-state predicates + current stage");
REXCVAR_COMMAND_ARGS(bd, game_stage, bd::commands::GameStage,
                     "Print the current stage name, category and id");
REXCVAR_COMMAND_ARGS(bd, game_cutscene, bd::commands::GameCutscene,
                     "Report engine event (.evt) and Sofdec movie (.sfd) "
                     "playback");
REXCVAR_COMMAND_ARGS(bd, game_language, bd::commands::GameLanguage,
                     "Print the installed UI languages and the latched locale");
REXCVAR_COMMAND_ARGS(bd, game_party, bd::commands::GameParty,
                     "List active party members and their stats");
REXCVAR_COMMAND_ARGS(bd, game_roster, bd::commands::GameRoster,
                     "List the full character roster incl. reserves");
REXCVAR_COMMAND_ARGS(bd, game_effects, bd::commands::GameEffects,
                     "Effect lists and status timers: game_effects [index]");
REXCVAR_COMMAND_ARGS(bd, game_tablename, bd::commands::GameTableName,
                     "Look an id up in the phenome and item tables");
REXCVAR_COMMAND_ARGS(bd, game_stats, bd::commands::GameStats,
                     "Stat contribution breakdown: game_stats [index]");
REXCVAR_COMMAND_ARGS(bd, game_character, bd::commands::GameCharacter,
                     "Dump one active character: derived stats and unlocked "
                     "classes");
REXCVAR_COMMAND_ARGS(bd, game_gold, bd::commands::GameGold,
                     "Print current gold");
REXCVAR_COMMAND_ARGS(bd, game_inventory, bd::commands::GameInventory,
                     "Dump gold and every non-empty inventory slot");
REXCVAR_COMMAND_ARGS(bd, game_battle, bd::commands::GameBattle,
                     "Battle overview: active flag, counters, party/enemy "
                     "counts, phase");
REXCVAR_COMMAND_ARGS(bd, game_set_hp, bd::commands::GameSetHP,
                     "Set an active character's current HP (clamped to maxHP)");
REXCVAR_COMMAND_ARGS(bd, game_set_mp, bd::commands::GameSetMP,
                     "Set an active character's current MP (clamped to maxMP)");
REXCVAR_COMMAND_ARGS(bd, game_set_gold, bd::commands::GameSetGold,
                     "Set gold (clamped 0..99999999)");
REXCVAR_COMMAND_ARGS(bd, game_set_item, bd::commands::GameSetItem,
                     "Set an inventory slot's itemId/qty (qty clamped to 99)");
REXCVAR_COMMAND_ARGS(bd, game_options, bd::commands::GameOptionsDump,
                     "Dump the stock game options");
REXCVAR_COMMAND_ARGS(bd, game_gimmicks, bd::commands::GameGimmicks,
                     "Gimmicks found. Takes a map stem, or nothing for the "
                     "current map");
REXCVAR_COMMAND_ARGS(bd, game_gimmicks_by_type, bd::commands::GameGimmicksByType,
                     "Gimmicks found, split by kind and barrier color");
REXCVAR_COMMAND_ARGS(bd, game_gimmick_points, bd::commands::GameGimmickPoints,
                     "List one map's gimmicks, chests and barriers with "
                     "positions and state");
REXCVAR_COMMAND_ARGS(bd, game_events, bd::commands::GameEvents,
                     "Print the most recently published engine events");
REXCVAR_COMMAND_ARGS(bd, input_binds, bd::commands::InputBinds,
                     "Dump every action and the inputs bound to it");
REXCVAR_COMMAND_ARGS(bd, input_sources, bd::commands::InputSourcesDump,
                     "Dump the sampled physical input state");
REXCVAR_COMMAND_ARGS(bd, input_actions, bd::commands::InputActionsDump,
                     "Dump the resolved action state");
REXCVAR_COMMAND_ARGS(bd, input_map, bd::commands::InputMap,
                     "Dump the derived button map");

REXCVAR_TABLE(kReblueCvars, profile, config_version, no_installer,
              devmode, language, i18n_keys, lang_path, profiler,
              shutdown_timeout_ms, update_check, update_base, update_channel,
              saves_path, cache_path, pso_precache, geometry_gpu_upload, dred,
              scene_color_r11g11b10, anisotropy, supersampling, msaa,
              post_quality, reflection_quality, ntsc_filter, dof_strength,
              shadow_dimension, shadow_distance, aspect_ratio, fov_offset,
              vsync, audio_gain, audio_center, audio_surround, audio_lfe,
              audio_qframes, audio_debug, audio_peak, audio_log, audio_limit,
              mod_log, mod_log_keep, mod_log_max_mb, disc_prefetch,
              fps_limit, save_anywhere, disable_tutorials,
              map_gimmick_markers, hud_mode, hud_fade_delay, mouse_input,
              mouse_cursor_sfx, mouse_cursor_opacity, glyph_set, glyph_pad,
              vibration, camera_speed, opt_msg_speed, opt_msg_size,
              opt_voice_type, opt_ruby, opt_subtitles, opt_audio_hints,
              opt_battle_hints, opt_skip_events, opt_camera, opt_target_first,
              opt_ctl_normal_type, opt_ctl_mechatt_type, opt_music_volume,
              opt_se_volume, opt_brightness, opt_screen_pos_x,
              opt_screen_pos_y, input_bindings, game_mode, game_stage,
              game_cutscene, game_language, game_party, game_roster,
              game_effects, game_tablename, game_stats, game_character,
              game_gold, game_inventory, game_battle, game_set_hp, game_set_mp,
              game_set_gold, game_set_item, game_options, game_gimmicks,
              game_gimmicks_by_type, game_gimmick_points, game_events,
              input_binds, input_sources, input_actions, input_map);

} // namespace bd::cvars
