/**
 * @file    gpu/hooks/device.cpp
 * @brief   Device lifecycle and per-frame device calls:
 *          Direct3D_CreateDevice, which allocates and seeds the guest
 *          D3DDevice struct while plume owns the real swapchain,
 *          Reset/SetResolution, Clear, Swap, the gamma ramp, the occlusion
 *          query, and the no-op stubs for device calls the host renderer does
 *          not model.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include <cstring>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

#include "core/hooks.h"
#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/engine.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/host_resource_heap.h"
#include "gpu/occlusion.h"

namespace {

// The Xenon ABI reserves a GPR slot for every float arg (Z is in f1 but still
// consumes r8, and call sites never write r8), while SDK marshaling numbers
// integer args without skipping it. The placeholder absorbs r8 so Stencil reads
// r9. Without it Stencil reads r8 garbage and the door blackout volume (gated
// on stencil NOTEQUAL 0 over a 0-cleared buffer) floods through walls.
u32 D3DDevice_Clear_hook(u32 /*device*/, u32 /*count*/, u32 /*rects*/,
                         u32 flags, u32 color, f64 z, u32 /*z_gpr_slot*/,
                         u32 stencil, u32 /*edram_clear*/) {
  bd::gpu::Video::RequestClear(flags, color, float(z), stencil);
  return 0;
}

// Builds the X360 scanout ramp pow(sqrt(x), (2-sub[ch]) * mul[ch]) and
// uploads it via SetGammaRamp. The defaults give x^0.5, which is how BD
// gamma-encodes its linear front buffer. The host has no scanout LUT, so the
// exponent is captured and applied in the present gamma pass instead.
void bdBuildGammaRampLUT_hook(mapped_f32 sub, mapped_f32 mul) {
  if (!sub || !mul)
    return;
  const be_f32 *s = sub;
  const be_f32 *m = mul;
  float g = 0.0f;
  for (int ch = 0; ch < 3; ++ch) {
    g += (2.0f - s[ch]) * m[ch];
  }
  bd::gpu::state().guest_gamma = g / 6.0f;
}

// D3DDevice_Swap(D3DDevice*, D3DBaseTexture* pFrontBuffer, scaler params).
// pFrontBuffer is the resolved finished frame, so present it exactly rather
// than guessing from draw history.
u32 D3DDevice_Swap_hook(u32 /*device*/, u32 front_buffer_va,
                        u32 /*pParameters*/) {
  auto *front_buffer =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
          front_buffer_va);
  if (bd::engine::SparseFrame())
    bd::gpu::Video::SkipPresent();
  else
    bd::gpu::Video::Present(front_buffer);
  return 0;
}

u32 D3DQuery_Issue_hook(u32 /*pThis*/, u32 dwIssueFlags) {
  // X360 D3DISSUE_BEGIN=2, D3DISSUE_END=1. bdLightScatterDraw brackets the sun
  // occlusion test quad, so drive the GPU occlusion counter and the lens flare
  // fades behind geometry. See Occlusion::Begin/End.
  if (dwIssueFlags == 2) {
    bd::gpu::Occlusion::Begin();
  } else if (dwIssueFlags == 1) {
    bd::gpu::Occlusion::End();
  }
  return 0;
}

u32 D3DQuery_GetData_hook(u32 /*pThis*/, rex::MappedPtr<u8> pData, u32 size,
                          u32 /*flags*/) {
  // Sun OCCLUSION query: the consumer (mcl__helper_3DE8) builds a 64-bit count
  // from dword[0] (low) + dword[3] (high), /16384.0 (128x128 test quad) -> 0..1
  // fraction-of-sun-visible scaling the lens flare. The high dword MUST stay 0:
  // nonzero overflows the count -> flare alpha NaN -> ghost quads render black.
  if (pData && size >= 16) {
    auto *q = reinterpret_cast<be_u32 *>(static_cast<u8 *>(pData));
    q[0] = bd::gpu::Occlusion::Count();
    q[1] = 0;
    q[2] = 0;
    q[3] = 0;
    if (size > 16)
      std::memset(static_cast<u8 *>(pData) + 16, 0, size - 16);
  } else if (pData && size) {
    std::memset(static_cast<u8 *>(pData), 0, size);
  }
  return 0;
}

// BD's real D3DDevice allocation size (D3D_AllocAlignedZeroed at
// Direct3D_CreateDevice 0x8246B710), which is also sizeof(D3DDevice).
constexpr u32 kGuestDeviceSize = bd::gpu::kD3DDeviceAllocSize;
constexpr u32 kRenderStateTableGuestAddr = 0x82751D68;
constexpr u32 kRenderStateTableEntries = 0x61;
constexpr u32 kRenderStateDispatchOffset = 56;
constexpr u32 kRenderStateDefaultOffset = 524;
constexpr u32 kSamplerStateTableGuestAddr = 0x827521F8;
constexpr u32 kSamplerStateTableEntries = 0x14;
constexpr u32 kSamplerStateDispatchOffset = 444;
constexpr u32 kSamplerStateDefaultOffset = 912;

void CopyDispatchTable(u32 device_guest, u32 table_addr, u32 entries,
                       u32 dispatch_offset, u32 default_offset) {
  const auto *table = bd::mem::at<const be_u32>(table_addr);
  auto *dispatch = bd::mem::at<be_u32>(device_guest + dispatch_offset);
  auto *defaults = bd::mem::at<be_u32>(device_guest + default_offset);
  if (!table || !dispatch || !defaults)
    return;
  for (u32 i = 0; i < entries; ++i) {
    defaults[i] = table[i * 3 + 0];
    dispatch[i] = table[i * 3 + 1];
  }
}

constexpr u32 kBlendControlNoBlend = 0x00010001; // src=ONE dst=ZERO

void SeedDeviceRenderState(u32 device_guest) {
  auto *device = bd::mem::at<bd::gpu::D3DDevice>(device_guest);
  if (!device)
    return;
  // RB_BLENDCONTROL0..3 (engine reads/writes all four): seed to no-blend.
  device->rbBlendControl0 = kBlendControlNoBlend;
  device->rbBlendControl1 = kBlendControlNoBlend;
  device->rbBlendControl2 = kBlendControlNoBlend;
  device->rbBlendControl3 = kBlendControlNoBlend;
}

u32 Direct3D_CreateDevice_hook(u32 /*adapter*/, u32 /*deviceType*/,
                               u32 /*focusWindow*/, u32 /*behaviorFlags*/,
                               u32 /*presentParams*/, mapped_u32 outDevice) {
  auto *memory = REX_KERNEL_MEMORY();
  const u32 device_guest = memory->SystemHeapAlloc(kGuestDeviceSize, 0x100);
  memory->Zero(device_guest, kGuestDeviceSize);
  CopyDispatchTable(device_guest, kRenderStateTableGuestAddr,
                    kRenderStateTableEntries, kRenderStateDispatchOffset,
                    kRenderStateDefaultOffset);
  CopyDispatchTable(device_guest, kSamplerStateTableGuestAddr,
                    kSamplerStateTableEntries, kSamplerStateDispatchOffset,
                    kSamplerStateDefaultOffset);

  SeedDeviceRenderState(device_guest);

  // D3DDevice viewport is at offset 0x3058, and the rest is zeroed above.
  if (auto *device = bd::mem::at<bd::gpu::D3DDevice>(device_guest)) {
    device->viewport.Width = 1280;
    device->viewport.Height = 720;
    device->viewport.MaxZ = 1.0f;
  }

  if (outDevice) {
    *outDevice = device_guest;
  }
  return 0;
}

// D3DPRESENT_PARAMETERS (D3DDevice_Reset arg): width@dword0, height@dword1,
// config flag@dword17 (0x44). reblue copies the 124-byte block into the device.
constexpr u32 kPresentParamsConfigDword = 17;

// D3DDevice_Reset / SetResolution: mirror what
// D3D::InitializePresentationParameters writes into the device struct so
// downstream code sees a populated device, but skip
// the PM4 path + surface allocations (Plume owns the swapchain).
u32 D3DDevice_SetResolution_hook(rex::MappedPtr<bd::gpu::D3DDevice> device,
                                 mapped_u32 params) {
  if (!device || !params)
    return 0;

  const u32 width = params[0];
  const u32 height = params[1];
  const u32 cfg = params[kPresentParamsConfigDword];

  device->resolutionWidth = width;
  device->resolutionHeight = height;
  device->resolutionWidthDup = width;
  device->resolutionConfig = cfg;

  std::memcpy(device->presentParams, params.host_address(),
              sizeof(device->presentParams));
  device->resolutionApplied |= 0x10;

  return 0;
}

} // namespace

REX_HOOK(Direct3D_CreateDevice, Direct3D_CreateDevice_hook);
REX_HOOK(D3DDevice_Reset, D3DDevice_SetResolution_hook);
REX_HOOK(D3DDevice_Clear, D3DDevice_Clear_hook);
REX_HOOK(D3DDevice_Swap, D3DDevice_Swap_hook);
REX_HOOK(bdBuildGammaRampLUT, bdBuildGammaRampLUT_hook);
REX_HOOK(D3DQuery_Issue, D3DQuery_Issue_hook);
REX_HOOK(D3DQuery_GetData, D3DQuery_GetData_hook);

BD_NOOP_RETURN(D3DDevice_ReleaseThreadOwnership, 0);

REX_STUB(D3DDevice_SetGammaRamp_1);
REX_STUB(D3DDevice_SetGammaRamp_0);
REX_STUB(D3DDevice_SetGammaRamp);
REX_STUB_RETURN(XGSurfaceSize, 0);
REX_STUB_RETURN(D3DDevice_SetRingBufferParameters, 0);
REX_STUB_RETURN(D3DDevice_RingBufferAlloc, 0);
REX_STUB_RETURN(D3DDevice_RingBufferFlush, 0);
REX_STUB_RETURN(D3DDevice_RingBufferSubmitBatch, 0);
REX_STUB_RETURN(D3DDevice_RingBufferWaitForSpace, 0);
REX_STUB_RETURN(D3DDevice_RingBufferPollReady, 1);
BD_NOOP(D3DDevice_SetShaderGPRAllocation);
REX_STUB(D3DDevice_BlockUntilIdle);
REX_STUB(D3D__ResetAllState);
REX_STUB(D3D__SetSurfaceInfo);
REX_STUB(D3DDevice_BeginZPass);
REX_STUB(D3DDevice_EndZPass);
REX_STUB(D3DDevice_SetScreenExtentQueryMode);
