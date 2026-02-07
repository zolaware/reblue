/**
 * @file    bdengine/d2anime_task.cpp
 * @brief   D2AnimeTask wrapper implementation.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause -- see LICENSE
 */
#include "bdengine/d2anime/d2anime_task.h"
#include "bdengine/common/logging.h"

#include <rex/types.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REX_IMPORT(__imp__LH_Binary__LoadAsync, LoadAsync_Task, u32(u32, u32, u32));
REX_IMPORT(__imp__D2AnimeTask_SetVisibleAndPlay, SetVisibleAndPlay_Task,
           void(u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetStringVar, VarBag_SetString_Task,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetColorRGBA, VarBag_SetColor_Task,
           void(u32, u32, u32));
REX_IMPORT(__imp__AnimeVarBag_SetFloatVar, VarBag_SetFloat_Task,
           void(u32, u32, f64));
REX_IMPORT(__imp__AnimeMenu_FindChildByName, FindChildByName_Task,
           u32(u32, u32));

namespace bd {

D2AnimeTask::D2AnimeTask(u32 guestAddr) {
  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  ptr_ = rex::MappedPtr<D2AnimeTask_t>(
      rex::memory::GuestPtr<D2AnimeTask_t *>(base, guestAddr), guestAddr);
}

D2AnimeTask D2AnimeTask::Load(u32 parentTask, const char *csvPath) {
  rex::ppc::stack_guard guard;
  u32 csvAddr = rex::ppc::stack_push_string(csvPath);
  u32 taskAddr = LoadAsync_Task(parentTask, csvAddr, 0);
  if (!taskAddr) {
    BD_ERROR("[d2anime] D2AnimeTask::Load failed for '{}'", csvPath);
    return D2AnimeTask();
  }

  D2AnimeTask task(taskAddr);
  task->loopFlag = 0;
  SetVisibleAndPlay_Task(taskAddr, 1);

  BD_INFO("[d2anime] D2AnimeTask::Load '{}' at 0x{:08X}", csvPath, taskAddr);
  return task;
}

void D2AnimeTask::SetVisibleAndPlay(bool visible) {
  if (!ptr_)
    return;
  SetVisibleAndPlay_Task(ptr_.guest_address(), visible ? 1 : 0);
}

void D2AnimeTask::Kill() {
  if (!ptr_)
    return;
  ptr_->flags = static_cast<u32>(ptr_->flags) | 0xDEAD0000;
  ptr_->destroyFlag = 1;
  BD_INFO("[d2anime] task 0x{:08X} killed", ptr_.guest_address());
  ptr_ = rex::MappedPtr<D2AnimeTask_t>();
}

bool D2AnimeTask::IsReady() const {
  if (!ptr_)
    return false;
  return static_cast<u32>(ptr_->loadState) == 4;
}

int D2AnimeTask::MenuCount() const {
  if (!ptr_)
    return 0;
  u32 mb = ptr_->menusBegin;
  u32 me = ptr_->menusEnd;
  if (!mb || mb == me)
    return 0;
  return static_cast<int>((me - mb) / 4);
}

D2AnimeMenu D2AnimeTask::Menu(int index) const {
  if (!ptr_)
    return D2AnimeMenu();
  u32 mb = ptr_->menusBegin;
  u32 me = ptr_->menusEnd;
  if (!mb || mb == me)
    return D2AnimeMenu();

  u32 count = (me - mb) / 4;
  if (static_cast<u32>(index) >= count)
    return D2AnimeMenu();

  auto *base = rex::system::kernel_state()->memory()->virtual_membase();
  u32 menuAddr =
      rex::memory::load_and_swap<u32>(base + mb + index * 4);
  if (!menuAddr)
    return D2AnimeMenu();

  return D2AnimeMenu(menuAddr);
}

void D2AnimeTask::SetFloat(const char *name, double value) {
  if (!ptr_)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  VarBag_SetFloat_Task(ptr_.guest_address() + kAnimeVarBagOffset, nameAddr,
                       value);
}

void D2AnimeTask::SetString(const char *name, const char *value) {
  if (!ptr_)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  u32 valAddr = rex::ppc::stack_push_string(value);
  VarBag_SetString_Task(ptr_.guest_address() + kAnimeVarBagOffset, nameAddr,
                        valAddr);
}

void D2AnimeTask::SetColor(const char *name, u32 rgba) {
  if (!ptr_)
    return;
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  VarBag_SetColor_Task(ptr_.guest_address() + kAnimeVarBagOffset, nameAddr,
                       rgba);
}

D2AnimeMenu D2AnimeTask::FindMenuByName(const char *name) const {
  if (!ptr_)
    return D2AnimeMenu();
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  u32 menuAddr = FindChildByName_Task(ptr_.guest_address(), nameAddr);
  if (!menuAddr)
    return D2AnimeMenu();
  return D2AnimeMenu(menuAddr);
}

void D2AnimeTask::Reset() { ptr_ = rex::MappedPtr<D2AnimeTask_t>(); }

} // namespace bd
