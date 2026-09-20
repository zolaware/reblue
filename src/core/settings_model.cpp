/**
 * @file    core/settings_model.cpp
 * @brief   Config menu row model implementation.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/settings_model.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/settings_rows.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>

#include "engine/engine.h"
#include "platform/platform.h"
#include "ui/ui.h"

namespace bd {
namespace {

// An empty key stays empty: that is how a row with no hint is spelled.
const char *Localized(const char *key) {
  return (key && *key) ? i18n::Text(key).c_str() : "";
}

const char *OptionLabel(const SettingOption &o) {
  return o.key ? i18n::Text(o.key).c_str() : o.text;
}

const SettingsPageTable &Table(SettingsPage page) { return SettingsRowTable(page); }

bool s_disableRestart = false;

bool Shown(const SettingRow &s) { return !s.hidden || !s.hidden(); }

// An index is a position among the rows this disc has, so a locale-dropped row
// is not merely skipped by callers, it is not addressable at all.
const SettingRow *Find(SettingsPage page, int index) {
  if (index < 0)
    return nullptr;
  const auto &table = Table(page);
  for (int i = 0; i < table.count; ++i) {
    if (!Shown(table.items[i]))
      continue;
    if (index-- == 0)
      return &table.items[i];
  }
  return nullptr;
}

bool InRange(SettingsPage page, int index) {
  return Find(page, index) != nullptr;
}

// A page's drawn slots: the section titles its rows name, interleaved with the
// rows themselves. Rebuilt whenever the locale drops or restores a row, which
// is the only thing that moves them.
struct PageSlots {
  static constexpr int kMaxSlots = 48;
  const char *header[kMaxSlots] = {}; // section key, null on a row slot
  int row[kMaxSlots] = {};            // model index, -1 on a title
  int slot[kMaxSlots] = {};           // model index to slot
  int count = 0;
  int rows = -1; // forces the first build
};

const PageSlots &Slots(SettingsPage page) {
  static PageSlots cache[kSettingsPageCount];
  PageSlots &p = cache[static_cast<int>(page)];
  const SettingsPageTable &table = Table(page);
  const int rows = static_cast<int>(SettingsCount(page));
  if (p.rows == rows)
    return p;

  p.count = 0;
  const char *open = nullptr;
  int row = 0;
  for (int i = 0; i < table.count && p.count + 1 < PageSlots::kMaxSlots; ++i) {
    const SettingRow &s = table.items[i];
    if (!Shown(s))
      continue;
    if (s.group && (!open || std::strcmp(s.group, open) != 0)) {
      open = s.group;
      p.header[p.count] = s.group;
      p.row[p.count] = -1;
      ++p.count;
    }
    p.header[p.count] = nullptr;
    p.row[p.count] = row;
    p.slot[row] = p.count;
    ++p.count;
    ++row;
  }
  p.rows = rows;
  return p;
}

const SettingRow &At(SettingsPage page, int index) { return *Find(page, index); }

bool MnkEnabled() { return rex::cvar::GetFlagByName("mnk_mode") == "true"; }

bool MouseEnabled() { return rex::cvar::GetFlagByName("mnk_mouse") == "true"; }

bool Windowed() { return rex::cvar::GetFlagByName("fullscreen") != "true"; }

// Menu spelling of one bind token. The cvar stores rex's canonical key names
// (rex::ui::ParseVirtualKey), which read as words rather than as the key legend
// a player is looking for, so punctuation shows its printed character and the
// long names shorten to what fits on a keycap.
struct KeyAlias {
  const char *name;
  const char *legend;
};

constexpr KeyAlias kKeyAliases[] = {
    {"Backtick", "`"},
    {"Minus", "-"},
    {"Plus", "="},
    {"Comma", ","},
    {"Period", "."},
    {"Semicolon", ";"},
    {"Slash", "/"},
    {"Backslash", "\\"},
    {"LBracket", "["},
    {"RBracket", "]"},
    {"Quote", "'"},
    {"Return", "Enter"},
    {"Backspace", "Bksp"},
    {"Escape", "Esc"},
    {"Delete", "Del"},
    {"Insert", "Ins"},
    {"PageUp", "PgUp"},
    {"PageDown", "PgDn"},
    {"CapsLock", "Caps"},
    {"NumLock", "NumLk"},
    {"ScrollLock", "ScrLk"},
    {"PrintScreen", "PrtSc"},
    {"Numpad0", "Num 0"},
    {"Numpad1", "Num 1"},
    {"Numpad2", "Num 2"},
    {"Numpad3", "Num 3"},
    {"Numpad4", "Num 4"},
    {"Numpad5", "Num 5"},
    {"Numpad6", "Num 6"},
    {"Numpad7", "Num 7"},
    {"Numpad8", "Num 8"},
    {"Numpad9", "Num 9"},
    {"NumpadPlus", "Num +"},
    {"NumpadMinus", "Num -"},
    {"NumpadStar", "Num *"},
    {"NumpadSlash", "Num /"},
    {"NumpadEnter", "Num Enter"},
    {"PadA", "A"},
    {"PadB", "B"},
    {"PadX", "X"},
    {"PadY", "Y"},
    {"PadLB", "LB"},
    {"PadRB", "RB"},
    {"PadLT", "LT"},
    {"PadRT", "RT"},
    {"PadStart", "Start"},
    {"PadBack", "Back"},
    {"PadLS", "L3"},
    {"PadRS", "R3"},
    {"PadDUp", "Pad Up"},
    {"PadDDown", "Pad Down"},
    {"PadDLeft", "Pad Left"},
    {"PadDRight", "Pad Right"},
    {"LStick", "L Stick"},
    {"RStick", "R Stick"},
    {"LStickUp", "L Up"},
    {"LStickDown", "L Down"},
    {"LStickLeft", "L Left"},
    {"LStickRight", "L Right"},
    {"RStickUp", "R Up"},
    {"RStickDown", "R Down"},
    {"RStickLeft", "R Left"},
    {"RStickRight", "R Right"},
    {"WheelUp", "Wheel Up"},
    {"WheelDown", "Wheel Dn"},
    {"MouseXY", "Mouse"},
};

constexpr engine::ActionContext kBindPages[kBindPageCount] = {
    engine::ActionContext::Field, engine::ActionContext::Menu,
    engine::ActionContext::Mechat, engine::ActionContext::System};

constexpr const char *kBindPageKeys[kBindPageCount] = {
    "settings.binds.field", "settings.binds.menu", "settings.binds.mechat",
    "settings.binds.system"};

int BindPageIndex(int page) {
  return (page < 0 || page >= kBindPageCount) ? 0 : page;
}

std::string KeyDisplay(const std::string &token) {
  // A bind on the '+' key is spelled Plus, so a '+' can only be a separator.
  size_t sep = token.find_last_of('+');
  std::string key = sep == std::string::npos ? token : token.substr(sep + 1);
  for (const KeyAlias &a : kKeyAliases) {
    if (key == a.name)
      return (sep == std::string::npos ? std::string()
                                       : token.substr(0, sep + 1)) +
             a.legend;
  }
  return token;
}

// === Language list (SettingSpecial::Language) ===
// Narrows to the languages BD's boot config declared, which BD only parses once
// the guest boots.

constexpr u32 kNoLocale = 0xFFFFFFFFu;

// The language setting holds a boot config code, and platform accepts the ISO
// aliases ('en', 'ja', 'ko', 'pt') that BD's own codes do not.
u32 LocaleOfCode(std::string_view code) {
  const u32 xlang = platform::XLanguageFromCode(code);
  return xlang ? engine::Locale::FromXLanguage(xlang).Id() : kNoLocale;
}

struct LangList {
  SettingOption opts[1 + engine::kLocaleCount] = {};
  std::string
      codes[1 + engine::kLocaleCount]; // stable storage for SettingOption::value
  std::string autoText;                // composed, so it is text and not a key
  int count = 0;
  bool builtValid = false; // built from a parsed boot config
  std::string builtCode;
  u32 builtLocale = 0;
};

// The catalog key naming a locale, held once per locale so an option can point
// its key at it and read the name the catalog already carries.
const char *LocaleKey(u32 locale) {
  if (locale >= engine::kLocaleCount)
    return "";
  static std::string keys[engine::kLocaleCount];
  std::string &key = keys[locale];
  if (key.empty())
    key = std::string("locale.") + engine::Locale(locale).CodeLower();
  return key.c_str();
}

const LangList &Langs() {
  static LangList list;
  const std::string cur = Settings::Get().Language();
  const u32 uiLocale = i18n::CurrentLocale();
  if (list.builtValid && list.builtCode == cur && list.builtLocale == uiLocale)
    return list;

  const engine::Language language;
  // Before the guest has parsed the boot config there is nothing to narrow
  // by, so offer everything rather than an empty row.
  u32 mask =
      language ? language.AvailableMask() : (1u << engine::kLocaleCount) - 1;
  const u32 curLocale = LocaleOfCode(cur);
  if (curLocale != kNoLocale)
    mask |= 1u << curLocale; // never hide what is already set

  // Auto reports the locale the guest actually latched, so a boot config
  // fallback shows up instead of the language that was asked for.
  const u32 autoLocale =
      language
          ? language.Current().Id()
          : engine::Locale::FromXLanguage(platform::DetectOsXLanguage()).Id();
  list.autoText = i18n::Fmt("settings.gameplay.ui_language.auto",
                            i18n::Text(LocaleKey(autoLocale)));
  list.codes[0] = "auto";
  list.opts[0] = {
      .text = list.autoText.c_str(), .num = 0, .value = list.codes[0].c_str()};
  list.count = 1;

  for (u32 i = 0; i < engine::kLocaleCount; ++i) {
    if (!(mask & (1u << i)))
      continue;
    list.codes[list.count] = engine::Locale(i).CodeLower();
    list.opts[list.count] = {.num = static_cast<double>(i),
                             .value = list.codes[list.count].c_str(),
                             .key = LocaleKey(i)};
    ++list.count;
  }

  list.builtValid = static_cast<bool>(language);
  list.builtCode = cur;
  list.builtLocale = uiLocale;
  return list;
}

// === Voice list (SettingSpecial::VoiceLanguage) ===
// bd_boot.ini's [Voice] section, which BD numbers from one and the game itself
// offers on its load screen alone. The tracks are named by the locale
// names the catalog already carries, so the options hold keys and the list has
// nothing to rebuild when the UI language changes.

struct VoiceList {
  SettingOption opts[engine::kLocaleCount] = {};
  int count = 0;
  int builtCount = -1;
};

const VoiceList &Voices() {
  static VoiceList list;
  const engine::Language language;
  const int voices = std::min(static_cast<int>(language.VoiceCount()),
                              static_cast<int>(engine::kLocaleCount));
  if (list.builtCount == voices)
    return list;

  list.count = 0;
  for (int type = 1; type <= voices; ++type) {
    const u32 voice = language.VoiceLocale(type).Id();
    if (voice >= engine::kLocaleCount)
      continue;
    list.opts[list.count++] = {.num = static_cast<double>(type),
                               .key = LocaleKey(voice)};
  }

  list.builtCount = voices;
  return list;
}

// === Monitor list (SettingSpecial::Monitor) ===
// Numbered the way the SDK's monitor cvar numbers displays: zero is whichever
// one the host would have picked, then its enumeration order from one.

struct MonitorList {
  // The monitor cvar's own ceiling.
  static constexpr int kMaxDisplays = 16;
  SettingOption opts[1 + kMaxDisplays] = {};
  std::string texts[1 + kMaxDisplays]; // stable storage for SettingOption::text
  std::string values[1 + kMaxDisplays];
  int count = 0;
  int builtDisplays = -1;
};

const MonitorList &Monitors() {
  static MonitorList list;
  const int displays =
      std::min(platform::DisplayCount(), MonitorList::kMaxDisplays);
  if (list.builtDisplays == displays)
    return list;

  list.values[0] = "0";
  list.opts[0] = {.num = 0, .value = list.values[0].c_str(), .key = "opt.auto"};
  list.count = 1;

  for (int i = 1; i <= displays; ++i) {
    const std::string name = platform::DisplayName(i);
    list.texts[i] = name.empty()
                        ? std::to_string(i)
                        : std::to_string(i) + ": " + name;
    list.values[i] = std::to_string(i);
    list.opts[i] = {.text = list.texts[i].c_str(),
                    .num = static_cast<double>(i),
                    .value = list.values[i].c_str()};
    ++list.count;
  }

  list.builtDisplays = displays;
  return list;
}

// One display leaves the row nothing to choose between, so it grays out the
// way a keybind row does without its mode.
bool SingleDisplay() { return platform::DisplayCount() <= 1; }

// A row's option list: static for every row but the two language rows and
// Monitor, whose options depend on what the install and the host declare.
struct RowOpts {
  const SettingOption *opts;
  int count;
};

RowOpts OptsOf(const SettingRow &s) {
  if (s.special == SettingSpecial::Language) {
    const LangList &l = Langs();
    return {l.opts, l.count};
  }
  if (s.special == SettingSpecial::VoiceLanguage) {
    const VoiceList &v = Voices();
    return {v.opts, v.count};
  }
  if (s.special == SettingSpecial::Monitor) {
    const MonitorList &m = Monitors();
    return {m.opts, m.count};
  }
  return {s.options, s.count};
}

double CurrentNum(const SettingRow &s) {
  if (s.binding.get)
    return s.binding.get();
  std::string v = rex::cvar::GetFlagByName(s.binding.cvar);
  if (v == "true")
    return 1.0;
  if (v == "false")
    return 0.0;
  return std::strtod(v.c_str(), nullptr);
}

int IndexForValue(const SettingRow &s, double cur) {
  const RowOpts o = OptsOf(s);
  int best = 0;
  double bestDiff = 1e30;
  for (int i = 0; i < o.count; ++i) {
    double diff = o.opts[i].num - cur;
    if (diff < 0)
      diff = -diff;
    if (diff < bestDiff) {
      bestDiff = diff;
      best = i;
    }
  }
  return best;
}

int CurrentIndex(const SettingRow &s) {
  if (s.special == SettingSpecial::Language) {
    const u32 locale = LocaleOfCode(Settings::Get().Language());
    if (locale == kNoLocale)
      return 0; // 'auto', empty, or a code nothing recognizes
    const RowOpts o = OptsOf(s);
    for (int i = 1; i < o.count; ++i)
      if (static_cast<u32>(o.opts[i].num) == locale)
        return i;
    return 0;
  }
  return IndexForValue(s, CurrentNum(s));
}

bool OptionDisabled(const SettingRow &s, const SettingOption &o) {
  return s.optionDisabled && s.optionDisabled(o);
}

// Continuous slider rows. The row's own format decides the precision on both
// paths: the name path writes the formatted text, and the binding path hands
// on what that text parses back to, so a setter that narrows to an integer
// rounds the way the displayed value does instead of truncating.
bool WriteSlider(const SettingRow &s, double value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), s.sfmt, value);
  if (s.binding.set)
    return s.binding.set(std::strtod(buf, nullptr));
  if (!rex::cvar::SetFlagByName(s.binding.cvar, buf)) {
    BD_WARN("[config] failed to set {} = {}", s.binding.cvar, buf);
    return false;
  }
  BD_DEBUG("[config] {} = {}", s.binding.cvar, buf);
  return true;
}

// Discrete rows: a reblue row hands the option's number to the module that
// owns the value, an SDK row writes the option's text by name.
bool WriteValue(const SettingRow &s, const SettingOption &o) {
  if (s.binding.setPair)
    return s.binding.setPair(o.num, o.num2);
  if (s.binding.setText)
    return s.binding.setText(o.value);
  if (s.binding.set)
    return s.binding.set(o.num);
  if (!rex::cvar::SetFlagByName(s.binding.cvar, o.value)) {
    BD_WARN("[config] failed to set {} = {}", s.binding.cvar, o.value);
    return false;
  }
  if (s.binding.cvar2 && o.value2)
    rex::cvar::SetFlagByName(s.binding.cvar2, o.value2);
  BD_DEBUG("[config] {} = {}", s.binding.cvar, OptionLabel(o));
  return true;
}

int NextEnabledOption(const SettingRow &s, int cur, int dir) {
  const RowOpts o = OptsOf(s);
  int next = cur;
  for (int i = 0; i < o.count; ++i) {
    next += dir;
    if (s.sliderUi) {
      // Stepped sliders clamp at the ends instead of wrapping.
      if (next < 0 || next >= o.count)
        return cur;
    } else if (next < 0) {
      next = o.count - 1;
    } else if (next >= o.count) {
      next = 0;
    }
    if (!OptionDisabled(s, o.opts[next]))
      return next;
  }
  return cur;
}

} // namespace

const char *SettingsPageLabel(SettingsPage page) {
  return Localized(Table(page).label);
}

size_t SettingsCount(SettingsPage page) {
  const auto &table = Table(page);
  size_t n = 0;
  for (int i = 0; i < table.count; ++i)
    n += Shown(table.items[i]) ? 1 : 0;
  return n;
}

int SettingsFindRow(SettingsPage page, const char *label) {
  const auto &table = Table(page);
  int index = 0;
  for (int i = 0; i < table.count; ++i) {
    if (!Shown(table.items[i]))
      continue;
    if (std::strcmp(table.items[i].label, label) == 0)
      return index;
    ++index;
  }
  return -1;
}

size_t SettingsSlotCount(SettingsPage page) {
  return static_cast<size_t>(Slots(page).count);
}

const char *SettingsSlotHeader(SettingsPage page, int slot) {
  const PageSlots &p = Slots(page);
  if (slot < 0 || slot >= p.count || !p.header[slot])
    return "";
  return Localized(p.header[slot]);
}

int SettingsSlotToRow(SettingsPage page, int slot) {
  const PageSlots &p = Slots(page);
  return (slot < 0 || slot >= p.count) ? -1 : p.row[slot];
}

int SettingsRowToSlot(SettingsPage page, int row) {
  const PageSlots &p = Slots(page);
  return (row < 0 || row >= p.rows) ? -1 : p.slot[row];
}

const char *SettingsLabel(SettingsPage page, int index) {
  if (!InRange(page, index))
    return "";
  return Localized(At(page, index).label);
}

void SettingsDisableRestartRows(bool disable) { s_disableRestart = disable; }

bool SettingsSaveScoped(SettingsPage page, int index) {
  return InRange(page, index) && At(page, index).saveScoped;
}

std::string SettingsValueText(SettingsPage page, int index) {
  if (!InRange(page, index))
    return "";
  const SettingRow &s = At(page, index);
  if (s.kind == SettingKind::Slider) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), s.sfmt, CurrentNum(s));
    return buf;
  }
  if (s.kind == SettingKind::Action)
    return "";
  return OptionLabel(OptsOf(s).opts[CurrentIndex(s)]);
}

engine::ActionContext BindPageContext(int page) {
  return kBindPages[BindPageIndex(page)];
}

const char *BindPageLabel(int page) {
  return Localized(kBindPageKeys[BindPageIndex(page)]);
}

size_t BindRowCount(engine::ActionContext context) {
  size_t rows = 0;
  for (int i = 0; i < engine::kActionCount; ++i)
    if (engine::Describe(static_cast<engine::Action>(i)).context == context)
      ++rows;
  return rows;
}

engine::Action BindRowAction(engine::ActionContext context, int index) {
  for (int i = 0; i < engine::kActionCount; ++i) {
    const auto action = static_cast<engine::Action>(i);
    if (engine::Describe(action).context != context)
      continue;
    if (index-- == 0)
      return action;
  }
  return static_cast<engine::Action>(0);
}

const char *BindRowLabel(engine::Action action) {
  return Localized(engine::Describe(action).labelKey);
}

std::string SettingsKeybindToken(engine::Action action, int slot) {
  const std::vector<engine::Source> &sources =
      engine::Bindings::Get().Sources(action);
  if (slot < 0 || static_cast<size_t>(slot) >= sources.size())
    return {};
  std::string token;
  if (!engine::FormatSource(sources[static_cast<size_t>(slot)], token))
    return {};
  return token;
}

std::string SettingsKeybindAlt(engine::Action action, int slot) {
  const std::string token = SettingsKeybindToken(action, slot);
  return token.empty() ? std::string() : KeyDisplay(token);
}

RowUi SettingsRowUi(SettingsPage page, int index) {
  if (!InRange(page, index))
    return RowUi::Buttons;
  const SettingRow &s = At(page, index);
  switch (s.kind) {
  case SettingKind::Slider:
    return RowUi::Slider;
  case SettingKind::Action:
    return RowUi::Action;
  default:
    return s.sliderUi ? RowUi::SliderSteps : RowUi::Buttons;
  }
}

SettingAction SettingsRowAction(SettingsPage page, int index) {
  return InRange(page, index) ? At(page, index).action : SettingAction::None;
}

bool SettingsRestartBound(SettingsPage page, int index) {
  return InRange(page, index) && At(page, index).restart;
}

bool SettingsPageHasRestart(SettingsPage page) {
  const SettingsPageTable &table = Table(page);
  for (int i = 0; i < table.count; ++i)
    if (table.items[i].restart && Shown(table.items[i]))
      return true;
  return false;
}

bool SettingsDisabled(SettingsPage page, int index) {
  if (!InRange(page, index))
    return false;
  const SettingRow &s = At(page, index);
  return (s.restart && s_disableRestart) || (s.kbGated && !MnkEnabled()) ||
         (s.mouseGated && !(MnkEnabled() && MouseEnabled())) ||
         (s.windowedGated && !Windowed()) ||
         (s.special == SettingSpecial::Monitor && SingleDisplay());
}

bool SettingsIsSlider(SettingsPage page, int index) {
  return InRange(page, index) && At(page, index).kind == SettingKind::Slider;
}

int SettingsOptionCount(SettingsPage page, int index) {
  return InRange(page, index) ? OptsOf(At(page, index)).count : 0;
}

const char *SettingsOptionText(SettingsPage page, int index, int option) {
  if (!InRange(page, index))
    return "";
  const RowOpts o = OptsOf(At(page, index));
  if (option < 0 || option >= o.count)
    return "";
  return OptionLabel(o.opts[option]);
}

int SettingsSelectedOption(SettingsPage page, int index) {
  if (!InRange(page, index) || At(page, index).kind != SettingKind::Buttons)
    return 0;
  return CurrentIndex(At(page, index));
}

bool SettingsOptionDisabled(SettingsPage page, int index, int option) {
  if (!InRange(page, index))
    return false;
  const SettingRow &s = At(page, index);
  const RowOpts o = OptsOf(s);
  if (option < 0 || option >= o.count)
    return false;
  return OptionDisabled(s, o.opts[option]);
}

double SettingsSliderFraction(SettingsPage page, int index) {
  if (!InRange(page, index))
    return 0.0;
  const SettingRow &s = At(page, index);
  if (s.kind == SettingKind::Buttons && s.sliderUi) {
    const int count = OptsOf(s).count;
    if (count < 2)
      return 0.0;
    return static_cast<double>(CurrentIndex(s)) / (count - 1);
  }
  if (s.kind != SettingKind::Slider || s.smax <= s.smin)
    return 0.0;
  double f = (CurrentNum(s) - s.smin) / (s.smax - s.smin);
  return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

double SettingsSliderMin(SettingsPage page, int index) {
  if (!InRange(page, index))
    return 0.0;
  const SettingRow &s = At(page, index);
  return s.kind == SettingKind::Slider ? s.smin : 0.0;
}

double SettingsSliderMax(SettingsPage page, int index) {
  if (!InRange(page, index))
    return 0.0;
  const SettingRow &s = At(page, index);
  return s.kind == SettingKind::Slider ? s.smax : 0.0;
}

bool CycleSetting(SettingsPage page, int index, int dir) {
  if (!InRange(page, index))
    return false;
  const SettingRow &s = At(page, index);
  if (s.kind == SettingKind::Action || SettingsDisabled(page, index))
    return false;

  if (s.kind == SettingKind::Slider) {
    double cur = CurrentNum(s);
    double next = cur + (dir > 0 ? s.sstep : -s.sstep);
    if (next < s.smin)
      next = s.smin;
    else if (next > s.smax)
      next = s.smax;
    if (next == cur)
      return false;
    return WriteSlider(s, next);
  }

  const RowOpts o = OptsOf(s);
  const int cur = CurrentIndex(s);
  const int next = NextEnabledOption(s, cur, dir > 0 ? 1 : -1);
  if (next == cur)
    return false;
  return WriteValue(s, o.opts[next]);
}

bool SetSelectedOption(SettingsPage page, int index, int option) {
  if (!InRange(page, index))
    return false;
  const SettingRow &s = At(page, index);
  if (s.kind != SettingKind::Buttons || SettingsDisabled(page, index))
    return false;
  const RowOpts o = OptsOf(s);
  if (option < 0 || option >= o.count)
    return false;
  if (OptionDisabled(s, o.opts[option]))
    return false;
  return WriteValue(s, o.opts[option]);
}

bool SetSliderValue(SettingsPage page, int index, double value) {
  if (!InRange(page, index))
    return false;
  const SettingRow &s = At(page, index);
  if (s.kind != SettingKind::Slider || SettingsDisabled(page, index))
    return false;
  double next = value;
  if (next < s.smin)
    next = s.smin;
  else if (next > s.smax)
    next = s.smax;
  return WriteSlider(s, next);
}

bool SetKeybind(engine::Action action, int slot, const std::string &token,
                engine::Action *conflict) {
  if (conflict)
    *conflict = action;

  engine::Source source;
  if (!engine::ParseSource(token, source)) {
    BD_WARN("[config] '{}' is not a bindable input", token);
    return false;
  }

  if (const auto other = engine::Bindings::Get().Conflict(action, source)) {
    if (conflict)
      *conflict = *other;
    BD_DEBUG("[config] {} already owns {}", engine::ToString(*other), token);
    return false;
  }

  if (!engine::Bindings::Get().SetSource(action, slot, source)) {
    BD_WARN("[config] failed to bind {} to {}", token,
            engine::ToString(action));
    return false;
  }
  BD_DEBUG("[config] {}[{}] = {}", engine::ToString(action), slot, token);
  return true;
}

bool ClearKeybindSlot(engine::Action action, int slot) {
  engine::Bindings &binds = engine::Bindings::Get();
  std::vector<engine::Source> kept = binds.Sources(action);
  if (slot < 0 || static_cast<size_t>(slot) >= kept.size())
    return false;
  kept.erase(kept.begin() + slot);
  if (!binds.ClearSources(action))
    return false;
  for (size_t i = 0; i < kept.size(); ++i)
    binds.SetSource(action, static_cast<int>(i), kept[i]);
  BD_DEBUG("[config] {} slot {} cleared", engine::ToString(action), slot);
  return true;
}

bool ClearKeybind(engine::Action action) {
  if (!engine::Bindings::Get().ClearSources(action)) {
    BD_WARN("[config] failed to clear {}", engine::ToString(action));
    return false;
  }
  BD_DEBUG("[config] {} cleared", engine::ToString(action));
  return true;
}

bool ResetKeybinds(engine::ActionContext context) {
  return engine::Bindings::Get().ResetContext(context);
}

} // namespace bd
