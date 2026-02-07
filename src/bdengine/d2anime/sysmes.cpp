/**
 * @file    bdengine/sysmes.cpp
 * @brief   SysMesConfirm - host-side wrapper for SelMesWinTask.
 *
 * Uses the engine's native VarBag -> LoadStrings -> Create pipeline.
 * Text, positioning, colors, and font are defined as RBDEL_* variables
 * in the config menu CSV and read by SelMesWinConfig_LoadStrings.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#include "bdengine/d2anime/sysmes.h"
#include "bdengine/d2anime/d2anime.h"
#include "bdengine/common/logging.h"

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

#include <cstring>

using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

// Engine functions
REX_IMPORT(__imp__SelMesWinTask_Create, CreateSelMes,
           u32(u32, u32));
REX_IMPORT(__imp__SelMesWinConfig_Init, InitSelMesConfig, void(u32));
REX_IMPORT(__imp__SelMesWinConfig_LoadStrings, LoadSelMesStrings,
           void(u32, u32, u32));
REX_IMPORT(__imp__NormMesWinTask_Create, CreateNormMes,
           u32(u32, u32));
REX_IMPORT(__imp__NormMesWinConfig_Init, InitNormMesConfig, void(u32));
REX_IMPORT(__imp__NormMesWinConfig_LoadStrings, LoadNormMesStrings,
           void(u32, u32, u32));

namespace {

constexpr u32 kConfigSize = 0x2B4;     // SelMesWin config: 692 bytes
constexpr u32 kOffDefaultSel = 0x2B0;
constexpr u32 kNormConfigSize = 0x1AC; // NormMesWin config: 428 bytes

// Task result offsets
constexpr u32 kOffConfirmed  = 0xDC8;
constexpr u32 kOffCancelled  = 0xDCC;

// CommandSelectTask (manages answer selection within SelMesWinTask)
constexpr u32 kOffCommandSelect = 0xDD0;
constexpr u32 kOffCursorIndex   = 0xB0;

// Task flag offsets
constexpr u32 kOffFlags      = 0x58;
constexpr u32 kOffDestroyFlg = 0x60;

// Notification parent offsets
constexpr u32 kOffNotifyPtr  = 0x48;
constexpr u32 kOffNotifyUID  = 0x50;
constexpr u32 kOffTaskUID    = 0x10;

} // namespace

namespace bd {

bool SysMesConfirm::Create(u32 parentTask, const char *q1, const char *q2,
                           const char *q3, const char *a1, const char *a2,
                           int defaultSel) {
  if (task_) {
    BD_WARN("[sysmes] already active, killing previous");
    Kill();
  }

  auto *base = rex::system::kernel_state()->memory()->virtual_membase();

  // Set dynamic text on the parent task's VarBag - the engine stores
  // these as wchar_t internally, and LoadStrings reads them back.
  u32 vb = parentTask + 0x74;
  VarBagSetString(vb, "RBDEL_SQ1", q1);
  VarBagSetString(vb, "RBDEL_SQ2", q2 ? q2 : "");
  VarBagSetString(vb, "RBDEL_SQ3", q3 ? q3 : "");
  VarBagSetString(vb, "RBDEL_SA1", a1 ? a1 : "Yes");
  VarBagSetString(vb, "RBDEL_SA2", a2 ? a2 : "No");

  // Allocate config struct on guest stack
  rex::CallFrame frame(*rex::runtime::ThreadState::Get()->context());
  rex::ppc::stack_guard guard(frame.ctx);

  alignas(8) u8 zeroBuf[kConfigSize]{};
  u32 configAddr =
      rex::ppc::stack_push(frame.ctx, base, zeroBuf, kConfigSize);

  // Init config with engine defaults
  InitSelMesConfig(frame, base, configAddr);

  // Load text, position, colors, font from VarBag (prefix "RBDEL")
  u32 prefixAddr =
      rex::ppc::stack_push_string(frame.ctx, base, "RBDEL");
  LoadSelMesStrings(frame, base, configAddr, parentTask, prefixAddr);

  store_and_swap<u32>(base + configAddr + kOffDefaultSel,
                          static_cast<u32>(defaultSel));

  // Create the task
  task_ = CreateSelMes(frame, base, parentTask, configAddr);

  if (!task_) {
    BD_ERROR("[sysmes] SelMesWinTask_Create returned null");
    return false;
  }

  // Set notification parent
  store_and_swap<u32>(base + task_ + kOffNotifyPtr, parentTask);
  u64 parentUID = load_and_swap<u64>(base + parentTask + kOffTaskUID);
  store_and_swap<u64>(base + task_ + kOffNotifyUID, parentUID);

  BD_INFO("[sysmes] created SelMesWinTask at 0x{:08X} (parent=0x{:08X})",
          task_, parentTask);
  return true;
}

bool SysMesConfirm::Poll() const {
  if (!task_)
    return true;
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  return load_and_swap<u32>(base + task_ + kOffConfirmed) != 0 ||
         load_and_swap<u32>(base + task_ + kOffCancelled) != 0;
}

bool SysMesConfirm::Confirmed() const {
  if (!task_)
    return false;
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  if (load_and_swap<u32>(base + task_ + kOffConfirmed) == 0)
    return false;
  return SelectedAnswer() == 0;
}

int SysMesConfirm::SelectedAnswer() const {
  if (!task_)
    return -1;
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  u32 cmdSel = load_and_swap<u32>(base + task_ + kOffCommandSelect);
  if (!cmdSel)
    return -1;
  return static_cast<int>(load_and_swap<u32>(base + cmdSel + kOffCursorIndex));
}

bool SysMesConfirm::Cancelled() const {
  if (!task_)
    return false;
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  return load_and_swap<u32>(base + task_ + kOffCancelled) != 0;
}

void SysMesConfirm::Kill() {
  if (!task_)
    return;
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  store_and_swap<u32>(base + task_ + kOffDestroyFlg, 1u);
  u32 flags = load_and_swap<u32>(base + task_ + kOffFlags);
  store_and_swap<u32>(base + task_ + kOffFlags, flags | 0xDEAD0000u);
  BD_INFO("[sysmes] killed SelMesWinTask at 0x{:08X}", task_);
  task_ = 0;
}

/**
 * @brief NormMesWinTask wrapper (message-only, no answers).
 */

/**
 * @brief Write an ASCII string as big-endian wchar_t into the guest config
 *        buffer. Each string slot holds 64 wchars (128 bytes).
 */
static void WriteConfigString(u8 *base, u32 configAddr,
                              u32 offset, const char *str) {
  u32 dst = configAddr + offset;
  size_t len = str ? std::strlen(str) : 0;
  if (len > 63)
    len = 63;
  for (size_t i = 0; i < len; ++i) {
    store_and_swap<u16>(base + dst + i * 2,
                             static_cast<u16>(str[i]));
  }
  store_and_swap<u16>(base + dst + len * 2, 0);
}

bool SysMesNotice::Create(u32 parentTask, const char *q1, const char *q2,
                          const char *q3) {
  if (task_)
    Kill();

  auto *base = rex::system::kernel_state()->memory()->virtual_membase();

  rex::CallFrame frame(*rex::runtime::ThreadState::Get()->context());
  rex::ppc::stack_guard guard(frame.ctx);

  alignas(8) u8 zeroBuf[kNormConfigSize]{};
  u32 configAddr =
      rex::ppc::stack_push(frame.ctx, base, zeroBuf, kNormConfigSize);

  InitNormMesConfig(frame, base, configAddr);

  // Write strings directly into config (skip LoadStrings / VarBag)
  WriteConfigString(base, configAddr, 0, q1);
  WriteConfigString(base, configAddr, 128, q2 ? q2 : "");
  WriteConfigString(base, configAddr, 256, q3 ? q3 : "");

  task_ = CreateNormMes(frame, base, parentTask, configAddr);

  if (!task_) {
    BD_ERROR("[sysmes] NormMesWinTask_Create returned null");
    return false;
  }

  store_and_swap<u32>(base + task_ + kOffNotifyPtr, parentTask);
  u64 parentUID =
      load_and_swap<u64>(base + parentTask + kOffTaskUID);
  store_and_swap<u64>(base + task_ + kOffNotifyUID, parentUID);

  BD_INFO("[sysmes] created NormMesWinTask at 0x{:08X}", task_);
  return true;
}

void SysMesNotice::Kill() {
  if (!task_)
    return;
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  store_and_swap<u32>(base + task_ + kOffDestroyFlg, 1u);
  u32 flags = load_and_swap<u32>(base + task_ + kOffFlags);
  store_and_swap<u32>(base + task_ + kOffFlags, flags | 0xDEAD0000u);
  BD_INFO("[sysmes] killed NormMesWinTask at 0x{:08X}", task_);
  task_ = 0;
}

} // namespace bd
