/**
 * @file    engine/menus/achievements_layout.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/achievements_layout.h"

#include "engine/achievements/achievement_icon_tex.h"
#include "engine/achievements/achievement_list.h"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <string>

namespace bd::engine {

namespace {

using Key = CsvBuilder::Key;

constexpr UVRect kWholeTexture = {0.0f, 0.0f, 1.0f, 1.0f};

// One keyframe of a chain that only fades: which role it plays, when it starts,
// how long the segment runs and the alpha it carries.
struct FadeStep {
  Key key;
  const char *frameStart;
  int interval;
  int alpha;
};

} // namespace

void AchievementRowTemplate::declareVars(CsvBuilder &b) {
  b.vars(score, iconU0, iconV0, iconU1, iconV1, rowAlpha, iconAlpha,
         iconDimAlpha, lineAlpha);
  ToggleRowTemplate::declareVars(b);
}

// The whole row fades with the list rather than holding the base's fixed 128.
AnimeWindow AchievementRowTemplate::rowWindow() const {
  AnimeWindow w = ToggleRowTemplate::rowWindow();
  w.alphaExpr = rowAlpha.name();
  return w;
}

AnimeMessage AchievementRowTemplate::rowLabel() const {
  AnimeMessage m = ToggleRowTemplate::rowLabel();
  m.offsetX = kLabelX;
  m.fontW = kFontW;
  m.fontH = kFontH;
  m.offsetY = CenterY(kFontH);
  m.priorityExpr = kLabelPri;
  return m;
}

void AchievementRowTemplate::buildValue(CsvBuilder &b) {
  // The same camp-style rule a settings row ends on, drawn from the same inset
  // and overhang so the two sections' lines sit on one column.
  b.comment("camp-style divider line under the row")
      .frame(AnimeFrameRel{.frameStart = "start",
                           .posRef = "pos",
                           .offsetX = kLineX,
                           .offsetY = pos_.h - 1,
                           .w = pos_.w + kLineOverhang,
                           .h = 1,
                           .alphaRef = lineAlpha.name(),
                           .priorityExpr = kLinePri})
      .blank();

  // The gamerscore is the label again, moved right and reading its own
  // variable.
  AnimeMessage gamerscore = rowLabel();
  gamerscore.offsetX = scoreX_;
  gamerscore.contentVar = score.name();
  b.message(gamerscore);

  // The lit and dimmed copies of the same atlas cell, so the pair carries the
  // lock state.
  const char *alphaVars[] = {iconAlpha.name(), iconDimAlpha.name()};
  static_assert(std::size(alphaVars) == std::size(kCheckStates));

  b.blank().comment("achievement icon, dimmed while locked").comment(kColsTex);
  for (size_t i = 0; i < std::size(kCheckStates); ++i)
    b.tex(AnimeTex{.frameStart = kCheckStates[i].gate,
                   .posRef = "pos",
                   .offsetX = kIconX,
                   .offsetY = CenterY(kIconSize),
                   .w = kIconSize,
                   .h = kIconSize,
                   .file = kAchievementIconAtlasRef,
                   .tag = kCheckStates[i].tag,
                   .priorityExpr = kIconPri,
                   .u0Expr = iconU0.name(),
                   .v0Expr = iconV0.name(),
                   .u1Expr = iconU1.name(),
                   .v1Expr = iconV1.name(),
                   .alphaExpr = alphaVars[i]});
}

void AchievementRowTemplate::PopulateNames(const AnimeMenu &menu) {
  const auto &rows = GetAchievementList();
  menu.ForEachSlot([&](int, int i, AnimeData vb) {
    if (i < 0 || i >= static_cast<int>(rows.size())) {
      vb.SetText("Name", std::string());
      vb.SetText("Score", std::string());
      return;
    }
    vb.SetText("Name", rows[i].label);
    vb.SetText("Score", std::format("{}G", rows[i].gamerscore));
  });
}

void AchievementRowTemplate::RefreshVisuals(AnimeMenu menu, f32 fade) {
  SyncAchievementIconAtlas();
  const auto &rows = GetAchievementList();
  const u32 enColor = menu.EnableColor();
  const u32 disColor = menu.DisableColor();

  menu.ForEachRow(rows.size(), [&](int slot, int i, AnimeData vb) {
    const bool unlocked = rows[i].unlocked;

    // Only the alpha channel carries the fade. The tint has to stay the
    // enabled or disabled color the row is meant to draw in. ChkOn / ChkOff,
    // which SetToggleRow writes, pick between the full-alpha and the dimmed
    // copy of the same cell.
    const u32 color = unlocked ? enColor : disColor;
    const u32 alpha = static_cast<u32>(((color >> 24) & 0xFF) * fade);
    menu.SetToggleRow(slot, vb, unlocked,
                      (color & 0x00FFFFFFu) | (alpha << 24));

    vb.SetFloat("RowAlpha", kRowAlpha * fade);
    vb.SetFloat("IconAlpha", kIconAlpha * fade);
    vb.SetFloat("IconDimAlpha", kIconDimAlpha * fade);
    vb.SetFloat("LineAlpha", kLineAlpha * fade);

    // An achievement with no icon takes a transparent cell.
    const AchievementIconUv uv = AchievementIconUVFor(rows[i].id);
    vb.SetFloat("IconU0", uv.u0);
    vb.SetFloat("IconV0", uv.v0);
    vb.SetFloat("IconU1", uv.u1);
    vb.SetFloat("IconV1", uv.v1);
  });
}

// Mirrors ConfigLayout::SetListCount: the +20 is the window's own padding, and
// defaultItem must be 0 or the engine pre-creates entries the host then
// duplicates through AddEntryData. The catalog is never empty (the XDBF ships
// 43), so there is no no-data branch here.
void AchievementsLayout::SetRowCount(size_t count) {
  const int rows = static_cast<int>(std::min(count, kMaxVisibleRows));
  list.rows = rows;
  list.h = rows * list.itemH + 20;
  list.defaultItem = 0;
}

void AchievementsLayout::build(CsvBuilder &b) {
  // Left-aligned on the list's own left edge: a centred second line sits under
  // a column of left-aligned rows and reads as misaligned rather than as a
  // caption.
  const auto rowDesc = [this](int y, const char *var) {
    return AnimeMessageAbs{.frameStart = "start",
                           .x = kListX,
                           .y = y,
                           .fontW = 15,
                           .fontH = 19,
                           .priority = 1,
                           .alpha = 200,
                           .r = 190,
                           .g = 190,
                           .b = 190,
                           .contentVar = var,
                           .alphaExpr = descAlpha.name()};
  };

  b.comment("variable definitions").vars(start, alpha);
  Declare(b, title, rowLabel, summary, rowDesc0, rowDesc1, listAlpha,
          descAlpha);
  b.blank();

  b.comment("record names, shared with the top screen")
      .panel(strBundle_)
      .blank();

  b.comment("screen title")
      .tex(AnimeTexAbs{.frameStart = "start",
                       .x = 115,
                       .y = 35,
                       .w = 64,
                       .h = 64,
                       .uv = kWholeTexture,
                       .file = "res\\icon_diary"})
      .message(AnimeMessageAbs{.frameStart = "start",
                               .x = 180,
                               .y = 45,
                               .fontW = 38,
                               .fontH = 48,
                               .priority = 0,
                               .contentVar = title.name()})
      .message(AnimeMessageAbs{.frameStart = "start",
                               .x = kListX,
                               .y = 112,
                               .fontW = 18,
                               .fontH = 22,
                               .priority = 1,
                               .g = 220,
                               .b = 120,
                               .contentVar = summary.name(),
                               .alphaExpr = listAlpha.name()})
      .blank();

  // The record strip, redrawn at the slid-left column exactly as every stock
  // record screen does. Static, because S_dia_top_ach.csv has already walked
  // it there by the time this screen is shown. Ours is the selected row, so it
  // alone takes the ON window at full alpha.
  b.comment("record strip anchors, one per row").comment(kColsPosPri);
  for (int row = 0; row < kRowCount; ++row)
    b.pos(AnimePos{kRowPos[row], kSubCellX, RowAnchorY(row), kCellW, kCellH,
                   kStripPri, true});

  b.blank().comment(kColsWindow);
  for (int row = 0; row < kRowCount; ++row) {
    const bool selected = row == kAchievementsRow;
    b.window(AnimeWindow{.frameStart = "start",
                         .posRef = kRowPos[row],
                         .wndTypeLiteral = selected ? "BTN01_ON" : "BTN01_OF",
                         .alpha = selected ? 255 : 128,
                         .offsetY = kRowWndDY});
  }

  b.blank().comment(kColsTex);
  for (int row = 0; row < kRowCount; ++row)
    b.tex(AnimeTex{.frameStart = "start",
                   .posRef = kRowPos[row],
                   .offsetX = kRowIconDX,
                   .offsetY = IconY(row) - RowAnchorY(row),
                   .w = kIconSize,
                   .h = kIconSize,
                   .priority = kStripIconPri,
                   .uv = kWholeTexture,
                   .file = kIcons[row]});

  b.blank().comment(kColsMessage);
  for (int row = 0; row < kRowCount; ++row)
    b.message(AnimeMessage{.frameStart = "start",
                           .posRef = kRowPos[row],
                           .offsetX = kRowLabelDX,
                           .offsetY = kRowLabelDY,
                           .fontW = kLabelFontW,
                           .fontH = kLabelFontH,
                           .priority = kStripLabelPri,
                           .alphaRef = "alpha",
                           .colorR = "255",
                           .colorG = "255",
                           .colorB = "255",
                           .contentVar = RowLabel(row, rowLabel.name())});

  b.blank()
      .comment(kColsFrame)
      .frame(AnimeFrame{.frameStart = "start",
                        .x = 594,
                        .y = 116,
                        .w = 2,
                        .h = 479,
                        .priority = 4,
                        .alphaRef = "alpha-127",
                        .tag = "#line"})
      .blank()
      .comment("list")
      .menu(list)
      .blank()
      .comment("selected row description")
      .message(rowDesc(560, rowDesc0.name()))
      .message(rowDesc(584, rowDesc1.name()));
}

void AchievementsTransitionLayout::build(CsvBuilder &b) {
  b.comment("variable definitions").var(dispInterval);
  Declare(b, title, rowLabel);
  b.blank();

  b.comment("record and camp string bundles")
      .panel(strBundle_)
      .panel(cmpBundle_)
      .blank();

  std::string column[kColumnCount];
  for (int i = 0; i < kColumnCount; ++i)
    column[i] = std::format("ItmCatbtn{:02d}", i + 1);

  b.comment("the top screen's left column, redrawn so it can fade out")
      .comment(kColsPanel);
  for (int i = 0; i < kColumnCount; ++i)
    b.panel(
        AnimePanel{column[i].c_str(), ":d2anime\\M_itm_catbtn.csv", "1", 0, 1});
  b.blank();

  // Each property of the redrawn column, one full pass per property, the way
  // the disc's own transitions group them. move_start staggers the fade one
  // button per frame, and interval is when M_itm_catbtn.csv pins the faded-out
  // value.
  for (int i = 0; i < kColumnCount; ++i)
    b.setVal(column[i] + ".pos.y", kColumnY[i]);
  b.blank();
  for (int i = 0; i < kColumnCount; ++i)
    b.set(column[i] + ".Name", kColumnNames[i]);
  b.blank();
  for (int i = 0; i < kColumnCount; ++i)
    b.setVal(column[i] + ".move_start", i + 1);
  b.blank();
  for (int i = 0; i < kColumnCount; ++i)
    b.setVal(column[i] + ".FadeOutFlg", 1);
  b.blank();
  for (int i = 0; i < kColumnCount; ++i)
    b.set(column[i] + ".interval", dispInterval.name());
  b.blank();

  // One chain per strip element: hold at the top screen's column for as many
  // frames as the row's index, slide, then hold so every row arrives together.
  const auto slide = [&](int row, auto emit) {
    if (row == 0) {
      emit(Key::Start, "1", kSlideFrames, true);
    } else {
      emit(Key::Start, "1", row, true);
      emit(Key::Hold, "0", kSlideFrames, true);
    }
    emit(Key::To, "", 0, false);
    emit(Key::Hold, "0", kSlideHold - row, false);
    emit(Key::Hold, "0", kTailHold, false);
  };

  b.comment("record strip walking to the sub-screen column")
      .comment(kColsWindow);
  for (int row = 0; row < kRowCount; ++row) {
    const bool selected = row == kAchievementsRow;
    slide(row, [&](Key k, const char *fs, int iv, bool atTop) {
      b.window(AnimeWindow{.frameStart = fs,
                           .frameInterval = iv,
                           .x = atTop ? kTopCellX : kSubCellX,
                           .y = CellY(row),
                           .w = kCellW,
                           .h = kCellH,
                           .priority = kStripPri,
                           .wndTypeLiteral = selected ? "BTN01_ON" : "BTN01_OF",
                           .alpha = selected ? 255 : 128},
               k);
    });
    b.blank();
  }

  b.comment(kColsTex);
  for (int row = 0; row < kRowCount; ++row) {
    slide(row, [&](Key k, const char *fs, int iv, bool atTop) {
      b.tex(AnimeTexAbs{.frameStart = fs,
                        .frameInterval = iv,
                        .x = atTop ? kTopIconX : kSubIconX,
                        .y = IconY(row),
                        .w = kIconSize,
                        .h = kIconSize,
                        .priority = kStripIconPri,
                        .uv = kWholeTexture,
                        .file = kIcons[row]},
            k);
    });
    b.blank();
  }

  b.comment(kColsMessage);
  for (int row = 0; row < kRowCount; ++row) {
    const char *label = RowLabel(row, rowLabel.name());
    slide(row, [&](Key k, const char *fs, int iv, bool atTop) {
      b.message(AnimeMessageAbs{.frameStart = fs,
                                .frameInterval = iv,
                                .x = atTop ? kTopLabelX : kSubLabelX,
                                .y = LabelY(row),
                                .fontW = kLabelFontW,
                                .fontH = kLabelFontH,
                                .priority = kStripLabelPri,
                                .contentVar = label},
                k);
    });
    b.blank();
  }

  // The Items header, its icon and the 'Category' heading all leave together,
  // each on its own chain built the same way.
  const auto fadeMsg = [&](const auto &chain, int x, int y, int fw, int fh,
                           const char *var) {
    for (const auto &s : chain)
      b.message(AnimeMessageAbs{.frameStart = s.frameStart,
                                .frameInterval = s.interval,
                                .x = x,
                                .y = y,
                                .fontW = fw,
                                .fontH = fh,
                                .priority = 0,
                                .alpha = s.alpha,
                                .contentVar = var},
                s.key);
  };
  const auto fadeTex = [&](const auto &chain, int x, int y, int size,
                           const char *file) {
    for (const auto &s : chain)
      b.tex(AnimeTexAbs{.frameStart = s.frameStart,
                        .frameInterval = s.interval,
                        .x = x,
                        .y = y,
                        .w = size,
                        .h = size,
                        .priority = 0,
                        .alpha = s.alpha,
                        .uv = kWholeTexture,
                        .file = file},
            s.key);
  };

  constexpr FadeStep kFadeOut[] = {{Key::Start, "1", 5, 255},
                                   {Key::Hold, "0", 7, 255},
                                   {Key::To, "", 0, 0}};
  constexpr FadeStep kFadeOutFast[] = {{Key::Start, "1", 5, 255},
                                       {Key::To, "", 0, 0}};
  // Fades in over the same window and then holds well past the arrival frame,
  // since a chain draws nothing once its last segment is behind it.
  constexpr FadeStep kFadeIn[] = {{Key::Start, "10", 7, 0},
                                  {Key::To, "", 0, 255},
                                  {Key::Hold, "0", 2, 255},
                                  {Key::Hold, "0", kTailHold, 255}};

  b.comment("the 'Items' header and its icon fading out");
  fadeMsg(kFadeOut, kTitleX, kTitleY, 43, 48, kOldTitle);
  b.blank();
  fadeTex(kFadeOut, kTitleIconX, kTitleIconY, kTitleIconW, kOldTitleIcon);
  b.blank();

  b.comment("the top screen's 'Category' heading goes with it");
  fadeMsg(kFadeOutFast, 554, 105, 20, 24, kCategoryLabel);
  b.blank();

  b.comment("the divider retracts downwards and grows back at the new column");
  b.frame(AnimeFrame{.frameStart = "1",
                     .frameInterval = 11,
                     .x = kOldDividerX,
                     .y = kOldDividerY,
                     .w = 2,
                     .h = kOldDividerH,
                     .priority = 4,
                     .alphaRef = "128"},
          Key::Start);
  b.frame(AnimeFrame{.frameStart = "",
                     .x = kOldDividerX,
                     .y = kOldDividerY + kOldDividerH,
                     .w = 2,
                     .h = 1,
                     .priority = 4,
                     .alphaRef = "0"},
          Key::To);
  b.blank();

  const AnimeFrame newDivider{.frameStart = "0",
                              .frameInterval = 1,
                              .x = kDividerX,
                              .y = kDividerY,
                              .w = 2,
                              .h = kDividerH,
                              .priority = 4,
                              .alphaRef = "128"};
  b.frame(AnimeFrame{.frameStart = "6",
                     .frameInterval = 12,
                     .x = kDividerX,
                     .y = kDividerY + kDividerH,
                     .w = 2,
                     .h = 1,
                     .priority = 4,
                     .alphaRef = "0"},
          Key::Start);
  b.frame(AnimeFrame{.frameStart = "",
                     .x = kDividerX,
                     .y = kDividerY,
                     .w = 2,
                     .h = kDividerH,
                     .priority = 4,
                     .alphaRef = "128"},
          Key::To);
  b.frame(newDivider, Key::Hold);
  b.frame(AnimeFrame{.frameStart = "0",
                     .frameInterval = kTailHold,
                     .x = kDividerX,
                     .y = kDividerY,
                     .w = 2,
                     .h = kDividerH,
                     .priority = 4,
                     .alphaRef = "128"},
          Key::Hold);
  b.blank();

  b.comment("this screen's own title, fading in behind a shrinking icon");
  fadeMsg(kFadeIn, kTitleX, kTitleY, 38, 48, title.name());
  b.blank();

  // The icon does not just fade: it starts oversized and off the top and
  // settles on the header's own 64px square, so its chain carries geometry
  // too.
  const struct {
    Key key;
    const char *frameStart;
    int interval, x, y, size, alpha;
  } titleIcon[] = {
      {Key::Start, "10", 8, 70, -10, 153, 0},
      {Key::To, "", 0, kTitleIconX, kTitleIconY, kTitleIconW, 255},
      {Key::Hold, "0", 1, kTitleIconX, kTitleIconY, kTitleIconW, 255},
      {Key::Hold, "0", kTailHold, kTitleIconX, kTitleIconY, kTitleIconW, 255},
  };
  for (const auto &s : titleIcon)
    b.tex(AnimeTexAbs{.frameStart = s.frameStart,
                      .frameInterval = s.interval,
                      .x = s.x,
                      .y = s.y,
                      .w = s.size,
                      .h = s.size,
                      .priority = 0,
                      .alpha = s.alpha,
                      .uv = kWholeTexture,
                      .file = kIcons[kAchievementsRow]},
          s.key);
}

void AchievementsRowLayout::build(CsvBuilder &b) {
  b.comment("variable definitions");
  Declare(b, top, fade, slide, sub, label);
  b.blank();

  const int cellY = CellY(kAchievementsRow);
  const int iconY = IconY(kAchievementsRow);
  const int labelY = LabelY(kAchievementsRow);
  const char *icon = kIcons[kAchievementsRow];

  // A keyframe is (role, frame start, interval, column, alpha), and a chain is
  // a list of them. Keyframes of one element have to stay contiguous, since a
  // To row names no type and reads as the destination of the row above it, so
  // each element emits its whole chain before the next one starts.
  struct Step {
    Key key;
    const char *frameStart;
    int interval;
    bool atTop;
    int alpha; // 255 at full, the window scales its own 128 by the same ratio
  };

  const auto emit = [&](std::initializer_list<Step> chain) {
    for (const auto &s : chain)
      b.window(AnimeWindow{.frameStart = s.frameStart,
                           .frameInterval = s.interval,
                           .x = s.atTop ? kTopCellX : kSubCellX,
                           .y = cellY,
                           .w = kCellW,
                           .h = kCellH,
                           .priority = kStripPri,
                           .wndTypeLiteral = "BTN01_OF",
                           .alpha = s.alpha * 128 / 255},
               s.key);
    for (const auto &s : chain)
      b.tex(AnimeTexAbs{.frameStart = s.frameStart,
                        .frameInterval = s.interval,
                        .x = s.atTop ? kTopIconX : kSubIconX,
                        .y = iconY,
                        .w = kIconSize,
                        .h = kIconSize,
                        .priority = kStripIconPri,
                        .alpha = s.alpha,
                        .uv = kWholeTexture,
                        .file = icon},
            s.key);
    for (const auto &s : chain)
      b.message(AnimeMessageAbs{.frameStart = s.frameStart,
                                .frameInterval = s.interval,
                                .x = s.atTop ? kTopLabelX : kSubLabelX,
                                .y = labelY,
                                .fontW = kLabelFontW,
                                .fontH = kLabelFontH,
                                .priority = kStripLabelPri,
                                .alpha = s.alpha,
                                .contentVar = label.name()},
                s.key);
    b.blank();
  };

  b.comment(kColsWindow).comment(kColsTex).comment(kColsMessage).blank();

  // The top screen draws this cell's window from SltTop's own sixth entry, so
  // a second one here would double-blend it.
  b.comment("top screen, static");
  b.tex(AnimeTexAbs{.frameStart = top.name(),
                    .x = kTopIconX,
                    .y = iconY,
                    .w = kIconSize,
                    .h = kIconSize,
                    .priority = kStripIconPri,
                    .uv = kWholeTexture,
                    .file = icon});
  b.message(AnimeMessageAbs{.frameStart = top.name(),
                            .x = kTopLabelX,
                            .y = labelY,
                            .fontW = kLabelFontW,
                            .fontH = kLabelFontH,
                            .priority = kStripLabelPri,
                            .contentVar = label.name()});
  b.blank();

  b.comment("battle / adventure / item / monster transition, fades in place");
  emit({{Key::Start, fade.name(), kFadeHold, true, 255},
        {Key::Hold, "0", kFadeFrames, true, 255},
        {Key::To, "", 0, true, 0}});

  b.comment("spell transition, walks to the sub-screen column with the rest");
  emit({{Key::Start, slide.name(), kSlideHold, true, 255},
        {Key::Hold, "0", kSlideFrames, true, 255},
        {Key::To, "", 0, false, 255},
        {Key::Hold, "0", kSlideArrive, false, 255},
        {Key::Hold, "0", kTailHold, false, 255}});

  b.comment("Spell Record screen, the sixth cell L_dia_mgc.csv has no row for");
  emit({{Key::Start, sub.name(), 0, false, 255}});
}

// Every cell of the pane this sits under draws at the pane's own DiaryAlpha,
// the labels and icons straight and the windows at DiaryAlpha-127
// (L_itm_cate.csv), so the whole pane ramps together as the Items cursor moves
// on and off the Encyclopedia category. Ours is a task of its own and cannot
// name that variable, so the Encyclopedia menu copies the value into this alpha
// every frame. Held at a literal 255, the sixth cell snapped in whole on the
// frame the gate left zero while the five above it were still ramping.
void AchievementsPreviewLayout::build(CsvBuilder &b) {
  b.comment("variable definitions").var(start);
  Declare(b, alpha, label);
  b.blank();

  b.comment("a sixth cell below the pane's own five").comment(kColsWindow);
  b.window(AnimeWindow{.frameStart = "start",
                       .x = kTopCellX,
                       .y = CellY(kAchievementsRow),
                       .w = kCellW,
                       .h = kCellH,
                       .priority = kStripPri,
                       .wndTypeLiteral = "BTN01_OF",
                       .alphaExpr = kPaneWindowAlpha});
  b.blank();

  b.comment(kColsTex);
  b.tex(AnimeTexAbs{.frameStart = "start",
                    .x = kTopIconX,
                    .y = IconY(kAchievementsRow),
                    .w = kIconSize,
                    .h = kIconSize,
                    .priority = kStripIconPri,
                    .uv = kWholeTexture,
                    .file = kIcons[kAchievementsRow],
                    .alphaExpr = alpha.name()});
  b.blank();

  b.comment(kColsMessage);
  b.message(AnimeMessageAbs{.frameStart = "start",
                            .x = kTopLabelX,
                            .y = LabelY(kAchievementsRow),
                            .fontW = kLabelFontW,
                            .fontH = kLabelFontH,
                            .priority = kStripLabelPri,
                            .contentVar = label.name(),
                            .alphaExpr = alpha.name()});
}

AchievementsLayout &AchievementsLayout::Get() {
  static AchievementsLayout s_layout;
  return s_layout;
}

AchievementsTransitionLayout &AchievementsTransitionLayout::Get() {
  static AchievementsTransitionLayout s_transition;
  return s_transition;
}

AchievementRowTemplate &AchievementRowTemplate::Config() {
  static AchievementRowTemplate s_row(kConfigRowW, kConfigRowH, kConfigScoreX);
  return s_row;
}

AchievementRowTemplate &AchievementRowTemplate::Screen() {
  static AchievementRowTemplate s_row(kScreenRowW, kScreenRowH, kScreenScoreX);
  return s_row;
}

AchievementsRowLayout &AchievementsRowLayout::Get() {
  static AchievementsRowLayout s_row;
  return s_row;
}

AchievementsPreviewLayout &AchievementsPreviewLayout::Get() {
  static AchievementsPreviewLayout s_preview;
  return s_preview;
}

} // namespace bd::engine
