/**
 * @file    gpu/constant_buffers.h
 * @brief   Shader constant upload path.
 *
 *   VS f4 constants at device+0x700, PS f4 at device+0x1700, SharedConstants
 *   bound through the main RenderPipelineLayout's three root descriptors
 *   (b0/b1/b2, space4).
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <rex/types.h>

#include <plume_render_interface.h>

namespace bd::gpu {

// Mirror of the SharedConstants cbuffer (b2, space4). Layout matches the
// packoffset(c<N>) emit in the recompiled shader prelude, so reordering or
// resizing means regenerating every shader in the DXIL cache.
struct SharedConstants {
  u32 texture2DIndices[16]{};   // c0..c3,  bytes 0..63
  u32 texture3DIndices[16]{};   // c4..c7,  bytes 64..127
  u32 textureCubeIndices[16]{}; // c8..c11, bytes 128..191
  u32 samplerIndices[16]{};     // c12..c15, bytes 192..255
  u32 booleansArr[8]{};         // c16, c17 (g_BooleansArr[2]) bytes 256..287
  u32 swappedTexcoords{};       // c18.x, byte 288
  float halfPixelOffsetX{};     // c18.y, byte 292
  float halfPixelOffsetY{};     // c18.z, byte 296
  float alphaThreshold{};       // c18.w, byte 300
  u32 swappedNormals{};         // c19.x, byte 304
  u32 swappedBinormals{};       // c19.y, byte 308
  u32 swappedTangents{};        // c19.z, byte 312
  u32 swappedBlendWeights{};    // c19.w, byte 316
  u32 swappedPositions{};       // c20.x, byte 320
  // Bit N set when TEXCOORD<N> is bound R16G16(B16A16)_UINT, and the shader's
  // sintTexcoord() sign-extends to recover the X360 raw-int-as-float vfetch.
  u32 sintTexcoords{}; // c20.y, byte 324
  // Sun shadow PCF tap scale (g_ShadowPcfScale). The kernel's tap offsets are
  // shader literals in 1/1024-of-the-map UV units, so their world footprint
  // grows with the bd_shadow_distance coverage box, so the recompiler
  // multiplies each by this to hold the penumbra constant in world space.
  // max(1/distance, 1024/dimension) never scales a tap below one map texel.
  float shadowPcfScale{1.0f}; // c20.z, byte 328
  u32 _pad_c20_w{};           // c20.w, byte 332
  // Half-pixel NDC nudge read only by the substituted 2D blit VS
  float blitHalfPixelOffsetX{}; // c21.x, byte 336
  float blitHalfPixelOffsetY{}; // c21.y, byte 340
  // 1 / (kx, ky) of the bound target, dividing the pixel position back to
  // logical pixels (g_PixelScaleInv).
  float pixelScaleInvX{1.0f}; // c21.z, byte 344
  float pixelScaleInvY{1.0f}; // c21.w, byte 348
  // Bit N set when the texture in fetch slot N is host-scaled, so its fetch
  // offsets are multiplied by (fetchScaleX, fetchScaleY).
  u32 scaledFetchSlots{};  // c22.x, byte 352
  float fetchScaleX{1.0f}; // c22.y, byte 356
  float fetchScaleY{1.0f}; // c22.z, byte 360
  u32 _pad_c22_w{};        // c22.w, byte 364
};
static_assert(sizeof(SharedConstants) == 0x170);
static_assert(offsetof(SharedConstants, pixelScaleInvX) == 0x158);
static_assert(offsetof(SharedConstants, scaledFetchSlots) == 0x160);
static_assert(offsetof(SharedConstants, fetchScaleX) == 0x164);

// memory stays valid until ResetFrame().
struct ConstantAllocation {
  u8 *memory = nullptr;
  plume::RenderBufferReference ref{};
  u64 gpuAddress = 0;
  u32 size = 0;
};

bool TryInit();

// Rewind a frame slot's upload chunks. Caller must have awaited that slot's
// GPU fence
// (DrainSlot), never while in flight.
void ResetFrame(u32 slot);

// Byte-swap VS/PS float constants from guest device+0x700 / device+0x1700 into
// the upload heap.
ConstantAllocation UploadVertexShaderConstants(u32 device_guest);
ConstantAllocation UploadPixelShaderConstants(u32 device_guest);

// Rebuilt from live guest state every draw: sampler fetch constants and bool
// constants come from unhooked recompiled code, so there is no dirty signal.
// size == 0 means byte-identical to what is already bound on this command list
// and the caller can skip the upload + root rebind.
ConstantAllocation UploadSharedConstants(u32 device_guest);

// Root bindings do not survive begin(), so this drops the 'shared CB still
// bound' reuse gate. Call on every command list reset.
void InvalidateSharedBinding();

// alignment defaults to 4 (the upload buffer is WRITE_COMBINE), and CBV callers
// pass 256.
ConstantAllocation UploadGuestBytesByteSwap32(u32 guest_va, u32 size,
                                              u32 alignment = 4);

// Verbatim, no swap, for texture pixel data.
ConstantAllocation UploadGuestBytes(u32 guest_va, u32 size, u32 alignment = 4);

// For pixel data already untiled to host staging.
ConstantAllocation UploadHostBytes(const void *host_data, u32 size,
                                   u32 alignment = 4);

} // namespace bd::gpu
