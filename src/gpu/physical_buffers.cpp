/**
 * @file    gpu/physical_buffers.cpp
 * @brief   Real geometry bypasses D3DDevice_CreateVertexBuffer. The
 *          XGOffsetResourceAddress hook registers the engine's physical vertex
 *          and index buffers, so the registry keys host GuestBuffers by struct
 *          VA for SetStreamSource and SetIndices to resolve.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/physical_buffers.h"

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>

#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>
#include <rex/logging/log_category.h>
#include <rex/kernel/title_runtime.h>
#include <rex/types.h>
#include <rex/ppc/midasm.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/device.h"

// Block mirroring: one host RenderBuffer per model load (XPhysical) block with
// each mesh bound as an offset view, collapsing a model's thousands of tiny
// VB/IB allocations into one buffer. Physical buffers outside any block
// (UI/effects) keep the per-buffer path.

namespace {

struct PhysicalBufferRecord {
  std::unique_ptr<bd::gpu::GuestBuffer> buffer;
};

std::mutex g_physicalBuffersMutex;
std::unordered_map<u32, PhysicalBufferRecord> g_physicalBuffers;
// Bridges struct VA -> GuestBuffer owned by g_physicalBuffers. SetIndices and
// SetStreamSource get the struct VA, never a HostResourceHeap::Alloc'd one, so
// FromGuest misses. Struct address reuse for a different base_va is unhandled.
std::unordered_map<u32, bd::gpu::GuestBuffer *> g_physicalBufferStructs;

// Parked rather than freed inline: a prior frame's command list may still
// reference them. DrainBufferGraveyard frees them post-fence.
std::vector<std::unique_ptr<plume::RenderBuffer>>
    g_physicalBufferGraveyard[bd::gpu::kNumFrames];

// One entry per live XPhysical geometry block, living exactly as long as the
// model. Each owns one host RenderBuffer plus the per-mesh views into it, keyed
// by byte offset from the block base. Ordered so a draw-time base_va resolves
// by interval lookup. Guarded by g_physicalBuffersMutex.
struct PhysicalBlock {
  u32 base = 0;
  u32 size = 0;
  std::unique_ptr<plume::RenderBuffer>
      buffer; // lazy, VERTEX|INDEX, GeometryHeapType
  std::unordered_map<u32, std::unique_ptr<bd::gpu::GuestBuffer>> meshes;
};
std::map<u32 /*block base*/, PhysicalBlock> g_physicalBlocks;

// Live host geometry buffers owned by g_physicalBuffers, both guarded by
// g_physicalBuffersMutex. Tracked so UPLOAD heap growth is visible.
u32 g_physLiveCount = 0;
u64 g_physUploadBytes = 0;
// A runaway must show in the log as a rising plateau, not just as a later
// out-of-memory crash. Never decreases, so steady-state churn stays quiet.
constexpr u64 kPhysUploadWatermarkStep = 128ull << 20;
u64 g_physUploadWatermark = kPhysUploadWatermarkStep;

// Caller holds g_physicalBuffersMutex. Logs once per 128 MiB past the peak.
void NotePhysicalGrowth() {
  if (g_physUploadBytes <= g_physUploadWatermark)
    return;
  g_physUploadWatermark = ((g_physUploadBytes / kPhysUploadWatermarkStep) + 1) *
                          kPhysUploadWatermarkStep;
  REXBD_WARN("Physical geometry footprint high-water: {} MiB across {} buffers",
          g_physUploadBytes >> 20, g_physLiveCount);
}

// INDEX16 must swap per index: a bswap32 over INDEX16 swaps adjacent index
// pairs at every 4-byte boundary, giving out-of-range indices. VBs bswap32 and
// undo the 16-bit pair reorder shader-side (swapFloats swap mask).
std::unique_ptr<plume::RenderBuffer>
CreatePhysicalPlumeBuffer(plume::RenderDevice *device, u32 base_va, u32 size,
                          bool is_index, u32 element_size) {
  const plume::RenderHeapType heap =
      bd::gpu::GeometryHeapType(device, bd::gpu::GeometryClass::Static);
  plume::RenderBufferDesc desc =
      is_index ? plume::RenderBufferDesc::IndexBuffer(size, heap)
               : plume::RenderBufferDesc::VertexBuffer(size, heap);
  // plume's createBuffer is false-safe (non-null wrapper, null d3d) and the
  // map() below would deref that. Callers handle a null return: serve-stale on
  // refresh, skip on first-create.
  auto plume_buffer = bd::gpu::CreateHostBuffer(
      device, desc, is_index ? "physical-ib" : "physical-vb");
  if (!plume_buffer) {
    REXBD_ERROR(
        "CreatePhysicalPlumeBuffer: failed (size={}, {}), live={} uploadMiB={}",
        size, is_index ? "index" : "vertex", g_physLiveCount,
        g_physUploadBytes >> 20);
    return nullptr;
  }
  const auto *src8 = bd::mem::at<const u8>(base_va);
  if (!src8) {
    REXBD_WARN("CreatePhysicalPlumeBuffer: base_va 0x{:08X} did not translate",
            base_va);
    return nullptr;
  }
  void *mapped = plume_buffer->map();
  if (!mapped) {
    REXBD_ERROR("CreatePhysicalPlumeBuffer: map failed (size={})", size);
    return nullptr;
  }
  if (element_size == 2) {
    rex::memory::copy_and_swap<u16>(static_cast<u16 *>(mapped),
                                    reinterpret_cast<const u16 *>(src8),
                                    size / 2);
  } else {
    rex::memory::copy_and_swap<u32>(static_cast<u32 *>(mapped),
                                    reinterpret_cast<const u32 *>(src8),
                                    size / 4);
  }
  plume_buffer->unmap();
  return plume_buffer;
}

bd::gpu::GuestBuffer *RegisterPhysicalBufferOnce(u32 base_va, u32 size,
                                                 bd::gpu::ResourceType rtype,
                                                 u32 element_size = 4) {
  if (!base_va || !size)
    return nullptr;
  std::lock_guard lock(g_physicalBuffersMutex);
  auto *device = bd::gpu::Video::HostDevice();
  if (!device) {
    static u32 s_warned = 0; // guarded by g_physicalBuffersMutex
    if (s_warned++ < 8)
      REXBD_ERROR("RegisterPhysicalBufferOnce: HostDevice null (base_va {:#010x})",
               base_va);
    return nullptr;
  }
  const bool is_index = (rtype == bd::gpu::ResourceType::IndexBuffer);

  auto it = g_physicalBuffers.find(base_va);
  if (it != g_physicalBuffers.end()) {
    auto *cached = it->second.buffer.get();
    if (cached->dataSize == size && cached->type == rtype) {
      return cached; // the same mesh is still at this address
    }

    auto fresh = CreatePhysicalPlumeBuffer(device, base_va, size, is_index,
                                           element_size);
    if (!fresh) {
      REXBD_WARN("RegisterPhysicalBufferOnce: refresh failed for base_va {:#010x} "
              "(size {}), serving stale buffer",
              base_va, size);
      return cached; // keep stale rather than crash
    }
    g_physicalBufferGraveyard[bd::gpu::Video::RetireSlot("physical buffer")]
        .push_back(std::move(cached->buffer));
    g_physUploadBytes += size;
    g_physUploadBytes -= cached->dataSize;
    cached->buffer = std::move(fresh);
    cached->dataSize = size;
    cached->type = rtype;
    cached->guestMirrorVa = base_va;
    cached->format = plume::RenderFormat::UNKNOWN; // caller re-pins IB format
    return cached;
  }

  auto plume_buffer =
      CreatePhysicalPlumeBuffer(device, base_va, size, is_index, element_size);
  if (!plume_buffer)
    return nullptr;

  auto guest_buffer = std::make_unique<bd::gpu::GuestBuffer>(rtype);
  guest_buffer->buffer = std::move(plume_buffer);
  guest_buffer->guestMirrorVa = base_va;
  guest_buffer->dataSize = size;
  guest_buffer->format = plume::RenderFormat::UNKNOWN;

  bd::gpu::GuestBuffer *raw = guest_buffer.get();
  g_physicalBuffers[base_va] = PhysicalBufferRecord{std::move(guest_buffer)};
  ++g_physLiveCount;
  g_physUploadBytes += size;
  NotePhysicalGrowth();
  return raw;
}

// Overwriting a prior entry is intended: the engine reuses a struct slot when
// meshes are reloaded.
void RememberBufferStruct(u32 struct_va, bd::gpu::GuestBuffer *buf) {
  if (!struct_va || !buf)
    return;
  std::lock_guard lock(g_physicalBuffersMutex);
  g_physicalBufferStructs[struct_va] = buf;
}

// --- Block helpers. All require the caller to hold g_physicalBuffersMutex. ---

// The block whose [base, base+size) contains base_va, or null.
PhysicalBlock *FindBlockForBaseLocked(u32 base_va) {
  if (g_physicalBlocks.empty())
    return nullptr;
  // First block with base > base_va.
  auto it = g_physicalBlocks.upper_bound(base_va);
  if (it == g_physicalBlocks.begin())
    return nullptr;
  --it; // greatest base <= base_va
  PhysicalBlock &a = it->second;
  if (base_va < a.base || base_va >= a.base + a.size)
    return nullptr;
  return &a;
}

// bswap16 per index for INDEX16, else bswap32 per dword. The source is the
// pristine guest big-endian block, so re-running rewrites the same bytes
// rather than swapping in place. The block buffer must already exist.
void SwapBlockRegionLocked(PhysicalBlock &a, u32 offset, u32 size,
                           u32 element_size) {
  const auto *src8 = bd::mem::at<const u8>(a.base + offset);
  if (!src8) {
    REXBD_WARN("Block swap: base {:#010x}+{:#x} did not translate", a.base,
            offset);
    return;
  }
  void *mapped = a.buffer->map();
  if (!mapped) {
    REXBD_ERROR("Block swap: map failed (block {:#010x})", a.base);
    return;
  }
  auto *dst8 = static_cast<u8 *>(mapped) + offset;
  if (element_size == 2) {
    rex::memory::copy_and_swap<u16>(reinterpret_cast<u16 *>(dst8),
                                    reinterpret_cast<const u16 *>(src8),
                                    size / 2);
  } else {
    rex::memory::copy_and_swap<u32>(reinterpret_cast<u32 *>(dst8),
                                    reinterpret_cast<const u32 *>(src8),
                                    size / 4);
  }
  a.buffer->unmap();
}

// Creates the block host buffer on first use, VERTEX|INDEX so one resource
// serves both binds. Null on any failure, so the caller falls back to the
// per-buffer path. A cached mesh is already byte-swapped.
bd::gpu::GuestBuffer *GetOrCreateBlockMeshLocked(PhysicalBlock &a, u32 base_va,
                                                 u32 size,
                                                 bd::gpu::ResourceType rtype,
                                                 u32 element_size,
                                                 plume::RenderFormat format) {
  if (base_va < a.base)
    return nullptr;
  const u32 offset = base_va - a.base;
  if (offset > a.size || size > a.size - offset) {
    REXBD_WARN("Block mesh [{:#010x}+{:#x}] outside block [{:#010x}+{:#x}]",
            base_va, size, a.base, a.size);
    return nullptr;
  }
  if (auto it = a.meshes.find(offset); it != a.meshes.end()) {
    bd::gpu::GuestBuffer *gb = it->second.get();
    if (format != plume::RenderFormat::UNKNOWN)
      gb->format = format;
    return gb;
  }
  auto *device = bd::gpu::Video::HostDevice();
  if (!device)
    return nullptr;
  if (!a.buffer) {
    plume::RenderBufferDesc desc;
    desc.size = a.size;
    desc.heapType =
        bd::gpu::GeometryHeapType(device, bd::gpu::GeometryClass::Static);
    desc.flags =
        plume::RenderBufferFlag::VERTEX | plume::RenderBufferFlag::INDEX;
    a.buffer = bd::gpu::CreateHostBuffer(device, desc, "physical-block");
    if (!a.buffer) {
      REXBD_ERROR("Block buffer create failed (block {:#010x}, {} KiB)", a.base,
               a.size >> 10);
      return nullptr;
    }
    g_physUploadBytes += a.size;
    ++g_physLiveCount;
    NotePhysicalGrowth();
  }
  SwapBlockRegionLocked(a, offset, size, element_size);

  auto gb = std::make_unique<bd::gpu::GuestBuffer>(rtype);
  gb->blockBuffer = a.buffer.get();
  gb->blockOffset = offset;
  // base_va so a read-only Lock returns base_va+off and reads the pristine
  // guest bytes. A mirror lock wants guest data, not the swapped host copy.
  gb->guestMirrorVa = base_va;
  gb->ownsMirror = false;
  gb->dataSize = size;
  gb->format = format;
  bd::gpu::GuestBuffer *raw = gb.get();
  a.meshes[offset] = std::move(gb);
  return raw;
}

// Also registers the view in the struct VA index, so draw and lock resolution
// are unchanged. Null when base_va is in no known block.
bd::gpu::GuestBuffer *TryRegisterBlockMesh(u32 struct_va, u32 base_va, u32 size,
                                           bd::gpu::ResourceType rtype,
                                           u32 element_size,
                                           plume::RenderFormat format) {
  if (!base_va || !size)
    return nullptr;
  std::lock_guard lock(g_physicalBuffersMutex);
  PhysicalBlock *a = FindBlockForBaseLocked(base_va);
  if (!a)
    return nullptr;
  bd::gpu::GuestBuffer *view = GetOrCreateBlockMeshLocked(
      *a, base_va, size, rtype, element_size, format);
  if (!view)
    return nullptr;
  if (struct_va)
    g_physicalBufferStructs[struct_va] = view;
  return view;
}

// Every physical VB/IB creation site takes this route: block fast path first,
// then the per-buffer registry plus the struct VA bridge.
bd::gpu::GuestBuffer *RegisterPhysicalGeometry(u32 struct_va, u32 base_va,
                                               u32 size,
                                               bd::gpu::ResourceType rtype,
                                               u32 element_size,
                                               plume::RenderFormat format) {
  if (auto *view = TryRegisterBlockMesh(struct_va, base_va, size, rtype,
                                        element_size, format)) {
    return view;
  }
  auto *buf = RegisterPhysicalBufferOnce(base_va, size, rtype, element_size);
  if (buf && format != plume::RenderFormat::UNKNOWN)
    buf->format = format;
  RememberBufferStruct(struct_va, buf);
  return buf;
}

// The engine frees a model's whole VB/IB block at once, so the host mirrors
// must go with it or the UPLOAD heap grows without bound. Serialized against
// the render thread's draw-time lookups by the BD DrawStart/DrawEnd handshake.
// The guest block memory is the engine's, freed by the following
// XPhysicalFree.
void EvictPhysicalBuffersInBlock(u32 block_base, u32 block_size) {
  if (!block_base || !block_size)
    return;
  const u32 block_end = block_base + block_size;
  // Scrubbed after the registry lock drops, since ScrubBufferBindings takes
  // s.mutex.
  std::vector<plume::RenderBuffer *> scrub;
  {
    std::lock_guard lock(g_physicalBuffersMutex);
    const u32 slot = bd::gpu::Video::RetireSlot("geometry block");
    u32 evicted = 0;
    u64 freed_bytes = 0;
    for (auto it = g_physicalBuffers.begin(); it != g_physicalBuffers.end();) {
      if (it->first < block_base || it->first >= block_end) {
        ++it;
        continue;
      }
      bd::gpu::GuestBuffer *gb = it->second.buffer.get();
      // Struct VA bridges are keyed by a malloc'd VA outside the block, so
      // match by value not range. Drop them before the owning record dies.
      for (auto sit = g_physicalBufferStructs.begin();
           sit != g_physicalBufferStructs.end();) {
        if (sit->second == gb)
          sit = g_physicalBufferStructs.erase(sit);
        else
          ++sit;
      }
      freed_bytes += gb->dataSize;
      if (gb->buffer) {
        scrub.push_back(gb->buffer.get());
        g_physicalBufferGraveyard[slot].push_back(std::move(gb->buffer));
      }
      it = g_physicalBuffers.erase(it);
      ++evicted;
    }
    // Exact base: the destroy hook passes the same obj+0x18 the build hook
    // registered.
    u32 block_buffers = 0, block_meshes = 0;
    if (auto ait = g_physicalBlocks.find(block_base);
        ait != g_physicalBlocks.end()) {
      PhysicalBlock &block = ait->second;
      block_meshes = static_cast<u32>(block.meshes.size());
      if (block.buffer) {
        plume::RenderBuffer *block_buf = block.buffer.get();
        for (auto sit = g_physicalBufferStructs.begin();
             sit != g_physicalBufferStructs.end();) {
          if (sit->second && sit->second->blockBuffer == block_buf)
            sit = g_physicalBufferStructs.erase(sit);
          else
            ++sit;
        }
        freed_bytes += block.size;
        block_buffers = 1;
        scrub.push_back(block_buf);
        g_physicalBufferGraveyard[slot].push_back(std::move(block.buffer));
      }
      g_physicalBlocks.erase(ait);
    }

    const u32 freed_buffers = evicted + block_buffers;
    if (freed_buffers || block_meshes) {
      g_physLiveCount = g_physLiveCount >= freed_buffers
                            ? g_physLiveCount - freed_buffers
                            : 0;
      g_physUploadBytes = g_physUploadBytes >= freed_bytes
                              ? g_physUploadBytes - freed_bytes
                              : 0;
      REXBD_DEBUG("Physical block freed [{:#010x}+{:#x}]: {} per-mesh + {} block "
               "buffer(s)"
               " ({} mesh views, {} KiB), live={} uploadMiB={}",
               block_base, block_size, evicted, block_buffers, block_meshes,
               freed_bytes >> 10, g_physLiveCount, g_physUploadBytes >> 20);
    }
  } // release g_physicalBuffersMutex before taking s.mutex in the scrub
  for (auto *b : scrub)
    bd::gpu::Video::ScrubBufferBindings(b);
}

} // namespace

namespace bd::gpu {

// SetIndices / SetStreamSource pass the struct VA, never a HostResourceHeap-
// allocated one, so FromGuest misses.
bd::gpu::GuestBuffer *FindPhysicalBufferByStruct(u32 struct_va) {
  if (!struct_va)
    return nullptr;
  std::lock_guard lock(g_physicalBuffersMutex);
  auto it = g_physicalBufferStructs.find(struct_va);
  return (it != g_physicalBufferStructs.end()) ? it->second : nullptr;
}

// Rebuild a GuestBuffer from a fully initialized X360 D3DIndexBuffer /
// D3DVertexBuffer struct: asset-loaded meshes patched by an unhooked loader,
// which is most of the scene geometry. The header after XGOffsetResourceAddress:
//   struct[+0x18] = base_va, with | 3 in a vertex buffer
//   struct[+0x1C] = size, with | 0x10000002 in a vertex buffer
//   struct[+0x00] bit 31 = index format flag (1 = INDEX32, 0 = INDEX16)
bd::gpu::GuestBuffer *
AdoptPhysicalBuffer(u32 struct_va, bd::gpu::ResourceType rtype) {
  if (!struct_va)
    return nullptr;
  if (auto *existing = FindPhysicalBufferByStruct(struct_va))
    return existing;
  // Both structs share this layout, and only Common/FetchLo/FetchHi are read.
  const auto *raw = bd::mem::at<const D3DVertexBuffer>(struct_va);
  if (!raw)
    return nullptr;
  const u32 fetch_lo = raw->FetchLo;
  const u32 fetch_hi = raw->FetchHi;
  const u32 base_va = fetch_lo & 0xFFFFFFFCu;
  const u32 size_bytes = fetch_hi & 0x03FFFFFCu;
  if (!base_va || !size_bytes)
    return nullptr;
  // The IB bswap stride depends on the index format (bit 31 of dword[0] =
  // INDEX32), so read it first and swap per element instead of per dword.
  u32 element_size = 4;
  bool index32 = false;
  if (rtype == bd::gpu::ResourceType::IndexBuffer) {
    index32 = (raw->resource.Common & 0x80000000u) != 0;
    element_size = index32 ? 4u : 2u;
  }
  const plume::RenderFormat ib_format =
      (rtype == bd::gpu::ResourceType::IndexBuffer)
          ? (index32 ? plume::RenderFormat::R32_UINT
                     : plume::RenderFormat::R16_UINT)
          : plume::RenderFormat::UNKNOWN;
  return RegisterPhysicalGeometry(struct_va, base_va, size_bytes, rtype,
                                  element_size, ib_format);
}

GuestBuffer *ResolveGuestBufferVa(u32 va, ResourceType rtype) {
  if (!va)
    return nullptr;
  if (auto *buf = FindPhysicalBufferByStruct(va))
    return buf;
  return AdoptPhysicalBuffer(va, rtype);
}

void DrainBufferGraveyard(u32 slot) {
  std::vector<std::unique_ptr<plume::RenderBuffer>> dead;
  {
    std::lock_guard lock(g_physicalBuffersMutex);
    dead.swap(g_physicalBufferGraveyard[slot]);
  }
  // Outside the registry lock. The refresh path replaces the plume buffer
  // without touching the draw state views, and BeginCommandList force-dirties
  // every stream, so the next flush would re-bind through the freed object.
  for (auto &b : dead)
    bd::gpu::Video::ScrubBufferBindings(b.get());
  dead.clear();
}

} // namespace bd::gpu

// The engine hands every physical vertex and index buffer here once its data
// sits in the geometry block. The base is patched the way the original does, by
// the Common type bits and not through D3DResource_GetType, which the host
// answers with 0 for these structs. Nothing else calls this function.
REX_IMPORT(__imp__XGOffsetResourceAddress, OriginalXGOffsetResourceAddress,
           void(u32, u32));

namespace {

void XGOffsetResourceAddress_hook(u32 resource, u32 base) {
  auto *header = bd::mem::at<bd::gpu::D3DVertexBuffer>(resource);
  if (!header) {
    OriginalXGOffsetResourceAddress(resource, base);
    return;
  }
  const u32 type = u32(header->resource.Common) & 0xFu;
  const u32 fetch_lo = header->FetchLo;
  const u32 fetch_hi = header->FetchHi;
  if (type == u32(bd::gpu::D3DResourceType::kVertexBuffer)) {
    header->FetchLo = (((fetch_lo & ~3u) + base) & ~3u) | (fetch_lo & 3u);
    RegisterPhysicalGeometry(resource, base, fetch_hi & 0x03FFFFFCu,
                             bd::gpu::ResourceType::VertexBuffer,
                             /*element_size=*/4, plume::RenderFormat::UNKNOWN);
  } else if (type == u32(bd::gpu::D3DResourceType::kIndexBuffer)) {
    header->FetchLo = fetch_lo + base;
    const bool index32 = (u32(header->resource.Common) & 0x80000000u) != 0;
    RegisterPhysicalGeometry(resource, base, fetch_hi,
                             bd::gpu::ResourceType::IndexBuffer,
                             index32 ? 4u : 2u,
                             index32 ? plume::RenderFormat::R32_UINT
                                     : plume::RenderFormat::R16_UINT);
  } else {
    OriginalXGOffsetResourceAddress(resource, base);
  }
}

} // namespace

REX_HOOK(XGOffsetResourceAddress, XGOffsetResourceAddress_hook);

// Only the two fields read here are named.
struct BdSceneGraphObject {
  u8 _pad00[0x04];
  // HDB header[1] copy taken at build. Block size = (hdbHeader1 >> 1) &
  // 0x0FFF0000, the exact size passed to XPhysicalAllocEx.
  be_u32 hdbHeader1;
  u8 _pad08[0x18 - 0x08];
  // XPhysicalAllocEx geometry block base, 0 = model has no geometry block.
  be_u32 physicalBlockBase;
};
static_assert(offsetof(BdSceneGraphObject, hdbHeader1) == 0x04);
static_assert(offsetof(BdSceneGraphObject, physicalBlockBase) == 0x18);

// Evicting at bdSceneGraphDestroy's XPhysicalFree ties host UPLOAD buffer
// lifetime to the guest geometry it mirrors, so a reused block cannot serve
// stale geometry.
REX_MIDASM_HOOK(bdSceneGraphDestroyHook,
                (u32 block_base, const BdSceneGraphObject *obj)) {
  if (!block_base)
    return;
  if (!obj)
    return;
  const u32 block_size = (obj->hdbHeader1 >> 1) & 0x0FFF0000u;
  EvictPhysicalBuffersInBlock(block_base, block_size);
}
REX_MIDASM_AT(bdSceneGraphDestroyHook, 0x8227EC14, before, r3, r31);

// After obj+0x18 receives the block base and before the meshes are
// bump-allocated into it, so the per-mesh feeders resolve base_va to this
// block. Registers [base, size) only, the host buffer waits for the first mesh.
// Reads the same two obj fields the destroy hook frees by, so the two agree.
REX_MIDASM_HOOK(bdSceneGraphBuildHook, (const BdSceneGraphObject *obj)) {
  if (!obj)
    return;
  const u32 block_base = obj->physicalBlockBase;
  if (!block_base)
    return;
  const u32 block_size = (obj->hdbHeader1 >> 1) & 0x0FFF0000u; // == build alloc
  if (!block_size)
    return;
  std::lock_guard lock(g_physicalBuffersMutex);
  PhysicalBlock &a = g_physicalBlocks[block_base];
  a.base = block_base;
  a.size = block_size;
}
REX_MIDASM_AT(bdSceneGraphBuildHook, 0x8227E8A0, before, r31);
