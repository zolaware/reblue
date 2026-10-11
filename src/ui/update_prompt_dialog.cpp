/**
 * @file    ui/update_prompt_dialog.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "ui/update_prompt_dialog.h"

#include <cfloat>

#include <imgui.h>

#include "core/i18n.h"
#include "platform/platform.h"

namespace bd::ui {

namespace {

using Updates = bd::platform::Updates;

const char *T(const char *key) { return i18n::Text(key).c_str(); }

const char *ErrorKey(Updates::ApplyResult result) {
  switch (result) {
  case Updates::ApplyResult::kDownloadFailed:
    return "update.error.download_failed";
  case Updates::ApplyResult::kHashMismatch:
    return "update.error.hash_mismatch";
  default:
    return "update.error.apply_failed";
  }
}

} // namespace

UpdatePromptDialog::UpdatePromptDialog(rex::ui::ImGuiDrawer *drawer,
                                       UpdatePromptContext ctx)
    : rex::ui::ImGuiDialog(drawer), ctx_(std::move(ctx)) {}

void UpdatePromptDialog::OnDraw(ImGuiIO &) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  // Auto height, pinned width: the download view is taller than the offer, and
  // a height fixed on the first frame gets a scrollbar instead of growing.
  ImGui::SetNextWindowSizeConstraints(ImVec2(520, 0), ImVec2(520, FLT_MAX));
  if (!ImGui::Begin(T("update.title"), nullptr,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }
  ImGui::SetWindowFontScale(1.35f);

  auto &updates = Updates::Get();
  const auto stage =
      accepted_ ? updates.ApplyState() : Updates::ApplyStage::kIdle;

  if (stage == Updates::ApplyStage::kWorking) {
    ImGui::TextUnformatted(T("update.downloading"));
    ImGui::Spacing();
    const u64 total = updates.ApplyBytesTotal();
    const u64 done = updates.ApplyBytesDone();
    const float fraction =
        total == 0 ? 0.0f
                   : static_cast<float>(done) / static_cast<float>(total);
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), nullptr);
    ImGui::Text("%s / %s", i18n::Bytes(done).c_str(),
                i18n::Bytes(total).c_str());
    ImGui::End();
    return;
  }

  if (stage == Updates::ApplyStage::kDone) {
    const auto result = updates.Applied();
    if (result == Updates::ApplyResult::kStaged) {
      ImGui::TextWrapped("%s",
                         i18n::Fmt("update.staged", ctx_.version).c_str());
      ImGui::End();
      // The swap runs at startup, so the restart is what applies it.
      bd::platform::RequestWarmReboot();
      return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.40f, 0.30f, 1.0f));
    ImGui::TextWrapped("%s", T(ErrorKey(result)));
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button(T("update.close"), ImVec2(120, 0)))
      Close();
    ImGui::End();
    return;
  }

  ImGui::TextWrapped(
      "%s",
      i18n::Fmt("update.body", ctx_.version, i18n::Bytes(ctx_.size)).c_str());
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button(T("update.accept"), ImVec2(140, 0))) {
    accepted_ = true;
    updates.BeginApply();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("update.decline"), ImVec2(140, 0)))
    Close();
  ImGui::End();
}

} // namespace bd::ui
