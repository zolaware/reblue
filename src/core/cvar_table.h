/**
 * @file    core/cvar_table.h
 * @brief   re:Blue settings, the bd scope owner the registry serves, and the
 *          registry accessors the settings objects write through.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <charconv>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <rex/console_command.h>
#include <rex/utility.h>
#include <rex/result.h>
#include <rex/settings_registry.h>
#include <rex/setting.h>
#include "core/build_info.h"
#include "gpu/settings.h"

namespace bd {

using SettingWatches = std::vector<rex::ScopeGuard>;

rex::SettingBase *FindSetting(std::string_view name);

bool CvarSet(std::string_view name, std::string_view value);
std::string CvarGet(std::string_view name);

// True when the player or the command line gave the value, not a title default.
bool CvarIsSet(std::string_view name);

f64 CvarDouble(std::string_view name, f64 fallback);
i32 CvarInt(std::string_view name, i32 fallback);

// Calls fn(name, new value as text) after every change of the named setting.
void CvarWatch(SettingWatches &handles, std::string_view name,
               std::function<void(std::string_view, std::string_view)> fn);

// Drops every watch CvarWatch made, so no callback fires into a torn down title.
void CvarUnwatchAll();

bool CvarReset(std::string_view name);

// app.cache_root, empty when the OS cache folder applies.
std::filesystem::path CacheRoot();

// The render resolution the player picked: ui.resolution when it names a size,
// else the video mode size when the player set one.
bool CvarResolution(i32 &w, i32 &h);

rex::Result<void> SaveConfig(const std::filesystem::path &file);

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
void PadLogRecord(std::string_view args);
void PadLogStop(std::string_view args);
void PadLogSave(std::string_view args);
void PadLogLoad(std::string_view args);
void PadLogPlay(std::string_view args);

} // namespace bd::commands

namespace bd::cvars {

inline rex::MoveOnlyFunction<rex::Result<void>(std::string_view)>
Command(void (*fn)(std::string_view)) {
  return [fn](std::string_view args) {
    fn(args);
    return rex::Ok();
  };
}

class Table : public rex::SettingOwner {
public:
  Table() : SettingOwner("bd") {}

  static Table &Get();

  REX_SETTING(std::string, profile, "default",
              "Active profile name. Selects <install_root>/profiles/<name> as "
              "the user data location for config, saves, and DLC toggles. Set "
              "by the launcher.",
              rex::kStartupOnly, rex::kNoSave);
  REX_SETTING(i32, config_version, 0,
              "Config schema version this file was last written by");
  REX_SETTING(bool, no_installer, false,
              "Skip the first-run installer, error out if no install is "
              "configured");

  REX_SETTING(bool, devmode, false,
              "Developer mode: debug menu boot, Mindows overlay (F11) and "
              "debug keyboard input.");
  REX_SETTING(std::string, language, "auto",
              "UI text language: auto, us, jp, de, fr, es, it, kr, tw, cn, "
              "po. Requires restart.",
              rex::kRestart);
  REX_SETTING(bool, i18n_keys, false,
              "Show catalog keys instead of translated text, so a string on "
              "screen names the entry that produced it.");
  REX_SETTING(std::string, lang_path, "lang",
              "Folder searched for a localization.toml of UI text overrides. "
              "Relative to the app data folder.");
  REX_SETTING(bool, perf_overlay, false,
              "Show the frame time overlay: FPS, worst frame, CPU main, CPU "
              "render and GPU time.");
  REX_SETTING(bool, update_check, true,
              "Ask the update endpoint at startup: offer a newer re:Blue "
              "release, and fetch the content packs it points at.");
  REX_SETTING(std::string, update_base, REBLUE_UPDATE_BASE,
              "Update endpoint the startup check asks, without a trailing "
              "slash. Empty disables the check.");
  REX_SETTING(std::string, update_channel, REBLUE_UPDATE_CHANNEL,
              "Which builds the check offers: stable or nightly. Names the "
              "manifest read under bd.update_base.");
  REX_SETTING(std::string, saves_path, "",
              "Save-game directory. Empty = the active profile's saves "
              "folder.");
  REX_SETTING(std::string, cache_path, "",
              "Transient cache directory (PSO capture). Empty = "
              "<exe_dir>/cache.");

  REX_SETTING(i32, anisotropy,
              static_cast<i32>(bd::gpu::kDefaultSettings.anisotropy),
              "Anisotropic texture filtering level.", rex::Range{0, 16});
  REX_SETTING(i32, supersampling,
              static_cast<i32>(bd::gpu::kDefaultSettings.superSampling),
              "Render the scene at 2x the output resolution and downsample. "
              "1 = off, 2 = on.",
              rex::Range{1, 2}, rex::Allowed("1", "2"));
  REX_SETTING(i32, msaa, static_cast<i32>(bd::gpu::kDefaultSettings.msaa),
              "MSAA sample count for the 3D scene: 0 = off, 2, 4, 8. Clamped "
              "to device support.",
              rex::Range{0, 8}, rex::Allowed("0", "2", "4", "8"));
  REX_SETTING(i32, post_quality,
              static_cast<i32>(bd::gpu::kDefaultSettings.postQuality),
              "Resolution the bloom and depth-of-field chain runs at: 0 = "
              "half the scene, 1 = the scene, 2 = the widened bloom target the "
              "native renderer builds.",
              rex::Range{static_cast<f64>(bd::gpu::PostQuality::Low),
                         static_cast<f64>(bd::gpu::PostQuality::High)});
  REX_SETTING(i32, reflection_quality,
              static_cast<i32>(bd::gpu::kDefaultSettings.reflectionQuality),
              "How far a planar reflection grows past the size BD authored: 0 "
              "= not at all, 1 = with the render resolution, 2 = with "
              "supersampling too.",
              rex::Range{static_cast<f64>(bd::gpu::ReflectionQuality::Low),
                         static_cast<f64>(bd::gpu::ReflectionQuality::High)});
  REX_SETTING(bool, ntsc_filter, false,
              "Restore BD's analog-TV scanline filter. Every shipped "
              "db_posteffect record disables it, so it only shows up in the "
              "Battle Viewer, where it strobes the whole screen.");
  REX_SETTING(f64, dof_strength, 1.0,
              "Depth-of-field intensity, 1.0 = the game's own blur, 0 = off.",
              rex::Range{0.0, 1.0});
  REX_SETTING(i32, shadow_dimension,
              static_cast<i32>(bd::gpu::kDefaultSettings.shadowDimension),
              "Sun shadow-map resolution in pixels. Only "
              "512/1024/2048/4096/8192.",
              rex::Range{512, 8192},
              rex::Allowed("512", "1024", "2048", "4096", "8192"));
  REX_SETTING(f64, shadow_distance,
              static_cast<f64>(bd::gpu::kDefaultSettings.shadowDistance),
              "Sun shadow draw-distance multiplier (1.0 = X360 native).",
              rex::Range{1.0, 4.0});
  REX_SETTING(i32, aspect_ratio,
              static_cast<i32>(bd::gpu::AspectMode::Auto),
              "Output aspect ratio: 0 = 16:9, 1 = 4:3, 2 = 16:10, 3 = 21:9, 4 "
              "= 32:9, 5 = match the display, 6 = fill the display and "
              "stretch.",
              rex::Range{0, static_cast<f64>(bd::gpu::AspectMode::Stretch)});
  REX_SETTING(i32, fov_offset, 0,
              "Horizontal degrees added to the game's own field of view at "
              "16:9, which the menu counts off 45. 0 keeps how the game "
              "frames itself. Battle and event scenes hold the game's own "
              "value, since their effects are drawn to span it.",
              rex::Range{0, 75});

  REX_SETTING(i32, audio_debug, 0,
              "Engine cue-monitor overlay: 0 off, 1 all cue slots, 2 active "
              "cues only. Needs bd.devmode.",
              rex::Range{0, 2});
  REX_SETTING(i32, audio_peak, 0,
              "Engine output peak-meter overlay: 0 off, 1 meters, 2 meters "
              "with dB readout. Needs bd.devmode.",
              rex::Range{0, 2});
  REX_SETTING(bool, audio_log, false,
              "Log cue playback and voice pitch changes.");
  REX_SETTING(f64, audio_limit, 8.0,
              "Peak level at which the output guard silences a stream that stays above it, 0 is off.",
              rex::Range{0.0, 8.0});

  REX_SETTING(bool, mod_log, false,
              "Log title file accesses to logs/file_access_summary.csv and "
              "per-session logs under logs/mod_access_logs/.");
  REX_SETTING(i32, mod_log_keep, 10,
              "Previous-session mod access logs kept at boot.");
  REX_SETTING(i32, mod_log_max_mb, 64,
              "Per-session mod access log size cap in MB. 0 = unlimited.");
  REX_SETTING(bool, disc_prefetch, true,
              "Open every loose file under pack, snd_memory and snd_memory_jp "
              "once at boot on a background thread so the OS caches their "
              "metadata and the game's first opens do not stall on a cold "
              "disk.");

  REX_SETTING(bool, save_anywhere, false,
              "Keep the camp menu's Save entry usable away from save points. "
              "May cause softlocks and other oddities.");
  REX_SETTING(bool, disable_tutorials, false,
              "Skip the tutorial pages. The story letters, the warp map and "
              "the loading animation still play.");
  REX_SETTING(bool, map_gimmick_markers, false,
              "Mark the area map with the search points, chests and barriers "
              "the current map still has untouched.");
  REX_SETTING(i32, hud_mode, 0,
              "Field HUD: 0 always on, 1 fades out while idle and returns on "
              "any face or shoulder button, 2 never drawn.");
  REX_SETTING(f64, hud_fade_delay, 5.0,
              "Seconds of no button before the idle field HUD starts fading.");
  REX_SETTING(bool, mouse_input, true,
              "Mouse input: raw mouse look in the field with the pointer "
              "locked, and the drawn cursor in menus.");
  REX_SETTING(bool, mouse_cursor_sfx, true,
              "Play the cursor-move sound effect on a mouse-driven row "
              "change.");
  REX_SETTING(i32, mouse_cursor_opacity, 80,
              "Opacity of the drawn mouse pointer, in percent.");
  REX_SETTING(i32, glyph_set, 0,
              "Button prompt glyphs: 0 = follow the input device, 1 = "
              "controller, 2 = keyboard.");
  REX_SETTING(i32, glyph_pad, -1,
              "Controller the prompt glyphs draw: -1 = follow the connected "
              "pad, 0 = Xbox 360, 1 = Xbox, 2 = PlayStation, 3 = Switch, 4 = "
              "Steam Deck.");
  REX_SETTING(f64, camera_speed, 1.2,
              "Field camera stick speed, scaling both axes. 1.2 is the value "
              "the disc ships.");

  REX_SETTING(i32, opt_msg_speed, 2,
              "Text display speed: 0 slow, 1 normal, 2 fast, 3 very fast.");
  REX_SETTING(i32, opt_msg_size, 0,
              "Text size: 0 small, 1 normal, 2 large. Absent on discs that "
              "drop the row.");
  REX_SETTING(i32, opt_voice_type, 1,
              "Voice track, 1-based into the disc's voice language list. "
              "Absent on discs that drop the row.");
  REX_SETTING(i32, opt_ruby, 1,
              "Furigana over kanji. Absent outside Japanese.");
  REX_SETTING(i32, opt_subtitles, 1, "Event subtitles: 1 on, 0 off.");
  REX_SETTING(i32, opt_audio_hints, 1, "Audio hints: 1 on, 0 off.");
  REX_SETTING(i32, opt_battle_hints, 1, "Battle hints: 1 on, 0 off.");
  REX_SETTING(i32, opt_skip_events, 1, "Let events be skipped: 1 on, 0 off.");
  REX_SETTING(i32, opt_camera, 0, "Camera: 0 standard, 1 inverted.");
  REX_SETTING(i32, opt_target_first, 0,
              "Battle target priority: 0 lowest HP, 1 left, 2 center, 3 "
              "right.");
  REX_SETTING(i32, opt_ctl_normal_type, 0,
              "Field controller layout: 0 through 3 for types A to D.");
  REX_SETTING(i32, opt_ctl_mechatt_type, 0,
              "Mechat shooting controller layout: 0 through 3 for types A to "
              "D.");
  REX_SETTING(f64, opt_music_volume, 1.0,
              "Music volume, -1 silent through 1 full.");
  REX_SETTING(f64, opt_se_volume, 1.0,
              "Sound effect volume, -1 silent through 1 full. Drives the "
              "voice bus alongside the effect bus.");
  REX_SETTING(f64, opt_brightness, 0.0,
              "Screen brightness, -1 through 1 with 0 untouched.");
  REX_SETTING(f64, opt_screen_pos_x, 0.0,
              "Screen position left to right, -1 through 1 with 0 centered.");
  REX_SETTING(f64, opt_screen_pos_y, 0.0,
              "Screen position up and down, -1 through 1 with 0 centered.");

  REX_SETTING(bool, input_bindings, true,
              "Route input through reblue's binding layer");

  REX_COMMAND(game_mode, "Dump engine-state predicates + current stage",
              Command(bd::commands::GameMode));
  REX_COMMAND(game_stage, "Print the current stage name, category and id",
              Command(bd::commands::GameStage));
  REX_COMMAND(game_cutscene,
              "Report engine event (.evt) and Sofdec movie (.sfd) playback",
              Command(bd::commands::GameCutscene));
  REX_COMMAND(game_language,
              "Print the installed UI languages and the latched locale",
              Command(bd::commands::GameLanguage));
  REX_COMMAND(game_party, "List active party members and their stats",
              Command(bd::commands::GameParty));
  REX_COMMAND(game_roster, "List the full character roster incl. reserves",
              Command(bd::commands::GameRoster));
  REX_COMMAND(game_effects,
              "Effect lists and status timers: game_effects [index]",
              Command(bd::commands::GameEffects));
  REX_COMMAND(game_tablename, "Look an id up in the phenome and item tables",
              Command(bd::commands::GameTableName));
  REX_COMMAND(game_stats, "Stat contribution breakdown: game_stats [index]",
              Command(bd::commands::GameStats));
  REX_COMMAND(game_character,
              "Dump one active character: derived stats and unlocked classes",
              Command(bd::commands::GameCharacter));
  REX_COMMAND(game_gold, "Print current gold", Command(bd::commands::GameGold));
  REX_COMMAND(game_inventory, "Dump gold and every non-empty inventory slot",
              Command(bd::commands::GameInventory));
  REX_COMMAND(game_battle,
              "Battle overview: active flag, counters, party/enemy counts, "
              "phase",
              Command(bd::commands::GameBattle));
  REX_COMMAND(game_set_hp,
              "Set an active character's current HP (clamped to maxHP)",
              Command(bd::commands::GameSetHP));
  REX_COMMAND(game_set_mp,
              "Set an active character's current MP (clamped to maxMP)",
              Command(bd::commands::GameSetMP));
  REX_COMMAND(game_set_gold, "Set gold (clamped 0..99999999)",
              Command(bd::commands::GameSetGold));
  REX_COMMAND(game_set_item,
              "Set an inventory slot's itemId/qty (qty clamped to 99)",
              Command(bd::commands::GameSetItem));
  REX_COMMAND(game_options, "Dump the stock game options",
              Command(bd::commands::GameOptionsDump));
  REX_COMMAND(game_gimmicks,
              "Gimmicks found. Takes a map stem, or nothing for the current "
              "map",
              Command(bd::commands::GameGimmicks));
  REX_COMMAND(game_gimmicks_by_type,
              "Gimmicks found, split by kind and barrier color",
              Command(bd::commands::GameGimmicksByType));
  REX_COMMAND(game_gimmick_points,
              "List one map's gimmicks, chests and barriers with positions "
              "and state",
              Command(bd::commands::GameGimmickPoints));
  REX_COMMAND(game_events, "Print the most recently published engine events",
              Command(bd::commands::GameEvents));
  REX_COMMAND(input_binds, "Dump every action and the inputs bound to it",
              Command(bd::commands::InputBinds));
  REX_COMMAND(input_sources, "Dump the sampled physical input state",
              Command(bd::commands::InputSourcesDump));
  REX_COMMAND(input_actions, "Dump the resolved action state",
              Command(bd::commands::InputActionsDump));
  REX_COMMAND(input_map, "Dump the derived button map",
              Command(bd::commands::InputMap));
  REX_COMMAND(pad_log_record,
              "Clear the pad log and start recording pad input (devmode)",
              Command(bd::commands::PadLogRecord));
  REX_COMMAND(pad_log_stop, "Stop pad log recording or playback (devmode)",
              Command(bd::commands::PadLogStop));
  REX_COMMAND(pad_log_save, "Save the pad log: pad_log_save [slot] (devmode)",
              Command(bd::commands::PadLogSave));
  REX_COMMAND(pad_log_load,
              "Load a saved pad log: pad_log_load [slot] (devmode)",
              Command(bd::commands::PadLogLoad));
  REX_COMMAND(pad_log_play, "Play back the loaded pad log (devmode)",
              Command(bd::commands::PadLogPlay));
};

inline Table &Get() { return Table::Get(); }

} // namespace bd::cvars
