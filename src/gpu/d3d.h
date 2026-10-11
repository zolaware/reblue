/**
 * @file    gpu/d3d.h
 * @brief   Byte-accurate guest layouts for the guest D3D9 SDK objects the
 *          recompiled BD engine reads/writes.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 *
 * Must match the guest layout byte-for-byte at the offsets the engine
 * accesses, or accessors like D3DTexture_GetLevelDesc decode garbage and
 * crash D3D12. Layouts recovered from the guest binary (D3DResource ord 933,
 * D3DTexture ord 2657, D3DSurface ord 2525, D3DVertexBuffer ord 1936,
 * D3DIndexBuffer ord 1937, D3DVertexShader ord 1641, D3DPixelShader ord 1263,
 * D3DVertexDeclaration ord 934, GPUTEXTURE_FETCH_CONSTANT ord 853). Only
 * host-accessed fields are named.
 */
#pragma once

#include <cstddef>
#include <rex/types.h>

namespace bd::gpu {

// D3DRESOURCETYPE values from the guest D3D9 SDK. D3DResource::Common carries
// the type in its low 4 bits, so these double as the Common base type bits.
enum class D3DResourceType : u32 {
  kSurface = 1,
  kTexture = 3,
  kVolumeTexture = 4,
  kCubeTexture = 5,
  kVertexBuffer = 6,
  kIndexBuffer = 7,
};

// 24-byte header shared by every D3D9 resource (D3DResource ord 933).
struct D3DResource {
  be_u32 Common;         // +0x00  type/flag bits, read by D3DResource_GetType
  be_u32 ReferenceCount; // +0x04  engine increments/decrements directly
  be_u32 Fence;          // +0x08
  be_u32 ReadFence;      // +0x0C
  be_u32 Identifier;     // +0x10
  be_u32 BaseFlush;      // +0x14
};
static_assert(sizeof(D3DResource) == 24);

// The pViewport argument to D3DDevice_SetViewport (BD 0x82473610), and the
// layout of the device's cached viewport at D3DDevice+0x3058.
struct D3DViewport9 {
  be_u32 X;
  be_u32 Y;
  be_u32 Width;
  be_u32 Height;
  be_f32 MinZ;
  be_f32 MaxZ;
};
static_assert(sizeof(D3DViewport9) == 24);

// Guest RECT (LTRB): the pRect argument to D3DDevice_SetScissorRect
// (BD 0x82473548).
struct D3DRect {
  be_i32 left;
  be_i32 top;
  be_i32 right;
  be_i32 bottom;
};
static_assert(sizeof(D3DRect) == 16);

// GPUTEXTURE_FETCH_CONSTANT (ord 853): 6-dword Xenos texture descriptor at
// D3DTexture+0x1C. reblue-created textures leave it zero since their accessors
// are hooked. Engine-created bdAllocRenderBuffer textures carry a real one that
// native_texture_mirror decodes.
struct GpuTextureFetchConstant {
  be_u32 dword[6];
};
static_assert(sizeof(GpuTextureFetchConstant) == 24);

// D3DTexture / D3DBaseTexture / D3DCubeTexture / D3DVolumeTexture, 52 bytes.
struct D3DTexture {
  D3DResource resource;           // +0x00
  be_u32 MipFlush;                // +0x18
  GpuTextureFetchConstant Format; // +0x1C
};
static_assert(sizeof(D3DTexture) == 52);
static_assert(offsetof(D3DTexture, Format) == 0x1C);

// D3DSurface, 48 bytes. D3DSurface_GetDesc (0x8246D668, non-texture path)
// unpacks Width/Height from the dword at +0x24:
//   Width  = (SizeBits >> 18) + 1            (width-1 in bits [31:18])
//   Height = ((SizeBits >> 3) & 0x7FFF) + 1  (height-1 in bits [17:3])
// Bits [2:0] are pad.
struct D3DSurface {
  D3DResource resource; // +0x00
  be_u32 SurfaceInfo;   // +0x18  GPU_SURFACEINFO union word
  be_u32 DepthInfo;     // +0x1C  GPU_DEPTHINFO union word
  be_u32 HiControl;     // +0x20
  be_u32 SizeBits;      // +0x24  packed Height/Width
  be_u32 Format;        // +0x28  D3DFORMAT
  be_u32 Size;          // +0x2C
};
static_assert(sizeof(D3DSurface) == 48);

// Verified against the code at 0x8246D668. Written by D3DSurface_GetDesc
// and D3DTexture_GetLevelDesc.
struct D3DSurfaceDesc {
  be_u32 Format;             // +0x00  guest D3DFORMAT
  be_u32 Type;               // +0x04  D3DRESOURCETYPE
  be_u32 Usage;              // +0x08  always written 0 by reblue
  be_u32 Pool;               // +0x0C  always written 0 by reblue
  be_u32 MultiSampleType;    // +0x10  0, since reblue surfaces are COUNT_1
  be_u32 MultiSampleQuality; // +0x14  0
  be_u32 Width;              // +0x18  surface / mip-level width
  be_u32 Height;             // +0x1C  surface / mip-level height
};
static_assert(sizeof(D3DSurfaceDesc) == 32);

// Written by D3DVolumeTexture_GetLevelDesc. Adds Depth and drops the
// MultiSample fields, so NOT the D3DSurfaceDesc layout.
struct D3DVolumeDesc {
  be_u32 Format; // +0x00  guest D3DFORMAT
  be_u32 Type;   // +0x04  4 == D3DRTYPE_VOLUMETEXTURE
  be_u32 Usage;  // +0x08  0
  be_u32 Pool;   // +0x0C  0
  be_u32 Width;  // +0x10  mip-level width
  be_u32 Height; // +0x14  mip-level height
  be_u32 Depth;  // +0x18  mip-level depth
};
static_assert(sizeof(D3DVolumeDesc) == 28);

// D3DLOCKED_RECT. Points the engine at the texture's guest scratch mirror.
struct D3DLockedRect {
  be_u32 Pitch; // +0x00  row pitch in bytes
  be_u32 pBits; // +0x04  guest VA of the locked pixel scratch
};
static_assert(sizeof(D3DLockedRect) == 8);

// D3DVertexBuffer / D3DIndexBuffer, 32 bytes. The +0x18/+0x1C pair is the Xenos
// fetch constant, encoded two ways depending on who wrote it:
//   Pre-XGOffset (bdPhysical*Create replication, raw dwords):
//     VB writes size@FetchLo/base@FetchHi, IB writes (size|3)@FetchLo/
//     ((base & 0x3FFFFFC)|0x10000002)@FetchHi.
//   Post-XGOffsetResourceAddress (asset-loaded structs, Bootstrap path):
//     FetchLo = base_va | tag_bits, FetchHi = (size & 0x3FFFFFC) | 0x10000002.
// reblue's own mirror and bootstrap paths use the post-XGOffset encoding.
struct D3DVertexBuffer {
  D3DResource resource;
  be_u32 FetchLo; // +0x18
  be_u32 FetchHi; // +0x1C
};
static_assert(sizeof(D3DVertexBuffer) == 32);
static_assert(offsetof(D3DVertexBuffer, FetchLo) == 0x18);
static_assert(offsetof(D3DVertexBuffer, FetchHi) == 0x1C);

struct D3DIndexBuffer {
  D3DResource resource;
  be_u32 FetchLo; // +0x18
  be_u32 FetchHi; // +0x1C
};
static_assert(sizeof(D3DIndexBuffer) == 32);
static_assert(offsetof(D3DIndexBuffer, FetchLo) == 0x18);
static_assert(offsetof(D3DIndexBuffer, FetchHi) == 0x1C);

// D3DVertexShader / D3DPixelShader / D3DVertexDeclaration, 24 bytes each.
struct D3DVertexShader {
  D3DResource resource;
};
struct D3DPixelShader {
  D3DResource resource;
};
struct D3DVertexDeclaration {
  D3DResource resource;
};
static_assert(sizeof(D3DVertexShader) == 24);
static_assert(sizeof(D3DPixelShader) == 24);
static_assert(sizeof(D3DVertexDeclaration) == 24);

// The microcode blob CreateVertexShader/CreatePixelShader are handed, layout
// mirrors thirdparty/XenosRecomp/XenosRecomp/shader.h.
struct ShaderContainer {
  be_u32 flags;
  be_u32 virtualSize;
  be_u32 physicalSize;
  be_u32 fieldC;
  be_u32 constantTableOffset;
  be_u32 definitionTableOffset;
  be_u32 shaderOffset;
  be_u32 field1C;
  be_u32 field20;
};
static_assert(sizeof(ShaderContainer) == 0x24);

// Direct3D_CreateDevice allocates 0x5000 via
// D3D_AllocAlignedZeroed: the 0x2A00-byte public D3DDevice plus BD-private
// slots past it.
constexpr u32 kD3DDeviceAllocSize = 0x5000;

// dword[0] bits [1:0] of a fetch constant tag what it describes.
constexpr u32 kFetchTypeMask = 0x3;
constexpr u32 kFetchTypeTexture = 2;
constexpr u32 kFetchTypeVertex = 3;

// 24-byte Xenos GPU fetch descriptor. D3DDevice holds 32 in fetchConstants[].
struct DeviceFetchConstant {
  be_u32 dword[6];
};
static_assert(sizeof(DeviceFetchConstant) == 24);

// Shared guest memory: the recompiled engine, reblue's hooks and the draw path
// all read and write it. Only the fields reblue reads are named. Verified
// against the recovered D3DDevice / _D3DConstants types and BD's own code. Each
// named field past the public struct cites where it came from.
struct D3DDevice {
  // m_Pending (_D3DTAGCOLLECTION): 5 dirty mask u64s. Set* setters OR into
  // m_Mask[2] (byte +0x10), SetSamplerStateInline into m_Mask[3].
  be_u64 m_Mask[5];        // +0x000
  be_u32 m_pRing;          // +0x028
  be_u32 m_pRingLimit;     // +0x02C
  be_u32 m_pRingGuarantee; // +0x030
  be_u32 m_ReferenceCount; // +0x034
  // Per-state dispatch tables, seeded by Direct3D_CreateDevice_hook /
  // CopyDispatchTable from the XEX template tables.
  be_u32 m_SetRenderStateCall[97];  // +0x038
  be_u32 m_SetSamplerStateCall[20]; // +0x1BC
  be_u32 m_GetRenderStateCall[97];  // +0x20C
  be_u32 m_GetSamplerStateCall[20]; // +0x390
  u8 pad_03E0[0x400 - 0x3E0];       // +0x3E0
  // m_Constants (_D3DConstants, +0x400..+0x27A0). The unified Xenos fetch
  // table: 32 slots shared by texture (type 2) and vertex (type 3) fetch
  // constants, flushed by CommitTextureFetchConstants 0x82486018.
  DeviceFetchConstant fetchConstants[32]; // +0x400
  be_f32 vsFloatConstants[256][4];        // +0x700
  be_f32 psFloatConstants[256][4];        // +0x1700
  // VS / PS boolean constants, 4 u32 (128 bits) each.
  // D3DDevice_SetVertexShaderConstantB writes dword (N/32)+2496 =
  // device+0x2700, and SetPixelShaderConstantB writes +2500 =
  // device+0x2710. The rest of the _D3DConstants tail is int/loop constants.
  be_u32 vsBoolConstants[4];    // +0x2700
  be_u32 psBoolConstants[4];    // +0x2710
  u8 pad_2720[0x27A0 - 0x2720]; // +0x2720  int/loop constants
  be_f32 m_ClipPlanes[6][4];    // +0x27A0
  u8 pad_2800[0x28B8 - 0x2800]; // +0x2800  GPU packets + guest tail
  // RB_BLENDCONTROL0..3 (Xenos blend ROP state), seeded to no-blend. Slot 0 is
  // non-contiguous with the 1..3 trio.
  be_u32 rbBlendControl0;       // +0x28B8
  u8 pad_28BC[0x28D8 - 0x28BC]; // +0x28BC
  be_u32 rbBlendControl1;       // +0x28D8
  be_u32 rbBlendControl2;       // +0x28DC
  be_u32 rbBlendControl3;       // +0x28E0
  u8 pad_28E4[0x2A39 - 0x28E4]; // +0x28E4
  // "Resolution applied" bit. D3DDevice_Reset/SetResolution ORs in 0x10.
  u8 resolutionApplied;         // +0x2A39
  u8 pad_2A3A[0x2D10 - 0x2A3A]; // +0x2A3A
  // D3DDevice_SetVertexDeclaration: stw r4, 0x2D10(r3).
  be_u32 vertexDeclaration;     // +0x2D10
  u8 pad_2D14[0x2D3C - 0x2D14]; // +0x2D14
  // Xenos RB_COLORCONTROL shadow. Bit 31 is the alpha blend master enable.
  // ReadDeviceRenderState reads regs[2895] (2895*4 = 0x2D3C). The regs[] index
  // == device byte offset / 4 mapping holds because regs[2606]*4 = 0x28B8, the
  // rbBlendControl0 offset asserted below.
  be_u32 rbColorControl;        // +0x2D3C
  u8 pad_2D40[0x2F88 - 0x2D40]; // +0x2D40
  // Bound RT/DS surface shadow. D3DDevice_GetRenderTarget (0x824739F0) reads
  // +0x2F88 + idx*4, and D3DDevice_GetDepthStencilSurface (0x82473A38) reads
  // +0x2F98. Hooks replaced the recompiled Set* bodies that wrote these, so the
  // hooks must maintain them or the RT stack save/restore
  // (bdRenderTargetStackPush/Pop 0x82273080/0x82273240, every Sofdec movie
  // frame) restores garbage.
  be_u32 renderTargetShadow[4]; // +0x2F88
  be_u32 depthStencilShadow;    // +0x2F98
  u8 pad_2F9C[0x3058 - 0x2F9C]; // +0x2F9C
  // D3DDevice_SetViewport stores X/Y/W/H/MinZ/MaxZ at +0x3058 and
  // the scissor rect at +0x3070.
  D3DViewport9 viewport; // +0x3058
  D3DRect scissorRect;   // +0x3070
  // BD-private slots past the public 0x2A00 struct.
  // D3DDevice_SetPixelShader: stw r29, 0x3080(r30).
  // D3DDevice_SetVertexShader: stw r29, 0x3084(r30).
  be_u32 pixelShader;           // +0x3080
  be_u32 vertexShader;          // +0x3084
  u8 pad_3088[0x3370 - 0x3088]; // +0x3088
  // D3DPRESENT_PARAMETERS copy written verbatim by D3DDevice_Reset (124 bytes).
  u8 presentParams[124];        // +0x3370
  u8 pad_33EC[0x4D38 - 0x33EC]; // +0x33EC
  // D3DDevice_SetIndices (0x8247B328): stw r30, 0x4D38(r31).
  be_u32 indexBuffer;           // +0x4D38
  u8 pad_4D3C[0x4DFC - 0x4D3C]; // +0x4D3C
  // Resolution block mirrored by D3DDevice_Reset
  // (D3D::InitializePresentationParameters): width, height, width(dup),
  // config flag.
  be_u32 resolutionWidth;       // +0x4DFC
  be_u32 resolutionHeight;      // +0x4E00
  be_u32 resolutionWidthDup;    // +0x4E04
  be_u32 resolutionConfig;      // +0x4E08
  u8 pad_4E0C[0x5000 - 0x4E0C]; // +0x4E0C
};
static_assert(sizeof(D3DDevice) == 0x5000);
static_assert(offsetof(D3DDevice, m_pRing) == 0x028);
static_assert(offsetof(D3DDevice, m_SetRenderStateCall) == 0x038);
static_assert(offsetof(D3DDevice, m_SetSamplerStateCall) == 0x1BC);
static_assert(offsetof(D3DDevice, m_GetRenderStateCall) == 0x20C);
static_assert(offsetof(D3DDevice, m_GetSamplerStateCall) == 0x390);
static_assert(offsetof(D3DDevice, fetchConstants) == 0x400);
static_assert(offsetof(D3DDevice, vsFloatConstants) == 0x700);
static_assert(offsetof(D3DDevice, psFloatConstants) == 0x1700);
static_assert(offsetof(D3DDevice, vsBoolConstants) == 0x2700);
static_assert(offsetof(D3DDevice, psBoolConstants) == 0x2710);
static_assert(offsetof(D3DDevice, m_ClipPlanes) == 0x27A0);
static_assert(offsetof(D3DDevice, vertexDeclaration) == 0x2D10);
static_assert(offsetof(D3DDevice, rbColorControl) == 0x2D3C);
static_assert(offsetof(D3DDevice, renderTargetShadow) == 0x2F88);
static_assert(offsetof(D3DDevice, depthStencilShadow) == 0x2F98);
static_assert(offsetof(D3DDevice, viewport) == 0x3058);
static_assert(offsetof(D3DDevice, scissorRect) == 0x3070);
static_assert(offsetof(D3DDevice, pixelShader) == 0x3080);
static_assert(offsetof(D3DDevice, vertexShader) == 0x3084);
static_assert(offsetof(D3DDevice, indexBuffer) == 0x4D38);
static_assert(offsetof(D3DDevice, resolutionApplied) == 0x2A39);
static_assert(offsetof(D3DDevice, presentParams) == 0x3370);
static_assert(offsetof(D3DDevice, resolutionWidth) == 0x4DFC);
static_assert(offsetof(D3DDevice, rbBlendControl0) == 0x28B8);
static_assert(offsetof(D3DDevice, rbBlendControl1) == 0x28D8);
static_assert(offsetof(D3DDevice, rbBlendControl2) == 0x28DC);
static_assert(offsetof(D3DDevice, rbBlendControl3) == 0x28E0);

// D3DResource_GetType decodes the type from D3DResource:
//   base = Common & 0xF
//     1 -> Surface  (extended to 16 if Common & 0x40000000 + texture-backed)
//     3 -> Texture, then dim = (fetch_dword5 >> 9) & 3:
//            0 -> returns 20 (1D / line texture)
//            1 -> returns 3 if !tiled, 19 if tiled (fetch_dword1 & 0x400)
//            2 -> returns 17 (volume texture)
//            3 -> returns 18 (stacked cube)
//     6 -> VertexBuffer
//     7 -> IndexBuffer
// The Common low-4 bits select the base type. Texture dimensionality lives in
// the fetch constant. Bit 30 (0x40000000) on a surface marks it texture-backed,
// which routes D3DSurface_GetDesc through D3DTexture_GetLevelDesc.
constexpr u32 kCommonFlagTextureBackedSurface = 0x40000000u;

constexpr u32 kClearTarget = 0x1u;
constexpr u32 kClearZBuffer = 0x10u;
constexpr u32 kClearStencil = 0x20u;
constexpr u32 kClearAll = kClearTarget | kClearZBuffer | kClearStencil;

constexpr u32 kResolveDepthStencil = 0x4u;
constexpr u32 kResolveExponentBiasShift = 26;

inline void InitResourceHeader(D3DResource &r, D3DResourceType type) {
  r.Common = u32(type);
  r.ReferenceCount = 1u;
  r.Fence = 0u;
  r.ReadFence = 0u;
  r.Identifier = 0u;
  r.BaseFlush = 0u;
}

} // namespace bd::gpu
