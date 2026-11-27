/**
 * @file    engine/menus/local_map_layout.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/local_map_layout.h"

#include "engine/d2anime/d2anime.h"
#include "engine/glyph_set.h"

namespace bd::engine {

namespace {

// The stock footer band, from d2anime\wrmap\L_wrmap_ftr.csv: its pos is
// 0,606,1280,85, icons sit 16 into it and their labels 18, so a prompt of ours
// only reads as one of the row's when it sits on the same two lines.
constexpr int kIconY = 622;
constexpr int kLabelY = 639;
constexpr int kIconSize = 64;
constexpr int kLabelFontW = 27;
constexpr int kLabelFontH = 30;
constexpr int kLabelGap = 51;

// Free slots in that row. The stock prompts hold 201 (B) and 629/669 (LB/RB),
// and the area map takes the pair over once it hides them.
constexpr int kOpenIconX = 860;
constexpr int kBackIconX = 400;
constexpr int kZoomIconX = 629;
constexpr int kZoomIconX2 = 669;
constexpr int kFloorIconX = 900;

// The stock header title sits at 180,45 with font 43x48, so its bottom edge
// sits at 93, two above its underline at 95. Two lines take the same anchor:
// the count line's bottom at 93, the title's ten above it. The longest name
// the game ships is 36 characters ("Nene's Fortress - 4F Machine Chamber"),
// so fontW 26 puts its right edge near x 1120.
constexpr int kTitleX = 180;
constexpr int kTitleY = 27;
constexpr int kTitleFontW = 26;
constexpr int kTitleFontH = 30;
constexpr int kCountY = 67;
constexpr int kCountFontW = 22;
constexpr int kCountFontH = 26;

// Hiding the stock header takes its underline at y 95 with it, so the area
// view draws the same line itself.
constexpr int kHeaderLineY = 95;
constexpr int kHeaderLineH = 2;
constexpr int kHeaderLinePri = 4;
constexpr const char *kHeaderLineAlpha = "128";

constexpr int kLegendLabelFontW = 14;
constexpr int kLegendLabelFontH = 18;
constexpr int kLegendLabelYOffset = 9;
constexpr int kLegendLabelAlpha = 128; // the stock column's gray

constexpr const char *kHelpTex = ":d2anime\\res\\cmn_help_menue";

constexpr const char *kCompassTex = ":d2anime\\res\\wrp_direct";
constexpr int kCompassRoseX = 930; // posx(-160) + 1090
constexpr int kCompassRoseY = 490;
constexpr int kCompassRoseSize = 128;
constexpr int kCompassRoseAlpha = 128; // alpha(255) - 127

constexpr const char *kBackdropTex = ":d2anime\\res\\wrp_sozai";

// Matching L_wrmap's own graph paper: MapPos.pri + 1, alpha - 204.
constexpr int kOverlayPri = 11;
constexpr int kOverlayAlpha = 51;

constexpr int kGridX = 253; // posx(-160) + 413
constexpr int kGridStartY = 97;
constexpr int kGridStepY = 40;
constexpr int kGridCount = 13;
constexpr int kGridW = 896;
constexpr int kGridH = 64;

constexpr int kHTickStartX = 293; // posx(-160) + 453
constexpr int kHTickY = 95;
constexpr int kHTickStepX = 40;
constexpr int kHTickCount = 19;
constexpr int kHTickW = 64;
constexpr int kHTickH = 32;

constexpr int kVTickX = 258; // posx(-160) + 418
constexpr int kVTickStartY = 135;
constexpr int kVTickStepY = 40;
constexpr int kVTickCount = 12;
constexpr int kVTickW = 32;
constexpr int kVTickH = 64;

constexpr UVRect kUvWrpHogan = {0.0f, 0.0f, 0.875f, 0.5f};
constexpr UVRect kUvWrpMemoriYoko = {0.0f, 0.75f, 0.0625f, 1.0f};
constexpr UVRect kUvWrpMemoriTate = {0.96875f, 0.0f, 1.0f, 0.5f};

void Icon(CsvBuilder &b, const char *flag, int x, const PromptGlyph &glyph) {
  Glyphs::EmitChordCap(b,
                       AnimeTexAbs{.frameStart = flag,
                                   .x = x,
                                   .y = kIconY,
                                   .w = kIconSize,
                                   .h = kIconSize,
                                   .priority = 0,
                                   .file = kHelpTex,
                                   .helpUv = &glyph.uv},
                       glyph.posVar);
}

void LegendRow(CsvBuilder &b, int row) {
  // No .font, so the row keeps the engine's own drop shadowed one.
  b.message(AnimeMessageAbs{.frameStart = kPromptLegendFlg[row],
                            .x = kLegendLabelX,
                            .y = LegendRowY(row) + kLegendLabelYOffset,
                            .fontW = kLegendLabelFontW,
                            .fontH = kLegendLabelFontH,
                            .priority = 0,
                            .alpha = kLegendLabelAlpha,
                            .r = 255,
                            .g = 255,
                            .b = 255,
                            .contentVar = kPromptLegendLabel[row]});
}

void Label(CsvBuilder &b, const char *flag, int x, const char *contentVar) {
  b.message(AnimeMessageAbs{.frameStart = flag,
                            .x = x,
                            .y = kLabelY,
                            .fontW = kLabelFontW,
                            .fontH = kLabelFontH,
                            .priority = 0,
                            .contentVar = contentVar,
                            .posType = "left",
                            .font = "meiryo"});
}

void BuildGridAndTicks(CsvBuilder &b) {
  for (int i = 0; i < kGridCount; ++i) {
    b.tex(AnimeTexAbs{.frameStart = kPromptAreaFlg,
                      .x = kGridX,
                      .y = kGridStartY + i * kGridStepY,
                      .w = kGridW,
                      .h = kGridH,
                      .priority = kOverlayPri,
                      .alpha = kOverlayAlpha,
                      .uv = kUvWrpHogan,
                      .file = kBackdropTex});
  }
  b.blank();

  for (int i = 0; i < kHTickCount; ++i) {
    b.tex(AnimeTexAbs{.frameStart = kPromptAreaFlg,
                      .x = kHTickStartX + i * kHTickStepX,
                      .y = kHTickY,
                      .w = kHTickW,
                      .h = kHTickH,
                      .priority = kOverlayPri,
                      .alpha = kOverlayAlpha,
                      .uv = kUvWrpMemoriYoko,
                      .file = kBackdropTex});
  }
  b.blank();

  for (int i = 0; i < kVTickCount; ++i) {
    b.tex(AnimeTexAbs{.frameStart = kPromptAreaFlg,
                      .x = kVTickX,
                      .y = kVTickStartY + i * kVTickStepY,
                      .w = kVTickW,
                      .h = kVTickH,
                      .priority = kOverlayPri,
                      .alpha = kOverlayAlpha,
                      .uv = kUvWrpMemoriTate,
                      .file = kBackdropTex});
  }
  b.blank();
}

} // namespace

std::string BuildLocalMapPromptCSV() {
  CsvBuilder b;

  b.comment("re:Blue - area map prompts and marker legend over the world map "
            "screen.");
  // Both start hidden: the layout loads with the field scene, well before the
  // map screen has anything to say.
  b.var(FloatV{kPromptOpenFlg, -1.0});
  b.var(FloatV{kPromptAreaFlg, -1.0});
  b.var(StringV{kPromptOpenLabel, "Area Map"});
  b.var(StringV{kPromptBackLabel, "World Map"});
  b.var(StringV{kPromptZoomLabel, "Zoom"});
  b.var(FloatV{kPromptFloorFlg, -1.0});
  b.var(StringV{kPromptFloorLabel, "Floor"});
  b.var(StringV{kPromptTitleLabel, "Area Map"});
  b.var(StringV{kPromptCountLabel, ""});
  for (int row = 0; row < kLegendRows; ++row) {
    b.var(FloatV{kPromptLegendFlg[row], -1.0});
    b.var(StringV{kPromptLegendLabel[row], ""});
  }
  // The cap rects, written by local_map.cpp before the layout ever shows.
  for (const PromptGlyph &g : kPromptGlyphs) {
    b.pos(AnimePos{g.posVar, 0, 0, 0, 0});
    Glyphs::DeclareChordCap(b, g.posVar);
  }
  b.blank();

  Icon(b, kPromptOpenFlg, kOpenIconX, kPromptGlyphs[kPromptGlyphOpen]);
  Label(b, kPromptOpenFlg, kOpenIconX + kLabelGap, kPromptOpenLabel);
  b.blank();

  BuildGridAndTicks(b);

  b.tex(AnimeTexAbs{.frameStart = kPromptAreaFlg,
                    .x = kCompassRoseX,
                    .y = kCompassRoseY,
                    .w = kCompassRoseSize,
                    .h = kCompassRoseSize,
                    .priority = 0,
                    .alpha = kCompassRoseAlpha,
                    .file = kCompassTex});
  b.blank();

  Icon(b, kPromptAreaFlg, kBackIconX, kPromptGlyphs[kPromptGlyphBack]);
  Label(b, kPromptAreaFlg, kBackIconX + kLabelGap, kPromptBackLabel);
  Icon(b, kPromptAreaFlg, kZoomIconX, kPromptGlyphs[kPromptGlyphZoomOut]);
  Icon(b, kPromptAreaFlg, kZoomIconX2, kPromptGlyphs[kPromptGlyphZoomIn]);
  Label(b, kPromptAreaFlg, kZoomIconX2 + kLabelGap, kPromptZoomLabel);
  Icon(b, kPromptFloorFlg, kFloorIconX, kPromptGlyphs[kPromptGlyphFloor]);
  Label(b, kPromptFloorFlg, kFloorIconX + kLabelGap, kPromptFloorLabel);
  b.blank();

  for (int row = 0; row < kLegendRows; ++row)
    LegendRow(b, row);
  b.blank();

  // Stands in for the stock header title, which the area map hides.
  b.frame(AnimeFrame{.frameStart = kPromptAreaFlg,
                     .y = kHeaderLineY,
                     .h = kHeaderLineH,
                     .priority = kHeaderLinePri,
                     .alphaRef = kHeaderLineAlpha});
  b.message(AnimeMessageAbs{.frameStart = kPromptAreaFlg,
                            .x = kTitleX,
                            .y = kTitleY,
                            .fontW = kTitleFontW,
                            .fontH = kTitleFontH,
                            .priority = 0,
                            .contentVar = kPromptTitleLabel});
  b.message(AnimeMessageAbs{.frameStart = kPromptAreaFlg,
                            .x = kTitleX,
                            .y = kCountY,
                            .fontW = kCountFontW,
                            .fontH = kCountFontH,
                            .priority = 0,
                            .contentVar = kPromptCountLabel,
                            .font = "meiryo"});

  return b.build();
}

} // namespace bd::engine
