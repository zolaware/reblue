/**
 * @file    engine/d2anime/d2anime_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause - see LICENSE
 */
#include "engine/d2anime/d2anime_task.h"

#include <cstddef>
#include <vector>

#include <rex/hook.h>
#include <rex/ppc/stack.h>
#include <rex/types.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/d2anime/anime_data.h"

REX_IMPORT(__imp__LH_Binary__LoadAsync, LoadAsync_Task, u32(u32, u32, u32));
REX_IMPORT(__imp__D2AnimeTask_SetVisibleAndPlay, SetVisibleAndPlay_Task,
           void(u32, u32));
REX_IMPORT(__imp__AnimeMenu_FindChildByName, FindChildByName_Task,
           u32(u32, u32));
// The engine takes the frame in f1, where single-precision values ride as
// doubles, matching the stock caller after its own frsp.
REX_IMPORT(__imp__AnimeData_SetAnimTime, AnimeData_SetAnimTime, void(u32, f64));

namespace bd::engine {

namespace {

// D2AnimeTask_t::loadState. reblue tests these two.
constexpr u32 kLoadStateReady = 4;
constexpr u32 kLoadStateFinished = 5;

struct D2AnimeTask_t {
  /* 0x000 */ u8 _pad000[0x58];
  /* 0x058 */ be_u32 flags;
  /* 0x05C */ u8 _pad05C[0x04];
  /* 0x060 */ be_u32 destroyFlag;
  /* 0x064 */ u8 _pad064[0x04];
  /* 0x068 */ be_u32 visible;
  /* 0x06C */ be_u32 autoPlay;  // init=1, propagated to child menu +0xD8
  /* 0x070 */ be_u32 loadState; // kLoadState*, 1..3 while still loading
  /* 0x074 */ u8 animeData[0x184];
  /* 0x1F8 */ u8 _pad1F8[0x238 - 0x1F8];
  /* 0x238 */ be_u32 loopFlag; // zero = one-shot, then finished
  /* 0x23C */ u8 _pad23C[0x08];
  /* 0x244 */ mem::GuestVec<u32> menus; // vector<AnimeMenu*>
  /* 0x250 */ u8 _pad250[0x10];
  /* 0x260 */ be_u32 drawDirty; // ping-pong between Draw and PostUpdate
  /* 0x264 */ u8 _pad264[0x04];
};
static_assert(offsetof(D2AnimeTask_t, flags) == 0x058);
static_assert(offsetof(D2AnimeTask_t, destroyFlag) == 0x060);
static_assert(offsetof(D2AnimeTask_t, visible) == 0x068);
static_assert(offsetof(D2AnimeTask_t, autoPlay) == 0x06C);
static_assert(offsetof(D2AnimeTask_t, loadState) == 0x070);
static_assert(offsetof(D2AnimeTask_t, loopFlag) == 0x238);
static_assert(offsetof(D2AnimeTask_t, animeData) == 0x074);
static_assert(offsetof(D2AnimeTask_t, menus) == 0x244);
static_assert(offsetof(D2AnimeTask_t, drawDirty) == 0x260);
static_assert(sizeof(D2AnimeTask_t) == 0x268);

// WhenReady tasks still waiting on their parse.
std::vector<D2AnimeTask> g_pendingReveals;

} // namespace

D2AnimeTask D2AnimeTask::Load(const Task &parent, const char *csvPath,
                              Reveal reveal) {
  rex::ppc::stack_guard guard;
  u32 csvAddr = rex::ppc::stack_push_string(csvPath);
  u32 taskAddr = LoadAsync_Task(parent.Address(), csvAddr, 0);
  if (!taskAddr) {
    BD_ERROR("[d2anime] D2AnimeTask::Load failed for '{}'", csvPath);
    return D2AnimeTask();
  }

  D2AnimeTask task(taskAddr);
  if (auto *self = task.Self<D2AnimeTask_t>())
    self->loopFlag = 0u;
  // Hidden until parsed, so a screen never draws its half-loaded widgets.
  SetVisibleAndPlay_Task(taskAddr, 0);
  if (reveal == Reveal::WhenReady)
    g_pendingReveals.push_back(task);

  BD_INFO("[d2anime] D2AnimeTask::Load '{}' at 0x{:08X}", csvPath, taskAddr);
  return task;
}

void D2AnimeTask::Tick() {
  for (size_t i = 0; i < g_pendingReveals.size();) {
    const u32 addr = g_pendingReveals[i].Address();
    const auto drop = [&] {
      g_pendingReveals[i] = g_pendingReveals.back();
      g_pendingReveals.pop_back();
    };
    // A task whose parent died mid-load goes with it.
    if (!addr) {
      drop();
      continue;
    }
    const u32 state = g_pendingReveals[i].LoadState();
    if (state == kLoadStateReady || state == kLoadStateFinished) {
      SetVisibleAndPlay_Task(addr, 1);
      drop();
      continue;
    }
    ++i;
  }
}

void D2AnimeTask::SetVisibleAndPlay(bool visible) {
  const u32 addr = Address();
  if (!addr)
    return;
  SetVisibleAndPlay_Task(addr, visible ? 1 : 0);
}

bool D2AnimeTask::IsVisible() const {
  const auto *self = Self<D2AnimeTask_t>();
  return self && static_cast<u32>(self->visible) != 0;
}

void D2AnimeTask::SetVisible(bool visible) {
  if (auto *self = Self<D2AnimeTask_t>())
    self->visible = visible ? 1u : 0u;
}

u32 D2AnimeTask::AutoPlay() const {
  const auto *self = Self<D2AnimeTask_t>();
  return self ? static_cast<u32>(self->autoPlay) : 0;
}

u32 D2AnimeTask::LoadState() const {
  const auto *self = Self<D2AnimeTask_t>();
  return self ? static_cast<u32>(self->loadState) : 0;
}

u32 D2AnimeTask::LoopFlag() const {
  const auto *self = Self<D2AnimeTask_t>();
  return self ? static_cast<u32>(self->loopFlag) : 0;
}

u32 D2AnimeTask::DrawDirty() const {
  const auto *self = Self<D2AnimeTask_t>();
  return self ? static_cast<u32>(self->drawDirty) : 0;
}

f32 D2AnimeTask::AnimSpeed() const { return AnimeData().Speed(); }

f32 D2AnimeTask::AnimLength() const { return AnimeData().Length(); }

bool D2AnimeTask::IsAnimFinished() const {
  return *this && LoadState() == kLoadStateFinished;
}

engine::AnimeData D2AnimeTask::AnimeData() const {
  const u32 addr = Address();
  return engine::AnimeData(addr ? addr + offsetof(D2AnimeTask_t, animeData)
                                : 0);
}

void D2AnimeTask::SetAnimTime(f32 frame) {
  if (!*this)
    return;
  AnimeData_SetAnimTime(AnimeData().Address(), static_cast<f64>(frame));
}

void D2AnimeTask::SetAnimSpeed(f32 speed) { AnimeData().SetSpeed(speed); }

f32 D2AnimeTask::AnimTime() const { return AnimeData().Frame(); }

void D2AnimeTask::Kill() {
  if (!*this)
    return;
  const u32 addr = Address();
  std::erase_if(g_pendingReveals,
                [addr](const D2AnimeTask &r) { return r.Address() == addr; });
  Task::Kill();
  BD_INFO("[d2anime] task 0x{:08X} killed", addr);
  Reset();
}

// Ready is parsed and playing. Finished is parsed with its one-shot timeline
// spent. A screen preloaded hidden reaches finished on its own, so both count.
bool D2AnimeTask::IsReady() const {
  if (!*this)
    return false;
  const u32 state = LoadState();
  return state == kLoadStateReady || state == kLoadStateFinished;
}

void D2AnimeTask::SetFloat(const char *name, double value) {
  AnimeData().SetFloat(name, value);
}

void D2AnimeTask::SetString(const char *name, const char *value) {
  AnimeData().SetString(name, value);
}

void D2AnimeTask::SetText(const char *name, std::string_view utf8) {
  AnimeData().SetText(name, utf8);
}

size_t D2AnimeTask::MenuCount() const {
  const auto *self = Self<D2AnimeTask_t>();
  return self ? self->menus.size() : 0;
}

AnimeMenu D2AnimeTask::MenuAt(size_t i) const {
  const auto *self = Self<D2AnimeTask_t>();
  return AnimeMenu(self ? self->menus[static_cast<u32>(i)] : 0);
}

AnimeMenu D2AnimeTask::FindMenu(const char *name) const {
  const u32 addr = Address();
  if (!addr)
    return AnimeMenu();
  rex::ppc::stack_guard guard;
  u32 nameAddr = rex::ppc::stack_push_string(name);
  return AnimeMenu(FindChildByName_Task(addr, nameAddr));
}

} // namespace bd::engine
