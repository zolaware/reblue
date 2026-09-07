/**
 * @file    core/settings_rows.h
 * @brief   The row tables behind the config menu: what a row is, and how one
 *          binds to the value it shows.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "core/settings_model.h"

namespace bd {

enum class SettingKind { Buttons, Slider, Keybind, Action };

// The rows whose options are not known at compile time: Language narrows to
// what the install declared, VoiceLanguage to the voice tracks it carries.
enum class SettingSpecial { None, Language, VoiceLanguage, Monitor };

struct SettingOption {
  const char *text; // notation ("1280x720", "4x"), used when key is null
  double num;       // numeric form: matches the current value, and is what a
                    // Settings binding writes
  const char *value = nullptr;  // exact string passed to SetFlagByName, so a
                                // table backing Settings-bound rows leaves it
                                // unset rather than carrying a dead string
  const char *value2 = nullptr; // paired second-cvar value, name path only
  const char *key = nullptr;    // catalog key, wins over text
  double num2 = 0;              // paired second value for a Settings binding
};

// Language rows match by code, not by number, so num carries the BD locale id.

// A row reads and writes through one binding. A reblue-owned value binds to a
// Settings accessor pair, because the module that owns it owns what the setting
// means. An SDK-owned value binds to a cvar name, because rexglue owns it and
// reblue cannot put a facade on it from here. As rexglue grows its own facades,
// rows move from the second form to the first.
//
// A row reads through get, or through cvar when it has no get. It writes
// through exactly one of four forms, which WriteValue resolves in this order:
// setPair, setText, set, cvar. SettingRow two is not a merge, it is the earlier
// one winning and the later one never firing.
struct SettingBinding {
  double (*get)() = nullptr;
  bool (*set)(double) = nullptr;
  bool (*setPair)(double, double) = nullptr; // two values, one row
  bool (*setText)(const char *) = nullptr;   // string-valued rows (Language)
  const char *cvar = "";       // set instead of get/set for SDK-owned values
  const char *cvar2 = nullptr; // paired SDK cvar (window_width + window_height)
};

// label and group are catalog keys, not text.
struct SettingRow {
  const char *label;
  // The titled section this row sits under. Rows naming the same section have
  // to be adjacent: the run is what the section is. Null leaves the row
  // untitled, as the keybind page wants, its own screen drawing the sections
  // itself.
  const char *group = nullptr;
  SettingBinding binding;
  SettingKind kind = SettingKind::Buttons;
  const SettingOption *options = nullptr; // kind == Buttons
  int count = 0;                   // kind == Buttons
  bool restart = false;
  double smin = 0, smax = 0, sstep = 0; // kind == Slider
  const char *sfmt = "%.2f";            // slider value/write format
  bool sliderUi = false;   // Buttons row rendered as a stepped slider
  bool kbGated = false;    // grayed-out while mnk_mode is off
  bool mouseGated = false; // grayed-out while mnk_mouse is off
  bool windowedGated = false;
  // Grays out one option while the row itself stays active. Cycling steps over
  // a grayed-out option rather than stopping on it.
  bool (*optionDisabled)(const SettingOption &) = nullptr;
  SettingSpecial special = SettingSpecial::None;
  // The value is one of the stock game options, so it participates in the
  // per-save override.
  bool saveScoped = false;
  // The disc does not have this row at all. The guest's own removal pass is
  // Camp__Config__MainTask__BuildRows and these predicates mirror its
  // conditions.
  bool (*hidden)() = nullptr;
  SettingAction action = SettingAction::None;
  // Keybind rows: the pad button this row's key presses, as an anime_input
  // Button value. Set only where the engine's action table gives the button a
  // name worth more than the letter on it, so the row can be labeled by what it
  // does. Negative means label the row by its own string.
  int padButton = -1;
};

struct SettingsPageTable {
  const char *label;
  const SettingRow *items;
  int count;
};

// The rows of one page, in sidebar order.
const SettingsPageTable &SettingsRowTable(SettingsPage page);

} // namespace bd
