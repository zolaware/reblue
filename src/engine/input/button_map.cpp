#include "engine/input/button_map.h"

#include <chrono>

#include <rex/hook.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/input/binding_store.h"
#include "gpu/gpu.h"

REX_EXTERN(__imp__bdGetCurrentMapPath);

namespace bd::engine {

namespace addr {
inline constexpr u32 kTypeRows = 0x8205D798;
inline constexpr u32 kMechatRows = 0x8205D888;
inline constexpr u32 kButtonMasks = 0x82062808;
} // namespace addr

namespace {

constexpr int kGeneralSlots = 15;
constexpr int kMechatSlots = 9;
constexpr u32 kIdBytes = 4;
constexpr u32 kGeneralRowBytes = u32(kGeneralSlots) * kIdBytes;
constexpr u32 kRowAlign = 16;

constexpr int kMechatRowCount = 4;
constexpr u32 kMechatRowBytes = u32(kMechatSlots) * kIdBytes;

constexpr int kUnboundId = 24;
constexpr int kFirstSpareId = 25;
constexpr int kIdEnd = 32;

constexpr int kRightStickSlot = 14;

constexpr int kMechatAimXSlot = 7;
constexpr int kMechatAimYSlot = 8;

constexpr u32 kAimAxisBase = 0;

struct PinnedId {
  Action action;
  int id;
};

constexpr PinnedId kDpadIds[] = {
    {Action::NavUp, 0},
    {Action::NavDown, 1},
    {Action::NavLeft, 2},
    {Action::NavRight, 3},
};

using Clock = std::chrono::steady_clock;
constexpr auto kInstallRetry = std::chrono::seconds(1);
Clock::time_point g_nextInstall{};

bool g_warnedNoMemory = false;
bool g_warnedReadOnly = false;
bool g_warnedNoFreeId = false;
bool g_imageWritable = false;

u32 g_shippedMechat[kMechatSlots] = {};
bool g_haveShippedMechat = false;

const u32 *ShippedMechat() {
  if (g_haveShippedMechat)
    return g_shippedMechat;
  const auto *row = mem::try_at<const be_u32>(addr::kMechatRows);
  if (!row)
    return nullptr;
  for (int slot = 0; slot < kMechatSlots; ++slot)
    g_shippedMechat[slot] = static_cast<u32>(row[slot]);
  g_haveShippedMechat = true;
  return g_shippedMechat;
}

bool MakeWritable(u32 va, u32 size) {
  auto *runtime = rex::Runtime::instance();
  auto *memory = runtime ? runtime->memory() : nullptr;
  auto *heap = memory ? memory->LookupHeap(va) : nullptr;
  if (!heap)
    return false;
  return heap->Protect(va, size,
                       rex::memory::kMemoryProtectRead |
                           rex::memory::kMemoryProtectWrite);
}

bool NeedsId(Action action) {
  const ActionDesc &desc = Describe(action);
  return desc.kind == ActionKind::Mapped && desc.axis == AxisPair::None;
}

int PadId(Action action) {
  for (const Source &source : Bindings::Get().Sources(action))
    if (source.kind == SourceKind::PadButton)
      return static_cast<int>(source.code);
  return -1;
}

Action Owner(const int (&id)[kActionCount], int wanted, Action except) {
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (action != except && id[i] == wanted)
      return action;
  }
  return Action::Count;
}

bool SameSources(Action a, Action b) {
  return Bindings::Get().Sources(a) == Bindings::Get().Sources(b);
}

void Assign(int (&id)[kActionCount]) {
  for (int i = 0; i < kActionCount; ++i)
    id[i] = -1;

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (NeedsId(action) && Describe(action).fixedId >= 0)
      id[i] = Describe(action).fixedId;
  }
  for (const PinnedId &pinned : kDpadIds)
    id[static_cast<int>(pinned.action)] = pinned.id;

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (id[i] >= 0 || !NeedsId(action))
      continue;
    const int pad = PadId(action);
    if (pad < 0 || pad >= kIdEnd)
      continue;
    const Action owner = Owner(id, pad, action);
    if (owner != Action::Count && !SameSources(action, owner))
      continue;
    id[i] = pad;
  }

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    if (id[i] >= 0 || !NeedsId(action))
      continue;
    const int shipped = ButtonMap::ShippedId(action, 0);
    if (shipped >= 0 && Owner(id, shipped, action) == Action::Count) {
      id[i] = shipped;
      continue;
    }
    for (int spare = kFirstSpareId; spare < kIdEnd; ++spare) {
      if (Owner(id, spare, action) == Action::Count) {
        id[i] = spare;
        break;
      }
    }
    if (id[i] < 0 && !g_warnedNoFreeId) {
      g_warnedNoFreeId = true;
      BD_WARN("[input] no free button id left for {}", ToString(action));
    }
  }
}

void BuildRows(const int (&id)[kActionCount], u32 (&general)[kGeneralSlots],
               u32 (&mechat)[kMechatSlots]) {
  for (u32 &value : general)
    value = u32(kUnboundId);
  general[kRightStickSlot] = 0;
  for (u32 &value : mechat)
    value = u32(kUnboundId);

  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    if (id[i] < 0 || desc.slot < 0 || !NeedsId(action))
      continue;
    if (desc.context == ActionContext::Mechat) {
      if (desc.slot < kMechatSlots)
        mechat[desc.slot] = u32(id[i]);
    } else if (desc.slot < kGeneralSlots) {
      general[desc.slot] = u32(id[i]);
    }
  }

  mechat[kMechatAimXSlot] = kAimAxisBase;
  mechat[kMechatAimYSlot] = kAimAxisBase + 1;
}

} // namespace

ButtonMap &ButtonMap::Get() {
  static ButtonMap s;
  return s;
}

int ButtonMap::ShippedId(Action action, int controlType) {
  const ActionDesc &desc = Describe(action);
  const bool mechat = desc.context == ActionContext::Mechat;
  const int slots = mechat ? kMechatSlots : kGeneralSlots;
  if (desc.slot < 0 || desc.slot >= slots)
    return -1;
  u32 value = 0;
  if (mechat) {
    const u32 *row = ShippedMechat();
    if (!row)
      return -1;
    value = row[desc.slot];
  } else {
    if (controlType < 0 || controlType >= kControlTypes)
      return -1;
    const auto *p = mem::try_at<const be_u32>(
        addr::kTypeRows + u32(controlType) * kGeneralRowBytes +
        u32(desc.slot) * kIdBytes);
    if (!p)
      return -1;
    value = static_cast<u32>(*p);
  }
  return value < u32(kUnboundId) ? static_cast<int>(value) : -1;
}

void ButtonMap::Rebuild() {
  Bindings::Get().Migrate();
  const u32 generation = Bindings::Get().Generation();
  if (generation == generation_ && installed_)
    return;
  if (!mem::ready())
    return;
  const Clock::time_point now = Clock::now();
  if (generation == generation_ && now < g_nextInstall)
    return;
  g_nextInstall = now + kInstallRetry;
  Assign(id_);
  generation_ = generation;
  Install();
}

int ButtonMap::Id(Action action) const {
  const int i = static_cast<int>(action);
  if (generation_ == kNoGeneration || i < 0 || i >= kActionCount)
    return -1;
  return id_[i];
}

u32 ButtonMap::Mask(Action action) const {
  if (!installed_)
    return 0;
  const int id = Id(action);
  if (id < 0 || id >= kIdEnd)
    return 0;
  return mem::try_load<u32>(addr::kButtonMasks + u32(id) * kIdBytes);
}

void ButtonMap::Install() {
  if (generation_ == kNoGeneration || !mem::ready())
    return;
  if (!rowVA_) {
    rowVA_ = gpu::HostHeap::Get().AllocGuest(kGeneralRowBytes, kRowAlign);
    if (!rowVA_) {
      if (!g_warnedNoMemory) {
        g_warnedNoMemory = true;
        BD_WARN("[input] no engine memory for the button map, the shipped "
                "rows stand");
      }
      return;
    }
  }

  u32 general[kGeneralSlots];
  u32 mechat[kMechatSlots];
  BuildRows(id_, general, mechat);

  for (int slot = 0; slot < kGeneralSlots; ++slot)
    mem::try_store<u32>(rowVA_ + u32(slot) * kIdBytes, general[slot]);

  if (!g_imageWritable) {
    g_imageWritable =
        MakeWritable(addr::kMechatRows,
                     u32(kMechatRowCount) * kMechatRowBytes) &&
        MakeWritable(addr::kButtonMasks, u32(kIdEnd) * kIdBytes);
  }
  if (!g_imageWritable) {
    if (!g_warnedReadOnly) {
      g_warnedReadOnly = true;
      BD_ERROR("[input] the mechat map stayed read-only, it keeps the "
               "shipped buttons");
    }
    return;
  }

  if (!ShippedMechat())
    return;

  for (int row = 0; row < kMechatRowCount; ++row) {
    const u32 base = addr::kMechatRows + u32(row) * kMechatRowBytes;
    for (int slot = 0; slot < kMechatSlots; ++slot)
      mem::try_store<u32>(base + u32(slot) * kIdBytes, mechat[slot]);
  }

  for (int spare = kFirstSpareId; spare < kIdEnd; ++spare) {
    const bool used = Owner(id_, spare, Action::Count) != Action::Count;
    mem::try_store<u32>(addr::kButtonMasks + u32(spare) * kIdBytes,
                        used ? (1u << spare) : 0u);
  }

  installed_ = true;
}

} // namespace bd::engine

REX_HOOK_RAW(bdGetCurrentMapPath) {
  const u32 row = bd::engine::ButtonMap::Get().GeneralRowVA();
  if (!row) {
    __imp__bdGetCurrentMapPath(ctx, base);
    return;
  }
  ctx.r3.u32 = row;
}
