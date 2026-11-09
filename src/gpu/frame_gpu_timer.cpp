/**
 * @file    gpu/frame_gpu_timer.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/frame_gpu_timer.h"

#include <memory>
#include <vector>

#include <plume_render_interface.h>
#if defined(REBLUE_D3D12)
#include <plume_d3d12.h>
#else
#include <plume_vulkan.h>
#endif

#include "core/frame_meter.h"
#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/device.h"

namespace bd::gpu {

namespace {

constexpr u32 kRing = 4;
constexpr u32 kQueries = 2;

struct Pair {
  std::unique_ptr<plume::RenderQueryPool> pool;
  u32 fenceSlot = 0;
  bool pending = false;
};

Pair g_ring[kRing];
u32 g_next = 0;
i32 g_active = -1;
bool g_checked = false;
bool g_supported = false;
[[maybe_unused]] u64 g_validMask = ~0ull;

void Disable() {
  g_supported = false;
  FrameMeterGpuUnavailable();
  for (Pair &p : g_ring)
    p.pending = false;
  g_active = -1;
}

// MoltenVK reports queryPools support but rejects the timestamp pools.
bool SupportsTimestamps(plume::RenderDevice *device) {
  if (g_mvk || !device->getCapabilities().queryPools)
    return false;
#if defined(REBLUE_D3D12)
  return true;
#else
  auto *vk = static_cast<plume::VulkanDevice *>(device);
  if (vk->physicalDeviceProperties.limits.timestampPeriod <= 0.0f)
    return false;
  u32 count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vk->physicalDevice, &count,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> props(count);
  vkGetPhysicalDeviceQueueFamilyProperties(vk->physicalDevice, &count,
                                           props.data());
  for (const VkQueueFamilyProperties &p : props) {
    if (!(p.queueFlags & VK_QUEUE_GRAPHICS_BIT))
      continue;
    if (p.timestampValidBits == 0)
      return false;
    g_validMask =
        p.timestampValidBits >= 64 ? ~0ull : (1ull << p.timestampValidBits) - 1;
    return true;
  }
  return false;
#endif
}

// Reads the pair as nanoseconds. D3D12's queryResults memcpy's the readback
// buffer without checking the map, so probe the same buffer first.
bool ReadElapsedNs(plume::RenderQueryPool *pool, f64 &elapsed_ns) {
#if defined(REBLUE_D3D12)
  auto *d3d_pool = static_cast<plume::D3D12QueryPool *>(pool);
  auto *readback = d3d_pool->readbackBuffer.get();
  if (!readback || !readback->map())
    return false;
  readback->unmap();
  pool->queryResults();
  const u64 *r = pool->getResults();
  elapsed_ns = r[1] > r[0] ? static_cast<f64>(r[1] - r[0]) : 0.0;
  return true;
#else
  auto *vk_pool = static_cast<plume::VulkanQueryPool *>(pool);
  u64 r[kQueries] = {};
  const VkResult res = vkGetQueryPoolResults(
      vk_pool->device->vk, vk_pool->vk, 0, kQueries, sizeof(r), r, sizeof(u64),
      VK_QUERY_RESULT_64_BIT);
  if (res != VK_SUCCESS)
    return false;
  const f64 period =
      vk_pool->device->physicalDeviceProperties.limits.timestampPeriod;
  elapsed_ns = static_cast<f64>((r[1] - r[0]) & g_validMask) * period;
  return true;
#endif
}

} // namespace

void GpuTimerBegin(plume::RenderDevice *device, plume::RenderCommandList *cmd) {
  if (!FrameMeterOn() || g_active >= 0 || !device || !cmd)
    return;
  if (!g_checked) {
    g_checked = true;
    g_supported = SupportsTimestamps(device);
    if (!g_supported)
      FrameMeterGpuUnavailable();
  }
  if (!g_supported)
    return;
  Pair &p = g_ring[g_next];
  if (p.pending)
    return;
  if (!p.pool) {
    p.pool = device->createQueryPool(kQueries);
    if (!p.pool) {
      Disable();
      return;
    }
  }
  cmd->resetQueryPool(p.pool.get(), 0, kQueries);
  cmd->writeTimestamp(p.pool.get(), 0);
  g_active = static_cast<i32>(g_next);
}

void GpuTimerEnd(plume::RenderCommandList *cmd, u32 fenceSlot) {
  if (g_active < 0 || !cmd)
    return;
  Pair &p = g_ring[g_active];
  cmd->writeTimestamp(p.pool.get(), 1);
  p.fenceSlot = fenceSlot;
  p.pending = true;
  g_next = (static_cast<u32>(g_active) + 1) % kRing;
  g_active = -1;
}

void GpuTimerCollect(u32 fenceSlot) {
  if (!g_supported)
    return;
  for (Pair &p : g_ring) {
    if (!p.pending || p.fenceSlot != fenceSlot)
      continue;
    p.pending = false;
    f64 ns = 0.0;
    if (!ReadElapsedNs(p.pool.get(), ns)) {
      REXBD_WARN("gpu timer: timestamp readback failed, GPU time off");
      CheckDeviceRemoved("gpu timer readback");
      Disable();
      return;
    }
    if (ns > 0.0)
      FrameMeterGpuTime(ns * 1e-6);
  }
}

} // namespace bd::gpu
