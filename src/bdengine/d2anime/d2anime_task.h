/**
 * @file    bdengine/d2anime_task.h
 * @brief   Host-side wrapper for guest D2AnimeTask objects.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#pragma once

#include "bdengine/d2anime/d2anime_menu.h"
#include "bdengine/d2anime/d2anime_types.h"

#include <cstdint>

#include <rex/types.h>

namespace bd {

class D2AnimeTask {
public:
  D2AnimeTask() = default;
  explicit D2AnimeTask(u32 guestAddr);

  /**
   * @brief Create via LH_Binary__LoadAsync. Returns null wrapper on failure.
   */
  static D2AnimeTask Load(u32 parentTask, const char *csvPath);

  D2AnimeTask_t *operator->() { return ptr_.host_address(); }
  const D2AnimeTask_t *operator->() const { return ptr_.host_address(); }
  u32 guest_address() const { return ptr_.guest_address(); }
  explicit operator bool() const { return static_cast<bool>(ptr_); }

  /**
   * @brief Call D2AnimeTask_SetVisibleAndPlay.
   */
  void SetVisibleAndPlay(bool visible);

  /**
   * @brief Set DEAD flag and destroyFlag. Clears the wrapper.
   */
  void Kill();

  /**
   * @brief True when loadState == 4 (ready).
   */
  bool IsReady() const;

  /**
   * @brief Number of menus in menusVec.
   */
  int MenuCount() const;

  /**
   * @brief Get D2AnimeMenu for menu at index in menusVec.
   */
  D2AnimeMenu Menu(int index) const;

  /**
   * @brief Find a menu by its CSV-defined name (e.g. "SltSection", "ModList").
   */
  D2AnimeMenu FindMenuByName(const char *name) const;

  /**
   * @brief VarBag helpers - task's embedded AnimeData at +0x74.
   */
  void SetFloat(const char *name, double value);
  void SetString(const char *name, const char *value);
  void SetColor(const char *name, u32 rgba);

  /**
   * @brief Clear wrapper without killing the guest task.
   */
  void Reset();

private:
  rex::MappedPtr<D2AnimeTask_t> ptr_;
};

} // namespace bd
