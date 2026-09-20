/**
 * @file    engine/menus/config_menu.h
 * @brief   Config menu - camp pattern state machine for the mod/DLC manager.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include "core/settings_model.h"
#include "engine/d2anime/d2anime.h"
#include "engine/input/actions.h"

#include <array>
#include <initializer_list>
#include <string>
#include <vector>

#include <rex/ppc/func.h>
#include <rex/types.h>

struct PPCContext;

namespace bd::engine {

class ConfigMenu {
public:
  // Which host is showing the menu. The in-game surface drops the sections that
  // only make sense before a save is loaded, and every restart-bound row with
  // them.
  enum class Surface { Title, InGame };

  enum class State {
    INIT,                // waiting for task ready + menu discovery
    SECTION,             // section sidebar active
    MODLIST,             // mod list active, detail panel visible
    DLCLIST,             // DLC list active, detail panel visible
    LANGLIST,
    LANGADD,
    LANGPICK,
    LANGJOB,
    LANGNOTICE,
    ACHVLIST,            // achievement list active (read-only)
    SETTINGS,            // a settings page list active, sidebar stays visible
    KEYBINDS,
    KEYBIND_CAPTURE,
    REORDER,             // reorder mode (mod list only)
    CONFIRM_DELETE,      // delete confirmation popup active
    CONFIRM_REBOOT,      // restart-to-apply confirmation popup active
    CONFIRM_RESET_BINDS, // reset-every-bind confirmation popup active
    CLOSING,             // exit sequence
  };

  // 'parentUpdate' is the host hook's original, which Update runs at the point
  // the engine drives its AnimeMenu updates. It differs per surface, so the
  // menu takes it rather than naming one host's symbol.
  void Create(Task parent, Surface surface, PPCFunc *parentUpdate);
  void Destroy();
  // Close for a host that keeps the task between opens: persists what Destroy
  // persists and rewinds the state machine, while the task and its discovered
  // menus stay loaded and hidden so the next open skips the load entirely.
  void Dismiss();
  void Update(PPCContext &ctx, u8 *base);
  // Discovers the menus and puts the screen up, without taking a frame's
  // input. True once it is drawing, which lets a host that has to hide
  // something else do it on the same frame this one appears.
  bool Prime();
  bool IsActive() const { return active_; }
  bool IsClosing() const { return state_ == State::CLOSING; }
  // True once the screen is up and drawing, which is when a fade held over
  // the load can start lifting.
  bool IsOnScreen() const { return active_ && task_ && task_.IsVisible(); }
  bool WantsRestart() const { return wants_restart_; }

  u32 TaskAddr() const { return task_.Address(); }

private:
  // Footer prompts for a state, as i18n catalog keys. A null key hides its
  // button, B has no visibility variable of its own, so a null there leaves
  // the standing label alone.
  struct FooterLabels {
    const char *a = nullptr;
    const char *b = nullptr;
    const char *x = nullptr;
    const char *y = nullptr;
    const char *back = nullptr;
  };

  void Transition(State next);
  void EnforceActiveFlags();
  void ActivateOnly(AnimeMenu *target);
  // Shows exactly the named lists and hides every other one.
  void ShowOnly(std::initializer_list<AnimeMenu *> visible);
  // Shows the lists the current state calls for.
  void ApplyVisibility();
  void SetHeaders(const std::string &sections, const std::string &mods,
                  const std::string &details);
  // The keybind screen's section headers and hint line, which the common
  // transition block clears alongside the row description.
  void SetKeybindChrome(const char *hintKey);
  void SetFooter(const FooterLabels &f);
  // The state a sidebar row leads to.
  State SectionState(int cursor) const;
  // The state whose list is on screen: the one this menu is in, or, from the
  // sidebar, the one the highlighted row previews.
  State ContentState() const;
  // The list that state shows, which the sidebar puts up beside itself so the
  // highlighted section can be read before it is entered.
  AnimeMenu *ContentMenu();
  // Points the preview at a sidebar row, on every cursor move.
  void SyncPreview(int cursor);
  // Hands focus to whichever of the sidebar and the list beside it the pointer
  // is over, so a click hits the row under it rather than the cursor some
  // other list is holding. True when the focus moved.
  bool PointerHop();
  void HandleSection();
  void HandleModlist();
  void HandleDLCList();
  void HandleLangList();
  void HandleLangAdd();
  void HandleLangPick();
  void HandleLangJob();
  void HandleLangNotice();
  void HandleAchvlist();
  void HandleSettings();
  // Sets the row's value from where the pointer sits along it: the button it
  // is over, or the position it names on a slider track. True when the pointer
  // was on a control, so a click that missed one does nothing.
  bool SetRowFromPointer(int row, f32 x, bool dragging);
  // Repeat step for a held direction on a bar row, or 0.
  int HeldStep(int cursor);
  void HandleKeybinds();
  void HandleKeybindCapture();
  void HandleReorder();
  void HandleConfirmDelete();
  void HandleConfirmReboot();
  void HandleConfirmResetBinds();

  void UpdateDetailPanel(int cursor);
  void UpdateDLCDetail(int cursor);
  void UpdateLanguageDetail(int index);
  void ShowLanguageNotice(const std::string &text);
  void HideDetailPanel();
  void HideDLCDetail();
  void UpdateFooter();
  void SetRowDesc(const std::string &text);
  void UpdateAchvRowDesc(int cursor);
  void PopulateNames();
  void RefreshModVisuals();
  void RefreshDLCVisuals();
  void RefreshAchvVisuals();
  void RefreshSettingsVisuals();
  void RefreshKeybindVisuals();
  bool DiscoverMenus();
  bool MenusReady();
  void ResetMenus();
  AnimeMenu &CurrentSettingsList();
  AnimeMenu &CurrentBindList();
  ActionContext BindContext() const;
  int BindRows() const;
  void SetBindPage(int page);

  static constexpr size_t kFixedMenus = 5;
  static constexpr size_t kMenuCount =
      kSettingsSectionCount + kFixedMenus + kBindPageCount;
  std::array<AnimeMenu *, kMenuCount> Menus();

  State state_ = State::INIT;
  Surface surface_ = Surface::Title;
  PPCFunc *parent_update_ = nullptr;
  bool active_ = false;
  bool dirty_ = false;
  bool settings_dirty_ = false;
  bool settings_restart_dirty_ = false;
  bool wants_restart_ = false;
  // Survives the Destroy/Create menu restart cycle, consumed on first
  // transition of the rebuilt task.
  State resume_state_ = State::SECTION;

  D2AnimeTask task_;
  // Last glyph generation pushed into the footer cap vars (see kFooterGlyphs).
  u32 glyph_gen_ = 0;
  AnimeMenu section_menu_;
  AnimeMenu modlist_menu_;
  AnimeMenu dlclist_menu_;
  AnimeMenu langlist_menu_;
  AnimeMenu achvlist_menu_;
  AnimeMenu settings_menus_[kSettingsSectionCount];
  AnimeMenu bind_menus_[kBindPageCount];

  SettingsPage settings_page_ = SettingsPage::Gameplay;
  int bind_page_ = 0;
  Action capture_action_ = Action::Confirm;
  int capture_slot_ = -1;
  Action conflict_action_ = Action::Confirm;
  bool conflict_shown_ = false;
  // Edge detector for the keybind screen's hover-Delete, a host key with no
  // engine button to edge-gate it.
  bool del_held_ = false;
  int last_settings_slot_ = 0;
  // Held-direction auto-repeat on bar rows, counted in menu frames: a short
  // delay so a tap still moves one step, then a step every other frame.
  static constexpr int kHeldRepeatDelay = 18;
  static constexpr int kHeldRepeatInterval = 2;
  int held_dir_ = 0;
  int held_frames_ = 0;
  // List slot a pointer drag has latched onto, or -1.
  int drag_row_ = -1;
  // Set when a cancel hands the sidebar back with the pointer still over the
  // list it left, so PointerHop does not immediately walk back in. Cleared the
  // moment the pointer is off that list.
  bool hop_blocked_ = false;
  // Shared by every list: a transition resets it, so the panels below the
  // cursor always rebuild for the list being entered.
  D2AnimeCursor cursor_;
  int reorder_origin_ = -1;
  int delete_index_ = -1;
  enum class DeleteKind { Mod, DLC, Language };
  static const char *DeleteKindName(DeleteKind kind);
  DeleteKind delete_kind_ = DeleteKind::Mod;
  std::string lang_prompt_;
  std::string lang_notice_;
  int lang_pick_ = 0;
  std::vector<bool> lang_accept_;
  SysMesConfirm confirm_popup_;
  SysMesNotice notice_popup_;
};

} // namespace bd::engine
