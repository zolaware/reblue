/**
 * @file    ui/update_status.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/update_status.h"

#include <string>

#include <imgui.h>
#include <rex/ui/style.h>

#include "core/i18n.h"
#include "engine/engine.h"
#include "platform/platform.h"

namespace bd::ui {

namespace {

// The content sync starts once the manifest naming it has been read, so this
// order is also the order they run in.
std::string CurrentLine() {
  using Updates = bd::platform::Updates;
  using Sync = bd::platform::ContentSync;

  // The title's own prompt puts both checks on screen while it holds.
  if (bd::engine::UpdatePrompt::Get().Active())
    return {};

  if (Updates::Get().State() == Updates::Stage::kChecking)
    return i18n::Text("update.status.checking");

  auto &sync = Sync::Get();
  switch (sync.State()) {
  case Sync::Stage::kChecking:
    return i18n::Text("update.status.content");
  case Sync::Stage::kFetching:
    return i18n::Fmt("update.status.downloading", sync.Current(),
                     sync.Done() + 1, sync.Total());
  default:
    return {};
  }
}

} // namespace

UpdateStatusOverlay::~UpdateStatusOverlay() = default;

bool UpdateStatusOverlay::Finished() { return CurrentLine().empty(); }

bool UpdateStatusOverlay::Draw(ImGuiIO &io) {
  const std::string line = CurrentLine();
  if (line.empty())
    return true;

  constexpr float kPad = 10.0f;
  constexpr rex::ui::Color kText = rex::ui::Color::FromRGBA8(255, 255, 255, 140);
  constexpr rex::ui::Color kShadow = rex::ui::Color::FromRGBA8(0, 0, 0, 128);

  ImDrawList *dl = ImGui::GetForegroundDrawList();
  const ImVec2 pos(kPad, io.DisplaySize.y - kPad - ImGui::GetTextLineHeight());
  dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), rex::ui::ToImU32(kShadow), line.c_str());
  dl->AddText(pos, rex::ui::ToImU32(kText), line.c_str());
  return true;
}

} // namespace bd::ui
