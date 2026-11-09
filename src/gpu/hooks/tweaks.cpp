/**
 * @file    gpu/hooks/tweaks.cpp
 * @brief   Bodies for the cvar-gated rendering tweaks declared in
 *          config/hooks/render_tweaks.toml, which names each hook site and the
 *          registers it receives.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/hooks/tweaks.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <rex/hook.h>
#include <rex/ppc/detour.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/runtime/xtypes.h>
#include <rex/types.h>
#include <rex/ppc/midasm.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/engine.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/host_resource_heap.h"
#include "gpu/output.h"
#include "gpu/settings.h"
#include "gpu/surface_registry.h"

namespace {
constexpr u32 kScreenUVScaleReg = 50;
constexpr u32 kSsScatterBlurEA = 0x82DF4344;

constexpr u32 kBloomPoolSlotDims[] = {0x30A8, 0x30F4};
constexpr u32 kDOFPoolSlotDims[] = {0x268, 0x318};

bool PostQualityChanged(u32 owner) {
  static std::unordered_map<u32, bd::gpu::PostQuality> applied;
  const bd::gpu::PostQuality quality = bd::gpu::Settings::Get().PostQuality();
  auto &entry = applied.try_emplace(owner, quality).first->second;
  if (entry == quality)
    return false;
  entry = quality;
  return true;
}

void InvalidatePoolKeys(u32 owner, const u32 (&slots)[2]) {
  for (const u32 slot : slots) {
    bd::mem::store<u32>(owner + slot, 0);
    bd::mem::store<u32>(owner + slot + 4, 0);
  }
}

f64 BloomTargetScale() {
  switch (bd::gpu::Settings::Get().PostQuality()) {
  case bd::gpu::PostQuality::Low:
    return 0.5;
  case bd::gpu::PostQuality::Medium:
    return 1.0;
  case bd::gpu::PostQuality::High:
    break;
  }
  return 2.0;
}

f64 DOFIntermediateScale() {
  return bd::gpu::Settings::Get().PostQuality() == bd::gpu::PostQuality::Low
             ? 0.5
             : 1.0;
}

// The step is a constant over the target's logical dims, so a host scale of
// kx by ky spreads each tap over kx by ky host pixels. Off 16:9 that stretches
// the kernel by kx / ky, so the width is folded back to the height's scale.
void ScaleBlurTapStep(PPCRegister &w, PPCRegister &h, f64 grown) {
  f32 kx = 1.0f;
  f32 ky = 1.0f;
  bd::gpu::Output::HostScale(kx, ky);
  const f64 aspect = static_cast<f64>(kx) / static_cast<f64>(ky);
  const f64 s = bd::gpu::SceneRenderScale() * grown;
  if (s == 1.0 && aspect == 1.0)
    return;
  w.f64 = w.f64 * aspect / s;
  h.f64 /= s;
}

void ScaleTargetDims(PPCRegister &w, PPCRegister &h, f64 s) {
  if (s == 1.0)
    return;
  w.u32 = std::max(1u, static_cast<u32>(w.u32 * s));
  h.u32 = std::max(1u, static_cast<u32>(h.u32 * s));
}
} // namespace

namespace bd::gpu {

// Event scenes hold BD's authored coverage, so pin it to original for the
// duration of an .evt scene.
f64 ShadowCoverageScale() {
  return bd::engine::IssEvent::LiveCount() > 0
             ? 1.0
             : Settings::Get().ShadowDistance();
}

f32 SceneRenderScale() {
  const f32 rate = bd::engine::VisualRender::Get().RenderRate();
  return rate > 0.0f ? rate : 1.0f;
}

} // namespace bd::gpu

REX_MIDASM_BRANCH(bdSceneTilingSuppressHook, ()) { return true; }
REX_MIDASM_AT(bdSceneTilingSuppressHook, 0x82186C40, before, 0x82186C54);

REX_MIDASM_HOOK(bdSceneRenderScaleHook, (PPCRegister &r31)) {
  bd::engine::VisualRender render(r31.u32);
  const f32 rate = static_cast<f32>(bd::gpu::Settings::Get().SuperSampling());
  if (render.RenderRate() != rate)
    render.SetRenderRate(rate);
  render.SetFSAA(bd::gpu::Settings::Get().MSAA() > 0);
}
REX_MIDASM_AT(bdSceneRenderScaleHook, 0x8213307C, after, r31);
REX_MIDASM_AT(bdSceneRenderScaleHook, 0x8213597C, before, r31);

// SAFE/RATE under the Mindows RENDER>DEBUG tree, the f32 scale of the guide box
// the renderer draws when SAFE/DISP is on. Stock 0.9 is the CRT overscan margin,
// which hides nothing on a display that shows the whole frame.
constexpr f32 kSafeAreaRate = 0.99f;

REX_DETOUR(VisualRender__ctor, u32, (u32 self)) {
  const u32 result = original(self);
  if (self)
    bd::engine::VisualRender(self).SetSafeRate(kSafeAreaRate);
  return result;
}

REX_MIDASM_HOOK(bdReflectionResolutionScaleHook,
                (rex::MappedPtr<bd::gpu::PlaneReflectInfo> info)) {
  if (!info)
    return;

  const u32 stock = static_cast<u32>(info->width);
  u32 width = stock;

  const bd::gpu::ReflectionQuality quality =
      bd::gpu::Settings::Get().ReflectionQuality();
  if (quality != bd::gpu::ReflectionQuality::Low) {
    const f64 rate = quality == bd::gpu::ReflectionQuality::High
                         ? bd::gpu::SceneRenderScale()
                         : 1.0;
    const u32 scene_w =
        static_cast<u32>(bd::gpu::kDesignCanvasWidth * rate) & ~31u;
    const u32 scaled =
        std::min(static_cast<u32>(stock * rate + 0.5) & ~31u, scene_w);
    if (scaled > stock)
      width = scaled;
  }
  info->width = width;

  static std::unordered_map<u32, u32> forced;
  u32 &last = forced[info.GetGuestAddress()];
  if (last != width) {
    last = width;
    info->lastScale = -1.0f;
  }
}
REX_MIDASM_AT(bdReflectionResolutionScaleHook, 0x82178670, before, r31);

REX_MIDASM_HOOK(bdReflectionSurfaceTagHook, (PPCRegister &r3)) {
  auto *surface =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(r3.u32);
  if (surface)
    surface->reflection = true;
}
REX_MIDASM_AT(bdReflectionSurfaceTagHook, 0x821876A4, after, r3);
REX_MIDASM_AT(bdReflectionSurfaceTagHook, 0x821876E0, after, r3);

REX_MIDASM_HOOK(bdReflectionTextureSeedHook, (PPCRegister &r3)) {
  auto *texture =
      bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(r3.u32);
  if (!texture)
    return;
  texture->reflection = true;
  bd::gpu::Video::ClearTexture(texture, bd::gpu::kOpaqueBlackArgb);
}
REX_MIDASM_AT(bdReflectionTextureSeedHook, 0x82177CCC, after, r3);
REX_MIDASM_AT(bdReflectionTextureSeedHook, 0x821786D0, after, r3);

// The light frustum is world-space and receivers sample by UV, so a larger map
// is just finer with no shader change.
REX_MIDASM_HOOK(bdShadowResolutionScaleHook, (PPCRegister &r3, PPCRegister &r4)) {
  const u32 d = static_cast<u32>(bd::gpu::Settings::Get().ShadowDimension());
  r3.u32 = d;
  r4.u32 = d;
  bd::gpu::SurfaceRegistry::ExemptNext(d, d);
}
REX_MIDASM_AT(bdShadowResolutionScaleHook, 0x821871E4, before, r3, r4);
REX_MIDASM_AT(bdShadowResolutionScaleHook, 0x8217517C, before, r3, r4);

// The cube shadow depth surface and the second shadow map texture are created
// at sizes of their own.
REX_MIDASM_HOOK(bdShadowTargetExemptHook, (PPCRegister &r3, PPCRegister &r4)) {
  bd::gpu::SurfaceRegistry::ExemptNext(r3.u32, r4.u32);
}
REX_MIDASM_AT(bdShadowTargetExemptHook, 0x8218746C, before, r3, r4);
REX_MIDASM_AT(bdShadowTargetExemptHook, 0x821751C4, before, r3, r4);

REX_MIDASM_HOOK(bdShadowMapTextureResizeHook,
                (bd::gpu::ShadowMapInfo * info)) {
  if (!info)
    return;
  auto *texture = bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
      info->texture);
  if (!texture)
    return;
  const u32 d = static_cast<u32>(bd::gpu::Settings::Get().ShadowDimension());
  bd::gpu::Video::ResizeTexture(texture, d, d);
}
REX_MIDASM_AT(bdShadowMapTextureResizeHook, 0x82175308, before, r31);

// f1 is the sun frustum's coverage scale, and BD's own curve saturates at a
// half-extent of ~512 world units, dropping distant casters laterally. Scaling
// it widens the fov of the virtual sun eye. 1.0 leaves BD's coverage alone.
REX_MIDASM_HOOK(bdShadowCoverageScaleHook, (PPCRegister &f1)) {
  const f64 cov = bd::gpu::ShadowCoverageScale();
  if (cov != 1.0)
    f1.f64 *= cov;
}
REX_MIDASM_AT(bdShadowCoverageScaleHook, 0x82175974, before, f1);

// The composite blur scale c27.y, stored at 4(r11) just above. The composite
// squares it to size the circle of confusion it picks a blur LOD with, so the
// square root of the wanted intensity is what makes half the setting read as
// half the blur.
REX_MIDASM_HOOK(bdDOFStrengthScaleHook, (be_f32 * constants)) {
  const f64 strength = bd::gpu::Settings::Get().DOFStrength();
  if (strength >= 1.0)
    return;
  if (!constants)
    return;
  be_f32 *y = constants + 1;
  *y = static_cast<f32>(static_cast<f32>(*y) * std::sqrt(strength));
}
REX_MIDASM_AT(bdDOFStrengthScaleHook, 0x82217224, after, r11);

REX_MIDASM_HOOK(bdGaussianBlurTapStepHook, (PPCRegister &f0, PPCRegister &f13, PPCRegister &r28)) {
  if (r28.u32 != kSsScatterBlurEA)
    ScaleBlurTapStep(f0, f13, DOFIntermediateScale());
}
REX_MIDASM_AT(bdGaussianBlurTapStepHook, 0x8221515C, before, f0, f13, r28);

REX_MIDASM_HOOK(bdBloomBlurTapStepHook, (PPCRegister &f0, PPCRegister &f13)) {
  ScaleBlurTapStep(f0, f13, BloomTargetScale());
}
REX_MIDASM_AT(bdBloomBlurTapStepHook, 0x82215B28, before, f0, f13);

REX_MIDASM_HOOK(bdBloomTargetSizeHook, (PPCRegister &r4, PPCRegister &r5)) {
  ScaleTargetDims(r4, r5, BloomTargetScale());
}
REX_MIDASM_AT(bdBloomTargetSizeHook, 0x8221B360, after, r4, r5);
REX_MIDASM_AT(bdBloomTargetSizeHook, 0x8221B38C, after, r4, r5);

REX_MIDASM_HOOK(bdBloomPoolInvalidateHook, (PPCRegister &r31)) {
  if (PostQualityChanged(r31.u32))
    InvalidatePoolKeys(r31.u32, kBloomPoolSlotDims);
}
REX_MIDASM_AT(bdBloomPoolInvalidateHook, 0x8221B238, before, r31);

REX_MIDASM_HOOK(bdDOFPoolInvalidateHook, (PPCRegister &r31)) {
  if (PostQualityChanged(r31.u32))
    InvalidatePoolKeys(r31.u32, kDOFPoolSlotDims);
}
REX_MIDASM_AT(bdDOFPoolInvalidateHook, 0x82217130, before, r31);

REX_MIDASM_HOOK(bdDOFIntermediateScaleHook, (PPCRegister &r28, PPCRegister &r26)) {
  ScaleTargetDims(r28, r26, DOFIntermediateScale());
}
REX_MIDASM_AT(bdDOFIntermediateScaleHook, 0x82217600, before, r28, r26);

// r3 is a shader constant flush descriptor: flags @0 (bit1 = pixel shader
// flush), startReg @4, endReg @8, pData @0xC. The fountain water object authors
// c51.w = 1000 and bd_water_ps computes color = 2*c51.w*lit, so a glint reaches
// ~5748 HDR. 1.0 matches the milder sibling water object.
REX_MIDASM_HOOK(bdWaterSpecIntensityClampHook, (const be_u32 *d)) {
  constexpr float ceiling = 1.0f;
  if (!d)
    return;
  if ((static_cast<u32>(d[0]) & 2) == 0)
    return; // pixel-shader flush only
  const u32 startReg = d[1];
  const u32 endReg = d[2];
  if (startReg > 51 || endReg <= 51)
    return; // must cover c51 (g_vWaveParams1)
  const u32 pData = d[3];
  if (!pData)
    return;
  auto *w = bd::mem::at<be_f32>(pData + (51 - startReg) * 16 + 12);
  if (w && static_cast<float>(*w) > static_cast<float>(ceiling)) {
    *w = static_cast<float>(ceiling);
  }
}
REX_MIDASM_AT(bdWaterSpecIntensityClampHook, 0x824547B0, before, r3);
REX_MIDASM_AT(bdWaterSpecIntensityClampHook, 0x824547B8, before, r3);

// The NDC->UV half-scale is always 0.5, but the guest derives it as
// sceneRT.dim/1280x720*0.5, so every resolution setting leaks into screen-space
// FX. Pixel stage only: Toon/Fur/caustics/cloud author VS reg50 as their own
// per-object data. The hook runs right before the writer ORs its group into
// m_Mask[1], so the pin rides the same upload.
REX_MIDASM_HOOK(bdCameraRefractionUvScaleHook, (bd::gpu::D3DDevice * dev)) {
  if (!dev)
    return;
  be_f32 *ps = dev->psFloatConstants[kScreenUVScaleReg];
  ps[0] = 0.5f;
  ps[1] = 0.5f;
}
REX_MIDASM_AT(bdCameraRefractionUvScaleHook, 0x82142FAC, before, r11);
REX_MIDASM_AT(bdCameraRefractionUvScaleHook, 0x8213d034, before, r11);

// The motion blur PS clamps its sample UV to [0.01, 0.99], an inset that hid
// under X360 TV overscan. Its UV is the quad's own position attribute, so the
// inset cannot be applied independently of coverage, so +/-0.98 (= 2*0.99-1)
// puts the visible edge exactly on the clamp bound. r5 is the 4-vertex buffer,
// pos xy at +0/+4, stride 0x28.
REX_MIDASM_HOOK(bdMotionBlurQuadInsetHook, (be_f32 * vertices)) {
  if (!vertices)
    return;
  constexpr u32 kVertexFloats = 0x28 / sizeof(f32);
  for (u32 i = 0; i < 4; ++i) {
    be_f32 *v = vertices + i * kVertexFloats;
    v[0] = static_cast<float>(v[0]) * 0.98f;
    v[1] = static_cast<float>(v[1]) * 0.98f;
  }
}
REX_MIDASM_AT(bdMotionBlurQuadInsetHook, 0x8213d18c, before, r5);

// The engine drops a 2D prim with no trace when the Visual::Tag frame pool has
// under 0x25A00 bytes free, which is indistinguishable from a task never
// submitting one. f31 is the prim's z.
REX_MIDASM_HOOK(bdPrimBeginDropWarnHook, (PPCRegister &f31)) {
  static u32 dropCount = 0;
  ++dropCount;
  if (dropCount <= 16 || (dropCount & 0x3FF) == 0)
    REXLOG_WARN(bd, "bdPrimBegin: 2D prim dropped (z={}, {} total this session)",
            f31.f64, dropCount);
}
REX_MIDASM_AT(bdPrimBeginDropWarnHook, 0x82146478, before, f31);

// All 300 NTSC blocks across BD's shipped db_posteffect records are Enable 0,
// so the only place the scanline offset fires is the Battle Viewer.
REX_MIDASM_BRANCH(bdNtscFilterNoiseDisableHook, ()) {
  return !bd::gpu::Settings::Get().NTSCFilter();
}
REX_MIDASM_AT(bdNtscFilterNoiseDisableHook, 0x8221914C, before, 0x822191A0);
