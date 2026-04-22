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

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc.h>
#include <rex/ppc/function.h>
#include <rex/system/format.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/types.h>
#include <rex/ui/keybinds.h>

#include <cctype>
#include <string>
#include <string_view>

extern "C" {
__declspec(dllimport) int __stdcall MultiByteToWideChar(
    unsigned int CodePage, unsigned long dwFlags,
    const char* lpMultiByteStr, int cbMultiByte,
    wchar_t* lpWideCharStr, int cchWideChar);
__declspec(dllimport) int __stdcall WideCharToMultiByte(
    unsigned int CodePage, unsigned long dwFlags,
    const wchar_t* lpWideCharStr, int cchWideChar,
    char* lpMultiByteStr, int cbMultiByte,
    const char* lpDefaultChar, int* lpUsedDefaultChar);
}

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
REXCVAR_DEFINE_BOOL(bd_dbgprint, false, "Blue Dragon",
                    "Print DbgPrint output to host log");
REXCVAR_DEFINE_BOOL(bd_dbgprint_sjis, true, "Blue Dragon",
                    "Convert Shift-JIS (CP932) bytes in DbgPrint output to UTF-8");

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
 * @brief File access trace; replaces bdLogFileAccess (0x82270B60).
 */
void bdLogFileAccessHook(mapped_string filepath) {
  if (REXCVAR_GET(bd_hcfile_log))
    BD_DEBUG("[hcfile] {}", filepath.value());
}
PPC_HOOK(bdLogFileAccess, bdLogFileAccessHook);

namespace {

std::string SjisToUtf8(std::string_view sjis) {
  if (sjis.empty())
    return {};
  int wlen = MultiByteToWideChar(932, 0, sjis.data(), (int)sjis.size(),
                                 nullptr, 0);
  if (wlen <= 0)
    return std::string(sjis);
  std::wstring wbuf((size_t)wlen, L'\0');
  MultiByteToWideChar(932, 0, sjis.data(), (int)sjis.size(), wbuf.data(),
                      wlen);
  int ulen = WideCharToMultiByte(65001, 0, wbuf.data(), wlen, nullptr, 0,
                                 nullptr, nullptr);
  if (ulen <= 0)
    return std::string(sjis);
  std::string out((size_t)ulen, '\0');
  WideCharToMultiByte(65001, 0, wbuf.data(), wlen, out.data(), ulen, nullptr,
                      nullptr);
  return out;
}

}  // namespace

/**
 * @brief DbgPrint_v at 0x820D1998 (retail stub: `li r3,1; blr`).
 *        Also used as a no-op callback default in vtables/data tables; the
 *        range check skips those before touching the format engine.
 */
u32 bdDebugPrintHook(mapped_string fmt) {
  if (!REXCVAR_GET(bd_dbgprint))
    return 1;

  u32 fmt_addr = fmt.guest_address();
  if (fmt_addr < 0x82000000u || fmt_addr >= 0xC0000000u)
    return 1;

  auto& ctx = *rex::runtime::current_ppc_context();
  auto* base = REX_KERNEL_MEMORY()->virtual_membase();

  rex::system::format::StackArgList args(ctx, base, 1);
  rex::system::format::StringFormatData data(
      reinterpret_cast<const u8*>(fmt.host_address()));

  int32_t count = rex::system::format::format_core(base, data, args,
                                                   /*wide=*/false);
  if (count <= 0)
    return 1;

  auto str = data.str();
  while (!str.empty() &&
         std::isspace(static_cast<unsigned char>(str.back())))
    str.pop_back();
  if (str.empty())
    return 1;

  if (REXCVAR_GET(bd_dbgprint_sjis))
    str = SjisToUtf8(str);

  BD_INFO("[dbg] {}", str);
  return 1;
}
PPC_HOOK(rex_DebugPrint, bdDebugPrintHook);
