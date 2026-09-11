/**
 * @file    gpu/gpu_timing.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/gpu_timing.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <plume_render_interface.h>
#if defined(REBLUE_D3D12)
#include <plume_d3d12.h>
#else
#include <plume_vulkan.h>
#endif

#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/device.h"
#include "gpu/frame_stats.h"

namespace bd::gpu {

namespace {

constexpr u32 kQueryCount = 512;

enum class Cat : u8 { Draw, Inter, Resolve };

struct SlotTiming {
  std::unique_ptr<plume::RenderQueryPool> pool;
  // journal[i] = category of the segment query i CLOSES ([0] = frame start,
  // unused). Queries >= journal.size() are padding.
  std::vector<Cat> journal;
  u32 used = 0;
  bool pending = false;
};

SlotTiming g_slots[kNumFrames];
u32 g_active_slot = 0;
Cat g_cat = Cat::Inter;
// MoltenVK reports queryPools support but rejects the timestamp pools, so it
// never starts supported rather than being switched off on the first frame.
bool g_supported = !g_mvk;
bool g_open = false;

void WriteMark(SlotTiming &st, plume::RenderCommandList *cmd, Cat closing) {
  if (st.used >= kQueryCount)
    return; // saturated: tail lumps into last seg
  cmd->writeTimestamp(st.pool.get(), st.used++);
  st.journal.push_back(closing);
}

void SetSegment(plume::RenderCommandList *cmd, Cat cat) {
  if (!g_supported || !g_open || cat == g_cat)
    return;
  WriteMark(g_slots[g_active_slot], cmd, g_cat);
  g_cat = cat;
}

// Reads the first n timestamps in nanoseconds. Only the written range is
// read: FrameEnd leaves the tail of the pool reset but unwritten, and on
// Vulkan vkGetQueryPoolResults reports VK_NOT_READY for the whole call if any
// query in the range is unavailable, so plume's whole-pool queryResults can
// never succeed there.
//
// D3D12QueryPool::queryResults memcpy's the whole readback buffer from map()
// without checking it, so a failed ID3D12Resource::Map (device removed, or an
// allocation failure) turns into a memcpy from null inside vendored plume.
// Probe the same buffer here first.
bool ReadTimestamps(plume::RenderQueryPool *pool, size_t n, u64 *out_ns) {
#if defined(REBLUE_D3D12)
  auto *d3d_pool = static_cast<plume::D3D12QueryPool *>(pool);
  auto *readback = d3d_pool->readbackBuffer.get();
  if (!readback)
    return false;
  if (!readback->map())
    return false;
  readback->unmap();
  pool->queryResults();
  const u64 *r = pool->getResults();
  std::copy(r, r + n, out_ns);
  return true;
#else
  auto *vk_pool = static_cast<plume::VulkanQueryPool *>(pool);
  const VkResult res = vkGetQueryPoolResults(
      vk_pool->device->vk, vk_pool->vk, 0, static_cast<u32>(n),
      sizeof(u64) * n, out_ns, sizeof(u64), VK_QUERY_RESULT_64_BIT);
  if (res != VK_SUCCESS) {
    BD_ERROR("gpu timing: vkGetQueryPoolResults({} queries) returned {}", n,
             static_cast<i32>(res));
    return false;
  }
  const f64 period =
      vk_pool->device->physicalDeviceProperties.limits.timestampPeriod;
  for (size_t i = 0; i < n; ++i)
    out_ns[i] = static_cast<u64>(static_cast<f64>(out_ns[i]) * period);
  return true;
#endif
}

} // namespace

void FrameBegin(plume::RenderDevice *device, plume::RenderCommandList *cmd,
                u32 slot) {
  if (!g_supported || !device || !cmd)
    return;
  if (!device->getCapabilities().queryPools) {
    g_supported = false;
    return;
  }
  auto &st = g_slots[slot];
  if (!st.pool) {
    st.pool = device->createQueryPool(kQueryCount);
    if (!st.pool) {
      g_supported = false;
      return;
    }
  }
  st.pending = false;
  st.used = 0;
  st.journal.clear();
  st.journal.push_back(Cat::Inter); // slot 0 = frame start marker
  cmd->resetQueryPool(st.pool.get(), 0, kQueryCount);
  cmd->writeTimestamp(st.pool.get(), st.used++);
  g_active_slot = slot;
  g_cat = Cat::Inter;
  g_open = true;
}

void MarkDraw(plume::RenderCommandList *cmd) { SetSegment(cmd, Cat::Draw); }

void MarkInter(plume::RenderCommandList *cmd) { SetSegment(cmd, Cat::Inter); }

void MarkResolve(plume::RenderCommandList *cmd) {
  SetSegment(cmd, Cat::Resolve);
}

void FrameEnd(plume::RenderCommandList *cmd) {
  if (!g_supported || !g_open)
    return;
  auto &st = g_slots[g_active_slot];
  WriteMark(st, cmd, g_cat);
  // No tail padding: on D3D12 writeTimestamp resolves ONE query per call, so
  // padding to kQueryCount cost 512 EndQuery + ResolveQueryData every frame to
  // fill slots nothing reads. D3D12's queryResults still copies and rescales
  // the whole pool, so its unwritten tail carries stale values that get
  // rescaled again each frame and saturate. Harmless, since ReadTimestamps
  // only reads below journal.size().
  st.pending = st.journal.size() > 1;
  g_open = false;
}

void CollectGPUTimings(u32 slot) {
  if (!g_supported)
    return;
  auto &st = g_slots[slot];
  if (!st.pending || !st.pool)
    return;
  st.pending = false;
  const size_t n = st.journal.size();
  if (n < 2)
    return;
  u64 r[kQueryCount]; // nanoseconds
  if (!ReadTimestamps(st.pool.get(), n, r)) {
    BD_ERROR("gpu timing: query readback failed (slot {}), timing disabled",
             slot);
    CheckDeviceRemoved("gpu timing readback");
    g_supported = false;
    return;
  }
  if (r[n - 1] <= r[0])
    return;
  u64 draw_ns = 0, resolve_ns = 0;
  for (size_t i = 1; i < n; ++i) {
    if (r[i] <= r[i - 1])
      continue;
    if (st.journal[i] == Cat::Draw) {
      draw_ns += r[i] - r[i - 1];
    } else if (st.journal[i] == Cat::Resolve) {
      resolve_ns += r[i] - r[i - 1];
    }
  }
  RecordGPUTime((r[n - 1] - r[0]) * 1e-6, draw_ns * 1e-6, resolve_ns * 1e-6);
}

} // namespace bd::gpu
