/**
 * @file    ui/watermark.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/watermark.h"

#include <format>
#include <rex/types.h>
#include <string>

#include <imgui.h>

#include <rex/version.h>

#include "core/build_info.h"
#include "gpu/gpu.h"

namespace bd::ui {

WatermarkOverlay::WatermarkOverlay(rex::ui::ImGuiDrawer *drawer)
    : rex::ui::ImGuiDialog(drawer) {}

WatermarkOverlay::~WatermarkOverlay() = default;

void WatermarkOverlay::OnDraw(ImGuiIO &io) {
  // The resolution and backend lines are last because they are the ones that
  // can be empty: the device is up long before the first overlay draw, but a
  // device that failed to start leaves them blank rather than lying about
  // what is running.
  const std::string &backend = bd::gpu::Video::GetBackendInfo();

  // Render size, then the display size when the present blit is scaling to it
  // (a fullscreen resolution below the monitor, or a resized window).
  u32 render_w = 0, render_h = 0;
  std::string resolution;
  if (bd::gpu::Output::RenderSize(render_w, render_h)) {
    resolution = std::format("{}x{}", render_w, render_h);
    const u32 out_w = bd::gpu::Video::OutputWidth();
    const u32 out_h = bd::gpu::Video::OutputHeight();
    if (out_w && out_h && (out_w != render_w || out_h != render_h))
      resolution += std::format(" -> {}x{}", out_w, out_h);
  }

  const char *lines[5];
  size_t line_count = 0;
  lines[line_count++] = "re:Blue v" REBLUE_VERSION_STRING;
  lines[line_count++] = "built " __DATE__;
  lines[line_count++] = "rexglue v" REXGLUE_VERSION_STRING;
  if (!resolution.empty())
    lines[line_count++] = resolution.c_str();
  if (!backend.empty())
    lines[line_count++] = backend.c_str();
  constexpr float kPad = 10.0f;
  constexpr ImU32 kText = IM_COL32(255, 255, 255, 96);
  constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 112);

  ImDrawList *dl = ImGui::GetForegroundDrawList();
  const float line_h = ImGui::GetTextLineHeight();
  float y = io.DisplaySize.y - kPad - line_h * float(line_count);
  for (size_t i = 0; i < line_count; ++i) {
    const char *line = lines[i];
    const ImVec2 size = ImGui::CalcTextSize(line);
    const ImVec2 pos(io.DisplaySize.x - kPad - size.x, y);
    dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), kShadow, line);
    dl->AddText(pos, kText, line);
    y += line_h;
  }
}

} // namespace bd::ui
