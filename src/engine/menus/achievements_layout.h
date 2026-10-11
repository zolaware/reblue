/**
 * @file    engine/menus/achievements_layout.h
 * @brief   Achievement viewer layout definitions using the AnimeLayout DSL.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "engine/d2anime/d2anime.h"
#include "engine/menus/config_layout.h"

#include <cstddef>
#include <iterator>

#include <rex/types.h>

namespace bd::engine {

// Geometry shared by the Encyclopedia screens. The stock record strip steps
// 50px from y=172 (windows) and y=179 (labels). x=700 is the top screen's
// column and x=289 the one every record sub-screen redraws it at after the list
// slides left (cf. L_dia_mgc.csv).
struct EncyclopediaGeometry {
  // Our CSVs are served from the stock directory so these resolve relatively,
  // exactly as they do for the disc's own sibling files.
  static constexpr const char *kIcons[] = {
      "res\\icon_btl",      "res\\icon_adv",       "res\\icon_monster",
      "res\\icon_itmzukan", "res\\icon_mahozukan", "res\\icon_diary"};

  static constexpr int kRowStride = 50;
  static constexpr int kRowCount = static_cast<int>(std::size(kIcons));
  // Ours is the row appended below the five the disc ships.
  static constexpr int kAchievementsRow = kRowCount - 1;

  // dia_str_xx.u16 keys for the stock records. Ours is a host string.
  static constexpr const char *kStockLabels[] = {
      "str.DIA_S0002", "str.DIA_S0003", "str.DIA_S0004", "str.DIA_S0005",
      "str.DIA_S0006"};
  static_assert(static_cast<int>(std::size(kStockLabels)) == kAchievementsRow);

  static constexpr int kTopCellX = 700, kTopIconX = 701, kTopLabelX = 749;
  static constexpr int kSubCellX = 289, kSubIconX = 290, kSubLabelX = 338;

  static constexpr int kCellY = 172, kIconY = 171, kLabelY = 179;
  // The icons are the one part of the strip the disc does not step evenly:
  // 171, then 220, 270, 320, 370. L_dia.csv, all five S_dia_top_*.csv and
  // L_dia_mgc.csv repeat those same numbers, so the sixth cell continues the
  // even part of the run and not the first step.
  static constexpr int kIconRunY = 170;
  static constexpr int kCellW = 280, kCellH = 41;
  static constexpr int kIconSize = 45;
  static constexpr int kLabelFontW = 21, kLabelFontH = 27;
  // Lower priority draws in front, the same layering L_dia_mgc.csv gives the
  // strip it redraws.
  static constexpr int kStripPri = 3, kStripLabelPri = 2, kStripIconPri = 1;

  static int CellY(int row) { return kCellY + kRowStride * row; }
  static int IconY(int row) {
    return row == 0 ? kIconY : kIconRunY + kRowStride * row;
  }
  static int LabelY(int row) { return kLabelY + kRowStride * row; }
  // What a screen drawing a whole row off one pos element anchors on. The cell
  // and the label are fixed offsets from it, but the icon's has to be taken per
  // row against IconY, since its run is the irregular one.
  static int RowAnchorY(int row) { return kIconY + kRowStride * row; }

  // The label a row carries: ours reads a host variable, the rest the disc's
  // own string bundle.
  static const char *RowLabel(int row, const char *ours) {
    return row == kAchievementsRow ? ours : kStockLabels[row];
  }
};

// One achievement row (L_dia_ach_row.csv): icon, label, gamerscore. The engine
// drives WndType for the full-row cursor highlight, and the enabled flag picks
// EnableColor / DisableColor per row.
class AchievementRowTemplate : public ToggleRowTemplate {
public:
  // Two callers with different pane widths share this template: the config
  // menu's full-width list and the camp screen's right-hand pane. A template
  // CSV carries one geometry, so each gets its own instance.
  AchievementRowTemplate(int rowW, int rowH, int scoreX)
      : ToggleRowTemplate(0, 0, rowW, rowH, kRowPri), scoreX_(scoreX) {
    wndType.set("BTN01_OF");
  }

  // What a fully faded-in row draws at. RefreshVisuals scales all three by the
  // caller's fade, so the CSV defaults and the runtime values are one set.
  static constexpr double kRowAlpha = 128.0;
  static constexpr double kIconAlpha = 255.0;
  static constexpr double kIconDimAlpha = 96.0;
  static constexpr double kLineAlpha = 64.0;

  StringV score{"Score", ""};
  // The row's cell in the icon atlas. Zeroed until the host picks one, which
  // is the atlas's reserved empty cell, so a row that draws before it is told
  // which achievement it holds draws nothing rather than the whole sheet.
  FloatV iconU0{"IconU0", 0.0};
  FloatV iconV0{"IconV0", 0.0};
  FloatV iconU1{"IconU1", 0.0};
  FloatV iconV1{"IconV1", 0.0};
  // Scaled by the host so the list fades in behind the entry transition instead
  // of appearing whole.
  FloatV rowAlpha{"RowAlpha", kRowAlpha};
  FloatV iconAlpha{"IconAlpha", kIconAlpha};
  FloatV iconDimAlpha{"IconDimAlpha", kIconDimAlpha};
  FloatV lineAlpha{"LineAlpha", kLineAlpha};

  // Camp Encyclopedia pane: the divider sits at x=594, so the row has to fit
  // between it and the screen edge.
  static constexpr int kScreenRowW = 600, kScreenRowH = 42;
  static constexpr int kScreenScoreX = 505;
  // Config menu: the same full-width geometry as a settings page, so the rows
  // sit on that pitch and the rules under them line up across the sections.
  static constexpr int kConfigRowW = kSettingsPageW;
  static constexpr int kConfigRowH = kSettingRowH;
  static constexpr int kConfigScoreX = 830;

  // Writes a list's visible rows from the achievement catalog. Both the config
  // menu's list and the camp screen's use this template, so they share the
  // spelling of every variable it declares. 'fade' scales the alphas so a list
  // can arrive behind a transition rather than pop in whole.
  static void PopulateNames(const AnimeMenu &menu);
  static void RefreshVisuals(AnimeMenu menu, f32 fade = 1.0f);

protected:
  void declareVars(CsvBuilder &b) override;
  AnimeWindow rowWindow() const override;
  AnimeMessage rowLabel() const override;
  void buildValue(CsvBuilder &b) override;

private:
  // x=8..40 is the achievement's own icon, and the label starts clear of it.
  static constexpr int kIconX = 8, kIconSize = 32;
  static constexpr int kLabelX = 48;
  static constexpr int kFontW = 18, kFontH = 24;

  // Lower priority draws in front. Depths are stated relative to pos.pri, as
  // L_shw_cate_btn.csv and M_itm_catbtn.csv do: the menu widget owns the
  // template's pos element, so a literal here would be measured against a pri
  // the widget picked and the selected row's BTN01_ON would bury its label.
  static constexpr int kRowPri = 3;
  static constexpr const char *kLabelPri = "pos.pri-0.5";
  static constexpr const char *kIconPri = "pos.pri-2";
  // Behind the row window, the layer a settings row draws its rule on.
  static constexpr const char *kLinePri = "pos.pri+1";

  // The rule closing a row, spelled the way a settings row spells it so the
  // two sections' lines run down one column.
  static constexpr int kLineX = 4, kLineOverhang = 10;

  int scoreX_;

public:
  // Full-width rows for the config menu's list (l_modmgr_achv.csv).
  static AchievementRowTemplate &Config();
  // Narrow rows for the camp Encyclopedia screen (L_dia_ach_row.csv).
  static AchievementRowTemplate &Screen();
};

// The Achievements screen (L_dia_ach.csv), modelled directly on the Spell
// Record screen L_dia_mgc.csv: the record strip redrawn on the left with our
// row selected, the divider, and this screen's own list on the right. No
// backdrop and no footer, since the camp frame already owns both.
class AchievementsLayout : public AnimeLayout, public EncyclopediaGeometry {
public:
  FloatV start{"start", 1.0};
  FloatV alpha{"alpha", 255.0};
  StringV title{"Title", ""};
  StringV rowLabel{"RowLabel", ""};
  StringV summary{"Summary", ""};
  StringV rowDesc0{"RowDesc0", ""};
  StringV rowDesc1{"RowDesc1", ""};
  // Scaled by the host alongside the list rows, so the whole right-hand pane
  // arrives together instead of the strip's transition ending on a full screen.
  FloatV listAlpha{"ListAlpha", 255.0};
  FloatV descAlpha{"DescAlpha", 200.0};

  // pri 3 puts the rows on the same layer the stock record lists use
  // (L_dia_mon.csv), the layer the row template's pos.pri-relative depths are
  // measured against.
  AnimeMenuWidget list{.name = "AchvList",
                       .x = kListX,
                       .y = kListY,
                       .w = AchievementRowTemplate::kScreenRowW,
                       .h = 0,
                       .priority = 3,
                       .startCurX = kListX - 10,
                       .startCurY = kListY + 20,
                       .curDir = "RIGHT",
                       .itemW = AchievementRowTemplate::kScreenRowW,
                       .itemH = AchievementRowTemplate::kScreenRowH,
                       .itemAlpha = 128,
                       .itemOnType = "BTN01_ON",
                       .itemOffType = "BTN01_OF",
                       .rows = 0,
                       .cols = 1,
                       .defaultItem = 0,
                       .templateCSV = "L_dia_ach_row.csv"};

  // AnimeMenu_CalcItemPosition spreads h over the row count, so nine 42px rows
  // from y=149 stride 44.5 and the last one ends at 547, clear of the
  // description lines at 560 and of the divider's own end at 595.
  static constexpr size_t kMaxVisibleRows = 9;

  void SetRowCount(size_t count);

  void build(CsvBuilder &b) override;

private:
  static constexpr int kListX = 620, kListY = 149;

  // One anchor per strip row. Every element in a row reads its pos, so a
  // single tween on pos.x slides the whole row in from the top screen's
  // column, the move the stock S_dia_top_*.csv transitions animate.
  static constexpr const char *kRowPos[kRowCount] = {"StripRow0", "StripRow1",
                                                     "StripRow2", "StripRow3",
                                                     "StripRow4", "StripRow5"};
  // Offsets from RowAnchorY: window y=+1, label x=+49 y=+8, and the icon a
  // per-row step because of the irregular run. Held here because the source and
  // destination columns differ only in x, so one tween moves all three
  // together.
  static constexpr int kRowWndDY = 1;
  static constexpr int kRowIconDX = 1;
  static constexpr int kRowLabelDX = 49, kRowLabelDY = 8;

  // dia_str_xx.u16 holds the five stock record names, so the redrawn strip
  // stays localized without restating the text.
  AnimePanel strBundle_{"str", "dia_str_xx.u16", "start", 0, 1};

public:
  static AchievementsLayout &Get();
};

// The top-screen-to-Achievements transition (S_dia_top_ach.csv), the missing
// twin of the five stock S_dia_top_*.csv. The engine reaches those through
// CampDiary__OpenSubScreen, called from the top screen's Exit, which parks the
// destination state at task+0x70 and runs state 0x13 until the transition
// task's loadState reports 5. Our state (0x14) is past every switch the engine
// dispatches, so AchievementsMenu drives the same wait itself.
//
// L_dia.csv, the screen Exit hides on the way out, owns the left item
// column, the 'Items' header, its divider at x=526 and the five-row strip at
// x=700, so all four have to be redrawn here to animate out. Anything left out
// pops instead. Modelled on S_dia_top_mgc.csv, the one stock transition whose
// destination keeps the strip (at x=289).
class AchievementsTransitionLayout : public AnimeLayout,
                                     public EncyclopediaGeometry {
public:
  FloatV dispInterval{"disp_interval", 18.0};
  StringV title{"Title", ""};
  StringV rowLabel{"RowLabel", ""};

  // The frame by which every chain has finished, and where AchievementsMenu
  // swaps in the real screen. It swaps on the anime clock rather than on
  // loadState 5, which arrives only for a CSV the parser gave a length to
  // (AnimeData +0x138, where -1 means endless and AdvanceFrame then never
  // raises FINISHED).
  //
  // 19, the frame every stock S_dia_top_*.csv ends on, and not one more:
  // SetVisibleAndPlay un-starts every child anime (AnimeData__ResetAnimChains,
  // 0x82153CC0) so the seek is what re-evaluates them, and M_itm_catbtn.csv
  // only holds the left column at alpha 0 over frames [18, 19].
  static constexpr int kEndFrame = 19;

  void build(CsvBuilder &b) override;

private:
  // Row r holds at the source column for r frames, slides for kSlideFrames,
  // then holds out the remainder, the stock's one-frame-per-row stagger, with
  // every row arriving on the same frame.
  static constexpr int kSlideFrames = 10;
  static constexpr int kSlideHold = 8;
  // A keyframe chain draws nothing once its last segment is behind it, so
  // everything that has to survive to kEndFrame holds well past it.
  static constexpr int kTailHold = 20;

  // The left-column buttons of L_dia.csv, redrawn here with FadeOutFlg so
  // M_itm_catbtn.csv's own tween takes them out one at a time.
  static constexpr const char *kColumnNames[] = {
      "cmp_str.CMP_S0101", "cmp_str.CMP_S0102", "cmp_str.CMP_S0103",
      "cmp_str.CMP_S0109", "cmp_str.CMP_S0104", "cmp_str.CMP_S0105",
      "cmp_str.CMP_S0107", "cmp_str.CMP_S0108", "cmp_str.CMP_S0009"};
  static constexpr double kColumnY[] = {137.0,  184.5, 232.0, 279.5, 327.0,
                                        374.5,  422.0, 469.5, 542.0};
  static constexpr int kColumnCount = static_cast<int>(std::size(kColumnNames));
  static_assert(std::size(kColumnY) == std::size(kColumnNames));

  // Where L_dia.csv puts what this screen replaces.
  static constexpr int kOldDividerX = 526, kOldDividerY = 115;
  static constexpr int kOldDividerH = 487;
  static constexpr int kDividerX = 594, kDividerY = 116, kDividerH = 479;
  static constexpr int kTitleX = 180, kTitleY = 45;
  static constexpr int kTitleIconX = 115, kTitleIconY = 35, kTitleIconW = 64;
  static constexpr const char *kOldTitle = "cmp_str.CMP_S0006";
  static constexpr const char *kOldTitleIcon =
      ":d2anime\\camp\\itm\\res\\icon_item";
  static constexpr const char *kCategoryLabel = "cmp_str.CMP_S0009";

  AnimePanel strBundle_{"str", "dia_str_xx.u16", "1", 0, 1};
  AnimePanel cmpBundle_{"cmp_str", ":d2anime\\camp\\cmp_str_xx.u16", "1", 0, 1};

public:
  // Plays once on the way in from the Encyclopedia top screen
  // (S_dia_top_ach.csv).
  static AchievementsTransitionLayout &Get();
};

// The Encyclopedia's sixth strip cell (L_dia_ach_tab.csv), loaded once as a
// child of Camp::Diary::MainTask and re-gated per screen. LH_Binary__LoadAsync
// parents by task, not by anime tree, so this row inherits no screen's
// visibility. The Encyclopedia menu picks the gate and copies the running
// transition's clock into it to keep it moving with the five stock rows.
class AchievementsRowLayout : public AnimeLayout, public EncyclopediaGeometry {
public:
  // Exactly one is 1 and the rest -1: a chain whose frame start never arrives
  // draws nothing. Named for the screen each belongs to.
  FloatV top{"start", 1.0};  // top screen, static, SltTop draws the window
  FloatV fade{"Fade", -1.0}; // btl / adv / itm / mon transition, fades in place
  FloatV slide{"Slide", -1.0}; // mgc transition, walks to the sub-screen column
  FloatV sub{"Sub", -1.0};     // Spell Record screen, static
  StringV label{"Label", ""};

  void build(CsvBuilder &b) override;

private:
  // Row r holds for r+1 frames, then takes kFadeFrames to reach alpha 0
  // (S_dia_top_mon.csv and its three twins, which all stagger identically).
  static constexpr int kFadeHold = kAchievementsRow + 1;
  static constexpr int kFadeFrames = 7;
  // S_dia_top_mgc.csv instead: hold r frames, slide for kSlideFrames, then hold
  // out the remainder so every row arrives on frame 19 together.
  static constexpr int kSlideHold = kAchievementsRow;
  static constexpr int kSlideFrames = 10;
  static constexpr int kSlideArrive = 8 - kAchievementsRow;
  // A chain of more than one segment stops drawing once its last one is behind
  // it, and the transition is left running well past its arrival frame.
  static constexpr int kTailHold = 20;

public:
  static AchievementsRowLayout &Get();
};

// The same row in the Encyclopedia pane the Items screen draws beside its
// category column (L_dia_ach_prev.csv), a sibling of M_itm_dia.csv
// (itemTask+0x84+4*9, per bdCampItemLoad). That CSV has no menu widget: its
// five rows are hardcoded window / tex / message triplets standing at the top
// screen's column from frame 1, so ours is a plain sixth cell.
class AchievementsPreviewLayout : public AnimeLayout,
                                  public EncyclopediaGeometry {
public:
  FloatV start{"start", 1.0};
  // The pane's own DiaryAlpha, copied in per frame. See the note on build().
  FloatV alpha{"alpha", 0.0};
  StringV label{"Label", ""};

  void build(CsvBuilder &b) override;

private:
  // What every window in L_itm_cate.csv's pane draws at, spelled against our
  // own variable.
  static constexpr const char *kPaneWindowAlpha = "alpha-127";

public:
  static AchievementsPreviewLayout &Get();
};

} // namespace bd::engine
