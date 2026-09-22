/**
 * @file    engine/menus/config_menu_visuals.cpp
 * @brief   ConfigMenu row visuals, detail panels and footer.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/config_menu.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/settings_model.h"
#include "core/text_wrap.h"
#include "engine/achievements/achievement_list.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime.h"
#include "engine/game_options.h"
#include "engine/glyph_set.h"
#include "engine/input/binding_store.h"
#include "engine/menus/achievements_layout.h"
#include "engine/menus/config_layout.h"
#include "engine/menus/config_menu_data.h"

#include <algorithm>

#include <rex/types.h>

namespace bd::engine {

namespace {

// SetColor takes 0xAARRGGBB.
constexpr u32 kWhite = 0xFFFFFFFFu;
constexpr u32 kHighlightYellow = 0xFFFFDC00; // restart-bound and capturing
constexpr u32 kReorderGold = 0xFFFFD700;     // the row being carried
constexpr u32 kFixedGray = 0xFFA0A0A0;
constexpr double kDimAlpha = 90.0, kFullAlpha = 255.0;

// Wrapped to the two description lines above the footer, which start at x=325.
// The second one shares its band with the restart note at x=1010, so the run
// is 685px at the 15px font's ~8px advance, and wrapping is by glyph count,
// so this is short of that by enough to absorb unusually wide ones.
constexpr int kRowDescWrapChars = 82;

constexpr int kNoticeWrapChars = 50;

double DimFor(bool disabled) { return disabled ? kDimAlpha : kFullAlpha; }

} // namespace

void ConfigMenu::UpdateDetailPanel(int index) {
  const auto &info = ModAt(index);

  task_.SetFloat("detail.start", 1.0);
  task_.SetText("detail.Author", info.author.empty()
                                     ? i18n::Text("menu.detail.unknown_author")
                                     : info.author);
  task_.SetText("detail.Version",
                info.version.empty() ? std::string("-") : info.version);
  task_.SetText("detail.Created",
                info.created.empty() ? std::string("-") : info.created);
  task_.SetText("detail.Desc", info.description.empty()
                                   ? i18n::Text("menu.detail.no_description")
                                   : WordWrap(info.description, 46));

  if (info.image.empty()) {
    task_.SetFloat("detail.HasPreview", -1.0);
    return;
  }
  const std::string previewRef = "res\\preview_" + std::to_string(index);
  task_.SetString("detail.PreviewFile", previewRef.c_str());
  task_.SetFloat("detail.HasPreview", 1.0);
}

void ConfigMenu::HideDetailPanel() {
  task_.SetFloat("detail.start", -1.0);
  task_.SetFloat("detail.HasPreview", -1.0);
}

void ConfigMenu::UpdateDLCDetail(int index) {
  auto &dlc = DLC();
  if (index < 0 || index >= static_cast<int>(dlc.Count()))
    return;

  const auto &info = dlc.At(static_cast<size_t>(index));

  task_.SetFloat("dlcdetail.start", 1.0);
  const std::string wrapped =
      info.description.empty() ? "" : WordWrap(info.description, 46);
  size_t pos = 0;
  for (int i = 0; i < DlcDetailTemplate::kDescLines; ++i) {
    std::string val;
    if (pos < wrapped.size()) {
      size_t nl = wrapped.find('\n', pos);
      if (nl == std::string::npos)
        nl = wrapped.size();
      val = wrapped.substr(pos, nl - pos);
      pos = nl + 1;
    }
    task_.SetText(fmt::format("dlcdetail.Desc{}", i).c_str(), val);
  }
}

void ConfigMenu::HideDLCDetail() { task_.SetFloat("dlcdetail.start", -1.0); }

void ConfigMenu::UpdateLanguageDetail(int index) {
  if (index < 0 || index >= static_cast<int>(LanguageCount())) {
    HideDLCDetail();
    return;
  }

  task_.SetFloat("dlcdetail.start", 1.0);
  task_.SetText("dlcdetail.Desc0", LanguageKinds(index));
  task_.SetText("dlcdetail.Desc1",
                i18n::Fmt("menu.language.on_disk", LanguageSize(index)));
  task_.SetText("dlcdetail.Desc2",
                LanguageRemovable(index)
                    ? std::string()
                    : i18n::Text("menu.language.required"));
}

void ConfigMenu::ShowLanguageNotice(const std::string &text) {
  const auto lines = WrapTwoLines(text, kNoticeWrapChars);
  notice_popup_.Show(task_, lines[0], lines[1]);
}

void ConfigMenu::SetRowDesc(const std::string &text) {
  auto &layout = GetLayout();
  const auto lines = WrapTwoLines(text, kRowDescWrapChars);
  // One line sits centered in the band under the list, two lines fill it.
  const bool single = lines[1].empty();
  layout.rowDescC.set(single ? lines[0] : std::string());
  layout.rowDesc0.set(single ? std::string() : lines[0]);
  layout.rowDesc1.set(lines[1]);
}

void ConfigMenu::UpdateAchvRowDesc(int cursor) {
  const auto &rows = GetAchievementList();
  SetRowDesc(cursor >= 0 && cursor < static_cast<int>(rows.size())
                 ? rows[cursor].desc
                 : std::string());
}

void ConfigMenu::UpdateFooter() {
  switch (state_) {
  case State::SECTION:
    SetFooter({.a = "footer.select",
               .b = surface_ == Surface::InGame ? "footer.back"
                                                : "footer.exit"});
    break;
  case State::MODLIST:
    if (ModCount() == 0)
      SetFooter({.b = "footer.back", .back = "footer.install"});
    else
      SetFooter({.a = "footer.reorder",
                 .b = "footer.back",
                 .x = "footer.delete",
                 .y = "footer.toggle",
                 .back = "footer.install"});
    break;
  case State::DLCLIST:
    if (DlcCount() == 0)
      SetFooter({.b = "footer.back", .back = "footer.install_dlc"});
    else
      SetFooter({.b = "footer.back",
                 .x = "footer.delete",
                 .y = "footer.toggle",
                 .back = "footer.install_dlc"});
    break;
  case State::LANGLIST: {
#ifdef REBLUE_BUILD_INSTALLER
    const int cursor = MenusReady() ? langlist_menu_.CursorIndex() : -1;
    const bool removable = cursor >= 0 &&
                           cursor < static_cast<int>(LanguageCount()) &&
                           LanguageRemovable(cursor);
    SetFooter({.b = "footer.back",
               .x = removable ? "footer.delete" : nullptr,
               .back = "footer.install_language"});
#else
    SetFooter({.b = "footer.back"});
#endif
    break;
  }
  case State::LANGADD:
  case State::LANGPICK:
    SetFooter({});
    break;
  case State::LANGNOTICE:
    SetFooter({.b = "footer.back"});
    break;
  case State::LANGJOB:
    SetFooter(LanguageJobCancelable() ? FooterLabels{.b = "footer.cancel"}
                                      : FooterLabels{});
    break;
  case State::SETTINGS: {
    // A opens the keybind screen only while an action row is highlighted.
    const int slot = MenusReady() ? CurrentSettingsList().CursorIndex() : -1;
    const int cursor = SettingsSlotToRow(settings_page_, slot);
    const bool action =
        cursor >= 0 && SettingsRowUi(settings_page_, cursor) == RowUi::Action &&
        !SettingsDisabled(settings_page_, cursor);
    SetFooter({.a = action ? "footer.configure" : nullptr, .b = "footer.back"});
    break;
  }
  case State::ACHVLIST:
    SetFooter({.b = "footer.back"});
    break;
  case State::KEYBINDS:
    SetFooter({.a = "footer.rebind",
               .b = "footer.back",
               .x = "footer.clear",
               .back = "footer.reset_binds"});
    break;
  case State::KEYBIND_CAPTURE:
    SetFooter({});
    break;
  case State::REORDER:
    SetFooter({.a = "footer.place", .b = "footer.cancel"});
    break;
  case State::CONFIRM_DELETE:
  case State::CONFIRM_REBOOT:
  case State::CONFIRM_RESET_BINDS:
    SetFooter({});
    break;
  default:
    break;
  }

  GetLayout().SyncVars(task_.AnimeData());
}

void ConfigMenu::PopulateNames() {
  const auto sections = static_cast<size_t>(GetLayout().SectionCount());
  section_menu_.ForEachRow(sections, [&](int, int i, AnimeData vb) {
    vb.SetText("Name", i18n::Text(ConfigLayout::kSectionKeys[i]));
  });

  // The trailing slots of a short list take the empty-list notice rather than
  // keeping whatever the last longer list left in them. Written for the list
  // the sidebar previews as much as for the one this menu has entered.
  const State content = ContentState();
  if (content == State::MODLIST || content == State::REORDER) {
    const size_t modCount = ModCount();
    modlist_menu_.ForEachSlot([&](int, int i, AnimeData vb) {
      if (i < static_cast<int>(modCount))
        vb.SetText("Name", ModAt(i).name);
      else
        vb.SetText("Name", modCount == 0 ? i18n::Text("menu.list.no_mods")
                                         : std::string());
    });
  } else if (content == State::DLCLIST) {
    auto &dlc = DLC();
    dlclist_menu_.ForEachSlot([&](int, int i, AnimeData vb) {
      if (i < static_cast<int>(dlc.Count()))
        vb.SetText("Name", dlc.At(static_cast<size_t>(i)).display_name);
      else
        vb.SetText("Name", dlc.Count() == 0 ? i18n::Text("menu.list.no_dlc")
                                            : std::string());
    });
  } else if (content == State::LANGLIST || content == State::LANGADD ||
             content == State::LANGPICK || content == State::LANGJOB ||
             content == State::LANGNOTICE) {
    const size_t langCount = LanguageCount();
    langlist_menu_.ForEachSlot([&](int, int i, AnimeData vb) {
      if (i < static_cast<int>(langCount))
        vb.SetText("Name", LanguageName(i));
      else
        vb.SetText("Name", langCount == 0
                               ? i18n::Text("menu.list.no_languages")
                               : std::string());
    });
  } else if (content == State::ACHVLIST) {
    AchievementRowTemplate::PopulateNames(achvlist_menu_);
  }
}

void ConfigMenu::RefreshAchvVisuals() {
  AchievementRowTemplate::RefreshVisuals(achvlist_menu_);
}

void ConfigMenu::RefreshModVisuals() {
  const u32 enColor = modlist_menu_.EnableColor();
  const u32 disColor = modlist_menu_.DisableColor();

  modlist_menu_.ForEachRow(ModCount(), [&](int slot, int i, AnimeData vb) {
    const bool enabled = ModIsEnabled(i);
    const bool carried = state_ == State::REORDER && i == reorder_origin_;
    modlist_menu_.SetToggleRow(
        slot, vb, enabled,
        carried ? kReorderGold : (enabled ? enColor : disColor));
  });
}

void ConfigMenu::RefreshDLCVisuals() {
  const u32 enColor = dlclist_menu_.EnableColor();
  const u32 disColor = dlclist_menu_.DisableColor();

  dlclist_menu_.ForEachRow(DLC().Count(), [&](int slot, int i, AnimeData vb) {
    const bool enabled = IsDLCEnabled(i);
    dlclist_menu_.SetToggleRow(slot, vb, enabled,
                               enabled ? enColor : disColor);
  });
}

// Re-applied every frame (engine async init resets template vars): each row
// shows its label plus a button strip, a fill bar slider (continuous or
// stepped), or a single action button. Restart-bound rows get a yellow label,
// matching the page footnote.
void ConfigMenu::RefreshSettingsVisuals() {
  const auto page = settings_page_;

  const auto hideButtons = [](AnimeData vb) {
    for (const auto &opt : kSettingOptVars)
      vb.SetFloat(opt.vis, -1.0);
  };

  const auto row = [&](int, int slot, AnimeData vb) {
    const int i = SettingsSlotToRow(page, slot);
    if (i < 0) {
      hideButtons(vb);
      vb.SetFloat("SldVis", -1.0);
      vb.SetFloat("RowVis", -1.0);
      vb.SetFloat("HdrVis", 1.0);
      vb.SetText("Hdr", SettingsSlotHeader(page, slot));
      return;
    }
    vb.SetFloat("RowVis", 1.0);
    vb.SetFloat("HdrVis", -1.0);

    const auto ui = SettingsRowUi(page, i);
    const bool slider = ui == RowUi::Slider || ui == RowUi::SliderSteps;
    const bool disabled = SettingsDisabled(page, i);
    vb.SetText("Name", SettingsLabel(page, i));
    vb.SetColor("LblCol",
                SettingsRestartBound(page, i) ? kHighlightYellow : kWhite);
    vb.SetFloat("Dim", DimFor(disabled));
    const int rowOpts =
        ui == RowUi::Action ? 1 : SettingsOptionCount(page, i);
    vb.SetFloat("RowW", SettingItemTemplate::RowWidth(slider, rowOpts));

    if (slider) {
        hideButtons(vb);
        const double frac = SettingsSliderFraction(page, i);

        // kSliderWidth = 396
        constexpr double kThumbMargin = 2.0; // Inset from each edge
        constexpr double kThumbTravel = SettingItemTemplate::kSliderWidth - (kThumbMargin * 2.0); // 384.0

        const double fill = frac * SettingItemTemplate::kSliderWidth;
        const double thumbTravelOffset = kThumbMargin + (frac * kThumbTravel);

        vb.SetFloat("SldVis", 1.0);
        vb.SetFloat("SldFill", fill);
        vb.SetFloat("SldRate", frac);
        vb.SetFloat("SldThumbTravel", thumbTravelOffset);
        vb.SetText("SldVal", SettingsValueText(page, i));
        return;
    }

    vb.SetFloat("SldVis", -1.0);

    if (ui == RowUi::Action) {
      hideButtons(vb);
      const auto &opt = kSettingOptVars[0];
      vb.SetFloat(opt.vis, 1.0);
      vb.SetText(opt.name, i18n::Text("footer.configure"));
      vb.SetString(opt.wnd, "BTN01_OF");
      vb.SetFloat(opt.dim, DimFor(disabled));
      return;
    }

    const int optCount = SettingsOptionCount(page, i);
    // The strip is as wide as the row, so anything past this would be lost
    // silently. Such a row belongs on a slider.
    if (optCount > SettingItemTemplate::kMaxOpts) {
      static bool warned;
      if (!warned) {
        warned = true;
        BD_WARN("config row '{}' has {} options, {} fit in a button strip",
                SettingsLabel(page, i), optCount,
                SettingItemTemplate::kMaxOpts);
      }
    }
    const int sel = SettingsSelectedOption(page, i);
    for (int k = 0; k < SettingItemTemplate::kMaxOpts; ++k) {
      const auto &opt = kSettingOptVars[k];
      if (k >= optCount) {
        vb.SetFloat(opt.vis, -1.0);
        continue;
      }
      vb.SetFloat(opt.vis, 1.0);
      vb.SetText(opt.name, SettingsOptionText(page, i, k));
      vb.SetString(opt.wnd, (!disabled && k == sel) ? "BTN01_ON" : "BTN01_OF");
      vb.SetFloat(opt.dim,
                  DimFor(disabled || SettingsOptionDisabled(page, i, k)));
    }
  };

  CurrentSettingsList().ForEachRow(SettingsSlotCount(page), row);
}

void ConfigMenu::RefreshKeybindVisuals() {
  constexpr const char *kCapturing = "...";

  AnimeMenu &list = bind_menu_;
  list.SetCursorShown(false);

  int hoverRow = -1, hoverChip = -1;
  f32 hoverX = 0.0f;
  if (state_ == State::KEYBINDS && MenuMouse::Get().MouseHasCursor() &&
      list.PointerRowX(hoverRow, hoverX))
    hoverChip = KeybindItemTemplate::ChipAt(hoverX);

  const std::string keysCaption = i18n::Text("settings.binds.keys");
  const std::string padCaption = i18n::Text("settings.binds.pad");

  list.ForEachTemplate([&](int gridSlot, AnimeData vb) {
    const BindEntry entry = BindGridEntry(gridSlot);
    const bool header = entry.cell == BindCell::Header;
    const bool row = !header && entry.cell != BindCell::Blank;

    vb.SetFloat("RowVis", row ? 1.0 : -1.0);
    vb.SetFloat("HdrVis", header ? 1.0 : -1.0);
    vb.SetText("Hdr", header ? BindEntryLabel(entry) : std::string());
    vb.SetText("HdrKeys", header ? keysCaption : std::string());
    vb.SetText("HdrPad", header ? padCaption : std::string());
    vb.SetText("Name", row ? BindEntryLabel(entry) : std::string());
    vb.SetFloat("Dim", DimFor(false));
    vb.SetFloat(kKeybindPadCapVar, -1.0);
    if (!row)
      vb.SetString("WndType", "NOWINDOW");

    const auto setUv = [&](const char *uvVar, const UVRect &r) {
      vb.SetFloat(fmt::format("{}.x", uvVar).c_str(), r.u0);
      vb.SetFloat(fmt::format("{}.y", uvVar).c_str(), r.v0);
      vb.SetFloat(fmt::format("{}.w", uvVar).c_str(), r.u1);
      vb.SetFloat(fmt::format("{}.h", uvVar).c_str(), r.v1);
    };

    for (int chip = 0; chip < KeybindItemTemplate::kChipCount; ++chip) {
      const KeybindSlotVars &vars = kKeybindSlotVars[chip];
      vb.SetFloat(vars.capVar, -1.0);
      vb.SetFloat(vars.pairVar, -1.0);

      const bool mouseLook = entry.cell == BindCell::MouseLook;
      if (!row || (mouseLook && chip > 0)) {
        vb.SetString(vars.wndVar, "NOWINDOW");
        vb.SetText(vars.textVar, "");
        continue;
      }

      const bool on = state_ == State::KEYBIND_CAPTURE &&
                      gridSlot == capture_slot_ && chip == capture_chip_;
      const bool fixed = BindChipFixed(entry, chip);
      const std::string token = BindChipToken(entry, chip);
      vb.SetString(vars.wndVar, on ? "BTN01_ON" : "BTN01_OF");
      vb.SetColor(vars.colorVar, on ? kHighlightYellow
                                 : fixed && !mouseLook ? kFixedGray
                                                       : kWhite);

      if (on) {
        vb.SetText(vars.textVar, kCapturing);
        continue;
      }
      if (mouseLook) {
        vb.SetText(vars.textVar,
                   i18n::Text(token.empty() ? "opt.off" : "opt.on"));
        continue;
      }
      if (token.empty()) {
        const bool offered =
            !fixed && gridSlot == hoverRow && chip == hoverChip;
        vb.SetText(vars.textVar, offered ? "+" : "");
        continue;
      }

      if (chip == kBindPadChip) {
        Source source;
        UVRect uv;
        if (ParseSource(token, source) &&
            source.kind == SourceKind::PadButton &&
            Glyphs::Get().PadButtonUV(source.code, uv)) {
          setUv(kKeybindPadUvVar, uv);
          vb.SetFloat(kKeybindPadCapVar, 1.0);
          vb.SetText(vars.textVar, "");
        } else {
          vb.SetText(vars.textVar, BindChipLegend(entry, chip));
        }
        continue;
      }

      const size_t plus = token.find_last_of('+');
      int key = KeyIndex(plus == std::string::npos
                             ? std::string_view(token)
                             : std::string_view(token).substr(plus + 1));
      int mod = -1;
      if (key >= 0 && plus != std::string::npos) {
        mod = Glyphs::ModifierIndex(
            std::string_view(token).substr(0, plus + 1));
        if (mod < 0)
          key = -1;
      }
      vb.SetText(vars.textVar, key < 0 ? BindChipLegend(entry, chip) : "");
      vb.SetFloat(vars.capVar, key >= 0 && mod < 0 ? 1.0 : -1.0);
      vb.SetFloat(vars.pairVar, key >= 0 && mod >= 0 ? 1.0 : -1.0);
      if (key >= 0) {
        setUv(vars.uvVar, Glyphs::KeyArtUV(key));
        if (mod >= 0)
          setUv(vars.modUvVar, Glyphs::ModifierArtUV(mod));
      }
    }
  });
}

} // namespace bd::engine
