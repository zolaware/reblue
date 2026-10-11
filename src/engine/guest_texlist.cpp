/**
 * @file    engine/guest_texlist.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/guest_texlist.h"

#include <cstdio>
#include <cstring>

#include <rex/hook.h>

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

// Reached through engine pointers rather than a struct, since the table and its
// entries are the engine's own copies rather than the ones handed to it.
constexpr u32 kVisualTexture_Table = 0x08;
constexpr u32 kTable_Entries = 0x04;
constexpr u32 kEntry_Texture = 0x18;

// One HostHeap block holds the lot: the engine keeps the directory string and
// descriptor for as long as the request is alive, so nothing here is scratch.
constexpr u32 kOffHolder = 0x00;
constexpr u32 kOffDesc = 0x20;
constexpr u32 kOffEntries = 0x28;
constexpr u32 kOffDir =
    kOffEntries + Texlist::kMaxTextures * u32(sizeof(TexlistEntry_t));
constexpr u32 kDirBytes = 0x40;
constexpr u32 kBlockBytes = kOffDir + kDirBytes;

} // namespace

bool Texlist::Request() {
  const u32 block = gpu::HostHeap::Get().AllocGuest(kBlockBytes, 16);
  if (!block) {
    BD_WARN("no HostHeap space for the {} texlist", dir_);
    return false;
  }
  auto *bytes = mem::try_at<u8>(block);
  if (!bytes) {
    BD_WARN("texlist block 0x{:08X} did not resolve", block);
    gpu::HostHeap::Get().FreeGuest(block);
    return false;
  }
  std::memset(bytes, 0, kBlockBytes);

  auto *holder = mem::at<VisualTexture_t>(block + kOffHolder);
  holder->vtable = kVisualTextureVtable;
  holder->handleVtable = kLoadHandleVtable;

  auto *entries = mem::at<TexlistEntry_t>(block + kOffEntries);
  for (u32 i = 0; i < count_; ++i)
    std::snprintf(entries[i].name, sizeof(entries->name), "%s", names_[i]);

  auto *desc = mem::at<TexlistDesc_t>(block + kOffDesc);
  desc->count = count_;
  desc->entries = block + kOffEntries;

  std::snprintf(reinterpret_cast<char *>(bytes + kOffDir), kDirBytes, "%s",
                dir_);

  block_.store(block, std::memory_order_relaxed);
  TexlistRequest(block + kOffHolder, block + kOffDir, block + kOffDesc);
  BD_DEBUG("requested {} textures under {} at 0x{:08X}", count_, dir_, block);
  return true;
}

// The load reporting done says the request finished, not that it found
// anything: a VisualTexture whose LoadTexture fell through publishes a null
// table, and Visual__SelectRenderTarget dereferences that table without
// checking it. Walk through to the texture objects once rather than trust the
// state word.
bool Texlist::Resolved() const {
  const u32 holder = block_.load(std::memory_order_relaxed) + kOffHolder;
  const u32 table = mem::try_field<u32>(holder, kVisualTexture_Table);
  if (!table)
    return false;
  const u32 count = mem::try_load<u32>(table);
  const u32 entries = mem::try_load<u32>(table + kTable_Entries);
  if (count < count_ || !entries)
    return false;
  for (u32 i = 0; i < count_; ++i) {
    const u32 entry = entries + i * u32(sizeof(TexlistEntry_t));
    if (!mem::try_field<u32>(entry, kEntry_Texture))
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
    const u32 holder = block_.load(std::memory_order_relaxed) + kOffHolder;
    if (TexlistPoll(holder) != kTexlistReady)
      break;
    if (Resolved()) {
      ready_.store(true, std::memory_order_release);
      state_ = State::kReady;
    } else {
      BD_WARN("{}{}.dds did not load", dir_, names_[0]);
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
  const u32 holder = block_.load(std::memory_order_relaxed) + kOffHolder;
  const u32 table = mem::try_field<u32>(holder, kVisualTexture_Table);
  const u32 entries = table ? mem::try_load<u32>(table + kTable_Entries) : 0;
  if (!entries)
    return 0;
  return mem::try_field<u32>(entries + slot * u32(sizeof(TexlistEntry_t)),
                             kEntry_Texture);
}

void Texlist::Select(u32 slot) const {
  if (Ready() && slot < count_)
    PrimSelectTexture(slot, block_.load(std::memory_order_relaxed) + kOffHolder);
}

} // namespace bd::engine
