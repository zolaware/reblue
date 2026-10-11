/**
 * @file    engine/config.h
 * @brief   bd::Config, the fixed singleton holding the bd_config.ini values the
 *          engine keeps consulting at runtime.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

class Config : public Object {
public:
  Config() = default;
  explicit Config(u32 address) : Object(address) {}

  static Config Get();

  u32 NewTables() const;
  bool CamRollInv() const;

  bool SetHddCache(u32 v);
  bool SetDebugInputKey(u32 v);
  bool SetDebugInputPad(u32 v);
  bool SetDebugMenuBoot(u32 v);
  bool SetDebugMenuBuild(u32 v);
  bool SetDebugMenuMemory(u32 v);
  bool SetDebugLabels(u32 v);
  bool SetMainMenu(u32 v);
  bool SetUserMenu(u32 v);
  bool SetToolMenu(u32 v);
  bool SetToolEntryBits(u32 v);
  bool SetDebugMindows(u32 v);
  bool SetCamRollInv(u32 v);
  bool SetCamRollSpd(f32 v);
};

} // namespace bd::engine
