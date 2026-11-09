/**
 * @file    ui/theme.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include "ui/theme.h"

#include <rex/ui/color.h>

namespace bd::ui {

namespace {

constexpr rex::Color kAccent = rex::Color::FromRGBA(0.082f, 0.063f, 0.200f);
constexpr rex::Color kAccentDeep = rex::Color::FromRGBA(0.049f, 0.039f, 0.122f);
constexpr rex::Color kAccentHovered = rex::Color::FromRGBA(0.141f, 0.114f, 0.341f);
constexpr rex::Color kAccentActive = rex::Color::FromRGBA(0.208f, 0.169f, 0.494f);
constexpr rex::Color kAccentSelected = rex::Color::FromRGBA(0.267f, 0.212f, 0.651f);

constexpr rex::Color White(f32 alpha) {
  return rex::colors::kWhite.WithAlpha(alpha);
}

ImVec4 Vec(rex::Color color) { return rex::ui::ToImVec4(color); }

ImVec4 WhiteVec(f32 alpha) { return Vec(White(alpha)); }

} // namespace

void Theme::Apply(rex::ui::Style &style) {
  rex::ui::Palette &palette = style.palette;
  palette.accent = kAccent;
  palette.button_alpha = 1.0f;
  palette.accent_hovered = kAccentHovered;
  palette.accent_active = kAccentActive;
  palette.panel = kAccent;
  palette.panel_popup = kAccentDeep.WithAlpha(0.98f);
  palette.text = rex::colors::kWhite;
  palette.text_disabled = White(0.42f);
  palette.border = White(0.28f);
  palette.highlight = kAccentSelected;
  palette.shadow = rex::colors::kTransparent;
  palette.overlay_dim = rex::colors::kBlack.WithAlpha(0.55f);

  rex::ui::ControlPalette &controls = palette.controls;
  controls.frame = White(0.08f);
  controls.frame_hovered = White(0.14f);
  controls.frame_active = White(0.20f);
  controls.title = kAccentDeep;
  controls.title_collapsed = kAccentDeep.WithAlpha(0.75f);
  controls.title_active = kAccent;
  controls.menu_bar = kAccentDeep;
  controls.scrollbar = White(0.06f);
  controls.scrollbar_grab = White(0.24f);
  controls.scrollbar_grab_hovered = White(0.38f);
  controls.scrollbar_grab_active = White(0.52f);
  controls.check_mark = rex::colors::kWhite;
  controls.slider_grab = White(0.70f);
  controls.slider_grab_active = rex::colors::kWhite;
  controls.header = kAccentHovered;
  controls.header_hovered = kAccentActive;
  controls.header_active = kAccentSelected;
  controls.separator = White(0.22f);
  controls.separator_hovered = White(0.45f);
  controls.separator_active = White(0.70f);
  controls.resize_grip = White(0.20f);
  controls.resize_grip_hovered = White(0.45f);
  controls.resize_grip_active = White(0.70f);
  controls.tab = kAccentDeep;
  controls.tab_hovered = kAccentHovered;
  controls.tab_active = kAccentActive;
  controls.tab_unfocused = kAccentDeep;
  controls.tab_unfocused_active = kAccent;
  controls.plot_lines = White(0.90f);
  controls.plot_lines_hovered = rex::colors::kWhite;
  controls.plot_histogram = White(0.75f);
  controls.plot_histogram_hovered = rex::colors::kWhite;
  controls.selection = White(0.22f);

  style.metrics.window_rounding = 0.0f;
  style.metrics.rounding = 4.0f;
  style.metrics.frame_rounding = 4.0f;
  style.metrics.border = 1.0f;

  style.toast.text = rex::colors::kWhite;
  style.toast.title = rex::colors::kWhite;
  style.achievements.header_text = White(0.90f);
}

void Theme::ApplyImGui(ImGuiStyle &style) {
  style.ChildRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.FrameBorderSize = 1.0f;

  ImVec4 *c = style.Colors;
  c[ImGuiCol_InputTextCursor] = WhiteVec(1.00f);
  c[ImGuiCol_TabSelectedOverline] = WhiteVec(0.85f);
  c[ImGuiCol_TabDimmedSelectedOverline] = WhiteVec(0.35f);
  c[ImGuiCol_TableHeaderBg] = Vec(kAccentHovered);
  c[ImGuiCol_TableBorderStrong] = WhiteVec(0.30f);
  c[ImGuiCol_TableBorderLight] = WhiteVec(0.16f);
  c[ImGuiCol_TableRowBg] = Vec(rex::colors::kTransparent);
  c[ImGuiCol_TableRowBgAlt] = WhiteVec(0.04f);
  c[ImGuiCol_TextLink] = WhiteVec(0.85f);
  c[ImGuiCol_TreeLines] = WhiteVec(0.25f);
  c[ImGuiCol_DragDropTarget] = WhiteVec(0.90f);
  c[ImGuiCol_DragDropTargetBg] = WhiteVec(0.15f);
  c[ImGuiCol_UnsavedMarker] = WhiteVec(0.80f);
  c[ImGuiCol_NavCursor] = WhiteVec(0.80f);
  c[ImGuiCol_NavWindowingHighlight] = WhiteVec(0.70f);
  c[ImGuiCol_NavWindowingDimBg] = Vec(rex::colors::kBlack.WithAlpha(0.50f));
}

} // namespace bd::ui
