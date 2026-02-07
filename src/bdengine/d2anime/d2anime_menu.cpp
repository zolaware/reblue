/**
 * @file    bdengine/d2anime_menu.cpp
 * @brief   D2AnimeMenu wrapper implementation.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#include "bdengine/d2anime/d2anime_menu.h"
#include "bdengine/common/logging.h"

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REX_IMPORT(__imp__AnimeMenu_attachToParent, MenuAttachToParent, void(u32));
REX_IMPORT(__imp__AnimeMenu_SetVisibleAndPlay, MenuSetVisibleAndPlay,
           void(u32, u32));
REX_IMPORT(__imp__AnimeMenu_AddEntryData, MenuAddEntryData, u32(u32, u32, u32));
REX_IMPORT(__imp__AnimeMenu_ResetAndRebuild, MenuResetAndRebuild, void(u32));

namespace bd {

D2AnimeMenu::D2AnimeMenu(u32 guestAddr) {
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  ptr_ = rex::MappedPtr<AnimeMenu_t>(
      rex::memory::GuestPtr<AnimeMenu_t *>(base, guestAddr), guestAddr);
}

void D2AnimeMenu::SetActive(bool active) {
  if (!ptr_)
    return;
  ptr_->activeFlag = active ? 1 : 0;
  ptr_->deselectAll = active ? 0 : 1;
  ptr_->cursorShowA = active ? 1 : 0;
  ptr_->needsRebuild = 1;
}

void D2AnimeMenu::SetVisible(bool visible) {
  if (!ptr_)
    return;
  MenuSetVisibleAndPlay(ptr_.guest_address(), visible ? 1 : 0);
}

void D2AnimeMenu::AttachCursor() {
  if (!ptr_)
    return;
  MenuAttachToParent(ptr_.guest_address());
}

int D2AnimeMenu::CursorIndex() const {
  if (!ptr_)
    return 0;
  return static_cast<int>(static_cast<u32>(ptr_->cursorIndex));
}

u32 D2AnimeMenu::EnableColor() const {
  if (!ptr_)
    return 0xFFFFFFFF;
  return static_cast<u32>(ptr_->enableColor);
}

u32 D2AnimeMenu::DisableColor() const {
  if (!ptr_)
    return 0x7F7F7FFF;
  return static_cast<u32>(ptr_->disableColor);
}

void D2AnimeMenu::ForEachTemplate(
    std::function<void(int, u32)> cb) const {
  if (!ptr_)
    return;
  u32 tplBegin = ptr_->templateBegin;
  u32 tplEnd = ptr_->templateEnd;
  if (!tplBegin || tplBegin == tplEnd)
    return;

  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  u32 count = (tplEnd - tplBegin) / 4;
  for (u32 i = 0; i < count; ++i) {
    u32 tplTask =
        rex::memory::load_and_swap<u32>(base + tplBegin + i * 4);
    if (!tplTask)
      continue;
    u32 varBag = tplTask + kAnimeVarBagOffset;
    cb(static_cast<int>(i), varBag);
  }
}

void D2AnimeMenu::SetItemEnabled(int index, bool enabled) {
  if (!ptr_)
    return;
  u32 idBegin = ptr_->itemDataBegin;
  u32 idEnd = ptr_->itemDataEnd;
  if (!idBegin || idBegin == idEnd)
    return;

  u32 count = (idEnd - idBegin) / 4;
  if (static_cast<u32>(index) >= count)
    return;

  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  u32 itemPtr =
      rex::memory::load_and_swap<u32>(base + idBegin + index * 4);
  auto *item = rex::memory::GuestPtr<AnimeItemData_t *>(base, itemPtr);
  item->enabled = enabled ? 1 : 0;
}

int D2AnimeMenu::ItemDataCount() const {
  if (!ptr_)
    return 0;
  u32 b = ptr_->itemDataBegin;
  u32 e = ptr_->itemDataEnd;
  if (!b)
    return 0;
  return static_cast<int>((e - b) / 4);
}

void D2AnimeMenu::AddEntryData(int index, bool enabled) {
  if (!ptr_)
    return;
  MenuAddEntryData(ptr_.guest_address(), static_cast<u32>(index),
                   enabled ? 1 : 0);
}

void D2AnimeMenu::ClearEntries() {
  if (!ptr_)
    return;
  MenuResetAndRebuild(ptr_.guest_address());
}

} // namespace bd
