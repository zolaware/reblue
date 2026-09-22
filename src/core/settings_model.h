/**
 * @file    core/settings_model.h
 * @brief   Config menu row model: pages, rows, options and their bindings.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <string>

#include <rex/types.h>

namespace bd::engine {
enum class Action : int;
enum class ActionContext : u8;
} // namespace bd::engine

namespace bd {

enum class SettingsPage : int {
  Gameplay = 0,
  Display = 1,
  Graphics = 2,
  Audio = 3,
  Controls = 4,
};
inline constexpr int kSettingsPageCount = 5;
inline constexpr int kSettingsSectionCount = 5; // pages shown in the sidebar


// How a row is rendered and driven.
enum class RowUi : int {
  Buttons,     // horizontal strip of value buttons
  Slider,      // continuous fill-bar slider
  SliderSteps, // discrete options presented as a fill-bar slider
  Action,
};

enum class SettingAction { None, Keybinds };

const char *SettingsPageLabel(SettingsPage page);

// Rows this disc actually has. A row the locale drops (Text Size outside
// JP/KR/TW/CN, Ruby outside JP) is absent from the count and from every index
// below, so an index is a position among the visible rows and never needs
// filtering by the caller.
size_t SettingsCount(SettingsPage page);
const char *SettingsLabel(SettingsPage page, int index);

// The visible index of the row carrying this label key, or -1 when the page
// does not have it or the locale dropped it. Lets a surface outside the config
// menu name the rows it wants instead of holding positions that move.
int SettingsFindRow(SettingsPage page, const char *label);

size_t SettingsSlotCount(SettingsPage page);

// Section title at a slot, empty for a slot carrying a row.
const char *SettingsSlotHeader(SettingsPage page, int slot);

// The row a slot carries, or -1 for a section title.
int SettingsSlotToRow(SettingsPage page, int slot);

// Where a row is drawn.
int SettingsRowToSlot(SettingsPage page, int row);

// True when the row's value belongs to the stock game options, and so
// participates in the per-save override.
bool SettingsSaveScoped(SettingsPage page, int index);

// Grays out every restart-bound row, for a surface that cannot act on one.
// The in-game menu sets this, the title screen clears it. The rows stay in
// the counts and indices above, so both surfaces page identically and the
// player can see what the title screen offers.
void SettingsDisableRestartRows(bool disable);

std::string SettingsValueText(SettingsPage page, int index);
bool SettingsRestartBound(SettingsPage page, int index);

RowUi SettingsRowUi(SettingsPage page, int index);

SettingAction SettingsRowAction(SettingsPage page, int index);

// True when any row on the page is restart-bound (footnote visibility).
bool SettingsPageHasRestart(SettingsPage page);

bool SettingsDisabled(SettingsPage page, int index);

// True when the row is a continuous slider (RowUi::Slider).
bool SettingsIsSlider(SettingsPage page, int index);

// Discrete rows (Buttons / SliderSteps): option list and selected index.
int SettingsOptionCount(SettingsPage page, int index);
const char *SettingsOptionText(SettingsPage page, int index, int option);
int SettingsSelectedOption(SettingsPage page, int index);

bool SettingsOptionDisabled(SettingsPage page, int index, int option);

// Slider rows (Slider / SliderSteps): current value as a 0..1 fraction for
// the fill bar.
double SettingsSliderFraction(SettingsPage page, int index);

// Continuous slider rows: value range for direct-set UIs.
double SettingsSliderMin(SettingsPage page, int index);
double SettingsSliderMax(SettingsPage page, int index);

// Step the setting's value one step in 'dir' (+1/-1): cycles button options
// (wrapping) or nudges a slider by its step (clamped). Returns true on change.
bool CycleSetting(SettingsPage page, int index, int dir);

// Set a discrete row directly to 'option'. Returns true on success.
bool SetSelectedOption(SettingsPage page, int index, int option);

// Set a continuous slider row directly to 'value' (clamped to the row's
// range). Returns true on success.
bool SetSliderValue(SettingsPage page, int index, double value);

enum class BindCell : u8 { Blank, Header, Button, AxisKey, MouseInput };

struct BindEntry {
  BindCell cell = BindCell::Blank;
  engine::ActionContext context{};
  engine::Action action{};
  int direction = -1;
};

inline constexpr int kBindKeyChips = 2;
inline constexpr int kBindPadChip = kBindKeyChips;
inline constexpr int kBindChipCount = kBindKeyChips + 1;

int BindGridRows();
BindEntry BindGridEntry(int slot);
std::string BindEntryLabel(const BindEntry &entry);
const char *BindRowLabel(engine::Action action);

std::string BindChipToken(const BindEntry &entry, int chip);
std::string BindChipLegend(const BindEntry &entry, int chip);
bool BindChipFixed(const BindEntry &entry, int chip);
bool BindChipAccepts(int chip, const std::string &token);

bool SetBindChip(const BindEntry &entry, int chip, const std::string &token,
                 engine::Action *conflict);
bool ClearBindChip(const BindEntry &entry, int chip);
bool ClearBindEntry(const BindEntry &entry);
bool ToggleMouseInput();
bool ResetAllKeybinds();

} // namespace bd
