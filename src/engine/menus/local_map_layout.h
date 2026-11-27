/**
 * @file    engine/menus/local_map_layout.h
 * @brief   Button prompts and the marker legend for the world map screen's
 *          area map view.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <string>

#include "engine/d2anime/anime_layout.h"

namespace bd::engine {

// Served beside the stock wrmap CSVs so ':d2anime\res\cmn_help_menue' and the
// global uv.csv resolve for it the way they do for L_wrmap_ftr.csv.
inline constexpr const char *kLocalMapPromptCSV =
    "d2anime\\wrmap\\l_wrmap_help.csv";

// Float vars gating a prompt row: 1 shows it, -1 hides it. Named apart from
// L_wrmap.csv's own WorldFlg, which means something else in its own bag.
inline constexpr const char *kPromptOpenFlg = "OpenFlg";
inline constexpr const char *kPromptAreaFlg = "AreaFlg";
inline constexpr const char *kPromptFloorFlg = "FloorFlg";

// The button caps, through per-layout element vars (see PromptGlyph): this
// layout lives a whole field scene, so a rebind or a device switch would
// never reach a uv. snapshot. local_map.cpp rewrites these on every glyph
// generation.
inline constexpr PromptGlyph kPromptGlyphs[] = {
    {"OpenUv", {"OpenUv.x", "OpenUv.y", "OpenUv.w", "OpenUv.h"},
     Action::AreaMap},
    {"BackUv", {"BackUv.x", "BackUv.y", "BackUv.w", "BackUv.h"},
     Action::AreaMap},
    {"ZoomOutUv",
     {"ZoomOutUv.x", "ZoomOutUv.y", "ZoomOutUv.w", "ZoomOutUv.h"},
     Action::ZoomOut},
    {"ZoomInUv",
     {"ZoomInUv.x", "ZoomInUv.y", "ZoomInUv.w", "ZoomInUv.h"},
     Action::ZoomIn},
    {"FloorUv",
     {"FloorUv.x", "FloorUv.y", "FloorUv.w", "FloorUv.h"},
     Action::Count},
};
inline constexpr int kPromptGlyphOpen = 0;
inline constexpr int kPromptGlyphBack = 1;
inline constexpr int kPromptGlyphZoomOut = 2;
inline constexpr int kPromptGlyphZoomIn = 3;
inline constexpr int kPromptGlyphFloor = 4;

// String vars carrying the host's own labels.
inline constexpr const char *kPromptOpenLabel = "OpenLabel";
inline constexpr const char *kPromptBackLabel = "BackLabel";
inline constexpr const char *kPromptZoomLabel = "ZoomLabel";
inline constexpr const char *kPromptFloorLabel = "FloorLabel";
inline constexpr const char *kPromptTitleLabel = "TitleLabel";
inline constexpr const char *kPromptCountLabel = "CountLabel";

// The marker legend, right-aligned inside the map window where the stock
// legend column stood. A square floor texture fits the 520-tall window with
// 136 to spare on either side, so the panel sits on that band rather than on
// the map. One row per distinct swatch the map can draw, filled from the top
// with the ones this map needs. Each row's label is CSV, its swatch a prim
// local_map.cpp draws at these coordinates, since only the marker code knows
// the marker geometry.
//
// The two x values are the stock column's, posx(-160) plus 1065 and 1091.
inline constexpr int kLegendRows = 9;
inline constexpr int kLegendTop = 248; // top row against the window's top edge
inline constexpr int kLegendRowH = 30; // the stock 377 -> 407 -> 437 pitch
inline constexpr int kLegendSwatchX = 905;
inline constexpr int kLegendLabelX = 931;

inline constexpr int LegendRowY(int row) {
  return kLegendTop + row * kLegendRowH;
}
inline constexpr int LegendSwatchY(int row) {
  return LegendRowY(row) + kLegendRowH / 2;
}

inline constexpr const char *kPromptLegendFlg[kLegendRows] = {
    "Legend0Flg", "Legend1Flg", "Legend2Flg", "Legend3Flg", "Legend4Flg",
    "Legend5Flg", "Legend6Flg", "Legend7Flg", "Legend8Flg"};
inline constexpr const char *kPromptLegendLabel[kLegendRows] = {
    "Legend0Label", "Legend1Label", "Legend2Label",
    "Legend3Label", "Legend4Label", "Legend5Label",
    "Legend6Label", "Legend7Label", "Legend8Label"};

std::string BuildLocalMapPromptCSV();

} // namespace bd::engine
