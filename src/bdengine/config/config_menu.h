/**
 * @file    bdengine/config_menu.h
 * @brief   Config menu -- camp-pattern state machine for the mod/DLC manager.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#pragma once

#include "bdengine/d2anime/d2anime_menu.h"
#include "bdengine/d2anime/d2anime_task.h"
#include "bdengine/d2anime/sysmes.h"

#include <cstdint>

#include <rex/types.h>

struct PPCContext;

namespace bd {

class ConfigMenu {
public:
  enum class State {
    INIT,           // waiting for task ready + menu discovery
    SECTION,        // section sidebar active
    MODLIST,        // mod list active, detail panel visible
    DLCLIST,        // DLC list active, detail panel visible
    REORDER,        // reorder mode (mod list only)
    CONFIRM_DELETE, // delete confirmation popup active
    CLOSING,        // exit sequence
  };

  void Create(u32 titleTask);
  void Destroy();
  void Update(PPCContext &ctx, u8 *base);
  bool IsActive() const { return active_; }
  bool IsClosing() const { return state_ == State::CLOSING; }
  bool WantsRestart() const { return wants_restart_; }
  u32 TaskAddr() const { return task_.guest_address(); }

private:
  void Transition(State next);
  void EnforceActiveFlags();
  void HandleSection();
  void HandleModlist();
  void HandleDlclist();
  void HandleReorder();
  void HandleConfirmDelete();

  void UpdateDetailPanel(int cursor);
  void UpdateDlcDetail(int cursor);
  void HideDetailPanel();
  void HideDlcDetail();
  void UpdateFooter();
  void PopulateNames();
  void RefreshModVisuals();
  void RefreshAfterModInstall();
  bool DiscoverMenus();

  State state_ = State::INIT;
  bool active_ = false;
  bool dirty_ = false;
  bool wants_restart_ = false;
  bool dlc_notice_active_ = false;

  D2AnimeTask task_;
  D2AnimeMenu section_menu_;
  D2AnimeMenu modlist_menu_;
  D2AnimeMenu dlclist_menu_;

  int last_cursor_ = -1;
  int reorder_origin_ = -1;
  int delete_index_ = -1;
  bool delete_is_dlc_ = false;
  SysMesConfirm confirm_popup_;
  SysMesNotice notice_popup_;
};

} // namespace bd
