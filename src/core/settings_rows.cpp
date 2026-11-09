/**
 * @file    core/settings_rows.cpp
 * @brief   Every config menu row, as data.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/settings_rows.h"

#include "core/cvar_table.h"
#include "core/i18n.h"
#include "core/settings.h"

#include <cstdlib>
#include <iterator>
#include <string>

#include <rex/types.h>

#include "audio/audio.h"
#include "engine/engine.h"
#include "gpu/gpu.h"
#include "ui/ui.h"

namespace bd {
namespace {

// Camp__Config__MainTask__BuildRows zeroes FontSizeAlpha for locale 1 through 5
// and 9, leaving Japanese and the three CJK locales.
bool TextSizeHidden() {
  const u32 locale = i18n::CurrentLocale();
  return locale != engine::kLocaleJP &&
         (locale < engine::kLocaleKR || locale > engine::kLocaleCN);
}

// The same pass zeroes KanaAlpha whenever the locale is not Japanese.
bool RubyHidden() { return i18n::CurrentLocale() != engine::kLocaleJP; }

// The stock screen zeroes VoiceAlpha unconditionally, so the disc offers the
// voice track on its load screen alone. reblue shows the row whenever
// bd_boot.ini's [Voice] list gives it something to choose between.
bool VoiceLanguageHidden() { return engine::Language().VoiceCount() < 2; }

double DisplayModeNum() {
  if (CvarGet("ui.fullscreen") != "true")
    return 0.0;
  return CvarGet("ui.fullscreen_exclusive") == "true" ? 2.0 : 1.0;
}

double RenderResolutionNum() {
  i32 w = 0, h = 0;
  return bd::CvarResolution(w, h)
             ? static_cast<double>(w)
             : 0.0;
}

bool SetRenderResolution(const char *preset) {
  (void)CvarReset("ui.video_mode_width");
  (void)CvarReset("ui.video_mode_height");
  bool ok = true;
  if (!preset || !*preset) {
    (void)CvarReset("ui.resolution");
  } else {
    ok = CvarSet("ui.resolution", preset);
  }
  gpu::Output::Recompute();
  return ok;
}

constexpr SettingOption kDisplayMode[] = {
    {.text = "Windowed",
     .num = 0,
     .value = "false",
     .value2 = "false",
     .key = "opt.windowed"},
    {.text = "Borderless",
     .num = 1,
     .value = "true",
     .value2 = "false",
     .key = "opt.borderless"},
    {.text = "Fullscreen",
     .num = 2,
     .value = "true",
     .value2 = "true",
     .key = "opt.fullscreen"}};
constexpr SettingOption kResolution[] = {
    {.text = "Auto", .num = 0, .value = "", .key = "opt.auto"},
    {.text = "1280x720", .num = 1280, .value = "1280x720"},
    {.text = "1600x900", .num = 1600, .value = "1600x900"},
    {.text = "1920x1080", .num = 1920, .value = "1920x1080"},
    {.text = "2560x1440", .num = 2560, .value = "2560x1440"},
    {.text = "3840x2160", .num = 3840, .value = "3840x2160"}};
constexpr SettingOption kWindowSize[] = {
    {.text = "Auto", .num = 0, .value = "0", .value2 = "0", .key = "opt.auto"},
    {.text = "1280x720", .num = 1280, .value = "1280", .value2 = "720"},
    {.text = "1600x900", .num = 1600, .value = "1600", .value2 = "900"},
    {.text = "1920x1080", .num = 1920, .value = "1920", .value2 = "1080"},
    {.text = "2560x1440", .num = 2560, .value = "2560", .value2 = "1440"},
    {.text = "3840x2160", .num = 3840, .value = "3840", .value2 = "2160"}};
// Auto first, then narrowest to widest, ending at the one mode that gives up on
// aspect ratio. Everything but Stretch reprojects to hold the vertical view, so a
// wider ratio shows more to the sides rather than distorting.
constexpr SettingOption kAspect[] = {
    {.text = "Auto", .num = 5, .key = "opt.auto"},
    {.text = "4:3", .num = 1},
    {.text = "16:10", .num = 2},
    {.text = "16:9", .num = 0},
    {.text = "21:9", .num = 3},
    {.text = "32:9", .num = 4},
    {.text = "Stretch", .num = 6, .key = "opt.stretch"}};
// Horizontal degrees at 16:9. 45 is the game's own framing, and num is what
// each step widens it by.
constexpr SettingOption kFOV[] = {
    {.text = "45", .num = 0},   {.text = "50", .num = 5},
    {.text = "55", .num = 10},  {.text = "60", .num = 15},
    {.text = "65", .num = 20},  {.text = "70", .num = 25},
    {.text = "75", .num = 30},  {.text = "80", .num = 35},
    {.text = "85", .num = 40},  {.text = "90", .num = 45},
    {.text = "95", .num = 50},  {.text = "100", .num = 55},
    {.text = "105", .num = 60}, {.text = "110", .num = 65},
    {.text = "115", .num = 70}, {.text = "120", .num = 75}};
constexpr SettingOption kFPS[] = {
    {.text = "30", .num = 30},
    {.text = "60", .num = 60},
    {.text = "90", .num = 90},
    {.text = "120", .num = 120},
    {.text = "Unlimited", .num = 0, .key = "opt.unlimited"}};
constexpr SettingOption kOnOff[] = {
    {.text = "Off", .num = 0, .value = "false", .key = "opt.off"},
    {.text = "On", .num = 1, .value = "true", .key = "opt.on"}};
constexpr SettingOption kUpdateChannel[] = {
    {.text = "Stable",
     .num = static_cast<double>(UpdateChannel::Stable),
     .key = "settings.gameplay.update_channel.stable"},
    {.text = "Nightly",
     .num = static_cast<double>(UpdateChannel::Nightly),
     .key = "settings.gameplay.update_channel.nightly"}};
// Which block of the button glyph sheet every prompt draws from. num is the
// GlyphSet value engine/glyph_set.h declares.
constexpr SettingOption kGlyphSet[] = {
    {.text = "Auto", .num = 0, .key = "opt.auto"},
    {.text = "Controller", .num = 1, .key = "opt.controller"},
    {.text = "Keyboard", .num = 2, .key = "opt.keyboard"}};
// Which controller those prompts draw. num is the PadSet value
// engine/glyph_set.h declares. Auto reads the connected pad, and falls back to
// the 360, whose art is the one the disc ships.
constexpr SettingOption kPadGlyphSet[] = {
    {.text = "Auto", .num = -1, .key = "opt.auto"},
    {.text = "Xbox 360", .num = 0, .key = "opt.pad_xbox360"},
    {.text = "Xbox", .num = 1, .key = "opt.pad_xbox"},
    {.text = "PlayStation", .num = 2, .key = "opt.pad_playstation"},
    {.text = "Switch", .num = 3, .key = "opt.pad_switch"},
    {.text = "Steam Deck", .num = 4, .key = "opt.pad_steamdeck"}};
// Stock game options. num is the guest value, not the widget index: the guest's
// page one refresh inverts several of these when it picks an index, but a row
// here matches on value, so On stays 1 and Off stays 0.
constexpr SettingOption kTextSpeed[] = {{.text = "Slow", .num = 0, .key = "opt.slow"},
                                 {.text = "Normal",
                                  .num = 1,
                                  .key = "opt.normal"},
                                 {.text = "Fast", .num = 2, .key = "opt.fast"},
                                 {.text = "Very Fast",
                                  .num = 3,
                                  .key = "opt.very_fast"}};
constexpr SettingOption kTextSize[] = {{.text = "Small", .num = 0, .key = "opt.small"},
                                {.text = "Normal",
                                 .num = 1,
                                 .key = "opt.normal"},
                                {.text = "Large", .num = 2, .key = "opt.large"}};
constexpr SettingOption kCamera[] = {
    {.text = "Standard", .num = 0, .key = "opt.standard"},
    {.text = "Invert", .num = 1, .key = "opt.invert"}};
constexpr SettingOption kTargetFirst[] = {
    {.text = "Lowest HP", .num = 0, .key = "opt.lowest_hp"},
    {.text = "Left", .num = 1, .key = "opt.left"},
    {.text = "Center", .num = 2, .key = "opt.center"},
    {.text = "Right", .num = 3, .key = "opt.right"}};

// The guest stores these sliders as -1 through 1 and the rows show 0 to 100.
// The stock screen steps 0.05, which is 2.5% and reads as an uneven 2, 3, 2, 3
// once rounded for display, so reblue steps a whole percent instead. Nothing
// requires matching the guest's step size: the value is written directly and
// its applicator takes any float.
constexpr double kStockSliderStep = 1.0;
constexpr double ToPercent(double v) { return (v + 1.0) * 50.0; }
constexpr double FromPercent(double v) { return v / 50.0 - 1.0; }

// Percent runs 0 to 100, so this is a rounding that stays constexpr.
constexpr double SnapPercent(double v) {
  return static_cast<double>(static_cast<int>(v + 0.5));
}

// Every stock row binds to a GameOptions accessor pair, so the pair is the only
// part worth writing per row.
template <auto Get, auto Set> constexpr SettingBinding OptInt() {
  return {.get =
              [] {
                return static_cast<double>((engine::GameOptions::Get().*Get)());
              },
          .set = [](double v) {
            return (engine::GameOptions::Get().*Set)(static_cast<i32>(v));
          }};
}

template <auto Get, auto Set> constexpr SettingBinding OptPercent() {
  return {.get = [] { return ToPercent((engine::GameOptions::Get().*Get)()); },
          .set = [](double v) {
            // Snapped, so the row shows whole percents whatever odd value
            // the save started from.
            return (engine::GameOptions::Get().*Set)(
                FromPercent(SnapPercent(v)));
          }};
}

// The compass, the dungeon minimap and the party cards. Battle is unaffected.
constexpr SettingOption kHudMode[] = {
    {.text = "Off", .num = 2, .key = "opt.off"},
    {.text = "Auto-Hide", .num = 1, .key = "opt.auto_hide"},
    {.text = "On", .num = 0, .key = "opt.on"}};
constexpr SettingOption kMSAA[] = {{.text = "Off", .num = 0, .key = "opt.off"},
                                   {.text = "2x", .num = 2},
                                   {.text = "4x", .num = 4},
                                   {.text = "8x", .num = 8}};
constexpr SettingOption kSuperSampling[] = {
    {.text = "Off", .num = 1, .key = "opt.off"},
    {.text = "On", .num = 2, .key = "opt.on"}};
// bd.anisotropy keeps its 0..16 range for anyone who wants a middle step, but
// the menus offer the two ends of it.
constexpr i32 kAnisotropyOn = 16;
constexpr SettingOption kAniso[] = {
    {.text = "Off", .num = 0, .key = "opt.off"},
    {.text = "On", .num = kAnisotropyOn, .key = "opt.on"}};
constexpr SettingOption kPostQuality[] = {
    {.text = "Low",
     .num = static_cast<double>(static_cast<i32>(gpu::PostQuality::Low)),
     .key = gpu::ToString(gpu::PostQuality::Low)},
    {.text = "Medium",
     .num = static_cast<double>(static_cast<i32>(gpu::PostQuality::Medium)),
     .key = gpu::ToString(gpu::PostQuality::Medium)},
    {.text = "High",
     .num = static_cast<double>(static_cast<i32>(gpu::PostQuality::High)),
     .key = gpu::ToString(gpu::PostQuality::High)}};
constexpr SettingOption kReflectionQuality[] = {
    {.text = "Low",
     .num = static_cast<double>(static_cast<i32>(gpu::ReflectionQuality::Low)),
     .key = gpu::ToString(gpu::ReflectionQuality::Low)},
    {.text = "Medium",
     .num =
         static_cast<double>(static_cast<i32>(gpu::ReflectionQuality::Medium)),
     .key = gpu::ToString(gpu::ReflectionQuality::Medium)},
    {.text = "High",
     .num = static_cast<double>(static_cast<i32>(gpu::ReflectionQuality::High)),
     .key = gpu::ToString(gpu::ReflectionQuality::High)}};
constexpr SettingOption kShadowQuality[] = {
    {.text = "1x", .num = 1.0, .num2 = 1024},
    {.text = "2x", .num = 2.0, .num2 = 2048},
    {.text = "3x", .num = 3.0, .num2 = 4096},
    {.text = "4x", .num = 4.0, .num2 = 8192}};
// Quality Preset row: "Custom" (index kPresetCustom) is a read-only state
// shown when the current settings match no bundle, never a selectable target.
// Keys come from gpu::ToString so this row and the installer's tier buttons
// cannot drift apart.
constexpr int kPresetCustom = static_cast<int>(gpu::QualityPreset::Custom);
constexpr SettingOption kPresetOpts[] = {
    {.text = "Low", .num = 0, .key = gpu::ToString(gpu::QualityPreset::Low)},
    {.text = "Medium",
     .num = 1,
     .key = gpu::ToString(gpu::QualityPreset::Medium)},
    {.text = "High", .num = 2, .key = gpu::ToString(gpu::QualityPreset::High)},
    {.text = "Ultra",
     .num = 3,
     .key = gpu::ToString(gpu::QualityPreset::Ultra)},
    {.text = "Custom",
     .num = 4,
     .key = gpu::ToString(gpu::QualityPreset::Custom)}};

template <size_t N> constexpr int OptCount(const SettingOption (&)[N]) {
  return static_cast<int>(N);
}

// The stock game options first, in the order the guest's own page one lists
// them, then reblue's gameplay rows.
constexpr SettingRow kGameplaySettings[] = {
    {.label = "settings.gameplay.text_speed.label",
     .group = "menu.header.text",
     .binding = OptInt<&engine::GameOptions::MsgSpeed,
                       &engine::GameOptions::SetMsgSpeed>(),
     .options = kTextSpeed,
     .count = OptCount(kTextSpeed)},
    {.label = "settings.gameplay.text_size.label",
     .group = "menu.header.text",
     .binding = OptInt<&engine::GameOptions::MsgSize,
                       &engine::GameOptions::SetMsgSize>(),
     .options = kTextSize,
     .count = OptCount(kTextSize),
     .hidden = TextSizeHidden},
    {.label = "settings.gameplay.ruby.label",
     .group = "menu.header.text",
     .binding =
         OptInt<&engine::GameOptions::Ruby, &engine::GameOptions::SetRuby>(),
     .options = kOnOff,
     .count = OptCount(kOnOff),
     .hidden = RubyHidden},
    {.label = "settings.gameplay.subtitles.label",
     .group = "menu.header.text",
     .binding = OptInt<&engine::GameOptions::Subtitles,
                       &engine::GameOptions::SetSubtitles>(),
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.ui_language.label",
     .group = "menu.header.text",
     .binding = {.setText =
                     [](const char *code) {
                       return bd::cvars::Get().language.Set(code).has_value();
                     }},
     .restart = true,
     .sliderUi = true,
     .special = SettingSpecial::Language},
    {.label = "settings.gameplay.voice_language.label",
     .group = "menu.header.text",
     .binding = OptInt<&engine::GameOptions::VoiceType,
                       &engine::GameOptions::SetVoiceType>(),
     .sliderUi = true,
     .special = SettingSpecial::VoiceLanguage,
     .hidden = VoiceLanguageHidden},
    {.label = "settings.gameplay.audio_hints.label",
     .group = "menu.header.assists",
     .binding = OptInt<&engine::GameOptions::AudioHints,
                       &engine::GameOptions::SetAudioHints>(),
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.battle_hints.label",
     .group = "menu.header.assists",
     .binding = OptInt<&engine::GameOptions::BattleHints,
                       &engine::GameOptions::SetBattleHints>(),
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.skip_events.label",
     .group = "menu.header.assists",
     .binding = OptInt<&engine::GameOptions::SkipEvents,
                       &engine::GameOptions::SetSkipEvents>(),
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.disable_tutorials.label",
     .group = "menu.header.assists",
     .binding = {.get =
                     [] {
                       return bd::cvars::Get().disable_tutorials.Get() ? 1.0
                                                                         : 0.0;
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().disable_tutorials.Set(v !=
                                                                          0.0).has_value();
                     }},
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.target_first.label",
     .group = "menu.header.battle_field",
     .binding = OptInt<&engine::GameOptions::TargetFirst,
                       &engine::GameOptions::SetTargetFirst>(),
     .options = kTargetFirst,
     .count = OptCount(kTargetFirst)},
    {.label = "settings.gameplay.save_anywhere.label",
     .group = "menu.header.battle_field",
     .binding = {.get =
                     [] {
                       return bd::cvars::Get().save_anywhere.Get() ? 1.0
                                                                     : 0.0;
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().save_anywhere.Set(v != 0.0).has_value();
                     }},
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.map_gimmick_markers.label",
     .group = "menu.header.battle_field",
     .binding = {.get =
                     [] {
                       return bd::cvars::Get().map_gimmick_markers.Get() ? 1.0
                                                                          : 0.0;
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().map_gimmick_markers.Set(v != 0.0).has_value();
                     }},
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.hud.label",
     .group = "menu.header.interface",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           static_cast<i32>(bd::engine::GetHudMode()));
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().hud_mode.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kHudMode,
     .count = OptCount(kHudMode)},
    {.label = "settings.gameplay.hud_fade_delay.label",
     .group = "menu.header.interface",
     .binding = {.get = [] { return bd::cvars::Get().hud_fade_delay.Get(); },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().hud_fade_delay.Set(v).has_value();
                     }},
     .kind = SettingKind::Slider,
     .smin = 1.0,
     .smax = 30.0,
     .sstep = 1.0,
     .sfmt = "%.0f"},
    {.label = "settings.gameplay.glyph_set.label",
     .group = "menu.header.interface",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           bd::cvars::Get().glyph_set.Get());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().glyph_set.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kGlyphSet,
     .count = OptCount(kGlyphSet)},
    {.label = "settings.gameplay.pad_glyphs.label",
     .group = "menu.header.interface",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           bd::cvars::Get().glyph_pad.Get());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().glyph_pad.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kPadGlyphSet,
     .count = OptCount(kPadGlyphSet),
     .sliderUi = true},
    {.label = "settings.gameplay.update_check.label",
     .group = "menu.header.advanced",
     .binding = {.get =
                     [] { return bd::cvars::Get().update_check.Get() ? 1.0 : 0.0; },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().update_check.Set(v != 0.0).has_value();
                     }},
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.gameplay.update_channel.label",
     .group = "menu.header.advanced",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           bd::GetUpdateChannel());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().update_channel.Set(std::string(bd::ToString(static_cast<UpdateChannel>(static_cast<i32>(v))))).has_value();
                     }},
     .options = kUpdateChannel,
     .count = OptCount(kUpdateChannel)},
    {.label = "settings.gameplay.developer_mode.label",
     .group = "menu.header.advanced",
     .binding = {
         .get = [] { return bd::cvars::Get().devmode.Get() ? 1.0 : 0.0; },
         .set = [](double v) { return bd::cvars::Get().devmode.Set(v != 0.0).has_value(); }},
     .options = kOnOff,
     .count = OptCount(kOnOff),
     .restart = true},
};

constexpr SettingRow kDisplaySettings[] = {
    {.label = "settings.display.display_mode.label",
     .group = "menu.header.window",
     .binding = {.get = DisplayModeNum,
                 .cvar = "ui.fullscreen",
                 .cvar2 = "ui.fullscreen_exclusive"},
     .options = kDisplayMode,
     .count = OptCount(kDisplayMode)},
    {.label = "settings.display.monitor.label",
     .group = "menu.header.window",
     .binding = {.cvar = "ui.monitor"},
     .sliderUi = true,
     .special = SettingSpecial::Monitor},
    {.label = "settings.display.resolution.label",
     .group = "menu.header.window",
     .binding = {.get = RenderResolutionNum, .setText = SetRenderResolution},
     .options = kResolution,
     .count = OptCount(kResolution),
     .sliderUi = true},
    {.label = "settings.display.window_size.label",
     .group = "menu.header.window",
     .binding = {.cvar = "ui.window_width", .cvar2 = "ui.window_height"},
     .options = kWindowSize,
     .count = OptCount(kWindowSize),
     .sliderUi = true,
     .windowedGated = true},
    {.label = "settings.display.aspect_ratio.label",
     .group = "menu.header.window",
     .binding = {
         .get =
             [] {
               return static_cast<double>(bd::cvars::Get().aspect_ratio.Get());
             },
         .set =
             [](double v) {
               return bd::cvars::Get().aspect_ratio.Set(static_cast<i32>(v)).has_value();
             }},
     .options = kAspect,
     .count = OptCount(kAspect),
     .sliderUi = true},
    {.label = "settings.display.cursor_auto_hide.label",
     .group = "menu.header.window",
     .binding = {.get =
                     [] {
                       return std::strtod(
                           CvarGet("ui.cursor_timeout").c_str(), nullptr);
                     },
                 .set =
                     [](double v) {
                       return CvarSet("ui.cursor_timeout",
                                      std::to_string(static_cast<i32>(v)));
                     }},
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 30.0,
     .sstep = 1.0,
     .sfmt = "%.0f"},
    {.label = "settings.display.brightness.label",
     .group = "menu.header.picture",
     .binding = OptPercent<&engine::GameOptions::Brightness,
                           &engine::GameOptions::SetBrightness>(),
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 100.0,
     .sstep = kStockSliderStep,
     .sfmt = "%.0f%%"},
    {.label = "settings.display.fov.label",
     .group = "menu.header.picture",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           bd::cvars::Get().fov_offset.Get());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().fov_offset.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kFOV,
     .count = OptCount(kFOV),
     .sliderUi = true},
    {.label = "settings.display.fps_limit.label",
     .group = "menu.header.frame_rate",
     .binding = {
         .get =
             [] {
               return static_cast<double>(bd::engine::GetFPSLimit());
             },
         .set =
             [](double v) {
               return bd::engine::SetFPSLimit(static_cast<i32>(v));
             }},
     .options = kFPS,
     .count = OptCount(kFPS),
     .sliderUi = true},
    {.label = "settings.display.vsync.label",
     .group = "menu.header.frame_rate",
     .binding = {
         .get = [] { return bd::gpu::IsVsyncEnabled() ? 1.0 : 0.0; },
         .set =
             [](double v) { return bd::gpu::SetVsync(v != 0.0); }},
     .options = kOnOff,
     .count = OptCount(kOnOff)},
};

constexpr SettingRow kGraphicsSettings[] = {
    {.label = "settings.graphics.quality_preset.label",
     .group = "menu.header.preset",
     .desc = "settings.graphics.quality_preset.desc",
     .binding = {.get =
                     [] {
                       return static_cast<double>(static_cast<u32>(
                           bd::gpu::GetPreset()));
                     },
                 .set =
                     [](double v) {
                       return bd::gpu::SetPreset(
                           static_cast<gpu::QualityPreset>(
                               static_cast<u32>(v)));
                     }},
     .options = kPresetOpts,
     .count = OptCount(kPresetOpts),
     .optionDisabled =
         [](const SettingOption &o) {
           return static_cast<int>(o.num) == kPresetCustom &&
                  bd::gpu::GetPreset() !=
                      gpu::QualityPreset::Custom;
         }},
    {.label = "settings.graphics.msaa.label",
     .group = "menu.header.anti_aliasing",
     .desc = "settings.graphics.msaa.desc",
     .binding = {.get =
                     [] {
                       return static_cast<double>(bd::cvars::Get().msaa.Get());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().msaa.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kMSAA,
     .count = OptCount(kMSAA)},
    {.label = "settings.graphics.supersampling.label",
     .group = "menu.header.anti_aliasing",
     .desc = "settings.graphics.supersampling.desc",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           bd::cvars::Get().supersampling.Get());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().supersampling.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kSuperSampling,
     .count = OptCount(kSuperSampling)},
    {.label = "settings.graphics.anisotropic.label",
     .group = "menu.header.detail",
     .desc = "settings.graphics.anisotropic.desc",
     .binding = {.get =
                     [] {
                       return bd::cvars::Get().anisotropy.Get() > 0
                                  ? static_cast<double>(kAnisotropyOn)
                                  : 0.0;
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().anisotropy.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kAniso,
     .count = OptCount(kAniso)},
    {.label = "settings.graphics.shadow_quality.label",
     .group = "menu.header.detail",
     .desc = "settings.graphics.shadow_quality.desc",
     .binding = {.get = [] { return bd::cvars::Get().shadow_distance.Get(); },
                 .setPair =
                     [](double distance, double dimension) {
                       return bd::gpu::SetShadowQuality(
                           distance, static_cast<i32>(dimension));
                     }},
     .options = kShadowQuality,
     .count = OptCount(kShadowQuality),
     .sliderUi = true},
    {.label = "settings.graphics.reflections.label",
     .group = "menu.header.detail",
     .desc = "settings.graphics.reflections.desc",
     .binding = {.get =
                     [] {
                       return static_cast<double>(static_cast<i32>(
                           bd::gpu::GetReflectionQuality()));
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().reflection_quality.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kReflectionQuality,
     .count = OptCount(kReflectionQuality),
     .sliderUi = true},
    {.label = "settings.graphics.post_processing.label",
     .group = "menu.header.detail",
     .desc = "settings.graphics.post_processing.desc",
     .binding = {.get =
                     [] {
                       return static_cast<double>(static_cast<i32>(
                           bd::gpu::GetPostQuality()));
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().post_quality.Set(static_cast<i32>(v)).has_value();
                     }},
     .options = kPostQuality,
     .count = OptCount(kPostQuality),
     .sliderUi = true},
    // Counted in percent, so the row reads as how much of the effect is left
    // rather than as the multiplier the setting stores.
    {.label = "settings.graphics.depth_of_field.label",
     .group = "menu.header.detail",
     .desc = "settings.graphics.depth_of_field.desc",
     .binding = {.get =
                     [] { return bd::cvars::Get().dof_strength.Get() * 100.0; },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().dof_strength.Set(v / 100.0).has_value();
                     }},
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 100.0,
     .sstep = 5.0,
     .sfmt = "%.0f%%"},
};

constexpr SettingRow kAudioSettings[] = {
    {.label = "settings.audio.music.label",
     .group = "menu.header.volume",
     .binding = OptPercent<&engine::GameOptions::MusicVolume,
                           &engine::GameOptions::SetMusicVolume>(),
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 100.0,
     .sstep = kStockSliderStep,
     .sfmt = "%.0f%%"},
    // One row, two buses: the stock screen steps the effect and voice levels
    // together and reblue keeps that rather than splitting them.
    {.label = "settings.audio.sound_effects.label",
     .group = "menu.header.volume",
     .binding = OptPercent<&engine::GameOptions::SeVolume,
                           &engine::GameOptions::SetSeVolume>(),
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 100.0,
     .sstep = kStockSliderStep,
     .sfmt = "%.0f%%"},
    {.label = "settings.audio.master_volume.label",
     .group = "menu.header.volume",
     .binding = {.cvar = "apu.gain"},
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 2.0,
     .sstep = 0.05,
     .sfmt = "%.2f"},
    {.label = "settings.audio.center_level.label",
     .group = "menu.header.surround",
     .binding = {.cvar = "apu.center"},
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 2.0,
     .sstep = 0.05,
     .sfmt = "%.2f"},
    {.label = "settings.audio.surround_level.label",
     .group = "menu.header.surround",
     .binding = {.cvar = "apu.surround"},
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 2.0,
     .sstep = 0.05,
     .sfmt = "%.2f"},
    {.label = "settings.audio.lfe_level.label",
     .group = "menu.header.surround",
     .binding = {.cvar = "apu.lfe"},
     .kind = SettingKind::Slider,
     .smin = 0.0,
     .smax = 2.0,
     .sstep = 0.05,
     .sfmt = "%.2f"},
};

constexpr SettingRow kControlsSettings[] = {
    {.label = "settings.controls.camera.label",
     .group = "menu.header.camera",
     .binding = OptInt<&engine::GameOptions::Camera,
                       &engine::GameOptions::SetCamera>(),
     .options = kCamera,
     .count = OptCount(kCamera)},
    {.label = "settings.controls.camera_speed.label",
     .group = "menu.header.camera",
     .binding = {.get =
                     [] {
                       return bd::cvars::Get().camera_speed.Get();
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().camera_speed.Set(v).has_value();
                     }},
     .kind = SettingKind::Slider,
     .smin = engine::kCameraSpeedMin,
     .smax = engine::kCameraSpeedMax,
     .sstep = 0.10,
     .sfmt = "%.2f"},
    {.label = "settings.controls.bindings.label",
     .group = "menu.header.controller",
     .kind = SettingKind::Action,
     .action = SettingAction::Keybinds},
    {.label = "settings.controls.vibration.label",
     .group = "menu.header.controller",
     .binding = {.get =
                     [] {
                       return bd::engine::IsVibrationEnabled() ? 1.0 : 0.0;
                     },
                 .set =
                     [](double v) {
                       return bd::engine::SetVibration(v != 0.0);
                     }},
     .options = kOnOff,
     .count = OptCount(kOnOff)},
    {.label = "settings.controls.mouse_sensitivity.label",
     .group = "menu.header.keyboard_mouse",
     .binding = {.cvar = "hid.mnk_sensitivity"},
     .kind = SettingKind::Slider,
     .smin = 0.25,
     .smax = 10.0,
     .sstep = 0.25},
    {.label = "settings.controls.mouse_cursor_opacity.label",
     .group = "menu.header.keyboard_mouse",
     .binding = {.get =
                     [] {
                       return static_cast<double>(
                           bd::cvars::Get().mouse_cursor_opacity.Get());
                     },
                 .set =
                     [](double v) {
                       return bd::cvars::Get().mouse_cursor_opacity.Set(static_cast<i32>(v)).has_value();
                     }},
     .kind = SettingKind::Slider,
     .smin = static_cast<double>(engine::kMouseCursorOpacityMin),
     .smax = static_cast<double>(engine::kMouseCursorOpacityMax),
     .sstep = 5.0,
     .sfmt = "%.0f"},
};

// Order matches SettingsPage, which is also sidebar order.
constexpr SettingsPageTable kPages[kSettingsPageCount] = {
    {"settings.page.gameplay", kGameplaySettings,
     static_cast<int>(std::size(kGameplaySettings))},
    {"settings.page.display", kDisplaySettings,
     static_cast<int>(std::size(kDisplaySettings))},
    {"settings.page.graphics", kGraphicsSettings,
     static_cast<int>(std::size(kGraphicsSettings))},
    {"settings.page.audio", kAudioSettings,
     static_cast<int>(std::size(kAudioSettings))},
    {"settings.page.controls", kControlsSettings,
     static_cast<int>(std::size(kControlsSettings))},
};

} // namespace

const SettingsPageTable &SettingsRowTable(SettingsPage page) {
  return kPages[static_cast<int>(page)];
}

} // namespace bd
