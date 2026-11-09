/**
 * @file    ui/theme.h
 * @brief   re:Blue chrome palette for the host ImGui overlays.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <imgui.h>
#include <rex/ui/style.h>

namespace bd::ui {

struct Theme {
  static constexpr rex::ui::Color kSuccess = rex::ui::Color::FromRGBA(0.30f, 0.90f, 0.30f);
  static constexpr rex::ui::Color kWarning = rex::ui::Color::FromRGBA(0.90f, 0.70f, 0.30f);
  static constexpr rex::ui::Color kError = rex::ui::Color::FromRGBA(0.90f, 0.40f, 0.30f);

  static void Apply(rex::ui::Style &style);

  static void ApplyImGui(ImGuiStyle &style);
};

} // namespace bd::ui
