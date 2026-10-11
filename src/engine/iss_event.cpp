/**
 * @file    engine/iss_event.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/iss_event.h"

#include <atomic>
#include <cstddef>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/events.h"

namespace bd::engine {

namespace {

struct IssEvent_t {
  /* 0x000 */ u8 _pad000[0x88];
  /* 0x088 */ be_u32 state;
  /* 0x08C */ u8 _pad08C[0x3AC - 0x8C];
  /* 0x3AC */ be_i32 eventId;
};
static_assert(offsetof(IssEvent_t, state) == 0x88);
static_assert(offsetof(IssEvent_t, eventId) == 0x3AC);

constexpr u32 kEvtPlaying = 2;

constexpr i32 kNoEvent = -1;
constexpr i32 kEventIdScale = 100;

// Live issEvent tasks. Slots rather than a counter because issGimmick shares
// the issEvent destructor, so an untracked teardown has to be a no op. The
// prefix is parsed from the .evt basename at creation and lives nowhere on the
// task, so it rides beside the address.
constexpr size_t kMaxLiveEvents = 8;
std::atomic<u32> g_liveEvents[kMaxLiveEvents]{};
std::atomic<u32> g_eventPrefix[kMaxLiveEvents]{};

// Neither hook body publishes. A subscriber runs engine code on the same
// context the mid-asm site interrupted, and both sites sit mid function with
// volatile registers the engine still needs, one of them not even declared to
// the hook. The body leaves the id here and the wrapper around the containing
// engine function publishes it once that function has returned.
std::atomic<i32> g_pendingStarted{kNoEvent};
std::atomic<i32> g_pendingEnded{kNoEvent};

constexpr u32 kPrefixHighShift = 8;
constexpr u32 kPrefixByteMask = 0xFF;

// One writer: event tasks are created and torn down from BD's logic step, so
// the scan needs no CAS. The release store on the address publishes the prefix.
void OnEventSceneCreated(u32 address, i32 eventId, u32 prefix) {
  if (!address)
    return;
  for (auto &slot : g_liveEvents) {
    if (slot.load(std::memory_order_relaxed))
      continue;
    g_eventPrefix[&slot - g_liveEvents].store(prefix, std::memory_order_relaxed);
    slot.store(address, std::memory_order_release);
    g_pendingStarted.store(eventId, std::memory_order_relaxed);
    return;
  }
}

void OnEventSceneDestroyed(u32 address) {
  if (!address)
    return;
  for (auto &slot : g_liveEvents) {
    if (slot.load(std::memory_order_relaxed) != address)
      continue;
    // The task is inside its destructor, so the id is read off the layout
    // rather than through a handle the DEAD flag would refuse.
    const auto *event = mem::try_at<const IssEvent_t>(address);
    g_pendingEnded.store(event ? static_cast<i32>(event->eventId) : kNoEvent,
                         std::memory_order_relaxed);
    slot.store(0u, std::memory_order_release);
    return;
  }
}

void PublishStarted() {
  const i32 eventId =
      g_pendingStarted.exchange(kNoEvent, std::memory_order_relaxed);
  if (eventId != kNoEvent)
    Events::Publish(CutsceneStarted{eventId});
}

void PublishEnded() {
  const i32 eventId = g_pendingEnded.exchange(kNoEvent, std::memory_order_relaxed);
  if (eventId != kNoEvent)
    Events::Publish(CutsceneEnded{eventId});
}

} // namespace

size_t IssEvent::LiveCount() {
  size_t n = 0;
  for (const auto &slot : g_liveEvents)
    if (slot.load(std::memory_order_acquire))
      ++n;
  return n;
}

IssEvent IssEvent::LiveAt(size_t i) {
  for (const auto &slot : g_liveEvents) {
    const u32 address = slot.load(std::memory_order_acquire);
    if (!address)
      continue;
    if (i-- == 0)
      return IssEvent(address);
  }
  return IssEvent();
}

bool IssEvent::AnyPlaying() {
  for (size_t i = 0; i < LiveCount(); ++i)
    if (LiveAt(i).Playing())
      return true;
  return false;
}

i32 IssEvent::EventId() const {
  const auto *self = Self<IssEvent_t>();
  return self ? static_cast<i32>(self->eventId) : kNoEvent;
}

bool IssEvent::Playing() const {
  const auto *self = Self<IssEvent_t>();
  return self && static_cast<u32>(self->state) == kEvtPlaying;
}

i32 IssEvent::EventNumber() const {
  const i32 id = EventId();
  return id < 0 ? kNoEvent : id / kEventIdScale;
}

i32 IssEvent::SceneNumber() const {
  const i32 id = EventId();
  return id < 0 ? kNoEvent : id % kEventIdScale;
}

std::string IssEvent::Prefix() const {
  const u32 address = Address();
  if (!address)
    return {};
  for (size_t i = 0; i < kMaxLiveEvents; ++i) {
    if (g_liveEvents[i].load(std::memory_order_acquire) != address)
      continue;
    const u32 p = g_eventPrefix[i].load(std::memory_order_relaxed);
    const char chars[3] = {
        static_cast<char>((p >> kPrefixHighShift) & kPrefixByteMask),
        static_cast<char>(p & kPrefixByteMask), '\0'};
    return std::string(chars);
  }
  return {};
}

} // namespace bd::engine

// bdEventSceneCreate. r30 = the new issEvent, r11 = its id, r31 = the .evt
// basename both were parsed from. Two instructions later BD stores that id to
// its own current event global, gated on id % 100 == 0. reblue mirrors it here
// instead of polling that global.
void bdEventSceneCreatedHook(PPCRegister &r30, PPCRegister &r11,
                             PPCRegister &r31) {
  const char *name = bd::mem::str(r31.u32);
  u32 prefix = 0;
  if (name[0])
    prefix = static_cast<u32>(static_cast<u8>(name[0])) << 8 |
             static_cast<u8>(name[1]);
  bd::engine::OnEventSceneCreated(r30.u32, static_cast<i32>(r11.u32), prefix);
}

// Publishing from here rather than from the body above works because the
// publish is host-only, so the returned issEvent in r3 survives it untouched.
REX_EXTERN(__imp__bdEventSceneCreate);
REX_HOOK_RAW(bdEventSceneCreate) {
  __imp__bdEventSceneCreate(ctx, base);
  bd::engine::PublishStarted();
}

// issEvent destructor, at BD's matching clear. r31 = the dying task.
void bdEventSceneDestroyedHook(PPCRegister &r31) {
  bd::engine::OnEventSceneDestroyed(r31.u32);
}

// The issEvent destructor itself, which issGimmick shares.
REX_EXTERN(__imp__issEvent__dtor);
REX_HOOK_RAW(issEvent__dtor) {
  __imp__issEvent__dtor(ctx, base);
  bd::engine::PublishEnded();
}
