/**
 * @file    gpu/shaders/guest_shaders.cpp
 * @brief   Turning a guest shader into a host one: cache lookup, DXIL/SPIR-V
 *          decompression, and spec constant linking.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/device.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include <plume_render_interface.h>
#include <rex/hash.h>
#include <zstd.h>
#include <xxhash.h>

#include "core/logging.h"
#include "gpu/backend.h"
#include "gpu/host_resource_heap.h"
#include "gpu/pipeline/pso_recorder.h"
#include "gpu/shaders/shader_cache.h"

#if defined(REBLUE_D3D12)
#define MINIZ_HEADER_FILE_ONLY
#include "gpu/shaders/linked_shader_cache.h"
#include "gpu/shaders/shader_linker.h"
#include "src/gpu/shaders/hlsl/bd_pe_ps_brightpass_clamp.hlsl.dxil.h"
#include "src/gpu/shaders/hlsl/bd_pe_ps_ms_bright_clamp.hlsl.dxil.h"
#include <miniz.h>
#else
#include "src/gpu/shaders/hlsl/bd_pe_ps_brightpass_clamp.hlsl.spirv.h"
#include "src/gpu/shaders/hlsl/bd_pe_ps_ms_bright_clamp.hlsl.spirv.h"
#include <smolv.h>
#endif

namespace bd::gpu {

namespace {
ShaderCacheEntry *FindShaderCacheEntry(u64 hash) {
  auto *end = g_shaderCacheEntries + g_shaderCacheEntryCount;
  auto *result = std::lower_bound(
      g_shaderCacheEntries, end, hash,
      [](const ShaderCacheEntry &lhs, u64 rhs) { return lhs.hash < rhs; });
  return (result != end && result->hash == hash) ? result : nullptr;
}

// Guards GuestShader::shader / linkedShaders lookups+inserts only. DXC link
// and createShader run unlocked so precache workers compile in parallel.
std::mutex g_shader_link_mutex;

#if defined(REBLUE_D3D12)
// The linked-in DXIL shader cache ships ZSTD-compressed. Decompress it once on
// first shader resolve.
std::once_flag g_dxil_cache_once;
std::unique_ptr<u8[]> g_dxil_cache;

const u8 *DxilCache() {
  std::call_once(g_dxil_cache_once, [] {
    g_dxil_cache = std::make_unique<u8[]>(g_dxilCacheDecompressedSize);
    const size_t n =
        ZSTD_decompress(g_dxil_cache.get(), g_dxilCacheDecompressedSize,
                        g_compressedDxilCache, g_dxilCacheCompressedSize);
    if (ZSTD_isError(n) || n != g_dxilCacheDecompressedSize) {
      REXBD_ERROR("DXIL shader cache decompression failed ({} of {} bytes)", n,
               g_dxilCacheDecompressedSize);
      g_dxil_cache.reset();
    }
  });
  return g_dxil_cache.get();
}

// Build-time pre-linked spec constant variants (reblue_prelink), miniz
// (deflate) compressed, decompressed once on first spec shader resolve.
std::once_flag g_linked_dxil_cache_once;
std::unique_ptr<u8[]> g_linked_dxil_cache;

const u8 *LinkedDxilCache() {
  std::call_once(g_linked_dxil_cache_once, [] {
    if (g_linkedShaderCacheEntryCount == 0)
      return;
    auto buf = std::make_unique<u8[]>(g_linkedDxilCacheDecompressedSize);
    mz_ulong out_len = static_cast<mz_ulong>(g_linkedDxilCacheDecompressedSize);
    if (mz_uncompress(buf.get(), &out_len, g_compressedLinkedDxilCache,
                      static_cast<mz_ulong>(g_linkedDxilCacheCompressedSize)) !=
            MZ_OK ||
        out_len != g_linkedDxilCacheDecompressedSize) {
      REXBD_ERROR("linked shader cache decompression failed");
      return;
    }
    g_linked_dxil_cache = std::move(buf);
    REXBD_DEBUG("linked shader cache: {} prelinked spec-constant variant(s)",
             g_linkedShaderCacheEntryCount);
  });
  return g_linked_dxil_cache.get();
}

const LinkedShaderCacheEntry *FindLinkedShaderCacheEntry(u64 hash, u32 masked) {
  const LinkedShaderCacheEntry *first = g_linkedShaderCacheEntries;
  const LinkedShaderCacheEntry *end = first + g_linkedShaderCacheEntryCount;
  const auto *it = std::lower_bound(
      first, end, std::pair{hash, masked},
      [](const LinkedShaderCacheEntry &lhs, const std::pair<u64, u32> &rhs) {
        if (lhs.hash != rhs.first)
          return lhs.hash < rhs.first;
        return lhs.specConstants < rhs.second;
      });
  return (it != end && it->hash == hash && it->specConstants == masked)
             ? it
             : nullptr;
}
#else
// The SPIR-V half of the same generated cache: ZSTD-compressed, per-entry
// modules smol-v encoded. Decompressed once on first shader resolve.
std::once_flag g_spirv_cache_once;
std::unique_ptr<u8[]> g_spirv_cache;

const u8 *SpirvCache() {
  std::call_once(g_spirv_cache_once, [] {
    g_spirv_cache = std::make_unique<u8[]>(g_spirvCacheDecompressedSize);
    const size_t n =
        ZSTD_decompress(g_spirv_cache.get(), g_spirvCacheDecompressedSize,
                        g_compressedSpirvCache, g_spirvCacheCompressedSize);
    if (ZSTD_isError(n) || n != g_spirvCacheDecompressedSize) {
      REXBD_ERROR("SPIR-V shader cache decompression failed ({} of {} bytes)", n,
               g_spirvCacheDecompressedSize);
      g_spirv_cache.reset();
    }
  });
  return g_spirv_cache.get();
}
#endif

// X360 exports BD's bloom bright mask into an LDR EDRAM tile, saturating it at
// 1.0 before the gaussian blur, but reblue's FP16 posteff chain keeps the raw
// mask (wc01 authors BLOOM BriMulti=50 -> exports ~59) and the blur spreads it
// into giant white blobs. These two masks are substituted by copies whose
// export is clamped to [0,1].
bool BloomMaskClampBlob(u64 hash, const void *&blob, size_t &size) {
  switch (hash) {
  case 0xFFDBD782126EB6E8ull: // bd_pe_ps_brightpass
    blob = REBLUE_BLOB_SYMBOL(bd_pe_ps_brightpass_clamp);
    size = sizeof(REBLUE_BLOB_SYMBOL(bd_pe_ps_brightpass_clamp));
    return true;
  case 0xD386EA2FABF16CE9ull: // bd_pe_ps_ms_bright
    blob = REBLUE_BLOB_SYMBOL(bd_pe_ps_ms_bright_clamp);
    size = sizeof(REBLUE_BLOB_SYMBOL(bd_pe_ps_ms_bright_clamp));
    return true;
  default:
    return false;
  }
}

// Publishes the first shader built for gs, a concurrent builder's copy is
// dropped before any caller can observe it.
plume::RenderShader *PublishShader(GuestShader *gs,
                                   std::unique_ptr<plume::RenderShader> sh) {
  std::lock_guard lock(g_shader_link_mutex);
  if (!gs->shader)
    gs->shader = std::move(sh);
  return gs->shader.get();
}

} // namespace

GuestShader *CreateShader(const be_u32 *function, ResourceType type) {
  // The cache key hashes the whole container, matching how XenosRecomp keyed
  // the entries it generated.
  const auto *container = reinterpret_cast<const ShaderContainer *>(function);
  const u32 hash_len = container->virtualSize + container->physicalSize;
  const u64 hash = XXH3_64bits(function, hash_len);
  ShaderCacheEntry *entry = FindShaderCacheEntry(hash);
  auto *shader = HostResourceHeap::Alloc<GuestShader>(type);
  if (entry) {
    if (entry->guestShader == nullptr) {
      entry->guestShader = shader;
    }
    shader->shaderCacheEntry = entry;
  } else {
    REXBD_WARN("Shader cache miss: hash=0x{:016X} len={} type={}", hash, hash_len,
            static_cast<u32>(type));
  }
  // Let boot cache replay enqueue any pending PSO that was waiting on this
  // shader's microcode hash now that its host object exists.
  OnShaderCreated(hash);
  return shader;
}

plume::RenderShader *GetOrLinkShader(GuestShader *gs, u32 specConstants) {
  if (!gs)
    return nullptr;
  // hcg*ShaderCreateByHlsl path: shader populated directly, no cache entry.
  if (gs->shader)
    return gs->shader.get();
  const ShaderCacheEntry *entry = gs->shaderCacheEntry;
  if (!entry)
    return nullptr;
  auto *device = Video::HostDevice();
  if (!device)
    return nullptr;

  const void *clampBlob = nullptr;
  size_t clampSize = 0;
  if (BloomMaskClampBlob(entry->hash, clampBlob, clampSize)) {
    return PublishShader(gs, device->createShader(clampBlob, clampSize, "main",
                                                  kHostShaderFormat));
  }

#if !defined(REBLUE_D3D12)
  // Vulkan: one decoded SPIR-V module serves every spec constant value. The
  // XenosRecomp SPIR-V keeps g_SpecConstants as specialization constant id 0,
  // supplied per pipeline by the pipeline cache, and no link step exists.
  (void)specConstants;
  const u8 *cache = SpirvCache();
  if (!cache)
    return nullptr;
  const u8 *smol = cache + entry->spirvOffset;
  std::vector<u8> spirv(smolv::GetDecodedBufferSize(smol, entry->spirvSize));
  if (spirv.empty() ||
      !smolv::Decode(smol, entry->spirvSize, spirv.data(), spirv.size())) {
    REXBD_ERROR("GetOrLinkShader: SPIR-V decode failed (hash=0x{:016X})",
             entry->hash);
    return nullptr;
  }
  return PublishShader(gs,
                       device->createShader(spirv.data(), spirv.size(), "main",
                                            plume::RenderShaderFormat::SPIRV));
#else
  const u8 *cache = DxilCache();
  if (!cache)
    return nullptr;

  const u32 masked = specConstants & entry->specConstantsMask;
  if (entry->specConstantsMask != 0) {
    std::lock_guard lock(g_shader_link_mutex);
    auto it = gs->linkedShaders.find(masked);
    if (it != gs->linkedShaders.end())
      return it->second.get();
  }

  // Miss: build outside the lock. DXC (per-call instances) and D3D12 shader
  // creation are free-threaded, so precache workers link in parallel and a
  // render thread miss waits only on its own build.
  if (entry->specConstantsMask == 0) {
    // The cache DXIL is already a complete vs/ps.
    return PublishShader(
        gs, device->createShader(cache + entry->dxilOffset, entry->dxilSize,
                                 "main", plume::RenderShaderFormat::DXIL));
  }

  // Spec constant shaders ship as DXIL libraries with an unresolved
  // g_SpecConstants() export. reblue_prelink bakes every mask subset of every
  // shipped shader, so the pre-linked lookup is the norm. The runtime DXC link
  // below only covers a prelink/shader cache skew.
  std::unique_ptr<plume::RenderShader> sh;
  const LinkedShaderCacheEntry *pre =
      FindLinkedShaderCacheEntry(entry->hash, masked);
  const u8 *linkedCache = pre ? LinkedDxilCache() : nullptr;
  if (pre && linkedCache) {
    sh = device->createShader(linkedCache + pre->dxilOffset, pre->dxilSize,
                              "main", plume::RenderShaderFormat::DXIL);
  } else {
    REXBD_WARN("GetOrLinkShader: no prelinked variant, runtime DXC link "
            "(hash=0x{:016X} mask=0x{:X})",
            entry->hash, masked);
    std::vector<u8> linked =
        LinkSpecConstant(cache + entry->dxilOffset, entry->dxilSize,
                         gs->type == ResourceType::PixelShader, masked);
    if (linked.empty())
      return nullptr;
    sh = device->createShader(linked.data(), linked.size(), "main",
                              plume::RenderShaderFormat::DXIL);
  }
  std::lock_guard lock(g_shader_link_mutex);
  auto [it, inserted] = gs->linkedShaders.try_emplace(masked, std::move(sh));
  return it->second.get();
#endif
}

GuestShader *FindGuestShaderByHash(u64 hash) {
  ShaderCacheEntry *entry = FindShaderCacheEntry(hash);
  if (!entry || !entry->guestShader)
    return nullptr;
  return entry->guestShader;
}

} // namespace bd::gpu
