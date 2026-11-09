/**
 * @file    ui/perf_overlay.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/perf_overlay.h"


#include <algorithm>
#include <format>
#include <string>

#include <imgui.h>
#include <rex/types.h>

#include "core/frame_meter.h"

namespace bd::ui {

namespace {

constexpr float kPad = 12.0f;
constexpr ImU32 kText = IM_COL32(255, 255, 255, 220);
constexpr ImU32 kDim = IM_COL32(200, 200, 200, 170);
constexpr ImU32 kHitch = IM_COL32(255, 110, 110, 230);

ImU32 BoundColor(bd::FrameBound bound) {
  switch (bound) {
  case bd::FrameBound::Gpu:
    return IM_COL32(255, 150, 90, 230);
  case bd::FrameBound::CpuMain:
  case bd::FrameBound::CpuRender:
    return IM_COL32(120, 200, 255, 230);
  default:
    return kDim;
  }
}

void Line(ImU32 color, const std::string &text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopStyleColor();
}

} // namespace

PerfOverlay::PerfOverlay(rex::ui::IUISystem &) {}

PerfOverlay::~PerfOverlay() = default;

bool PerfOverlay::Draw(ImGuiIO &io) {
  if (!bd::FrameMeterOn())
    return true;
  const bd::FrameMeterSnapshot s = bd::FrameMeterRead();
  if (!s.valid)
    return true;

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kPad, kPad),
                          ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.35f);
  constexpr ImGuiWindowFlags kFlags =
      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
  if (!ImGui::Begin("##perfoverlay", nullptr, kFlags)) {
    ImGui::End();
    return true;
  }

  Line(kText, std::format("{:.1f} fps   {:.2f} ms", s.fps, s.frameMs));
  Line(s.worstMs > s.frameMs * 2.0 ? kHitch : kDim,
       std::format("worst {:.1f} ms in 1 s", s.worstMs));
  Line(kDim, std::format("CPU main {:.1f} ms per tick", s.mainMs));
  Line(kDim, std::format("CPU render {:.1f} ms", s.renderMs));
  Line(kDim, s.gpuAvailable ? std::format("GPU {:.1f} ms", s.gpuMs)
                            : std::string("GPU n/a"));
  Line(BoundColor(s.bound),
       std::format("bound by {}", bd::FrameBoundName(s.bound)));

  f32 peak = 1.0f;
  for (u32 i = 0; i < s.graphCount; ++i)
    peak = std::max(peak, s.graph[i]);
  ImGui::PlotLines("##frametimes", s.graph, static_cast<int>(s.graphCount), 0,
                   nullptr, 0.0f, peak, ImVec2(220.0f, 40.0f));
  ImGui::End();
  return true;
}

} // namespace bd::ui
