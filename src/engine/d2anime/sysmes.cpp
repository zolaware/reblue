/**
 * @file    engine/d2anime/sysmes.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/sysmes.h"
#include "core/i18n.h"
#include "core/logging.h"
#include "engine/d2anime/anime_data.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/task.h"

#include <cstddef>

#include <rex/hook.h>
#include <rex/ppc/stack.h>
#include <rex/system/thread_state.h>
#include <rex/types.h>

REX_IMPORT(__imp__SelMesWinTask_Create, CreateSelMes, u32(u32, u32));
REX_IMPORT(__imp__SelMesWinConfig_Init, InitSelMesConfig, void(u32));
REX_IMPORT(__imp__SelMesWinConfig_LoadStrings, LoadSelMesStrings,
           void(u32, u32, u32));
REX_IMPORT(__imp__NormMesWinTask_Create, CreateNormMes, u32(u32, u32));
REX_IMPORT(__imp__NormMesWinConfig_Init, InitNormMesConfig, void(u32));
REX_IMPORT(__imp__NormMesWinConfig_LoadStrings, LoadNormMesStrings,
           void(u32, u32, u32));

namespace {

// SelMesWin config blob passed to SelMesWinTask_Create. The body is opaque,
// only the trailing defaultSel field is written by the host.
struct SelMesWinConfig_t {
  u8 _pad000[0x2B0];
  be_u32 defaultSel;
};
static_assert(offsetof(SelMesWinConfig_t, defaultSel) == 0x2B0);
static_assert(sizeof(SelMesWinConfig_t) == 0x2B4);

// NormMesWin config blob passed to NormMesWinTask_Create, copied into the
// task there. Opaque: NormMesWinConfig_Init and LoadStrings fill it.
struct NormMesWinConfig_t {
  u8 _pad000[0x1AC];
};
static_assert(sizeof(NormMesWinConfig_t) == 0x1AC);

} // namespace

namespace bd::engine {

bool SysMesConfirm::Create(const Task &parent, const char *q1, const char *q2,
                           const char *q3, const char *a1, const char *a2,
                           int defaultSel) {
  if (task_) {
    BD_WARN("[sysmes] already active, killing previous");
    Kill();
  }

  // Raw base: stack_push and the call bridges address the PPC stack, not the
  // heap.
  auto *memory = REX_KERNEL_MEMORY();
  if (!memory)
    return false;
  auto *base = memory->virtual_membase();

  // Engine stores these as wchar_t, and LoadStrings reads them back by RBDEL_
  // name.
  const u32 parentTask = parent.Address();
  AnimeData vb = D2AnimeTask(parentTask).AnimeData();
  vb.SetText("RBDEL_SQ1", q1);
  vb.SetText("RBDEL_SQ2", q2 ? q2 : "");
  vb.SetText("RBDEL_SQ3", q3 ? q3 : "");
  vb.SetText("RBDEL_SA1", a1 ? std::string_view(a1)
                             : std::string_view(i18n::Text("common.yes")));
  vb.SetText("RBDEL_SA2", a2 ? std::string_view(a2)
                             : std::string_view(i18n::Text("common.no")));

  rex::CallFrame frame(*rex::runtime::ThreadState::Get()->context());
  rex::ppc::stack_guard guard(frame.ctx);

  alignas(8) u8 zeroBuf[sizeof(SelMesWinConfig_t)]{};
  u32 configAddr =
      rex::ppc::stack_push(frame.ctx, base, zeroBuf, sizeof(SelMesWinConfig_t));

  InitSelMesConfig(frame, base, configAddr);

  u32 prefixAddr = rex::ppc::stack_push_string(frame.ctx, base, "RBDEL");
  LoadSelMesStrings(frame, base, configAddr, parentTask, prefixAddr);

  reinterpret_cast<SelMesWinConfig_t *>(base + configAddr)->defaultSel =
      static_cast<u32>(defaultSel);

  task_ = SelMesWinTask(CreateSelMes(frame, base, parentTask, configAddr));

  if (!task_ || !parent) {
    BD_ERROR("[sysmes] SelMesWinTask_Create gave no usable task");
    task_.Reset();
    return false;
  }
  task_.SetNotifyParent(parent);

  BD_INFO("[sysmes] created SelMesWinTask at 0x{:08X} (parent=0x{:08X})",
          task_.Address(), parentTask);
  return true;
}

// A popup that no longer resolves reports done, so a screen waiting on one the
// engine has already torn down moves on.
bool SysMesConfirm::Poll() const {
  return !task_ || task_.Confirmed() || task_.Canceled();
}

bool SysMesConfirm::Confirmed() const {
  return task_.Confirmed() && SelectedAnswer() == 0;
}

bool SysMesConfirm::Canceled() const { return task_.Canceled(); }

int SysMesConfirm::SelectedAnswer() const {
  const CommandSelectTask select = task_.CommandSelect();
  if (!select)
    return -1;
  return select.CursorIndex();
}

bool SysMesNotice::Show(const Task &parent, std::string_view line1,
                        std::string_view line2, std::string_view line3) {
  if (task_ && shown1_ == line1 && shown2_ == line2 && shown3_ == line3)
    return true;
  Kill();

  const u32 parentTask = parent.Address();

  auto *memory = REX_KERNEL_MEMORY();
  if (!memory)
    return false;
  auto *base = memory->virtual_membase();

  AnimeData vb = D2AnimeTask(parentTask).AnimeData();
  vb.SetText("RBNOT_S1", line1);
  vb.SetText("RBNOT_S2", line2);
  vb.SetText("RBNOT_S3", line3);

  rex::CallFrame frame(*rex::runtime::ThreadState::Get()->context());
  rex::ppc::stack_guard guard(frame.ctx);

  alignas(8) u8 zeroBuf[sizeof(NormMesWinConfig_t)]{};
  u32 configAddr = rex::ppc::stack_push(frame.ctx, base, zeroBuf,
                                        sizeof(NormMesWinConfig_t));

  InitNormMesConfig(frame, base, configAddr);

  u32 prefixAddr = rex::ppc::stack_push_string(frame.ctx, base, "RBNOT");
  LoadNormMesStrings(frame, base, configAddr, parentTask, prefixAddr);

  task_ = Task(CreateNormMes(frame, base, parentTask, configAddr));
  if (!task_) {
    BD_ERROR("[sysmes] NormMesWinTask_Create gave no usable task");
    return false;
  }

  shown1_ = line1;
  shown2_ = line2;
  shown3_ = line3;
  BD_DEBUG("[sysmes] created NormMesWinTask at 0x{:08X} (parent=0x{:08X})",
           task_.Address(), parentTask);
  return true;
}

void SysMesNotice::Kill() {
  const u32 addr = task_.Address();
  if (!addr)
    return;
  task_.Kill();
  task_.Reset();
  shown1_.clear();
  shown2_.clear();
  shown3_.clear();
}

void SysMesNotice::Drop() {
  task_.Reset();
  shown1_.clear();
  shown2_.clear();
  shown3_.clear();
}

void SysMesVars::Emit(CsvBuilder &b) {
  b.blank()
      .comment("sysmes confirmation dialog variables")
      .vars(sq1, sq2, sq3, sa1, sa2, fs, ln)
      .pos(ps)
      .pos(ofs)
      .vars(wcl, ecl, fcl)
      .blank()
      .comment("sysmes notice window variables")
      .vars(s1, s2, s3, noticeFs, noticeLn)
      .pos(noticePs)
      .vars(noticeWcl, noticeEcl, noticeFcl);
}

void SysMesConfirm::Kill() {
  const u32 addr = task_.Address();
  if (!addr)
    return;
  task_.Kill();
  BD_INFO("[sysmes] killed SelMesWinTask at 0x{:08X}", addr);
  task_.Reset();
}

} // namespace bd::engine
