/**
 * @file    gpu/frame_ring.cpp
 * @brief   The kNumFrames command list ring: opening a list on the recording
 *          slot, retiring resources against it, and advancing past its fence.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/frame.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include <plume_render_interface.h>
#if defined(REBLUE_D3D12)
#include <plume_d3d12.h>
#endif

#include "gpu/gpu_profiling.h"

#include "core/logging.h"
#include "gpu/constant_buffers.h"
#include "gpu/host_resource_heap.h"
#include "gpu/native_texture_mirror.h"
#include "gpu/physical_buffers.h"
#include "gpu/surface_registry.h"

namespace bd::gpu {

void BeginCommandList(VideoState &s) {
  if (s.command_list_open)
    return;
  if (s.shutting_down.load(std::memory_order_acquire))
    return;
  if (!s.pipeline_layout)
    return;
  const u32 cur = s.frame.load(std::memory_order_relaxed);
  s.command_list = s.command_lists[cur].get();
  s.draw_framebuffer_bound = false;
  s.last_resolved_dst = nullptr;
  s.command_list->begin();
  if (!s.null_texture_barriers_submitted) {
    plume::RenderTextureBarrier barriers[kNullTextureDescriptorCount];
    for (u32 i = 0; i < kNullTextureDescriptorCount; ++i) {
      barriers[i] = plume::RenderTextureBarrier(
          s.null_textures[i].get(), plume::RenderTextureLayout::SHADER_READ);
    }
    s.command_list->barriers(plume::RenderBarrierStage::NONE, barriers,
                             kNullTextureDescriptorCount);
    s.null_texture_barriers_submitted = true;
  }
  s.command_list->setGraphicsPipelineLayout(s.pipeline_layout.get());
  s.command_list->setGraphicsDescriptorSet(s.texture_descriptor_set.get(), 0);
  s.command_list->setGraphicsDescriptorSet(s.texture_descriptor_set.get(), 1);
  s.command_list->setGraphicsDescriptorSet(s.texture_descriptor_set.get(), 2);
  s.command_list->setGraphicsDescriptorSet(s.sampler_descriptor_set.get(), 3);
  s.command_list_open = true;
  s.dirtyStates.vertexStreamFirst = 0;
  s.dirtyStates.vertexStreamLast = 15;
  s.dirtyStates.indices = true;
  s.dirtyStates.pipelineState = true;
  s.dirtyStates.vertexShaderConstants = true;
  s.dirtyStates.pixelShaderConstants = true;
  s.current_pso = nullptr;
  InvalidateSharedBinding();
#if defined(REXGLUE_ENABLE_PROFILING) && defined(REBLUE_D3D12)
  SetGPUProfilerCommandList(
      static_cast<plume::D3D12CommandList *>(s.command_list)->d3d);
#endif
}

void Video::OpenCommandList() {
  auto &s = state();
  std::lock_guard lock(s.mutex);
  BeginCommandList(s);
}

void Video::OpenCommandListLocked() { BeginCommandList(state()); }

plume::RenderCommandList *Video::CommandList() {
  return state().command_list;
}

namespace {
std::mutex g_destroy_pending_mutex;
std::unordered_set<u32> g_destroy_pending;
} // namespace

void Video::QueueResourceDestroy(u32 guest_va, ResourceType type) {
  if (type == ResourceType::VertexShader || type == ResourceType::PixelShader ||
      type == ResourceType::VertexDeclaration) {
    return;
  }
  auto &s = state();
  {
    std::lock_guard lock(g_destroy_pending_mutex);
    if (!g_destroy_pending.insert(guest_va).second)
      return;
  }
  if (type == ResourceType::RenderTarget ||
      type == ResourceType::DepthStencil) {
    auto *surface = HostResourceHeap::FromGuest<GuestTexture>(guest_va);
    if (surface && surface->registered) {
      std::lock_guard lock(g_destroy_pending_mutex);
      g_destroy_pending.erase(guest_va);
      return;
    }
  }
  s.deferred_destroy[Video::RetireSlot("guest resource")].Queue(guest_va, type);
}

u32 Video::CurrentFrameSlot() {
  return state().frame.load(std::memory_order_relaxed);
}

u32 Video::RetireSlot(const char *what) {
  auto &s = state();
  const u32 slot = s.frame.load(std::memory_order_relaxed);
  if (static_cast<i32>(slot) ==
      s.reclaiming_slot.load(std::memory_order_relaxed)) {
    static std::atomic<u32> s_hits{0};
    const u32 n = s_hits.fetch_add(1, std::memory_order_relaxed);
    if (n < 16 || (n & 0xFF) == 0) {
      BD_WARN("[retire-race] #{} {} parked into slot {} while it is being "
              "reclaimed, freed with no fence covering it",
              n, what, slot);
    }
  }
  return slot;
}

namespace {

constexpr size_t kTextureFreesPerDrain = 24;
constexpr size_t kTextureBacklogFlushAll = 512;

void DrainTextureBacklog(VideoState &s) {
  std::vector<std::unique_ptr<plume::RenderTexture>> batch;
  {
    std::lock_guard lock(s.mutex);
    const size_t pending = s.texture_free_backlog.size();
    if (!pending)
      return;
    if (pending > kTextureBacklogFlushAll) {
      static std::atomic<u32> s_logged{0};
      if (s_logged.fetch_add(1, std::memory_order_relaxed) < 8) {
        BD_WARN("texture free backlog reached {}, flushing it in one drain",
                pending);
      }
    }
    const size_t take =
        pending > kTextureBacklogFlushAll
            ? pending
            : (pending < kTextureFreesPerDrain ? pending
                                               : kTextureFreesPerDrain);
    batch.reserve(take);
    for (size_t i = 0; i < take; ++i) {
      batch.push_back(std::move(s.texture_free_backlog[i]));
    }
    const auto first = s.texture_free_backlog.begin();
    s.texture_free_backlog.erase(
        first, first + static_cast<std::ptrdiff_t>(take));
  }
  BD_CPU_ZONE("DrainTextureBacklog");
  batch.clear();
}

} // namespace

void DrainSlot(VideoState &s, u32 slot) {
  {
    std::lock_guard lock(s.mutex);
    s.texture_view_graveyard[slot].clear();
    for (auto &tex : s.texture_graveyard[slot])
      s.texture_free_backlog.push_back(std::move(tex));
    s.texture_graveyard[slot].clear();
    s.buffer_graveyard[slot].clear();
    DrainDescriptorSlotsLocked(s, slot);
    s.reclaiming_slot.store(-1, std::memory_order_relaxed);
  }
  DrainTextureBacklog(s);
  SurfaceRegistry::Tick();
  auto dead = s.deferred_destroy[slot].Take();
  for (const auto &d : dead) {
    if (d.type == ResourceType::Texture ||
        d.type == ResourceType::VolumeTexture ||
        d.type == ResourceType::RenderTarget ||
        d.type == ResourceType::DepthStencil) {
      if (auto *t = HostResourceHeap::FromGuest<GuestTexture>(d.guest_va)) {
        t->pendingDestroy = true;
      }
    }
  }
  {
    BD_CPU_ZONE("DestroyResources");
    for (const auto &d : dead) {
      {
        std::lock_guard lock(g_destroy_pending_mutex);
        g_destroy_pending.erase(d.guest_va);
      }
      DestroyResourceNow(d.guest_va, d.type);
    }
  }
  {
    BD_CPU_ZONE("DrainNativeMirrors");
    DrainEvictedNativeTextures(slot);
  }
  {
    BD_CPU_ZONE("DrainPhysGraveyard");
    DrainBufferGraveyard(slot);
  }
}

void AdvanceAndWaitReused(VideoState &s) {
  const u32 slot = s.next_frame;
  s.reclaiming_slot.store(static_cast<i32>(slot), std::memory_order_relaxed);
  s.frame.store(slot, std::memory_order_relaxed);
  s.next_frame = (slot + 1) % kNumFrames;
  s.command_list = s.command_lists[slot].get();
  if (s.command_list_submitted[slot]) {
    s.queue->waitForCommandFence(s.fences[slot].get());
    s.command_list_submitted[slot] = false;
#if defined(REXGLUE_ENABLE_PROFILING) && defined(REBLUE_D3D12)
    if (auto *ctx = GpuProfilerCtx()) {
      TracyD3D12NewFrame(ctx);
      TracyD3D12Collect(ctx);
    }
#endif
  }
  ResetFrame(slot);
}

void SubmitOpenListLocked(VideoState &s) {
  if (!s.command_list_open)
    return;
  const u32 cur = s.frame.load(std::memory_order_relaxed);
  s.command_lists[cur]->end();
  s.command_list_open = false;
  const plume::RenderCommandList *lists[] = {s.command_lists[cur].get()};
  s.queue->executeCommandLists(lists, 1, nullptr, 0, nullptr, 0,
                               s.fences[cur].get());
  s.command_list_submitted[cur] = true;
}

} // namespace bd::gpu
