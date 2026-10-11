/**
 * @file    ui/pause_overlay.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/pause_overlay.h"

#include <algorithm>
#include <cfloat>
#include <string>

#include <imgui.h>
#include <rex/types.h>

#include "core/i18n.h"
#include "embedded.h"
#include "engine/engine.h"

namespace bd::ui {

namespace {

constexpr float kCanvasW = 1280.0f;
constexpr float kCanvasH = 720.0f;
constexpr float kBoxW = 870.0f;
constexpr float kBoxH = 150.0f;
constexpr float kFontSize = 32.0f;
constexpr float kEdgeWidth = 2.0f;
constexpr float kFrameInset = 4.0f;
constexpr float kFrameWidth = 1.0f;

constexpr ImU32 kWindow = IM_COL32(0, 0, 0, 192);
constexpr ImU32 kEdge = IM_COL32(192, 192, 192, 255);
constexpr ImU32 kFrame = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kText = IM_COL32(255, 255, 255, 255);

ImFont *g_font = nullptr;

} // namespace

PauseOverlay::PauseOverlay(rex::ui::ImGuiDrawer *drawer)
    : rex::ui::ImGuiDialog(drawer) {}

PauseOverlay::~PauseOverlay() = default;

void PauseOverlay::InitFonts(ImFontAtlas *atlas) {
  constexpr auto kFont = bd::Embedded("fonts/HelveticaNeueRoman.otf");
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;
  g_font = atlas->AddFontFromMemoryTTF(const_cast<u8 *>(kFont.data),
                                       static_cast<int>(kFont.size), kFontSize,
                                       &cfg);
}

void PauseOverlay::OnDraw(ImGuiIO &io) {
  if (!bd::engine::CutscenePause::Get().Active())
    return;

  const float scale =
      std::min(io.DisplaySize.x / kCanvasW, io.DisplaySize.y / kCanvasH);
  const ImVec2 size(kBoxW * scale, kBoxH * scale);
  const ImVec2 min((io.DisplaySize.x - size.x) * 0.5f,
                   (io.DisplaySize.y - size.y) * 0.5f);
  const ImVec2 max(min.x + size.x, min.y + size.y);
  const float inset = kFrameInset * scale;

  ImDrawList *dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(min, max, kWindow);
  dl->AddRect(min, max, kEdge, 0.0f, 0, kEdgeWidth * scale);
  dl->AddRect(ImVec2(min.x + inset, min.y + inset),
              ImVec2(max.x - inset, max.y - inset), kFrame, 0.0f, 0,
              kFrameWidth * scale);

  const std::string &text = i18n::Text("common.paused");
  ImFont *font = g_font ? g_font : ImGui::GetFont();
  const float fontSize = kFontSize * scale;
  const ImVec2 extent =
      font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
  dl->AddText(font, fontSize,
              ImVec2(min.x + (size.x - extent.x) * 0.5f,
                     min.y + (size.y - extent.y) * 0.5f),
              kText, text.c_str());
}

} // namespace bd::ui
