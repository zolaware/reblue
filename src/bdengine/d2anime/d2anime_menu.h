/**
 * @file    bdengine/d2anime_menu.h
 * @brief   Host-side wrapper for guest AnimeMenu objects.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#pragma once

#include "bdengine/d2anime/d2anime_types.h"

#include <cstdint>
#include <functional>

#include <rex/types.h>

namespace bd {

class D2AnimeMenu {
public:
  D2AnimeMenu() = default;
  explicit D2AnimeMenu(u32 guestAddr);

  AnimeMenu_t *operator->() { return ptr_.host_address(); }
  const AnimeMenu_t *operator->() const { return ptr_.host_address(); }
  u32 guest_address() const { return ptr_.guest_address(); }
  explicit operator bool() const { return static_cast<bool>(ptr_); }

  /**
   * @brief Set activeFlag, deselectAll, cursorShowA, needsRebuild.
   */
  void SetActive(bool active);

  /**
   * @brief Call AnimeMenu_SetVisibleAndPlay - properly adds/removes templates
   *        from the scene tree. Use to hide overlapping menus (crash fix).
   */
  void SetVisible(bool visible);

  /**
   * @brief Call AnimeMenu_attachToParent - transfers cursor ownership.
   */
  void AttachCursor();

  /**
   * @brief Read cursorIndex (+0xCC).
   */
  int CursorIndex() const;

  /**
   * @brief Read enableColor/disableColor (+0x164/+0x168).
   */
  u32 EnableColor() const;
  u32 DisableColor() const;

  /**
   * @brief Iterate template VarBags. Callback receives (index, varBagGuestAddr).
   */
  void ForEachTemplate(std::function<void(int, u32)> cb) const;

  /**
   * @brief Set AnimeItemData.enabled for a specific item index.
   */
  void SetItemEnabled(int index, bool enabled);

  /**
   * @brief Count of items in the itemData vector.
   */
  int ItemDataCount() const;

  /**
   * @brief Add an entry to the allEntries vector. Sets needsRebuild.
   *        Engine will show/hide templates based on entry count.
   */
  void AddEntryData(int index, bool enabled);

  /**
   * @brief Clear all entries (allEntries + visibleItems), reset cursor/scroll.
   */
  void ClearEntries();

private:
  rex::MappedPtr<AnimeMenu_t> ptr_;
};

} // namespace bd
