/**
 * @file    bdengine/hooks/debug.cpp
 * @brief   Debug tool hooks: wireframe, camera bbox, debug config overlay.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "bdengine/common/global_config.h"
#include "bdengine/common/logging.h"
#include "bdengine/platform/keyboard_bridge.h"

#include <rex/types.h>
#include <rex/cvar.h>
#include <rex/memory/utils.h>
#include <rex/ppc.h>
#include <rex/ppc/function.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>

// CVars
REXCVAR_DEFINE_BOOL(bd_wireframe, false, "Blue Dragon",
                    "Enable wireframe rendering");
REXCVAR_DEFINE_BOOL(bd_camera_bbox, false, "Blue Dragon",
                    "Enable camera bounding box debug display");
REXCVAR_DEFINE_BOOL(bd_debug_menu, false, "Blue Dragon",
                    "Enable debug menu boot, tools, and labels");
REXCVAR_DEFINE_BOOL(bd_mindows, false, "Blue Dragon",
                    "Enable Mindows config overlay (F11 to toggle visibility)");
REXCVAR_DEFINE_BOOL(bd_hcfile_log, false, "Blue Dragon",
                    "Log file accesses to console (hcfile trace)");

namespace bd {

void ApplyDebugConfig() {
  auto *cfg = GetGlobalConfig();
  if (!cfg)
    return;

  if (REXCVAR_GET(bd_debug_menu)) {
    cfg->debugMenuBoot = 1u;
    cfg->debugMenuBuild = 1u;
    cfg->debugMenuMemory = 1u;
    cfg->debugLabels = 1u;
    cfg->mainMenu = 1u;
    cfg->userMenu = 1u;
    cfg->toolMenu = 1u;
    cfg->toolEntryBits = kAllToolEntryBits;
  }

  if (REXCVAR_GET(bd_mindows)) {
    cfg->debugMindows = 1u;
    auto *flag = GetMindowsHiddenFlag();
    if (flag)
      *flag = 0u;
  }
}

void ToggleMindows() {
  auto *flag = GetMindowsHiddenFlag();
  if (flag) {
    u32 cur = *flag;
    *flag = cur ^ 1u;
    BD_INFO("Mindows overlay {}", cur ? "shown" : "hidden");
  }
}

} // namespace bd

/**
 * @brief Guest controller button reading (replicates bdInputCheckButton).
 */
static bool ReadGuestButton(u8* base, int button_id) {
    constexpr u32 kInputStateAddr = 0x82DC9844;
    constexpr u32 kSlotTableAddr  = 0x82772620;
    constexpr u32 kBitmaskTable   = 0x82062808;

    u32 input = rex::memory::load_and_swap<u32>(base + kInputStateAddr);
    if (!input) return false;

    u32 slot = rex::memory::load_and_swap<u32>(base + input + 4 * (0 + 44));
    i32 locked = static_cast<i32>(
        rex::memory::load_and_swap<u32>(base + input + 4 * (slot + 4)));
    if (locked > 0) return false;

    u32 slot_ptr = rex::memory::load_and_swap<u32>(base + kSlotTableAddr + slot * 4);
    u32 buttons = rex::memory::load_and_swap<u32>(base + slot_ptr + 4);
    u32 mask = rex::memory::load_and_swap<u32>(base + kBitmaskTable + button_id * 4);
    return (buttons & mask) != 0;
}

/**
 * @brief Post-init hook; fires after bdGameSettingsInit writes defaults.
 */
void bdPostConfigInitHook() {
  bd::ApplyDebugConfig();
  rex::ui::RegisterBind("bind_mindows", "F11", "Toggle Mindows config overlay",
                        [] { bd::ToggleMindows(); });
  BD_INFO("bd::ApplyDebugConfig applied (debug_menu={})",
          REXCVAR_GET(bd_debug_menu));
}

/**
 * @brief Per-frame keyboard bridge (midasm at 0x82126B04).
 */
void bdKeyboardPollHook() {
    bd::PollKeyboardToGuest();
}

/**
 * @brief Wireframe rendering (midasm).
 */
bool bdWireframeHook(PPCRegister &r11) {
  if (REXCVAR_GET(bd_wireframe)) {
    r11.u64 = 1;
    return true;
  }
  return false;
}

/**
 * @brief Camera bounding box (midasm).
 */
bool bdCameraBBoxHook(PPCRegister &r11) {
  if (REXCVAR_GET(bd_camera_bbox)) {
    r11.u64 = 1;
    return true;
  }
  return false;
}

/**
 * @brief Debug printf capture. Midasm at 0x82273420, after vsnprintf formats
 *        the string into a stack buffer. r3 points to the formatted string.
 */
void bdDebugPrintfCapture(mapped_string fmt, mapped_string arg2) {
    if(fmt.value().empty())
        return;
    if(fmt.value()[0] == '\n')
        return;

    BD_INFO("[dbg] {}", fmt.value());
  return;
}

/**
 * @brief File access trace; replaces bdLogFileAccess (0x82270B60).
 *        Redirects guest console output to the host logger.
 */
void bdLogFileAccessHook(mapped_string filepath) {
  if (REXCVAR_GET(bd_hcfile_log))
    BD_DEBUG("[hcfile] {}", filepath.value());
}
PPC_HOOK(bdLogFileAccess, bdLogFileAccessHook);
