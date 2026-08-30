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
// re-spaces. 44 on an 8px gap ends eight rows at 548, clear of the
// row description line at 568.
inline constexpr int kSectionRowH = 44;
inline constexpr int kSectionRowGap = 8;

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

// The keybind grid is sectioned: nine rows interleave the Actions column with
// the Movement & Camera column, then two empty rows carry the Controller
// Compatibility panel's own header, and the compat binds fill the tail. The
// bind a grid slot carries is kKeybindSlotBind, with -1 for a cell that
// carries no bind: the Movement column ends a row before the Actions column,
// the spacer band sits mid-grid, and the compat block's left column runs out
// a row before the D-pad column beside it.
// 15 rows of 32 from here end at 612, inside the rule that closes the content
// band at 617, which is the band the game's own config screens draw into.
inline constexpr int kKeybindGridY = 132;
inline constexpr int kKeybindRowH = 32;
inline constexpr int kKeybindTopRows = 9;
inline constexpr int kKeybindGridX = 76;
inline constexpr int kKeybindCellW = 552;
inline constexpr int kKeybindColStride = 576;
// Where a row's box stops: after its one key, or after both of them. A box run
// out to the cell width reads as a rule across the screen rather than a row.
inline constexpr int kKeybindRowSoloW = 412;
inline constexpr int kKeybindRowPairW = 544;

inline constexpr int kKeybindSlotBind[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
    15, 16, -1,                                     // top band, 9 rows
    -1, -1, -1, -1,                                 // spacer band, 2 rows
    17, 18, 19, 20, 21, 22, -1, 23};                // compat band, 4 rows
inline constexpr int kKeybindSlotCount =
    static_cast<int>(std::size(kKeybindSlotBind));

inline constexpr int KeybindSlotToIndex(int slot) {
  return slot >= 0 && slot < kKeybindSlotCount ? kKeybindSlotBind[slot] : -1;
}
inline constexpr bool KeybindSlotIsSpacer(int slot) {
  return KeybindSlotToIndex(slot) < 0;
}
inline constexpr int KeybindIndexToSlot(int index) {
  for (int slot = 0; slot < kKeybindSlotCount; ++slot)
    if (kKeybindSlotBind[slot] == index)
      return slot;
  return -1;
}

// Keybind row template (l_modmgr_keybind.csv): a bind label plus two key
// buttons, laid out by the engine in a 2-column table (KeybindList). Slot one
// is the primary key, slot two the alternate. The active slot lights up
// (BTN01_ON + yellow) while capturing a new bind.
class KeybindItemTemplate : public RowTemplate {
public:
  // 32px pitch: 15 rows have to end above the footer rule at 617, and the
  // boxes only have to hold caps and short fallback names.
  KeybindItemTemplate() : RowTemplate(80, 0, kKeybindCellW, kKeybindRowH) {}

  // Which key box a row-local x hits: 0 primary, 1 alternate, -1 outside
  // both. Hit testing reads the same constants the boxes are drawn from.
  static int ChipAt(f32 x);

protected:
  void declareVars(CsvBuilder &b) override;
  AnimeWindow rowWindow() const override;
  AnimeMessage rowLabel() const override;
  void buildValue(CsvBuilder &b) override;
};

inline constexpr const char *kPadGeneralTex =
    ":d2anime\\camp\\cfg\\res\\key_config_01";
inline constexpr const char *kPadMechatTex =
    ":d2anime\\camp\\cfg\\res\\key_config_M_01";
inline constexpr const char *kPadMechatAltTex =
    ":d2anime\\camp\\cfg\\res\\key_config_M_02";
inline constexpr const char *kPadArrowTex = ":d2anime\\res\\cur_01";

inline constexpr int kPadArrowY = 100;
inline constexpr int kPadArrowSize = 32;
inline constexpr int kPadArrowLeftX = 68;
inline constexpr int kPadArrowRightX = 1180;

// Rebuilt from the disc's own control type pages, d2anime\camp\cfg\L_key_*:
// a label box on the end of every leader line of the pad art. A type change
// moves which action a box names, never where a box sits.
class PadLayoutTemplate : public AnimeLayout {
public:
  static constexpr int kGeneralSlots = 11;
  static constexpr int kMechatSlots = 5;
  static constexpr int kInvertSlot = kMechatSlots;
  static constexpr int kSlots = kGeneralSlots;
  static constexpr int kTypeCount = 4;

  FloatV start{"start", -1.0};
  FloatV alpha{"alpha", 255.0};
  FloatV generalVis{"GeneralVis", -1.0};
  FloatV mechatVis{"MechatVis", -1.0};
  FloatV aimShortVis{"AimShortVis", -1.0};
  FloatV aimTallVis{"AimTallVis", -1.0};
  // One row per diagram rather than one row whose path a var swaps: writing a
  // bound string var re-runs the load through AnimeVar_PropagateStringToElements,
  // which rebuilds the path without the engine's ':' disc-root escape and leaves
  // the element pointing at a texture list that never resolved.
  FloatV artGeneralVis{"ArtGeneralVis", -1.0};
  FloatV artMechatVis{"ArtMechatVis", -1.0};
  FloatV artMechatAltVis{"ArtMechatAltVis", -1.0};
  StringV typeName{"TypeName", ""};
  StringV label[kSlots]{{"Lbl0", ""}, {"Lbl1", ""}, {"Lbl2", ""},
                        {"Lbl3", ""}, {"Lbl4", ""}, {"Lbl5", ""},
                        {"Lbl6", ""}, {"Lbl7", ""}, {"Lbl8", ""},
                        {"Lbl9", ""}, {"Lbl10", ""}};

  void build(CsvBuilder &b) override;
};

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
  AnimePanel pad{"pad", "l_modmgr_pad.csv"};
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

  // Keybind screen chrome: the two column headers, and the spacer band's
  // compat header plus the rebind hint beside it. The grid runs past the
  // row description line, so the hint lives in the band instead.
  StringV hdrActions{"HdrActions", ""};
  StringV hdrMovement{"HdrMovement", ""};
  StringV hdrCompat{"HdrCompat", ""};
  StringV kbHint{"KbHint", ""};
  // Gates the three section panels and their rules. Text clears itself by
  // going empty, a box has to be told not to draw.
  FloatV kbChromeVis{"KbChromeVis", -1.0};

  // Sections shown in the sidebar, in cursor order: the five settings pages
  // (SettingsPage 0..4), then Mods, Official DLC and Achievements.
  static constexpr const char *kSectionKeys[] = {
      "settings.page.gameplay", "settings.page.display",
      "settings.page.graphics", "settings.page.audio",
      "settings.page.controls", "settings.page.cheats",
      "menu.header.mods",       "menu.header.dlc",
      "menu.header.achievements"};
  // Every section the title screen offers. A surface that shows fewer takes a
  // prefix of this, so the settings pages come first.
  static constexpr int kSectionCount =
      static_cast<int>(std::size(kSectionKeys));

  // Guest names of the per-page settings menus, indexed by SettingsPage.
  static constexpr const char *kSettingsListNames[kSettingsSectionCount] = {
      "GameplayList", "DisplayList", "GraphicsList", "AudioList",
      "ControlsList", "CheatsList"};

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

  // One list per settings page, beside the always-visible sidebar. Filled by
  // the constructor so the page names have a single spelling.
  AnimeMenuWidget settingsList[kSettingsSectionCount]{};

  // Read-only achievement list, same full-width geometry as a settings page.
  AnimeMenuWidget achvList = PageList("AchvList", "l_modmgr_achv.csv");

  // 2-column table (engine pattern, cf. camp L_sta_skill.csv 11x2) on a
  // dedicated full-width screen reached from the Input page. The extent is one
  // cell plus one stride: the engine spreads the extent over the cells, so
  // those two numbers are what set the pitch and the gap between the columns.
  AnimeMenuWidget keybindList{.name = "KeybindList",
                              .x = kKeybindGridX,
                              .y = kKeybindGridY,
                              .w = kKeybindCellW + kKeybindColStride,
                              .h = 0,
                              .priority = 3,
                              .startCurX = kKeybindGridX - 5,
                              .startCurY = kKeybindGridY + 16,
                              .curDir = "RIGHT",
                              .itemW = kKeybindCellW,
                              .itemH = kKeybindRowH,
                              .itemAlpha = 128,
                              .itemOnType = "FRAME01",
                              // Every cell carries a box, the way the camp's
                              // own 11x2 skill table does. Unfilled cells go
                              // back to NOWINDOW from RefreshKeybindVisuals.
                              .itemOffType = "BTN01_OF",
                              .rows = 0,
                              .cols = 2,
                              .defaultItem = 0,
                              .templateCSV = "l_modmgr_keybind.csv"};

  // Rows visible at once. Longer lists scroll (the engine window keeps a
  // scrollbar). 10 rows of 45px from y=140 end at 610, above the footer line.
  static constexpr size_t kMaxVisibleRows = 10;

  void SetModCount(size_t count);
  void SetDLCCount(size_t count);
  void SetAchievementCount(size_t count);
  void SetSettingsCounts(const size_t (&pageCounts)[kSettingsSectionCount],
                         size_t keybinds);

  void build(CsvBuilder &b) override;

private:
  // A scrolling content list beside the sidebar (mods, DLC), and the
  // full-width variant the settings pages and the achievement list share.
  static constexpr AnimeMenuWidget ItemList(const char *name, int width,
                                            const char *tpl);
  static constexpr AnimeMenuWidget PageList(const char *name, const char *tpl);

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

} // namespace bd::engine
