/**
 * @file    gpu/hooks/shader.cpp
 * @brief   Guest hooks that create shader and vertex declaration objects,
 *          including BD's runtime-HLSL blit shaders.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <cstring>
#include <mutex>

#include <rex/hook.h>
#include <rex/runtime.h>
#include <rex/types.h>

#include <plume_render_interface.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/backend.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/host_resource_heap.h"
#include "gpu/vertex_declaration.h"

#if defined(REBLUE_D3D12)
#include "src/gpu/shaders/hlsl/bd_2d_blit_ps.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/bd_2d_blit_vs.hlsl.dxil.h"
#else
#include "src/gpu/shaders/hlsl/bd_2d_blit_ps.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/bd_2d_blit_vs.hlsl.spirv.h"
#endif

namespace {

// Replaces the engine's declaration struct with a HostResourceHeap
// GuestVertexDeclaration so SetVertexDeclaration_hook can FromGuest it. The
// engine only stores and forwards the pointer, so the layout need not match.
bd::gpu::GuestVertexDeclaration *hcgCreateVertexDeclaration_hook(
    rex::MappedPtr<bd::gpu::GuestVertexElement> elements) {
  if (!elements)
    return nullptr;
  return bd::gpu::CreateVertexDeclaration(
      static_cast<bd::gpu::GuestVertexElement *>(elements));
}

bd::gpu::GuestShader *hcgCreateVertexShaderResource_hook(MappedU32 function) {
  if (!function)
    return nullptr;
  auto *shader = bd::gpu::CreateShader(static_cast<const be_u32 *>(function),
                                       bd::gpu::ResourceType::VertexShader);
  return shader;
}

bd::gpu::GuestShader *hcgCreatePixelShaderResource_hook(MappedU32 function) {
  if (!function)
    return nullptr;
  auto *shader = bd::gpu::CreateShader(static_cast<const be_u32 *>(function),
                                       bd::gpu::ResourceType::PixelShader);
  return shader;
}

namespace {

constexpr u32 kHcgVSTableBase = 0x82DDB290;
constexpr u32 kHcgVSTableEnd = 0x82DDB690; // exclusive
constexpr u32 kHcgPSTableBase = 0x82DDB690;
constexpr u32 kHcgPSTableEnd = 0x82DDBA90; // exclusive

bd::gpu::GuestShader *g_blitVs = nullptr;
bd::gpu::GuestShader *g_blitPs = nullptr;
std::mutex g_blitShaderMutex;

u32 FindEmptyHcgSlot(u32 base, u32 end) {
  for (u32 va = base; va < end; va += 4) {
    const auto *slot = bd::mem::at<const be_u32>(va);
    if (slot && slot->Get() == 0)
      return va;
  }
  return 0;
}

bd::gpu::GuestShader *
GetOrCreateBlitShader(bd::gpu::ResourceType type, const void *blob,
                             size_t blob_size,
                             bd::gpu::GuestShader **cache_slot) {
  if (*cache_slot)
    return *cache_slot;
  auto *device = bd::gpu::Video::HostDevice();
  if (!device) {
    REXLOG_ERROR(bd, "hcg*ShaderCreateByHlsl_hook: host device not ready");
    return nullptr;
  }
  auto *shader = bd::gpu::HostResourceHeap::Alloc<bd::gpu::GuestShader>(type);
  shader->shader =
      device->createShader(blob, blob_size, "main", bd::gpu::kHostShaderFormat);
  if (!shader->shader) {
    REXLOG_ERROR(bd, "hcg*ShaderCreateByHlsl_hook: createShader failed (type={})",
             static_cast<u32>(type));
    bd::gpu::HostResourceHeap::Free(shader);
    return nullptr;
  }
  *cache_slot = shader;
  return shader;
}

u32 HcgCreateByHlslImpl(bd::gpu::ResourceType type, const void *blob,
                        size_t blob_size, bd::gpu::GuestShader **cache_slot,
                        u32 table_base, u32 table_end) {
  std::lock_guard lock(g_blitShaderMutex);
  auto *shader =
      GetOrCreateBlitShader(type, blob, blob_size, cache_slot);
  if (!shader)
    return 0;
  const u32 slot_va = FindEmptyHcgSlot(table_base, table_end);
  if (!slot_va) {
    REXLOG_ERROR(bd, "hcg*ShaderCreateByHlsl_hook: slot table full");
    return 0;
  }
  bd::mem::store<u32>(slot_va, bd::gpu::HostResourceHeap::ToGuest(shader));
  return slot_va;
}

} // namespace

u32 hcgVertexShaderCreateByHlsl_hook() {
  const u32 slot = HcgCreateByHlslImpl(
      bd::gpu::ResourceType::VertexShader, REBLUE_SHADER_BLOB(bd_2d_blit_vs),
      &g_blitVs, kHcgVSTableBase, kHcgVSTableEnd);
  return slot;
}

u32 hcgPixelShaderCreateByHlsl_hook() {
  const u32 slot = HcgCreateByHlslImpl(
      bd::gpu::ResourceType::PixelShader, REBLUE_SHADER_BLOB(bd_2d_blit_ps),
      &g_blitPs, kHcgPSTableBase, kHcgPSTableEnd);
  return slot;
}

} // namespace

REX_HOOK(D3DDevice_CreateVertexDeclaration, hcgCreateVertexDeclaration_hook);
REX_HOOK(D3DDevice_CreateVertexShader, hcgCreateVertexShaderResource_hook);
REX_HOOK(D3DDevice_CreatePixelShader, hcgCreatePixelShaderResource_hook);
REX_HOOK(hcgVertexShaderCreateByHlsl, hcgVertexShaderCreateByHlsl_hook);
REX_HOOK(hcgPixelShaderCreateByHlsl, hcgPixelShaderCreateByHlsl_hook);
