/**
 * @file    engine/menus/config_menu.cpp
 * @brief   ConfigMenu lifecycle, menu discovery and state machine.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/menus/config_menu.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "core/memory_helpers.h"
#include "core/settings_model.h"
#include "engine/achievements/achievement_list.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/d2anime/d2anime.h"
#include "engine/input/binding_store.h"
#include "engine/input/input_sources.h"
#include "engine/sfx.h"
#include "engine/glyph_set.h"
#include "engine/menus/config_layout.h"
#include "engine/menus/config_menu_data.h"
#include "engine/task.h"
#include "platform/platform.h"

#include <algorithm>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/types.h>

namespace bd::engine {

std::array<AnimeMenu *, ConfigMenu::kMenuCount> ConfigMenu::Menus() {
  std::array<AnimeMenu *, kMenuCount> all = {
      &section_menu_,  &modlist_menu_,  &dlclist_menu_,
      &langlist_menu_, &achvlist_menu_, &bind_menu_};
  for (int p = 0; p < kSettingsSectionCount; ++p)
    all[kFixedMenus + p] = &settings_menus_[p];
  return all;
}

void ConfigMenu::ResetMenus() {
  for (auto *m : Menus())
    *m = AnimeMenu();
}

bool ConfigMenu::MenusReady() {
  for (auto *m : Menus())
    if (!*m)
      return false;
  return true;
}

void ConfigMenu::ShowOnly(std::initializer_list<AnimeMenu *> visible) {
  for (auto *m : Menus())
    m->SetVisibleAndPlay(std::find(visible.begin(), visible.end(), m) !=
                         visible.end());
}

const char *ConfigMenu::DeleteKindName(DeleteKind kind) {
  switch (kind) {
  case DeleteKind::DLC:
    return "dlc";
  case DeleteKind::Language:
    return "language";
  default:
    return "mod";
  }
}

ConfigMenu::State ConfigMenu::SectionState(int cursor) const {
  if (cursor >= 0 && cursor < kSettingsSectionCount)
    return State::SETTINGS;
  // In-game shows the settings pages alone, so there is nothing past them.
  if (surface_ == Surface::InGame)
    return State::SECTION;
  if (cursor == kSettingsSectionCount)
    return State::MODLIST;
  if (cursor == kSettingsSectionCount + 1)
    return State::DLCLIST;
  if (cursor == kSettingsSectionCount + 2)
    return State::LANGLIST;
  return State::ACHVLIST;
}

ConfigMenu::State ConfigMenu::ContentState() const {
  return state_ == State::SECTION ? SectionState(section_menu_.CursorIndex())
                                  : state_;
}

AnimeMenu *ConfigMenu::ContentMenu() {
  switch (ContentState()) {
  case State::SETTINGS:
    return &CurrentSettingsList();
  case State::MODLIST:
  case State::REORDER:
    return &modlist_menu_;
  case State::DLCLIST:
    return &dlclist_menu_;
  case State::LANGLIST:
  case State::LANGADD:
  case State::LANGPICK:
  case State::LANGJOB:
  case State::LANGNOTICE:
    return &langlist_menu_;
  case State::ACHVLIST:
    return &achvlist_menu_;
  default:
    return nullptr;
  }
}

// A settings page is picked here rather than on entry, so the sidebar shows the
// page it is standing on and A only has to hand it the focus.
void ConfigMenu::SyncPreview(int cursor) {
  const State next = SectionState(cursor);
  if (next == State::SETTINGS)
    settings_page_ = static_cast<SettingsPage>(cursor);

  ApplyVisibility();
  switch (next) {
  case State::SETTINGS:
    SetHeaders("", SettingsPageLabel(settings_page_), "");
    break;
  case State::MODLIST:
    SetHeaders("", i18n::Text("menu.header.mods"), "");
    break;
  case State::DLCLIST:
    SetHeaders("", i18n::Text("menu.header.dlc"), "");
    break;
  case State::LANGLIST:
    SetHeaders("", i18n::Text("menu.header.languages"), "");
    break;
  case State::ACHVLIST:
    SetHeaders("", i18n::Text("menu.header.achievements"), "");
    break;
  default:
    SetHeaders("", "", "");
    break;
  }
  GetLayout().restartVis.set(
      next == State::SETTINGS && SettingsPageHasRestart(settings_page_) ? 1.0
                                                                       : -1.0);
}

// The one place that decides which lists a state shows. The settings pages keep
// the sidebar beside them, mirroring the mod and DLC views.
void ConfigMenu::ApplyVisibility() {
  switch (state_) {
  case State::SECTION:
    ShowOnly({&section_menu_, ContentMenu()});
    break;
  case State::MODLIST:
  case State::REORDER:
    ShowOnly({&section_menu_, &modlist_menu_});
    break;
  case State::DLCLIST:
    ShowOnly({&section_menu_, &dlclist_menu_});
    break;
  case State::LANGLIST:
  case State::LANGADD:
  case State::LANGPICK:
  case State::LANGJOB:
  case State::LANGNOTICE:
    ShowOnly({&section_menu_, &langlist_menu_});
    break;
  case State::ACHVLIST:
    ShowOnly({&section_menu_, &achvlist_menu_});
    break;
  case State::SETTINGS:
    ShowOnly({&section_menu_, &CurrentSettingsList()});
    break;
  case State::KEYBINDS:
  case State::KEYBIND_CAPTURE:
    ShowOnly({&bind_menu_});
    break;
  default:
    // The popups keep whichever list they were raised over.
    break;
  }
}

void ConfigMenu::ActivateOnly(AnimeMenu *target) {
  for (auto *m : Menus())
    m->SetActive(m == target);
}

void ConfigMenu::SetHeaders(const std::string &sections,
                            const std::string &mods,
                            const std::string &details) {
  auto &layout = GetLayout();
  layout.hdrSections.set(sections);
  layout.hdrMods.set(mods);
  layout.hdrDetails.set(details);
}

void ConfigMenu::SetKeybindChrome(const char *hintKey) {
  auto &layout = GetLayout();
  if (conflict_shown_)
    layout.kbHint.set(
        i18n::Fmt("menu.hint.conflict", BindRowLabel(conflict_action_)));
  else
    layout.kbHint.set(hintKey ? i18n::Text(hintKey) : std::string());
  layout.kbChromeVis.set(1.0);
}

void ConfigMenu::SetFooter(const FooterLabels &f) {
  auto &layout = GetLayout();
  const auto prompt = [](StringV &label, FloatV &vis, const char *key) {
    label.set(key ? i18n::Text(key) : std::string());
    vis.set(key ? 1.0 : -1.0);
  };
  prompt(layout.ftrA, layout.ftrAVis, f.a);
  prompt(layout.ftrX, layout.ftrXVis, f.x);
  prompt(layout.ftrY, layout.ftrYVis, f.y);
  prompt(layout.ftrBack, layout.ftrBackVis, f.back);
  // Always drawn, so it has no visibility variable to clear.
  if (f.b)
    layout.ftrB.set(i18n::Text(f.b));
}

void ConfigMenu::Create(Task parent, Surface surface, PPCFunc *parentUpdate) {
  // Before the CSV is generated: its defaults are baked from the catalog.
  i18n::SyncLocale();

  state_ = State::INIT;
  surface_ = surface;
  parent_update_ = parentUpdate;
  active_ = false;
  dirty_ = false;
  settings_dirty_ = false;
  settings_restart_dirty_ = false;
  wants_restart_ = false;
  cursor_.Reset();
  reorder_origin_ = -1;
  capture_slot_ = -1;
  capture_chip_ = -1;
  last_bind_slot_ = 0;
  conflict_shown_ = false;
  settings_page_ = SettingsPage::Gameplay;
  glyph_gen_ = 0;
  ResetMenus();

  task_ = D2AnimeTask::Load(parent, "d2anime\\modmgr\\L_modmgr.csv",
                            D2AnimeTask::Reveal::Held);
  if (!task_) {
    BD_ERROR("[config] LoadAsync failed");
    active_ = false;
    return;
  }

  // A previous life's popup died with its parent's task tree, the handle must
  // not carry into this one.
  confirm_popup_.Drop();
  notice_popup_.Drop();

  // TaskBase__ctor sets the structural parent. The notification link is what
  // TitleTask_OnChildComplete fires on, so only the title surface wires it:
  // in-game the parent is the camp task, whose notify slot is not ours to
  // take.
  if (surface == Surface::Title) {
    parent.SetNotifyChild(task_);
    task_.SetNotifyParent(parent);
  }

  active_ = true;

  // Section titles are entries of the settings lists, so the pointer has to be
  // told not to park on one. Cleared in Destroy, since the filter outlives the
  // menu otherwise.
  MenuMouse::Get().SetRowFilter([this](u32 listVA, int index) {
    for (int p = 0; p < kSettingsSectionCount; ++p) {
      if (settings_menus_[p].Address() != listVA)
        continue;
      return SettingsSlotToRow(static_cast<SettingsPage>(p), index) >= 0;
    }
    return true;
  });
  BD_DEBUG("[config] child task at 0x{:08X}", task_.Address());
}

void ConfigMenu::Destroy() {
  MenuMouse::Get().SetRowFilter(nullptr);
  CancelLanguageJob();

  if (dirty_) {
    SaveAndReload();
    dirty_ = false;
  }

  // Persist cvar changes so hot settings survive the next launch. The warm
  // reboot path saves too, and a double-write here is harmless (last wins).
  if (settings_dirty_) {
    rex::cvar::SaveConfig(bd::platform::ConfigFilePath());
    settings_dirty_ = false;
  }

  // Clear the parent's notification pointer before Kill so it doesn't dangle.
  if (task_)
    task_.NotifyParent().ClearNotifyChild();

  task_.Kill();
  // A live popup is a child of task_ and dies with it, so drop the handle
  // without Kill() so no DEAD flag write reaches freed engine memory later.
  confirm_popup_.Drop();
  notice_popup_.Drop();
  ResetMenus();

  if (!wants_restart_)
    resume_state_ = State::SECTION;

  state_ = State::INIT;
  active_ = false;
  settings_restart_dirty_ = false;
  wants_restart_ = false;
  cursor_.Reset();
  reorder_origin_ = -1;
  capture_slot_ = -1;
  conflict_shown_ = false;

  BD_DEBUG("[config] destroyed");
}

void ConfigMenu::Dismiss() {
  if (settings_dirty_) {
    rex::cvar::SaveConfig(bd::platform::ConfigFilePath());
    settings_dirty_ = false;
  }

  // A forced close can reach here from any state, popup up and a list active.
  confirm_popup_.Kill();
  notice_popup_.Kill();
  for (auto *m : Menus())
    m->SetActive(false);
  if (task_)
    task_.SetVisibleAndPlay(false);

  state_ = State::INIT;
  resume_state_ = State::SECTION;
  settings_restart_dirty_ = false;
  wants_restart_ = false;
  cursor_.Reset();
  reorder_origin_ = -1;
  capture_slot_ = -1;
  conflict_shown_ = false;
  held_dir_ = 0;
  held_frames_ = 0;
  drag_row_ = -1;
  hop_blocked_ = false;

  BD_DEBUG("[config] dismissed");
}

AnimeMenu &ConfigMenu::CurrentSettingsList() {
  int page = static_cast<int>(settings_page_);
  if (page < 0 || page >= kSettingsSectionCount)
    page = 0;
  return settings_menus_[page];
}

bool ConfigMenu::DiscoverMenus() {
  if (MenusReady())
    return true;
  if (!task_ || !task_.IsReady())
    return false;

  section_menu_ = task_.FindMenu("SltSection");
  modlist_menu_ = task_.FindMenu("ModList");
  dlclist_menu_ = task_.FindMenu("DlcList");
  langlist_menu_ = task_.FindMenu("LangList");
  achvlist_menu_ = task_.FindMenu("AchvList");
  for (int p = 0; p < kSettingsSectionCount; ++p)
    settings_menus_[p] = task_.FindMenu(ConfigLayout::kSettingsListNames[p]);
  bind_menu_ = task_.FindMenu(GetLayout().bindList.name);

  if (!MenusReady()) {
    ResetMenus();
    return false;
  }

  modlist_menu_.SeedEntries(ModCount());
  dlclist_menu_.SeedEntries(DlcCount());
  langlist_menu_.SeedEntries(LanguageCount());
  achvlist_menu_.SeedEntries(GetAchievementList().size());
  for (int p = 0; p < kSettingsSectionCount; ++p)
    settings_menus_[p].SeedEntries(
        SettingsSlotCount(static_cast<SettingsPage>(p)));

  BD_DEBUG("[config] discovered menus: section=0x{:08X} modlist=0x{:08X} "
           "dlclist=0x{:08X} ({}mods, {}dlc)",
           section_menu_.Address(), modlist_menu_.Address(),
           dlclist_menu_.Address(), ModCount(), DlcCount());

  return true;
}

void ConfigMenu::Transition(State next) {
    // Suppress all transition SFX while booting from INIT
    if (state_ != State::INIT) {
        // Backing out / Dismissing submenus & screens -> play kCancel
        if (next == State::CLOSING ||
            (state_ != State::SECTION && next == State::SECTION) ||
            (state_ == State::KEYBINDS && next == State::SETTINGS) ||
            (state_ == State::KEYBIND_CAPTURE && next == State::KEYBINDS) ||
            (state_ == State::REORDER && next == State::MODLIST)) {
            sfx::Play(sfx::kCancel);
        }
        // Entering content lists from sidebar -> play kOpen
        else if (state_ == State::SECTION && next != State::SECTION && next != State::CLOSING) {
            sfx::Play(sfx::kOpen);
        }
        // Opening submenus, reorder mode, capture, and popups -> play kOpen
        else if (next == State::KEYBINDS ||
            next == State::KEYBIND_CAPTURE || next == State::REORDER ||
            next == State::CONFIRM_DELETE || next == State::CONFIRM_REBOOT ||
            next == State::CONFIRM_RESET_BINDS || next == State::LANGADD ||
            next == State::LANGPICK) {
            sfx::Play(sfx::kOpen);
        }
    }
  
  // Backing out to the sidebar with the pointer still over the list would hop
  // straight back into it, since the sidebar keeps that list up as its preview.
  // The block lifts as soon as the pointer leaves the list.
  if (next == State::SECTION && state_ != State::SECTION &&
      MenuMouse::Get().MouseHasCursor())
    hop_blocked_ = true;

  switch (state_) {
  case State::SECTION:
    section_menu_.SetActive(false);
    break;
  case State::MODLIST:
    modlist_menu_.SetActive(false);
    HideDetailPanel();
    break;
  case State::DLCLIST:
    dlclist_menu_.SetActive(false);
    HideDLCDetail();
    break;
  case State::LANGLIST:
    langlist_menu_.SetActive(false);
    break;
  case State::LANGADD:
  case State::LANGPICK:
    confirm_popup_.Kill();
    break;
  case State::LANGJOB:
  case State::LANGNOTICE:
    notice_popup_.Kill();
    break;
  case State::ACHVLIST:
    achvlist_menu_.SetActive(false);
    break;
  case State::SETTINGS:
    CurrentSettingsList().SetActive(false);
    break;
  case State::KEYBINDS:
  case State::KEYBIND_CAPTURE:
    bind_menu_.SetActive(false);
    break;
  case State::CONFIRM_DELETE:
  case State::CONFIRM_REBOOT:
  case State::CONFIRM_RESET_BINDS:
    confirm_popup_.Kill();
    break;
  default:
    break;
  }

  state_ = next;
  cursor_.Reset();
  held_dir_ = 0;
  held_frames_ = 0;
  drag_row_ = -1;

  auto &layout = GetLayout();
  layout.title.set(i18n::Text(surface_ == Surface::InGame ? "menu.title_ingame"
                                                          : "menu.title"));
  layout.restartNote.set(i18n::Text(surface_ == Surface::InGame
                                        ? "menu.restart_note_ingame"
                                        : "menu.restart_note"));
  layout.restartVis.set(-1.0);
  layout.rowDesc0.set("");
  layout.rowDesc1.set("");
  layout.rowDescC.set("");
  layout.kbHint.set("");
  layout.kbChromeVis.set(-1.0);

  // Brings up a list with its cursor on it, what every content state does
  // once the panels are settled.
  const auto open = [](AnimeMenu menu) {
    menu.SetActive(true);
    menu.AttachCursor();
  };

  ApplyVisibility();

  switch (state_) {
  case State::SECTION:
    open(section_menu_);
    HideDetailPanel();
    HideDLCDetail();
    SyncPreview(section_menu_.CursorIndex());
    PopulateNames();
    BD_DEBUG("[config] state -> SECTION");
    break;

  case State::MODLIST:
    HideDLCDetail();
    if (ModCount() == 0) {
      modlist_menu_.SetActive(false);
      HideDetailPanel();
      SetHeaders("", "", "");
    } else {
      open(modlist_menu_);
      RefreshModVisuals();
      SetHeaders("", i18n::Text("menu.header.mods"),
                 i18n::Text("menu.header.details"));
    }
    PopulateNames();
    BD_DEBUG("[config] state -> MODLIST");
    break;

  case State::DLCLIST:
    HideDetailPanel();
    // Re-scanned on the way in, whichever way in it was.
    RefreshDLC();
    if (DlcCount() == 0) {
      dlclist_menu_.SetActive(false);
      HideDLCDetail();
      SetHeaders("", "", "");
    } else {
      open(dlclist_menu_);
      RefreshDLCVisuals();
      SetHeaders("", i18n::Text("menu.header.dlc"),
                 i18n::Text("menu.header.details"));
    }
    PopulateNames();
    BD_DEBUG("[config] state -> DLCLIST");
    break;

  case State::LANGLIST:
    HideDetailPanel();
    HideDLCDetail();
    if (LanguageCount() == 0) {
      langlist_menu_.SetActive(false);
      SetHeaders("", "", "");
    } else {
      open(langlist_menu_);
      SetHeaders("", i18n::Text("menu.header.languages"),
                 i18n::Text("menu.header.details"));
    }
    PopulateNames();
    UpdateLanguageDetail(langlist_menu_.CursorIndex());
    BD_DEBUG("[config] state -> LANGLIST ({} installed)", LanguageCount());
    break;

  case State::LANGADD: {
    const bool first = lang_prompt_.empty();
    const std::string q1 =
        first ? i18n::Text("menu.language.add_ask")
              : i18n::Fmt("menu.language.missing_discs", lang_prompt_);
    const std::string q2 = first ? i18n::Text("menu.language.add_discs")
                                 : i18n::Text("menu.language.missing_ask");
    confirm_popup_.Create(task_, q1.c_str(), q2.c_str(), "", nullptr, nullptr,
                          0);
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> LANGADD (missing \"{}\")", lang_prompt_);
    break;
  }

  case State::LANGPICK: {
    const int offers = static_cast<int>(LanguageOfferCount());
    const bool movies = lang_pick_ >= offers;
    const std::string q1 =
        movies ? i18n::Text("menu.language.movies_ask")
               : i18n::Fmt("menu.language.add_one",
                           LanguageOfferName(lang_pick_));
    const std::string q2 = movies ? LanguageOfferMovies()
                                  : LanguageOfferKinds(lang_pick_);
    confirm_popup_.Create(task_, q1.c_str(), q2.c_str(), "", nullptr, nullptr,
                          0);
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> LANGPICK ({} of {})", lang_pick_, offers);
    break;
  }

  case State::LANGJOB:
    SetHeaders("", i18n::Text("menu.header.languages"),
               i18n::Text("menu.header.details"));
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> LANGJOB");
    break;

  case State::LANGNOTICE:
    ShowLanguageNotice(lang_notice_);
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> LANGNOTICE (\"{}\")", lang_notice_);
    break;

  case State::ACHVLIST: {
    RefreshAchievementList();
    open(achvlist_menu_);
    HideDetailPanel();
    HideDLCDetail();
    RefreshAchvVisuals();
    const AchievementSummary s = GetAchievementSummary();
    SetHeaders("", i18n::Text("menu.header.achievements"),
               fmt::format("{} / {}   {}G / {}G", s.unlocked, s.total,
                           s.earnedScore, s.totalScore));
    PopulateNames();
    BD_DEBUG("[config] state -> ACHVLIST ({}/{})", s.unlocked, s.total);
    break;
  }

  case State::SETTINGS: {
    open(CurrentSettingsList());
    // Slot zero is the first section's title, so a page opens on the row under
    // it rather than on a line it cannot act on. A page re-entered keeps where
    // it was left, so the cursor moves only when it sits on a title.
    const int first = SettingsRowToSlot(settings_page_, 0);
    if (CurrentSettingsList() &&
        SettingsSlotToRow(settings_page_,
                          CurrentSettingsList().CursorIndex()) < 0 &&
        first >= 0)
      CurrentSettingsList().SetCursorIndex(first);
    last_settings_slot_ = CurrentSettingsList().CursorIndex();
    HideDetailPanel();
    HideDLCDetail();
    RefreshSettingsVisuals();
    SetHeaders("", SettingsPageLabel(settings_page_), "");
    layout.restartVis.set(SettingsPageHasRestart(settings_page_) ? 1.0 : -1.0);
    BD_DEBUG("[config] state -> SETTINGS ({})",
             SettingsPageLabel(settings_page_));
    break;
  }

  case State::KEYBINDS:
    open(bind_menu_);
    if (bind_menu_) {
      const BindCell cell = BindGridEntry(bind_menu_.CursorIndex()).cell;
      if (cell == BindCell::Header || cell == BindCell::Blank) {
        int first = 0;
        while (BindGridEntry(first).cell == BindCell::Header)
          first += 2;
        bind_menu_.SetCursorIndex(first);
      }
    }
    last_bind_slot_ = bind_menu_.CursorIndex();
    HideDetailPanel();
    HideDLCDetail();
    RefreshKeybindVisuals();
    SetHeaders("", "", "");
    // The pointer interactions are the ones nothing on screen names.
    SetKeybindChrome("menu.hint.binds");
    BD_DEBUG("[config] state -> KEYBINDS");
    break;

  case State::KEYBIND_CAPTURE:
    RefreshKeybindVisuals();
    SetKeybindChrome(capture_chip_ == kBindPadChip ? "menu.hint.capture_pad"
                                                   : "menu.hint.capture");
    BD_DEBUG("[config] state -> KEYBIND_CAPTURE ({} chip {})",
             BindEntryLabel(BindGridEntry(capture_slot_)), capture_chip_);
    break;

  case State::REORDER:
    reorder_origin_ = modlist_menu_.CursorIndex();
    HideDetailPanel();
    layout.hdrDetails.set("");
    BD_DEBUG("[config] state -> REORDER, origin={}", reorder_origin_);
    break;

  case State::CONFIRM_DELETE: {
    std::string name;
    switch (delete_kind_) {
    case DeleteKind::DLC: {
      auto &dlc = DLC();
      if (delete_index_ < static_cast<int>(dlc.Count()))
        name = dlc.At(static_cast<size_t>(delete_index_)).display_name;
      break;
    }
    case DeleteKind::Language:
      name = LanguageName(delete_index_);
      break;
    case DeleteKind::Mod:
      name = ModAt(delete_index_).name;
      break;
    }
    confirm_popup_.Create(task_, i18n::Fmt("menu.confirm.delete", name).c_str(),
                          i18n::Text("menu.confirm.undone").c_str());
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> CONFIRM_DELETE ({}[{}] \"{}\")",
             DeleteKindName(delete_kind_), delete_index_, name);
    break;
  }

  case State::CONFIRM_REBOOT:
    confirm_popup_.Create(task_, i18n::Text(bd::platform::IsSteamGameMode()
                                                ? "menu.confirm.quit"
                                                : "menu.confirm.restart")
                                     .c_str());
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> CONFIRM_REBOOT");
    break;

  case State::CONFIRM_RESET_BINDS:
    // The list stays up behind the popup, so its section panels and titles,
    // which every transition clears, have to be put back with it.
    SetKeybindChrome("menu.hint.binds");
    confirm_popup_.Create(task_, i18n::Text("menu.confirm.reset_binds").c_str(),
                          i18n::Text("menu.confirm.undone").c_str());
    ActivateOnly(nullptr);
    BD_DEBUG("[config] state -> CONFIRM_RESET_BINDS");
    break;

  case State::CLOSING:
    BD_DEBUG("[config] state -> CLOSING");
    break;

  default:
    break;
  }

  layout.SyncVars(task_.AnimeData());
  UpdateFooter();
}

// Re-applied every frame: engine async init can reset the active flags.
void ConfigMenu::EnforceActiveFlags() {
  switch (state_) {
  case State::SECTION:
    ActivateOnly(&section_menu_);
    break;
  case State::MODLIST:
  case State::REORDER:
    ActivateOnly(ModCount() > 0 ? &modlist_menu_ : nullptr);
    break;
  case State::DLCLIST:
    ActivateOnly(DlcCount() > 0 ? &dlclist_menu_ : nullptr);
    break;
  case State::LANGLIST:
    ActivateOnly(LanguageCount() > 0 ? &langlist_menu_ : nullptr);
    break;
  case State::ACHVLIST:
    ActivateOnly(&achvlist_menu_);
    break;
  case State::SETTINGS:
    ActivateOnly(&CurrentSettingsList());
    break;
  case State::KEYBINDS:
    ActivateOnly(&bind_menu_);
    break;
  case State::KEYBIND_CAPTURE:
  case State::LANGADD:
  case State::LANGPICK:
  case State::LANGJOB:
  case State::LANGNOTICE:
  case State::CONFIRM_DELETE:
  case State::CONFIRM_REBOOT:
  case State::CONFIRM_RESET_BINDS:
    ActivateOnly(nullptr);
    break;
  default:
    break;
  }
}

bool ConfigMenu::Prime() {
  if (state_ == State::INIT) {
    if (DiscoverMenus()) {
      State resume = resume_state_;
      resume_state_ = State::SECTION;
      Transition(resume);
    }
  }

  // Shown only once every menu is found, and re-shown if the camp scene's
  // async init clears the flag: each menu widget is a child task with a
  // visible flag of its own, so a cleared parent leaves them drawing alone.
  const bool ready = task_ && MenusReady();
  if (task_ && task_.IsVisible() != ready) {
    task_.SetVisibleAndPlay(ready);
    // The engine's own show puts every menu on the task back up, keybind rows
    // included, so the state's set goes back on top of it.
    if (ready)
      ApplyVisibility();
  }
  return ready;
}

// Called from the TitleTask_Update hook.
void ConfigMenu::Update(PPCContext &ctx, u8 *base) {
  Prime();

  // The footer caps live in the layout's own vars (see kFooterGlyphs), so a
  // rebind made on the keybind page reaches this very screen's footer.
  if (task_ && MenusReady()) {
    const u32 gen = Glyphs::Get().Generation();
    if (gen != glyph_gen_) {
      glyph_gen_ = gen;
      for (const PromptGlyph &g : ConfigLayout::kFooterGlyphs) {
        const UVRect r = Glyphs::Get().PromptUV(g);
        task_.SetFloat(g.uv.u0, r.u0);
        task_.SetFloat(g.uv.v0, r.v0);
        task_.SetFloat(g.uv.u1, r.u1);
        task_.SetFloat(g.uv.v1, r.v1);
      }
    }
  }

  // Must run before the host update drives the AnimeMenu updates.
  if (MenusReady())
    EnforceActiveFlags();

  parent_update_(ctx, base);

  if (state_ == State::INIT || state_ == State::CLOSING)
    return;

  // Engine resets template names on rebuild, so re-apply every frame.
  PopulateNames();

  // Row visuals go in every frame (engine async init clears them), while the
  // detail panels and the row description follow the cursor and so are only
  // rebuilt when it moves.
  switch (state_) {
  case State::SECTION:
    // The sidebar's own cursor drives the preview, so the pad and the pointer
    // both reach it through the one path.
    cursor_.Poll(section_menu_, [&](int c) { SyncPreview(c); });
    switch (ContentState()) {
    case State::SETTINGS:
      RefreshSettingsVisuals();
      break;
    case State::MODLIST:
      RefreshModVisuals();
      break;
    case State::DLCLIST:
      RefreshDLCVisuals();
      break;
    case State::ACHVLIST:
      RefreshAchvVisuals();
      break;
    default:
      break;
    }
    break;
  case State::MODLIST:
    RefreshModVisuals();
    cursor_.Poll(modlist_menu_, ModCount(),
                 [&](int c) { UpdateDetailPanel(c); });
    break;
  case State::REORDER:
    RefreshModVisuals();
    break;
  case State::DLCLIST:
    RefreshDLCVisuals();
    cursor_.Poll(dlclist_menu_, DlcCount(), [&](int c) { UpdateDLCDetail(c); });
    break;
  case State::LANGLIST:
    cursor_.Poll(langlist_menu_, LanguageCount(), [&](int c) {
      UpdateLanguageDetail(c);
      UpdateFooter();
    });
    break;
  case State::LANGJOB:
    ShowLanguageNotice(LanguageJobStatus());
    if (const int percent = LanguageJobPercent(); percent >= 0)
      SetRowDesc(i18n::Fmt("menu.language.progress", percent));
    break;
  case State::LANGNOTICE:
    ShowLanguageNotice(lang_notice_);
    break;
  case State::ACHVLIST:
    RefreshAchvVisuals();
    cursor_.Poll(achvlist_menu_, [&](int c) { UpdateAchvRowDesc(c); });
    break;
  case State::SETTINGS:
    RefreshSettingsVisuals();
    cursor_.Poll(CurrentSettingsList(), [&](int slot) {
      UpdateSettingsRowDesc(slot);
      UpdateFooter();
    });
    break;
  case State::KEYBINDS:
  case State::KEYBIND_CAPTURE:
    RefreshKeybindVisuals();
    break;
  default:
    break;
  }

  GetLayout().SyncVars(task_.AnimeData());

  if (state_ == State::KEYBINDS || state_ == State::KEYBIND_CAPTURE) {
    constexpr u16 kPadIdCount = 24;
    u32 buttons = 0;
    for (u16 id = 0; id < kPadIdCount; ++id) {
      Source source;
      source.kind = SourceKind::PadButton;
      source.code = id;
      if (InputSources::Get().Active(source))
        buttons |= 1u << id;
    }
    bd::platform::SetCapturePadButtons(buttons);
  }

  // Ahead of the handlers, so a click made as the pointer crosses between the
  // sidebar and the list beside it is read by the one it hit.
  PointerHop();

  switch (state_) {
  case State::SECTION:
    HandleSection();
    break;
  case State::MODLIST:
    HandleModlist();
    break;
  case State::DLCLIST:
    HandleDLCList();
    break;
  case State::LANGLIST:
    HandleLangList();
    break;
  case State::LANGADD:
    HandleLangAdd();
    break;
  case State::LANGPICK:
    HandleLangPick();
    break;
  case State::LANGJOB:
    HandleLangJob();
    break;
  case State::LANGNOTICE:
    HandleLangNotice();
    break;
  case State::ACHVLIST:
    HandleAchvlist();
    break;
  case State::SETTINGS:
    HandleSettings();
    break;
  case State::KEYBINDS:
    HandleKeybinds();
    break;
  case State::KEYBIND_CAPTURE:
    HandleKeybindCapture();
    break;
  case State::REORDER:
    HandleReorder();
    break;
  case State::CONFIRM_DELETE:
    HandleConfirmDelete();
    break;
  case State::CONFIRM_REBOOT:
    HandleConfirmReboot();
    break;
  case State::CONFIRM_RESET_BINDS:
    HandleConfirmResetBinds();
    break;
  default:
    break;
  }
}

} // namespace bd::engine
