/**
 * @file    engine/menus/config_layout.h
 * @brief       Config menu layout definitions using AnimeLayout DSL.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include "core/settings_model.h"
#include "engine/d2anime/d2anime.h"

#include <iterator>
#include <type_traits>

#include <rex/types.h>

namespace bd::engine {

// The config menu borrows the camp settings menu's icon and slider bar sheets.
// A tex path is resolved relative to the CSV that names it, so our CSVs (served
// at d2anime\modmgr) cannot reach them as 'res\...'. The leading ':' is the
// engine's own disc root escape, used the same way by the game's
// d2anime\camp\top\M_top_cfg.csv to reach d2anime\camp\cfg\res. Extension is
// implied, as everywhere else.
inline constexpr const char *kMarkConfigTex =
    ":d2anime\\camp\\cfg\\res\\mark_config";
inline constexpr const char *kBarConfig01Tex =
    ":d2anime\\camp\\cfg\\res\\bar_config01";
inline constexpr const char *kBarConfig02Tex =
    ":d2anime\\camp\\cfg\\res\\bar_config02";
inline constexpr const char* kSlideConfigTex =
    ":d2anime\\camp\\cfg\\res\\slide_config";

// Sidebar row pitch. AnimeMenu_CalcItemPosition derives the stride from this
// height and the row count, so it has to track kSectionCount or every row
// re-spaces. 40 on a 6px gap ends nine rows at 548, clear of the
// row description line at 568.
inline constexpr int kSectionRowH = 40;
inline constexpr int kSectionRowGap = 6;

// Sidebar button template (l_modmgr_section.csv). The cell fills the sidebar
// row exactly: a shorter window leaves the engine cursor frame hanging past the
// button edge, and a taller one overlaps the row below. The narrower cell (vs
// the content column) is what keeps the content list's selection cursor from
// bouncing into the sidebar.
class SectionTemplate : public RowTemplate {
public:
  SectionTemplate();

protected:
  AnimeWindow rowWindow() const override;
  AnimeMessage rowLabel() const override;
};

// Toggle row template shared by the mod list (l_modmgr_info.csv) and the DLC
// list (l_modmgr_dlcinfo.csv). The two differ only in label, row width and
// where the checkbox sits.
class ListItemTemplate : public ToggleRowTemplate {
public:
  ListItemTemplate(const char *defaultName, int rowWidth, int checkX);

protected:
  void buildValue(CsvBuilder &b) override;

private:
  int checkX_;
};

class LanguageItemTemplate : public RowTemplate {
public:
  LanguageItemTemplate() : RowTemplate(110, 121, 470, 45) {
    name.set("Language");
    wndType.set("BTN01_OF");
  }
};

// DLC detail panel template (l_modmgr_dlcdetail.csv).
class DlcDetailTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};

  StringV descLines[3]{{"Desc0", ""}, {"Desc1", ""}, {"Desc2", ""}};
  static constexpr int kDescLines =
      static_cast<int>(std::extent_v<decltype(descLines)>);

  ColorV labelColor{"LabelColor", 255, 255, 255, 255};
  ColorV valueColor{"ValueColor", 255, 255, 255, 255};

  void build(CsvBuilder &b) override;

private:
  AnimePos pos_{"pos", 815, 140, 400, 450};
};

// Shown when no mods installed (l_modmgr_nodata.csv).
class NodataTemplate : public RowTemplate {
public:
  NodataTemplate() : RowTemplate(110, 121, 420, 45) {}

protected:
  bool hasWindow() const override { return false; }
  AnimeMessage rowLabel() const override;
};

// Detail panel template (l_modmgr_detail.csv).
class DetailTemplate : public AnimeLayout {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  FloatV hasPreview{"HasPreview", -1.0};
  StringV previewFile{"PreviewFile", kMarkConfigTex};

  StringV author{"Author", ""};
  StringV version{"Version", ""};
  StringV created{"Created", ""};
  StringV desc{"Desc", ""};

  ColorV labelColor{"LabelColor", 255, 255, 255, 255};
  ColorV valueColor{"ValueColor", 255, 255, 255, 255};

  void build(CsvBuilder &b) override;

private:
  AnimePos pos_{"pos", 815, 140, 400, 450};
};

// Per-option variable names of a settings row. The CSV declares them and
// ConfigMenu::RefreshSettingsVisuals writes them every frame, and nothing
// checks that the two spellings agree, so both sides read this one table.
struct SettingOptVars {
  const char *vis, *name, *wnd, *dim;
};

#define BD_SETTING_OPT_VARS(n)                                                 \
  { "Opt" #n "Vis", "Opt" #n "Name", "Opt" #n "Wnd", "Opt" #n "Dim" }
inline constexpr SettingOptVars kSettingOptVars[] = {
    BD_SETTING_OPT_VARS(0), BD_SETTING_OPT_VARS(1), BD_SETTING_OPT_VARS(2),
    BD_SETTING_OPT_VARS(3), BD_SETTING_OPT_VARS(4)};
#undef BD_SETTING_OPT_VARS

// Row pitch of a settings page. 34 fits twelve rows between the list top at
// y=140 and the row description line at 568, enough for the longest page
// (Gameplay).
inline constexpr int kSettingRowH = 34;

// Width of a full-width page list and its rows. The engine's scrollbar draws
// 15 past the extent and 10 wide, so 325 + 920 + 25 keeps it on the
// 1280-wide screen.
inline constexpr int kSettingsPageW = 920;

// Settings row template (l_modmgr_setting.csv), modeled on BD's camp config
// (d2anime\camp\cfg\L_cfg01.csv): a left label plus one of a horizontal strip
// of value buttons, a fill bar slider (continuous or stepped over discrete
// options), or a single action button. Rows end in a thin camp-style divider
// line. Every value element is driven per-row by
// ConfigMenu::RefreshSettingsVisuals through the item var bag, and the engine
// drives WndType for the full-row cursor highlight.
class SettingItemTemplate : public RowTemplate {
public:
  SettingItemTemplate() : RowTemplate(110, 121, kSettingsPageW, kSettingRowH) {}

  static constexpr int kMaxOpts = static_cast<int>(std::size(kSettingOptVars));

  // Slider fill width spans [0, kSliderWidth], set by RefreshSettingsVisuals.
  // Stock game uses 300px max fill inside a 306px track.
  static constexpr int kSliderWidth = 300;

  // Snug cursor highlight width: row left edge through the last value
  // element, capped to the row width.
  static int RowWidth(bool slider, int optCount);

  // Which value button a row-local x hits, or -1 for the gaps between them
  // and the label to their left. Hit testing reads the same constants the
  // row is drawn from, so a click cannot fall outside the art.
  static int ButtonAt(f32 x, int optCount);

  // Where a row-local x sits along the slider track, 0 at the empty end and 1
  // at the full one. False when x is off the track entirely.
  static bool SliderFractionAt(f32 x, double &fraction);

protected:
  void declareVars(CsvBuilder &b) override;
  AnimeWindow rowWindow() const override;
  AnimeMessage rowLabel() const override;
  void buildValue(CsvBuilder &b) override;

private:
  static constexpr int kRowContentW = 930;
  static constexpr int kBtnX0 = 340, kBtnStride = 118, kBtnW = 113;
  static constexpr int kBtnY = 2, kBtnH = 30;
  // Window sits at 340 (w=400). Track is inset with 46px left padding and 306px width.
  static constexpr int kSldX = 340, kSldW = 400, kSldH = 32, kSldY = 1;
  static constexpr int kTrackX = kSldX + 46; // 386
  static constexpr int kTrackW = 306;
};

inline constexpr int kKeybindGridY = 104;
inline constexpr int kKeybindRowH = 25;
inline constexpr int kKeybindMaxRows = 20;
inline constexpr int kKeybindGridX = 40;
inline constexpr int kKeybindCellW = 590;
inline constexpr int kKeybindColStride = 610;

struct KeybindSlotVars {
  const char *comment;
  const char *wndVar, *wndDefault, *textVar, *colorVar;
  const char *capVar, *uvVar, *pairVar, *modUvVar;
};

inline constexpr KeybindSlotVars kKeybindSlotVars[] = {
    {"key button", "KeyWnd", "BTN01_OF", "Key", "KeyCol", "KeyCap", "KeyUv",
     "KeyPair", "KeyModUv"},
    {"second key button", "KeyWnd2", "NOWINDOW", "Key2", "KeyCol2", "KeyCap2",
     "KeyUv2", "KeyPair2", "KeyModUv2"},
    {"third key button", "KeyWnd3", "NOWINDOW", "Key3", "KeyCol3", "KeyCap3",
     "KeyUv3", "KeyPair3", "KeyModUv3"},
};

inline constexpr const char *kKeybindPadCapVar = "PadCap";
inline constexpr const char *kKeybindPadUvVar = "PadUv";

class KeybindItemTemplate : public RowTemplate {
public:
  KeybindItemTemplate() : RowTemplate(80, 0, kKeybindCellW, kKeybindRowH) {}

  static constexpr int kChipCount = kBindChipCount;

  static constexpr int ChipX(int index) {
    return kChipX0 + index * (kChipW + kChipGap) +
           (index >= kBindPadChip ? kPadGap : 0);
  }

  static constexpr int RowWidth() {
    return ChipX(kChipCount - 1) + kChipW + kRowPad;
  }

  static int ChipAt(f32 x);

protected:
  void declareVars(CsvBuilder &b) override;
  AnimeWindow rowWindow() const override;
  AnimeMessage rowLabel() const override;
  void buildValue(CsvBuilder &b) override;

private:
  static constexpr int kChipX0 = 290;
  static constexpr int kChipW = 90;
  static constexpr int kChipGap = 6;
  static constexpr int kPadGap = 10;
  static constexpr int kRowPad = 6;
};

static_assert(KeybindItemTemplate::RowWidth() <= kKeybindCellW);

// Main three-panel config menu (l_modmgr.csv).
class ConfigLayout : public AnimeLayout {
public:
  ConfigLayout();

  // The camp Config screen's own animated header icon, borrowed whole. A panel
  // path resolves relative to the CSV naming it, so this takes the engine's
  // disc root escape the same way kMarkConfigTex does.
  AnimePanel cfgIcon{.name = "cfg_icon",
                     .csvFile = ":d2anime\\camp\\cfg\\L_cfg_icon.csv",
                     .loop = 1};
  AnimePanel detail{"detail", "l_modmgr_detail.csv"};
  AnimePanel dlcDetail{"dlcdetail", "l_modmgr_dlcdetail.csv"};

  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV title{"Title", "re:Blue Configuration"};
  StringV hdrSections{"HdrSections", ""};
  StringV hdrMods{"HdrMods", ""};
  StringV hdrDetails{"HdrDetails", ""};
  StringV ftrA{"FtrA", ""};
  StringV ftrB{"FtrB", ""};
  StringV ftrY{"FtrY", ""};
  StringV ftrX{"FtrX", ""};
  StringV ftrBack{"FtrBack", ""};
  FloatV ftrAVis{"FtrAVis", 1.0};
  FloatV ftrXVis{"FtrXVis", -1.0};
  FloatV ftrYVis{"FtrYVis", -1.0};
  FloatV ftrBackVis{"FtrBackVis", -1.0};

  // Footer caps in per-layout element vars (see PromptGlyph): this layout is
  // on screen while the keybind page rebinds the very keys its caps show, so
  // a uv. snapshot would go stale in front of the player. ConfigMenu rewrites
  // these on every glyph generation. Standalone only, like the icons.
  static constexpr PromptGlyph kFooterGlyphs[] = {
      {"FtrAUv", {"FtrAUv.x", "FtrAUv.y", "FtrAUv.w", "FtrAUv.h"}, "Help_A_Uv"},
      {"FtrBUv", {"FtrBUv.x", "FtrBUv.y", "FtrBUv.w", "FtrBUv.h"}, "Help_B_Uv"},
      {"FtrXUv", {"FtrXUv.x", "FtrXUv.y", "FtrXUv.w", "FtrXUv.h"}, "Help_X_Uv"},
      {"FtrYUv", {"FtrYUv.x", "FtrYUv.y", "FtrYUv.w", "FtrYUv.h"}, "Help_Y_Uv"},
      {"FtrBackUv",
       {"FtrBackUv.x", "FtrBackUv.y", "FtrBackUv.w", "FtrBackUv.h"},
       "Help_BACK_Uv"},
  };
  // Yellow "Requires restart" footnote naming the color the restart-bound rows
  // are drawn in. Shown on settings pages that have one.
  FloatV restartVis{"RestartVis", -1.0};
  StringV restartNote{"RestartNote", ""};

  // Selected row's description, shown above the footer (2 wrapped lines). A
  // description that fits one line takes the centered slot instead, halfway
  // between the list bottom and the footer rule rather than against the list.
  StringV rowDesc0{"RowDesc0", ""};
  StringV rowDesc1{"RowDesc1", ""};
  StringV rowDescC{"RowDescC", ""};

  StringV kbHint{"KbHint", ""};
  FloatV kbChromeVis{"KbChromeVis", -1.0};

  // Sections shown in the sidebar, in cursor order: the five settings pages
  // (SettingsPage 0..4), then Mods, Official DLC, Languages and Achievements.
  static constexpr const char *kSectionKeys[] = {
      "settings.page.gameplay",  "settings.page.display",
      "settings.page.graphics",  "settings.page.audio",
      "settings.page.controls",  "menu.header.mods",
      "menu.header.dlc",         "menu.header.languages",
      "menu.header.achievements"};
  // Every section the title screen offers. A surface that shows fewer takes a
  // prefix of this, so the settings pages come first.
  static constexpr int kSectionCount =
      static_cast<int>(std::size(kSectionKeys));

  // Engine names of the per-page settings menus, indexed by SettingsPage.
  static constexpr const char *kSettingsListNames[kSettingsSectionCount] = {
      "GameplayList", "DisplayList", "GraphicsList", "AudioList",
      "ControlsList"};

  static constexpr int kSectionMenuH =
      kSectionCount * kSectionRowH + (kSectionCount - 1) * kSectionRowGap;

  // Sections this surface actually shows. Sizes the sidebar and bounds the
  // names PopulateNames writes.
  void SetSectionCount(int count);
  int SectionCount() const { return sectionCount_; }

  // True when reblue owns the whole screen, as on the title menu, and so draws
  // its own backdrop, header and the rule closing it. Hosted on the camp Config
  // task the game already owns that band, so this turns off rather than
  // stacking a second header over the game's own.
  void SetStandalone(bool standalone) { standalone_ = standalone; }


  // The cursor starts on the first row's center, so it follows the row height.
  static constexpr int kSectionMenuY = 140;
  AnimeMenuWidget sectionMenu{.name = "SltSection",
                              .x = 105,
                              .y = kSectionMenuY,
                              .w = 150,
                              .h = kSectionMenuH,
                              .priority = 3,
                              .startCurX = 100,
                              .startCurY = kSectionMenuY + kSectionRowH / 2,
                              .curDir = "RIGHT",
                              .itemW = 150,
                              .itemH = kSectionRowH,
                              .itemAlpha = 128,
                              .itemOnType = "FRAME01",
                              .itemOffType = "BTN01_OF",
                              .rows = kSectionCount,
                              .cols = 1,
                              .defaultItem = kSectionCount,
                              .templateCSV = "l_modmgr_section.csv"};

  AnimeMenuWidget modList = ItemList("ModList", 420, "l_modmgr_info.csv");
  AnimeMenuWidget dlcList = ItemList("DlcList", 470, "l_modmgr_dlcinfo.csv");
  AnimeMenuWidget langList = ItemList("LangList", 470, "l_modmgr_langinfo.csv");

  // One list per settings page, beside the always-visible sidebar. Filled by
  // the constructor so the page names have a single spelling.
  AnimeMenuWidget settingsList[kSettingsSectionCount]{};

  // Read-only achievement list, same full-width geometry as a settings page.
  AnimeMenuWidget achvList = PageList("AchvList", "l_modmgr_achv.csv");

  AnimeMenuWidget bindList = BindList("BindList");

  // Rows visible at once. Longer lists scroll (the engine window keeps a
  // scrollbar). 10 rows of 45px from y=140 end at 610, above the footer line.
  static constexpr size_t kMaxVisibleRows = 10;

  void SetModCount(size_t count);
  void SetDLCCount(size_t count);
  void SetLanguageCount(size_t count);
  void SetAchievementCount(size_t count);
  void SetSettingsCounts(const size_t (&pageCounts)[kSettingsSectionCount],
                         int bindRows);

  void build(CsvBuilder &b) override;

private:
  // A scrolling content list beside the sidebar (mods, DLC), and the
  // full-width variant the settings pages and the achievement list share.
  static constexpr AnimeMenuWidget ItemList(const char *name, int width,
                                            const char *tpl);
  static constexpr AnimeMenuWidget PageList(const char *name, const char *tpl);
  static constexpr AnimeMenuWidget BindList(const char *name);

  int sectionCount_ = kSectionCount;
  bool standalone_ = true;

  // Shared by the mod and DLC lists: an empty one swaps in the no-data
  // template rather than showing a bare frame.
  static void SetListCount(AnimeMenuWidget &list, size_t count,
                           const char *tpl);

  SysMesVars sysmes;
};

constexpr AnimeMenuWidget ConfigLayout::ItemList(const char *name, int width,
                                                 const char *tpl) {
  return {.name = name,
          .x = 325,
          .y = 140,
          .w = width,
          .h = 0,
          .priority = 3,
          .startCurX = 320,
          .startCurY = 161,
          .curDir = "RIGHT",
          .itemW = width,
          .itemH = 45,
          .itemAlpha = 128,
          .itemOnType = "FRAME01",
          .itemOffType = "BTN01_OF",
          .rows = 0,
          .cols = 1,
          .defaultItem = 0,
          .templateCSV = tpl};
}

constexpr AnimeMenuWidget ConfigLayout::PageList(const char *name,
                                                 const char *tpl) {
  return {.name = name,
          .x = 325,
          .y = 140,
          .w = kSettingsPageW,
          .h = 0,
          .priority = 3,
          .startCurX = 320,
          .startCurY = 140 + kSettingRowH / 2,
          .curDir = "RIGHT",
          .itemW = kSettingsPageW,
          .itemH = kSettingRowH,
          .itemAlpha = 128,
          .itemOnType = "FRAME01",
          .itemOffType = "NOWINDOW",
          .rows = 0,
          .cols = 1,
          .defaultItem = 0,
          .templateCSV = tpl};
}

constexpr AnimeMenuWidget ConfigLayout::BindList(const char *name) {
  return {.name = name,
          .x = kKeybindGridX,
          .y = kKeybindGridY,
          .w = kKeybindCellW + kKeybindColStride,
          .h = 0,
          .priority = 3,
          .startCurX = kKeybindGridX - 5,
          .startCurY = kKeybindGridY + kKeybindRowH / 2,
          .curDir = "RIGHT",
          .itemW = kKeybindCellW,
          .itemH = kKeybindRowH,
          .itemAlpha = 128,
          .itemOnType = "FRAME01",
          .itemOffType = "BTN01_OF",
          .rows = 0,
          .cols = 2,
          .defaultItem = 0,
          .templateCSV = "l_modmgr_keybind.csv"};
}

} // namespace bd::engine
