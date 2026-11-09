/**
 * @file    engine/guest_texlist.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/guest_texlist.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <rex/ppc/hook.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/guest_prim.h"
#include "gpu/gpu.h"

// ReXGlue numbers GPR ordinals over integer parameters only, so every register
// a float argument reserves has to be spelled out as a placeholder for the ones
// behind it to sit where the engine reads them.
REX_IMPORT(__imp__VisualTexture__RequestTexlist, TexlistRequest,
           u32(u32, u32, u32));
REX_IMPORT(__imp__VisualTexture__PollLoadState, TexlistPoll, u32(u32));

namespace bd::engine {

namespace {

// A VisualTexture is the holder Visual__SelectRenderTarget takes: it reads the
// texture table at +8 and picks entry 'slot' out of it. The second half is the
// LH_Texlist load handle the request runs through, so the whole thing gets
// built rather than just the two words the draw path reads.
struct VisualTexture_t {
  /* 0x00 */ be_u32 vtable;
  /* 0x04 */ be_u32 state;
  /* 0x08 */ be_u32 table;
  /* 0x0C */ be_u32 handleVtable;
  /* 0x10 */ be_u32 handleObject;
  /* 0x14 */ be_u32 handleState;
  /* 0x18 */ be_u32 handleArg0;
  /* 0x1C */ be_u32 handleArg1;
};
static_assert(sizeof(VisualTexture_t) == 0x20);

// What the request takes for the list itself. The engine copies both the
// descriptor and its entries into a table of its own, and publishes that table
// through the VisualTexture once every file has loaded.
struct TexlistDesc_t {
  /* 0x00 */ be_u32 count;
  /* 0x04 */ be_u32 entries;
};
static_assert(sizeof(TexlistDesc_t) == 0x08);

// One texture of the list. The name is a bare basename, joined to the request's
// directory as '%s%s.dds'. The engine writes the loaded texture object into
// the last word, and Visual__SelectRenderTarget hands that to the prim.
struct TexlistEntry_t {
  /* 0x00 */ char name[0x14];
  /* 0x14 */ be_f32 scale;
  /* 0x18 */ be_u32 texture;
};
static_assert(sizeof(TexlistEntry_t) == 0x1C);

constexpr u32 kVisualTextureVtable = 0x8206A9EC;
constexpr u32 kLoadHandleVtable = 0x8206A9B0; // LH_Texlist
// VisualTexture__PollLoadState's ready state, and the only one that means the
// table at +8 has been published.
constexpr u32 kTexlistReady = 2;

constexpr u32 kDirBytes = 0x40;

// One HostHeap block holds the lot: the engine keeps the directory string and
// descriptor for as long as the request is alive, so nothing here is scratch.
struct TexlistRequest_t {
  /* 0x00 */ VisualTexture_t holder;
  /* 0x20 */ TexlistDesc_t desc;
  /* 0x28 */ TexlistEntry_t entries[Texlist::kMaxTextures];
  /* 0x28 + entries */ char dir[kDirBytes];
};
static_assert(offsetof(TexlistRequest_t, holder) == 0x00);
static_assert(offsetof(TexlistRequest_t, desc) == 0x20);
static_assert(offsetof(TexlistRequest_t, entries) == 0x28);
static_assert(offsetof(TexlistRequest_t, dir) ==
              0x28 + Texlist::kMaxTextures * 0x1C);

// The table the engine publishes through the holder has the descriptor's shape,
// but it and its entries are the engine's own copies rather than the ones
// handed to it.
const TexlistDesc_t *PublishedTable(u32 block) {
  const auto *holder = mem::try_at<const VisualTexture_t>(
      block + offsetof(TexlistRequest_t, holder));
  return holder ? mem::try_at<const TexlistDesc_t>(u32(holder->table)) : nullptr;
}

} // namespace

bool Texlist::Request() {
  const u32 block =
      gpu::HostHeap::Get().AllocGuest(sizeof(TexlistRequest_t), 16);
  if (!block) {
    REXBD_WARN("no HostHeap space for the {} texlist", dir_);
    return false;
  }
  auto *request = mem::try_at<TexlistRequest_t>(block);
  if (!request) {
    REXBD_WARN("texlist block 0x{:08X} did not resolve", block);
    gpu::HostHeap::Get().FreeGuest(block);
    return false;
  }
  std::memset(request, 0, sizeof(*request));

  request->holder.vtable = kVisualTextureVtable;
  request->holder.handleVtable = kLoadHandleVtable;

  for (u32 i = 0; i < count_; ++i)
    std::snprintf(request->entries[i].name, sizeof(request->entries->name),
                  "%s", names_[i]);

  request->desc.count = count_;
  request->desc.entries = block + offsetof(TexlistRequest_t, entries);

  std::snprintf(request->dir, sizeof(request->dir), "%s", dir_);

  block_.store(block, std::memory_order_relaxed);
  TexlistRequest(block + offsetof(TexlistRequest_t, holder),
                 block + offsetof(TexlistRequest_t, dir),
                 block + offsetof(TexlistRequest_t, desc));
  REXBD_DEBUG("requested {} textures under {} at 0x{:08X}", count_, dir_, block);
  return true;
}

// The load reporting done says the request finished, not that it found
// anything: a VisualTexture whose LoadTexture fell through publishes a null
// table, and Visual__SelectRenderTarget dereferences that table without
// checking it. Walk through to the texture objects once rather than trust the
// state word.
bool Texlist::Resolved() const {
  const auto *table = PublishedTable(block_.load(std::memory_order_relaxed));
  if (!table)
    return false;
  const u32 entries = table->entries;
  if (u32(table->count) < count_ || !entries)
    return false;
  for (u32 i = 0; i < count_; ++i) {
    const auto *entry = mem::try_at<const TexlistEntry_t>(
        entries + i * u32(sizeof(TexlistEntry_t)));
    if (!entry || !entry->texture)
      return false;
  }
  return true;
}

bool Texlist::Poll() {
  switch (state_) {
  case State::kIdle:
    if (count_)
      state_ = Request() ? State::kLoading : State::kFailed;
    break;
  case State::kLoading: {
    const u32 holder = block_.load(std::memory_order_relaxed) +
                       offsetof(TexlistRequest_t, holder);
    if (TexlistPoll(holder) != kTexlistReady)
      break;
    if (Resolved()) {
      ready_.store(true, std::memory_order_release);
      state_ = State::kReady;
    } else {
      REXBD_WARN("{}{}.dds did not load", dir_, names_[0]);
      state_ = State::kFailed;
    }
    break;
  }
  case State::kReady:
  case State::kFailed:
    break;
  }
  return state_ == State::kReady;
}

u32 Texlist::Texture(u32 slot) const {
  if (!Ready() || slot >= count_)
    return 0;
  const auto *table = PublishedTable(block_.load(std::memory_order_relaxed));
  const u32 entries = table ? u32(table->entries) : 0;
  if (!entries)
    return 0;
  const auto *entry = mem::try_at<const TexlistEntry_t>(
      entries + slot * u32(sizeof(TexlistEntry_t)));
  return entry ? u32(entry->texture) : 0;
}

void Texlist::Select(u32 slot) const {
  if (Ready() && slot < count_)
    PrimSelectTexture(slot, block_.load(std::memory_order_relaxed) +
                              offsetof(TexlistRequest_t, holder));
}

} // namespace bd::engine
