/**
 * @file    ui/pause_overlay.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/pause_overlay.h"

#include <rex/ui/overlay.h>

#include <algorithm>
#include <cfloat>
#include <string>

#include <imgui.h>
#include <rex/types.h>
#include <rex/color.h>
#include <rex/ui/color.h>

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

constexpr rex::Color kWindow = rex::Color::FromRGBA8(0, 0, 0, 192);
constexpr rex::Color kEdge = rex::Color::FromRGBA8(192, 192, 192);
constexpr rex::Color kFrame = rex::Color::FromRGBA8(240, 240, 240);
constexpr rex::Color kText = rex::colors::kWhite;

ImFont *g_font = nullptr;

} // namespace

PauseOverlay::PauseOverlay(const rex::ui::OverlayContext &) {}

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
  dl->AddRectFilled(min, max, rex::ui::ToImU32(kWindow));
  dl->AddRect(min, max, rex::ui::ToImU32(kEdge), 0.0f, 0, kEdgeWidth * scale);
  dl->AddRect(ImVec2(min.x + inset, min.y + inset),
              ImVec2(max.x - inset, max.y - inset), rex::ui::ToImU32(kFrame),
              0.0f, 0, kFrameWidth * scale);

  const std::string &text = i18n::Text("common.paused");
  ImFont *font = g_font ? g_font : ImGui::GetFont();
  const float fontSize = kFontSize * scale;
  const ImVec2 extent =
      font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
  dl->AddText(font, fontSize,
              ImVec2(min.x + (size.x - extent.x) * 0.5f,
                     min.y + (size.y - extent.y) * 0.5f),
              rex::ui::ToImU32(kText), text.c_str());
}

} // namespace bd::ui
