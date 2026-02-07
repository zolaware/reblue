/**
 * @file    bdengine/config_menu.cpp
 * @brief   ConfigMenu state machine implementation.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#include "bdengine/config/config_menu.h"
#include "bdengine/config/config_layout.h"
#include "bdengine/config/config_menu_data.h"
#include "bdengine/d2anime/d2anime.h"
#include "bdengine/common/logging.h"

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>

using rex::memory::store_and_swap;

REX_EXTERN(__imp__TitleTask_Update);

namespace {

std::string WordWrap(const std::string &text, int maxChars) {
  std::string result;
  int lineLen = 0;
  size_t i = 0;
  while (i < text.size()) {
    size_t spacePos = text.find(' ', i);
    size_t nlPos = text.find('\n', i);
    if (spacePos == std::string::npos)
      spacePos = text.size();
    if (nlPos == std::string::npos)
      nlPos = text.size();

    bool hitNewline = nlPos <= spacePos;
    size_t wordEnd = hitNewline ? nlPos : spacePos;
    int wordLen = static_cast<int>(wordEnd - i);

    if (lineLen > 0 && lineLen + 1 + wordLen > maxChars) {
      result += '\n';
      lineLen = 0;
    } else if (lineLen > 0 && !hitNewline) {
      result += ' ';
      lineLen++;
    }

    result.append(text, i, wordLen);
    if (hitNewline) {
      result += '\n';
      lineLen = 0;
    } else {
      lineLen += wordLen;
    }
    i = wordEnd + 1;
  }
  return result;
}

} // namespace

namespace bd {

/**
 * @brief Load the config task as a TitleTask child.
 */
void ConfigMenu::Create(u32 titleTask) {
  state_ = State::INIT;
  active_ = false;
  dirty_ = false;
  wants_restart_ = false;
  dlc_notice_active_ = false;
  last_cursor_ = -1;
  reorder_origin_ = -1;
  section_menu_ = D2AnimeMenu();
  modlist_menu_ = D2AnimeMenu();
  dlclist_menu_ = D2AnimeMenu();

  task_ = D2AnimeTask::Load(titleTask, "d2anime\\modmgr\\L_modmgr.csv");
  if (!task_) {
    BD_ERROR("[config] LoadAsync failed");
    active_ = false;
    return;
  }

  // Link as TitleTask child (structural parent set by LoadAsync's TaskBase__ctor,
  // but we must set the notification parent pointer + 64-bit UID manually)
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  store_and_swap<u32>(base + titleTask + 0xA0, task_.guest_address());
  store_and_swap<u32>(base + task_.guest_address() + 0x48, titleTask);
  u64 titleUID =
      rex::memory::load_and_swap<u64>(base + titleTask + 0x10);
  store_and_swap<u64>(base + task_.guest_address() + 0x50, titleUID);

  active_ = true;
  BD_INFO("[config] child task at 0x{:08X}", task_.guest_address());
}

/**
 * @brief Save if dirty, kill task, reset state.
 */
void ConfigMenu::Destroy() {
  if (dirty_) {
    config::SaveAndReload();
    dirty_ = false;
  }

  // Clear TitleTask+0xA0 before killing (prevents dangling pointer)
  if (task_) {
    auto *base = rex::system::kernel_state()->memory()->virtual_membase();
    u32 parent = rex::memory::load_and_swap<u32>(
        base + task_.guest_address() + 0x48);
    if (parent)
      store_and_swap<u32>(base + parent + 0xA0, 0u);
  }

  task_.Kill();
  section_menu_ = D2AnimeMenu();
  modlist_menu_ = D2AnimeMenu();
  dlclist_menu_ = D2AnimeMenu();

  state_ = State::INIT;
  active_ = false;
  wants_restart_ = false;
  dlc_notice_active_ = false;
  last_cursor_ = -1;
  reorder_origin_ = -1;

  BD_INFO("[config] destroyed");
}

/**
 * @brief Find section, modlist, dlclist from task's menu vector.
 */
bool ConfigMenu::DiscoverMenus() {
  if (section_menu_ && modlist_menu_ && dlclist_menu_)
    return true;
  if (!task_ || !task_.IsReady())
    return false;

  section_menu_ = task_.FindMenuByName("SltSection");
  modlist_menu_ = task_.FindMenuByName("ModList");
  dlclist_menu_ = task_.FindMenuByName("DlcList");

  if (!section_menu_ || !modlist_menu_ || !dlclist_menu_) {
    section_menu_ = D2AnimeMenu();
    modlist_menu_ = D2AnimeMenu();
    dlclist_menu_ = D2AnimeMenu();
    return false;
  }

  // Populate entry-data (defaultItem=0 means engine created templates
  // but no entries). AddEntryData triggers proper SetVisibleAndPlay
  // initialization on templates, preventing null render target crashes.
  int modCount = static_cast<int>(config::ModCount());
  for (int i = 0; i < modCount; ++i)
    modlist_menu_.AddEntryData(i, 1);

  int dlcCount = static_cast<int>(config::DlcCount());
  for (int i = 0; i < dlcCount; ++i)
    dlclist_menu_.AddEntryData(i, 1);

  BD_INFO("[config] discovered menus: section=0x{:08X} modlist=0x{:08X} "
          "dlclist=0x{:08X} ({}mods, {}dlc)",
          section_menu_.guest_address(), modlist_menu_.guest_address(),
          dlclist_menu_.guest_address(), modCount, dlcCount);

  // Dump all child tasks of the main task for crash diagnosis
  {
    auto *b = rex::system::kernel_state()->memory()->virtual_membase();
    u32 taskAddr = task_.guest_address();
    // Walk sibling chain at +0x38/+0x3C (first child at +0x38)
    u32 child = rex::memory::load_and_swap<u32>(b + taskAddr + 0x38);
    int idx = 0;
    while (child) {
      u32 loadState = rex::memory::load_and_swap<u32>(b + child + 0x70);
      BD_INFO("[config] child[{}] task=0x{:08X} loadState={}",
              idx++, child, loadState);
      child = rex::memory::load_and_swap<u32>(b + child + 0x3C);
    }
  }

  return true;
}

/**
 * @brief Central state transition (leave current, enter next).
 */
void ConfigMenu::Transition(State next) {
  // Leave current state
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
    HideDlcDetail();
    break;
  case State::REORDER:
    // modlist stays active, nothing to leave
    break;
  case State::CONFIRM_DELETE:
    confirm_popup_.Kill();
    break;
  default:
    break;
  }

  state_ = next;
  last_cursor_ = -1;

  // Enter new state
  switch (state_) {
  case State::SECTION:
    section_menu_.SetActive(true);
    section_menu_.AttachCursor();
    modlist_menu_.SetVisible(false);
    dlclist_menu_.SetVisible(false);
    HideDetailPanel();
    HideDlcDetail();
    config::GetLayout().hdrMods.set("");
    config::GetLayout().hdrDetails.set("");
    BD_INFO("[config] state -> SECTION");
    break;

  case State::MODLIST:
    modlist_menu_.SetVisible(true);
    dlclist_menu_.SetVisible(false);
    if (config::ModCount() == 0) {
      modlist_menu_.SetActive(false);
      HideDetailPanel();
      config::GetLayout().hdrMods.set("");
      config::GetLayout().hdrDetails.set("");
    } else {
      modlist_menu_.SetActive(true);
      modlist_menu_.AttachCursor();
      RefreshModVisuals();
      config::GetLayout().hdrMods.set("Mods");
      config::GetLayout().hdrDetails.set("Details");
    }
    PopulateNames();
    BD_INFO("[config] state -> MODLIST");
    break;

  case State::DLCLIST:
    dlclist_menu_.SetVisible(true);
    modlist_menu_.SetVisible(false);
    if (config::DlcCount() == 0) {
      dlclist_menu_.SetActive(false);
      HideDlcDetail();
      config::GetLayout().hdrMods.set("");
      config::GetLayout().hdrDetails.set("");
    } else {
      dlclist_menu_.SetActive(true);
      dlclist_menu_.AttachCursor();
      config::GetLayout().hdrMods.set("Official DLC");
      config::GetLayout().hdrDetails.set("Details");
    }
    PopulateNames();
    BD_INFO("[config] state -> DLCLIST");
    break;

  case State::REORDER:
    reorder_origin_ = modlist_menu_.CursorIndex();
    HideDetailPanel();
    config::GetLayout().hdrDetails.set("");
    BD_INFO("[config] state -> REORDER, origin={}", reorder_origin_);
    break;

  case State::CONFIRM_DELETE: {
    std::string name;
    if (delete_is_dlc_) {
      auto &list = config::GetDlcList();
      if (delete_index_ < static_cast<int>(list.size()))
        name = list[delete_index_].display_name;
    } else {
      name = config::GetModInfo(delete_index_).name;
    }
    confirm_popup_.Create(task_.guest_address(),
                          ("Delete \"" + name + "\"?").c_str(),
                          "This cannot be undone.");
    section_menu_.SetActive(false);
    modlist_menu_.SetActive(false);
    dlclist_menu_.SetActive(false);
    BD_INFO("[config] state -> CONFIRM_DELETE ({}[{}] \"{}\")",
            delete_is_dlc_ ? "dlc" : "mod", delete_index_, name);
    break;
  }

  case State::CLOSING:
    BD_INFO("[config] state -> CLOSING");
    break;

  default:
    break;
  }

  config::GetLayout().syncVars(task_.guest_address());
  UpdateFooter();
}

/**
 * @brief Set active/inactive per current state (every frame).
 */
void ConfigMenu::EnforceActiveFlags() {
  switch (state_) {
  case State::SECTION:
    section_menu_.SetActive(true);
    modlist_menu_.SetActive(false);
    dlclist_menu_.SetActive(false);
    break;
  case State::MODLIST:
  case State::REORDER:
    section_menu_.SetActive(false);
    modlist_menu_.SetActive(config::ModCount() > 0);
    dlclist_menu_.SetActive(false);
    break;
  case State::DLCLIST:
    section_menu_.SetActive(false);
    modlist_menu_.SetActive(false);
    dlclist_menu_.SetActive(!dlc_notice_active_ && config::DlcCount() > 0);
    break;
  case State::CONFIRM_DELETE:
    section_menu_.SetActive(false);
    modlist_menu_.SetActive(false);
    dlclist_menu_.SetActive(false);
    break;
  default:
    break;
  }
}

/**
 * @brief Main per-frame update, called from TitleTask_Update hook.
 */
void ConfigMenu::Update(PPCContext &ctx, u8 *base) {
  // INIT: discover menus, transition to SECTION
  if (state_ == State::INIT) {
    if (DiscoverMenus())
      Transition(State::SECTION);
  }

  // Enforce active flags before engine processes menus
  if (section_menu_ && modlist_menu_ && dlclist_menu_)
    EnforceActiveFlags();

  __imp__TitleTask_Update(ctx, base);

  // Early out for non-interactive states
  if (state_ == State::INIT || state_ == State::CLOSING)
    return;

  // Populate names every frame (engine resets on rebuild)
  PopulateNames();

  // Refresh mod visuals if in mod-related state
  if (state_ == State::MODLIST || state_ == State::REORDER)
    RefreshModVisuals();

  config::GetLayout().syncVars(task_.guest_address());

  // Detail panel cursor tracking
  if (state_ == State::MODLIST) {
    int cursor = modlist_menu_.CursorIndex();
    if (cursor != last_cursor_ && cursor >= 0 &&
        cursor < static_cast<int>(config::ModCount())) {
      last_cursor_ = cursor;
      UpdateDetailPanel(cursor);
    }
  } else if (state_ == State::DLCLIST) {
    int cursor = dlclist_menu_.CursorIndex();
    if (cursor != last_cursor_ && cursor >= 0 &&
        cursor < static_cast<int>(config::DlcCount())) {
      BD_INFO("[config] DLC cursor {} -> {} (task=0x{:08X}, "
              "dlcmenu=0x{:08X}, templates={}, items={})",
              last_cursor_, cursor, task_.guest_address(),
              dlclist_menu_.guest_address(),
              dlclist_menu_->templateCount(),
              dlclist_menu_.ItemDataCount());

      // Log each DLC template task address + its element count
      dlclist_menu_.ForEachTemplate([&](int i, u32 vb) {
        u32 tplTask = vb - 0x74; // VarBag is at task+0x74
        BD_INFO("[config]   tpl[{}] task=0x{:08X} vb=0x{:08X}", i, tplTask, vb);
      });

      last_cursor_ = cursor;
      UpdateDlcDetail(cursor);

      BD_INFO("[config] DLC detail updated for cursor {}", cursor);
    }
  }

  // Input handling per state
  switch (state_) {
  case State::SECTION:
    HandleSection();
    break;
  case State::MODLIST:
    HandleModlist();
    break;
  case State::DLCLIST:
    HandleDlclist();
    break;
  case State::REORDER:
    HandleReorder();
    break;
  case State::CONFIRM_DELETE:
    HandleConfirmDelete();
    break;
  default:
    break;
  }
}

/**
 * @brief Input: section sidebar.
 */
void ConfigMenu::HandleSection() {
  if (CheckButton(Button::A)) {
    int cursor = section_menu_.CursorIndex();
    if (cursor == 0) {
      Transition(State::MODLIST);
    } else {
      config::RefreshDlc();
      Transition(State::DLCLIST);
    }
    return;
  }

  if (CheckButton(Button::B)) {
    Transition(State::CLOSING);
  }
}

/**
 * @brief Input: mod list.
 */
void ConfigMenu::HandleModlist() {
  if (CheckButton(Button::A)) {
    Transition(State::REORDER);
    return;
  }

  if (CheckButton(Button::Y)) {
    int cursor = modlist_menu_.CursorIndex();
    config::ToggleMod(cursor);
    dirty_ = true;
    BD_INFO("[config] toggled mod[{}]", cursor);
    return;
  }

  if (CheckButton(Button::X) && config::ModCount() > 0) {
    delete_index_ = modlist_menu_.CursorIndex();
    delete_is_dlc_ = false;
    Transition(State::CONFIRM_DELETE);
    return;
  }

  if (CheckButton(Button::Back)) {
    if (config::InstallMod()) {
      RefreshAfterModInstall();
    }
    return;
  }

  if (CheckButton(Button::B)) {
    Transition(State::SECTION);
  }
}

/**
 * @brief Input: DLC list.
 */
void ConfigMenu::HandleDlclist() {
  if (dlc_notice_active_) {
    if (CheckButton(Button::A) || CheckButton(Button::B)) {
      notice_popup_.Kill();
      dlc_notice_active_ = false;
    }
    return;
  }

  if (CheckButton(Button::X) && config::DlcCount() > 0) {
    delete_index_ = dlclist_menu_.CursorIndex();
    delete_is_dlc_ = true;
    Transition(State::CONFIRM_DELETE);
    return;
  }

  if (CheckButton(Button::Back)) {
    if (config::InstallDlc()) {
      dlc_notice_active_ = true;
      notice_popup_.Create(task_.guest_address(),
                           "Restart the game for",
                           "DLC to take effect.");
    }
    return;
  }

  if (CheckButton(Button::B)) {
    Transition(State::SECTION);
  }
}

/**
 * @brief Input: reorder mode.
 */
void ConfigMenu::HandleReorder() {
  int cursor = modlist_menu_.CursorIndex();

  if (cursor != reorder_origin_) {
    config::SwapMods(reorder_origin_, cursor);
    reorder_origin_ = cursor;
    dirty_ = true;
    BD_INFO("[config] reorder: swapped to position {}", cursor);
  }

  if (CheckButton(Button::A)) {
    Transition(State::MODLIST);
    BD_INFO("[config] reorder confirmed at position {}", cursor);
    return;
  }

  if (CheckButton(Button::B)) {
    Transition(State::MODLIST);
    BD_INFO("[config] reorder cancelled");
  }
}

/**
 * @brief Input: delete confirmation popup.
 */
void ConfigMenu::HandleConfirmDelete() {
  if (!confirm_popup_.Poll())
    return;

  if (confirm_popup_.Confirmed()) {
    confirm_popup_.Kill();

    bool ok;
    if (delete_is_dlc_) {
      ok = config::DeleteDlc(delete_index_);
      if (ok) {
        config::RescanDlc();
        BD_INFO("[config] deleted dlc[{}]", delete_index_);
      } else {
        BD_ERROR("[config] dlc delete failed for [{}]", delete_index_);
        // TODO: show error sysmes "Failed to delete DLC"
        Transition(State::DLCLIST);
        return;
      }
    } else {
      ok = config::DeleteMod(delete_index_);
      if (!ok) {
        BD_ERROR("[config] mod delete failed for [{}]", delete_index_);
        Transition(State::MODLIST);
        return;
      }
      BD_INFO("[config] deleted mod[{}]", delete_index_);
    }
    wants_restart_ = true;
    Transition(State::CLOSING);
    return;
  }

  confirm_popup_.Kill();
  BD_INFO("[config] delete cancelled");
  Transition(delete_is_dlc_ ? State::DLCLIST : State::MODLIST);
}

/**
 * @brief Detail panel: mod.
 */
void ConfigMenu::UpdateDetailPanel(int index) {
  const auto &info = config::GetModInfo(index);

  task_.SetFloat("detail.start", 1.0);
  task_.SetString("detail.Author",
                  info.author.empty() ? "Unknown" : info.author.c_str());
  task_.SetString("detail.Version",
                  info.version.empty() ? "-" : info.version.c_str());
  task_.SetString("detail.Created",
                  info.created.empty() ? "-" : info.created.c_str());
  std::string desc = info.description.empty() ? "No description"
                                               : WordWrap(info.description, 46);
  task_.SetString("detail.Desc", desc.c_str());

  if (!info.image_path.empty()) {
    std::string previewRef = "res\\preview_" + std::to_string(index);
    task_.SetString("detail.PreviewFile", previewRef.c_str());
    task_.SetFloat("detail.HasPreview", 1.0);
  } else {
    task_.SetFloat("detail.HasPreview", -1.0);
  }
}

void ConfigMenu::HideDetailPanel() {
  task_.SetFloat("detail.start", -1.0);
  task_.SetFloat("detail.HasPreview", -1.0);
}

/**
 * @brief Detail panel: DLC.
 */
void ConfigMenu::UpdateDlcDetail(int index) {
  auto &dlcList = config::GetDlcList();
  if (index < 0 || index >= static_cast<int>(dlcList.size()))
    return;

  const auto &info = dlcList[index];

  task_.SetFloat("dlcdetail.start", 1.0);
  std::string wrapped = info.description.empty() ? ""
                                                  : WordWrap(info.description, 46);
  size_t pos = 0;
  for (int i = 0; i < DlcDetailTemplate::kDescLines; ++i) {
    std::string val;
    if (pos < wrapped.size()) {
      size_t nl = wrapped.find('\n', pos);
      if (nl == std::string::npos) nl = wrapped.size();
      val = wrapped.substr(pos, nl - pos);
      pos = nl + 1;
    }
    task_.SetString(
        fmt::format("dlcdetail.Desc{}", i).c_str(), val.c_str());
  }
}

void ConfigMenu::HideDlcDetail() {
  task_.SetFloat("dlcdetail.start", -1.0);
}

/**
 * @brief Set footer labels and visibility per state.
 */
void ConfigMenu::UpdateFooter() {
  auto &layout = config::GetLayout();

  switch (state_) {
  case State::SECTION:
    layout.ftrA.set("Select");
    layout.ftrB.set("Exit");
    layout.ftrAVis.set(1.0);
    layout.ftrXVis.set(-1.0);
    layout.ftrYVis.set(-1.0);
    layout.ftrBackVis.set(-1.0);
    break;
  case State::MODLIST:
    layout.ftrB.set("Back");
    layout.ftrBack.set("Install");
    layout.ftrBackVis.set(1.0);
    if (config::ModCount() == 0) {
      layout.ftrA.set("");
      layout.ftrX.set("");
      layout.ftrY.set("");
      layout.ftrAVis.set(-1.0);
      layout.ftrXVis.set(-1.0);
      layout.ftrYVis.set(-1.0);
    } else {
      layout.ftrA.set("Reorder");
      layout.ftrX.set("Delete");
      layout.ftrY.set("Toggle");
      layout.ftrAVis.set(1.0);
      layout.ftrXVis.set(1.0);
      layout.ftrYVis.set(1.0);
    }
    break;
  case State::DLCLIST:
    layout.ftrA.set("");
    layout.ftrB.set("Back");
    layout.ftrY.set("");
    layout.ftrBack.set("Install DLC");
    layout.ftrAVis.set(-1.0);
    layout.ftrYVis.set(-1.0);
    layout.ftrBackVis.set(1.0);
    if (config::DlcCount() > 0) {
      layout.ftrX.set("Delete");
      layout.ftrXVis.set(1.0);
    } else {
      layout.ftrXVis.set(-1.0);
    }
    break;
  case State::REORDER:
    layout.ftrA.set("Place");
    layout.ftrB.set("Cancel");
    layout.ftrAVis.set(1.0);
    layout.ftrXVis.set(-1.0);
    layout.ftrYVis.set(-1.0);
    layout.ftrBackVis.set(-1.0);
    break;
  case State::CONFIRM_DELETE:
    layout.ftrAVis.set(-1.0);
    layout.ftrXVis.set(-1.0);
    layout.ftrYVis.set(-1.0);
    layout.ftrBackVis.set(-1.0);
    break;
  default:
    break;
  }

  layout.syncVars(task_.guest_address());
}

/**
 * @brief Set template names every frame.
 */
void ConfigMenu::PopulateNames() {
  // Section sidebar names
  const char *secNames[] = {"Mods", "Official DLC"};
  section_menu_.ForEachTemplate([&](int i, u32 vb) {
    if (i < 2)
      VarBagSetString(vb, "Name", secNames[i]);
  });

  // Active list names
  if (state_ == State::MODLIST || state_ == State::REORDER) {
    size_t modCount = config::ModCount();
    modlist_menu_.ForEachTemplate([&](int i, u32 vb) {
      if (i < static_cast<int>(modCount)) {
        const auto &info = config::GetModInfo(i);
        VarBagSetString(vb, "Name", info.name.c_str());
      }
    });
  } else if (state_ == State::DLCLIST) {
    auto &dlcList = config::GetDlcList();
    dlclist_menu_.ForEachTemplate([&](int i, u32 vb) {
      if (i < static_cast<int>(dlcList.size()))
        VarBagSetString(vb, "Name", dlcList[i].display_name.c_str());
      else
        VarBagSetString(vb, "Name",
                        dlcList.empty() ? "No DLC installed" : "");
    });
  }
}

/**
 * @brief Enable/disable colors, checkmarks, reorder highlight.
 */
void ConfigMenu::RefreshModVisuals() {
  size_t modCount = config::ModCount();
  u32 enColor = modlist_menu_.EnableColor();
  u32 disColor = modlist_menu_.DisableColor();

  modlist_menu_.ForEachTemplate([&](int i, u32 vb) {
    if (i >= static_cast<int>(modCount))
      return;
    bool enabled = config::IsModEnabled(i);
    modlist_menu_.SetItemEnabled(i, enabled);

    u32 rowColor;
    if (state_ == State::REORDER && i == reorder_origin_) {
      rowColor = 0xFFD700FF;
    } else {
      rowColor = enabled ? enColor : disColor;
    }
    VarBagSetColor(vb, "Color", rowColor);
    VarBagSetFloat(vb, "ChkOn", enabled ? 1.0 : -1.0);
    VarBagSetFloat(vb, "ChkOff", enabled ? -1.0 : 1.0);
  });
}

/**
 * @brief Update mod list in-place without menu restart.
 */
void ConfigMenu::RefreshAfterModInstall() {
  size_t newCount = config::ModCount();
  config::GetLayout().setModCount(newCount);

  // Add entry data for the newly installed mod
  if (newCount > 0)
    modlist_menu_.AddEntryData(static_cast<int>(newCount) - 1, 1);

  dirty_ = true;
  Transition(State::MODLIST);
  BD_INFO("[config] refreshed mod list after install ({} mods)", newCount);
}

} // namespace bd
