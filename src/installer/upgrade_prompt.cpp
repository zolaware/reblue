/**
 * @file    installer/upgrade_prompt.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "installer/upgrade_prompt.h"

#include <cfloat>
#include <utility>

#include <imgui.h>

#include <rex/ui/title_app.h>

#include "core/build_info.h"
#include "core/i18n.h"
#include "platform/platform.h"

namespace bd::installer {

namespace {

const char *T(const char *key) { return i18n::Text(key).c_str(); }

} // namespace

UpgradePrompt::UpgradePrompt(std::filesystem::path install_root,
                             std::string installed, DoneCallback on_done)
    : install_root_(std::move(install_root)), installed_(std::move(installed)),
      on_done_(std::move(on_done)) {
  // Nothing else has read the locale this early: the wizard, which does, is
  // not the path that got here.
  i18n::SyncLocale();
}

void UpgradePrompt::Answer(bool accepted) {
  if (answered_)
    return;
  answered_ = true;
  // Deferred so OnDraw returns before the owner deletes this in the callback.
  auto cb = on_done_;
  rex::ui::InvokeFunction([cb, accepted] { cb(accepted); });
}

void UpgradePrompt::OnDraw(ImGuiIO &) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(620, 0), ImVec2(620, FLT_MAX));
  if (!ImGui::Begin(T("upgrade.title"), nullptr,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }
  ImGui::SetWindowFontScale(1.35f);

  const std::string body =
      installed_.empty()
          ? i18n::Fmt("upgrade.body_unknown", install_root_.string(),
                      REBLUE_VERSION_STRING)
          : i18n::Fmt("upgrade.body", install_root_.string(), installed_,
                      REBLUE_VERSION_STRING);
  ImGui::TextWrapped("%s", body.c_str());
  ImGui::Spacing();
  ImGui::TextWrapped("%s", T("upgrade.notice"));
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const float button_width =
      bd::platform::UI()->GetStyle().metrics.value_width;
  ImGui::BeginDisabled(answered_);
  if (ImGui::Button(T("upgrade.accept"), ImVec2(button_width, 0)))
    Answer(true);
  ImGui::SameLine();
  if (ImGui::Button(T("upgrade.decline"), ImVec2(button_width, 0)))
    Answer(false);
  ImGui::EndDisabled();
  ImGui::End();
}

} // namespace bd::installer
