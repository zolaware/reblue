/**
 * @file    gpu/hooks/output.cpp
 * @brief   Retarget BD's render chain from its fixed design canvas to the
 *          window's aspect-fit rectangle, and keep the 2D layer authored
 *          against that canvas.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/output.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

#include <rex/hook.h>
#include <rex/ppc/detour.h>
#include <rex/ppc.h>
#include <rex/types.h>
#include <rex/ppc/midasm.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/camera_view.h"
#include "engine/engine.h"
#include "engine/free_dfs_task.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/host_resource_heap.h"
#include "gpu/settings.h"
#include "gpu/hooks/tweaks.h"

// screenW/H on VisualRender is BD's master dim: the scene RT base, the
// post/bloom pyramid, the composite and the 2D basis all derive from it. The
// device backbuffer dims size only the front buffer and the post ping-pong.
// Output res sets all of them to one logical size, then neutralizes
// bdRenderStep's per-frame force back to 1280.

// The hook bodies below are global symbols the recompiler dispatch externs by
// name, so they use these names unqualified.
using bd::gpu::kDesignCanvasAspect;
using bd::gpu::kDesignCanvasAspectEpsilon;
using bd::gpu::kDesignCanvasHeight;
using bd::gpu::kDesignCanvasWidth;
using bd::gpu::Output;

namespace {

constexpr f32 kViewFitSlack = 8.0f;

// SAFE/RATE under the Mindows RENDER>DEBUG tree, the f32 scale of the guide box
// the renderer draws when SAFE/DISP is on. Stock 0.9 is the CRT overscan margin,
// which hides nothing on a display that shows the whole frame.
constexpr f32 kSafeAreaRate = 0.99f;

// Guest globals the output res hooks rewrite.
constexpr u32 kDeviceBackBufferWEA = 0x82DDA670;
constexpr u32 kDeviceBackBufferHEA = 0x82DDA674;
constexpr u32 kDisplayFloatDimsEA = 0x82DDA5E8; // {width, height} f32 pair
constexpr u32 kViewportWidthEA = 0x82DE8918;
constexpr u32 kViewportHeightEA = 0x82DE891C;

constexpr u32 kVisualRenderScreens[] = {0x1A28, 0x1B14, 0x1B1C};

constexpr u32 kCompositeScreenEA = 0x82DC98D0;

constexpr u32 kScreenFormat = 0x28280106;
constexpr u32 kCompositeFormat = 0x182801B6;

struct RenderTargetNode_t {
  /* 0x00 */ u8 _pad000[0x08];
  /* 0x08 */ be_u32 next;
  /* 0x0C */ be_u32 owner;
};
static_assert(offsetof(RenderTargetNode_t, next) == 0x08);
static_assert(offsetof(RenderTargetNode_t, owner) == 0x0C);

struct RenderTargetOwner_t {
  /* 0x00 */ u8 _pad000[0x04];
  /* 0x04 */ be_u32 texture;
};
static_assert(offsetof(RenderTargetOwner_t, texture) == 0x04);

// An authored sequence sizes its screen-covering effect quads to just span the
// fov the game frames itself at, so a wider frame leaves them short of the
// edges. Battle carries the summon and corporeal sequences, which run off the
// battle action steps rather than an .evt scene.
bool AuthoredFraming() {
  return bd::engine::IssEvent::LiveCount() > 0 ||
         static_cast<bool>(bd::engine::Game::Get().BattleCameraTask());
}

void WriteGuestOutputDims(u32 w, u32 h) {
  bd::mem::store<u32>(kDeviceBackBufferWEA, w);
  bd::mem::store<u32>(kDeviceBackBufferHEA, h);
  bd::mem::store<float>(kDisplayFloatDimsEA, static_cast<float>(w));
  bd::mem::store<float>(kDisplayFloatDimsEA + 4, static_cast<float>(h));
}

bool ScaleDesignDims(f64 &w, f64 &h) {
  u32 fit_w, fit_h;
  if (!Output::EngineSize(fit_w, fit_h))
    return false;
  if (w > kDesignCanvasWidth || h > kDesignCanvasHeight)
    return false;
  if (w == kDesignCanvasWidth && h == kDesignCanvasHeight &&
      Output::EngineSize(fit_w, fit_h)) {
    w = fit_w;
    h = fit_h;
    return true;
  }
  const f64 s = Output::RenderDensity();
  if (s <= 1.0)
    return false;
  w *= s;
  h *= s;
  return true;
}

bool IsFullFrameView(f64 width) {
  u32 fit_w, fit_h;
  if (!Output::LogicalSize(fit_w, fit_h))
    fit_w = static_cast<u32>(kDesignCanvasWidth);
  return width + kViewFitSlack >= fit_w;
}

} // namespace

// Right after the ctor seeds screenW/H, so every buffer derived from them is
// created after this point.
REX_MIDASM_HOOK(bdOutputResScreenDimsHook, (PPCRegister &r31)) {
  u32 w, h;
  if (!Output::EngineSize(w, h))
    return;
  bd::engine::VisualRender render(r31.u32);
  render.SetScreenW(static_cast<f32>(w));
  render.SetScreenH(static_cast<f32>(h));
}
REX_MIDASM_AT(bdOutputResScreenDimsHook, 0x82133174, after, r31);

// The bdInitGpuMemory tail, after the device dims and the float pair are
// written. Overwriting all three sizes the front buffer, the post ping-pong and
// the resolve source rect together.
REX_MIDASM_HOOK(bdOutputResDeviceDimsHook, ()) {
  u32 w, h;
  if (!Output::EngineSize(w, h))
    return;
  WriteGuestOutputDims(w, h);
  REXLOG_INFO(bd, "output-res BD render dims -> {}x{} (swapchain {}x{})", w, h,
          bd::gpu::Video::OutputWidth(), bd::gpu::Video::OutputHeight());
}
REX_MIDASM_AT(bdOutputResDeviceDimsHook, 0x82272E38, before);

REX_MIDASM_BRANCH(bdOutputResRenderStepNeutralizeHook, ()) {
  u32 w, h;
  return Output::EngineSize(w, h);
}
REX_MIDASM_AT(bdOutputResRenderStepNeutralizeHook, 0x82132D28, before, 0x82132D60);

// r3/r4 are the hardcoded 1280x720 the ctor creates its composite/history
// texture at. It is the scene composite resolve target, so scaling it to the
// output dims keeps that resolve 1:1 against the source rect.
REX_MIDASM_HOOK(bdOutputResCompositeTexScaleHook, (PPCRegister &r3, PPCRegister &r4)) {
  u32 w, h;
  if (!Output::EngineSize(w, h))
    return;
  r3.u32 = w;
  r4.u32 = h;
}
REX_MIDASM_AT(bdOutputResCompositeTexScaleHook, 0x82133408, before, r3, r4);

REX_IMPORT(__imp__bdCreateDynamicTexture, CreateDynamicTexture,
           u32(u32, u32, u32, u32, u32));
REX_IMPORT(__imp__bdCameraViewInit, CameraViewInit, void(u32, u32));

namespace {

void ResizeStaleRenderTargets(u32 node, u32 w, u32 h) {
  if (!w || !h)
    return;
  for (u32 guard = 0; node && guard < 256; ++guard) {
    const auto *entry = bd::mem::at<RenderTargetNode_t>(node);
    if (!entry)
      return;
    const u32 owner = entry->owner;
    const auto *ownerEntry =
        owner ? bd::mem::at<RenderTargetOwner_t>(owner) : nullptr;
    if (ownerEntry) {
      const auto *texture =
          bd::gpu::HostResourceHeap::FromGuest<bd::gpu::GuestTexture>(
              u32(ownerEntry->texture));
      if (texture && (texture->width != w || texture->height != h))
        CreateDynamicTexture(owner, w, h, 1, kScreenFormat);
    }
    node = entry->next;
  }
}

void RebuildForOutputSize(u32 w, u32 h) {
  const u32 render = bd::engine::VisualRender::Get().Address();
  if (!render)
    return;

  WriteGuestOutputDims(w, h);
  for (const u32 screen : kVisualRenderScreens)
    CreateDynamicTexture(render + screen, w, h, 1, kScreenFormat);
  CreateDynamicTexture(kCompositeScreenEA, w, h, 1, kCompositeFormat);

  CameraViewInit(render, 0);
  REXLOG_INFO(bd, "output-res engine surfaces rebuilt at {}x{}", w, h);
}

} // namespace

REX_DETOUR(bdCreateRenderTargetTextures, void,
           (u32 head, f64 width, f64 height)) {
  u32 w = 0;
  u32 h = 0;
  if (Output::EngineSize(w, h)) {
    const u32 want_w = (static_cast<u32>(width) + 7) & ~7u;
    const u32 want_h = (static_cast<u32>(height) + 7) & ~7u;

    static u32 applied = Output::Generation();
    const u32 generation = Output::Generation();
    if (generation != applied) {
      applied = generation;
      RebuildForOutputSize(w, h);
    }
    ResizeStaleRenderTargets(head, want_w, want_h);
  }
  original(head, width, height);
}

// The projection aspect is a camera field seeded with a literal 16:9, never
// derived from the screen, so widening the render target alone stretches the
// image. The composed ratio holds the vertical view and opens the horizontal,
// and going narrower holds the horizontal instead rather than crop what the
// game was framed for. Cameras carrying an aspect keep it. bd_fov_offset
// applies here too, since scaling the half-angle's tangent changes the term,
// and drops out under AuthoredFraming.
//
// BD open-codes this matrix in three places, all hooked here. Patching the
// register rather than the camera leaves the globals BD reads back for its
// own fov conversions at the ratio those were written for.
REX_MIDASM_HOOK(bdProjectionAspectHook, (PPCRegister &fov_half, PPCRegister &aspect)) {
  if (std::fabs(aspect.f64 - kDesignCanvasAspect) > kDesignCanvasAspectEpsilon)
    return;

  double tan_scale =
      AuthoredFraming() ? 1.0 : bd::gpu::Settings::Get().FOVTanScale();
  const double target = bd::gpu::Output::ProjectionAspect();
  if (std::fabs(target - kDesignCanvasAspect) > kDesignCanvasAspectEpsilon) {
    aspect.f64 = target;
    if (target < kDesignCanvasAspect)
      tan_scale *= kDesignCanvasAspect / target;
  }
  // Left exactly alone when nothing asked for a change, rather than rebuilt
  // through atan(tan(x)) and moved by the float residue.
  if (tan_scale != 1.0)
    fov_half.f64 = std::atan(std::tan(fov_half.f64) * tan_scale);
}
REX_MIDASM_AT(bdProjectionAspectHook, 0x82168E78, after, f1, f29);
REX_MIDASM_AT(bdProjectionAspectHook, 0x822781E4, after, f1, f29);
REX_MIDASM_AT(bdProjectionAspectHook, 0x821782F4, after, f1, f29);

REX_MIDASM_HOOK(bdOutputResViewScaleHook, (PPCRegister &w, PPCRegister &h)) {
  ScaleDesignDims(w.f64, h.f64);
}
REX_MIDASM_AT(bdOutputResViewScaleHook, 0x82134E34, before, f1, f2);
REX_MIDASM_AT(bdOutputResViewScaleHook, 0x82134E74, before, f31, f30);

REX_MIDASM_HOOK(bdSubViewRenderScaleHook, (PPCRegister &r31)) {
  u32 fit_w, fit_h;
  auto *view = bd::mem::at<bd::engine::CameraView_t>(r31.u32);
  if (!view || !Output::LogicalSize(fit_w, fit_h) ||
      IsFullFrameView(static_cast<float>(view->renderSize[0])))
    return;
  view->rate = 1.0f;
}
REX_MIDASM_AT(bdSubViewRenderScaleHook, 0x8213BA7C, before, r31);

REX_MIDASM_HOOK(bdFreeDfsViewTextureSizeHook, (PPCRegister &r11)) {
  auto *view = bd::mem::at<bd::engine::CameraView_t>(r11.u32);
  if (!view)
    return;
  f64 w = static_cast<float>(view->texSize[0]);
  f64 h = static_cast<float>(view->texSize[1]);
  if (!ScaleDesignDims(w, h))
    return;
  view->texSize[0] = static_cast<float>(w);
  view->texSize[1] = static_cast<float>(h);
}
REX_MIDASM_AT(bdFreeDfsViewTextureSizeHook, 0x8228C084, after, r11);

REX_MIDASM_HOOK(bdIssEventDimHook, (PPCRegister &r10, PPCRegister &r11)) {
  f64 w = r11.u32;
  f64 h = r10.u32;
  if (!ScaleDesignDims(w, h))
    return;
  r11.u32 = static_cast<u32>(w);
  r10.u32 = static_cast<u32>(h);
}
REX_MIDASM_AT(bdIssEventDimHook, 0x823F76E4, after, r10, r11);

// This site takes its aspect from the view's own width over height, which for
// the full-frame view is already the render rect's. It still gets the framing
// every other full-frame camera does, so it is handed in as the design ratio. A
// sub-view is drawn into a rect of its own ratio and keeps it.
REX_MIDASM_HOOK(bdViewProjectionAspectHook, (PPCRegister &r31, PPCRegister &fov_half, PPCRegister &aspect)) {
  const auto *task = bd::mem::at<bd::engine::FreeDfsTask_t>(r31.u32);
  if (!task)
    return;
  const f32 width = static_cast<float>(task->width);
  if (!IsFullFrameView(width / bd::gpu::SceneRenderScale()))
    return;
  aspect.f64 = kDesignCanvasAspect;
  bdProjectionAspectHook(fov_half, aspect);
}
REX_MIDASM_AT(bdViewProjectionAspectHook, 0x8228C17C, after, r31, f1, f30);

// The seed hook above let the ctor build its RTs at the output dims, so restore
// the design canvas on exit. screenW/H stays there from now on, and the
// renderer takes the output dims from the per-site register patches below.
REX_DETOUR(VisualRender__ctor, u32, (u32 self)) {
  const u32 result = original(self);
  if (!self)
    return result;
  bd::engine::VisualRender render(self);
  render.SetSafeRate(kSafeAreaRate);
  u32 w, h;
  if (!Output::EngineSize(w, h))
    return result;
  render.SetScreenW(kDesignCanvasWidth);
  render.SetScreenH(kDesignCanvasHeight);
  return result;
}

// The patch sites cover every read that wants the output dims, so the struct
// itself stays at the design canvas for the main thread UI layout reads. One
// wrapper per (axis, register) pair the sites load into.
namespace {
void OutputResPatchDim(PPCRegister &fr, bool height) {
  u32 w, h;
  if (!Output::EngineSize(w, h))
    return;
  fr.f64 = static_cast<double>(height ? h : w);
}
} // namespace
REX_MIDASM_HOOK(bdOutputResScreenWf0Hook, (PPCRegister &f0)) { OutputResPatchDim(f0, false); }
REX_MIDASM_AT(bdOutputResScreenWf0Hook, 0x821359E0, after, f0);
REX_MIDASM_AT(bdOutputResScreenWf0Hook, 0x82135E14, after, f0);
REX_MIDASM_AT(bdOutputResScreenWf0Hook, 0x82136120, after, f0);
REX_MIDASM_HOOK(bdOutputResScreenHf0Hook, (PPCRegister &f0)) { OutputResPatchDim(f0, true); }
REX_MIDASM_AT(bdOutputResScreenHf0Hook, 0x821359CC, after, f0);
REX_MIDASM_AT(bdOutputResScreenHf0Hook, 0x82135E90, after, f0);
REX_MIDASM_HOOK(bdOutputResScreenWf1Hook, (PPCRegister &f1)) { OutputResPatchDim(f1, false); }
REX_MIDASM_AT(bdOutputResScreenWf1Hook, 0x821341F0, after, f1);
REX_MIDASM_HOOK(bdOutputResScreenHf2Hook, (PPCRegister &f2)) { OutputResPatchDim(f2, true); }
REX_MIDASM_AT(bdOutputResScreenHf2Hook, 0x821341E4, after, f2);
REX_MIDASM_HOOK(bdOutputResScreenHf8Hook, (PPCRegister &f8)) { OutputResPatchDim(f8, true); }
REX_MIDASM_AT(bdOutputResScreenHf8Hook, 0x82136178, after, f8);
REX_MIDASM_HOOK(bdOutputResScreenWf9Hook, (PPCRegister &f9)) { OutputResPatchDim(f9, false); }
REX_MIDASM_AT(bdOutputResScreenWf9Hook, 0x82134F74, after, f9);
REX_MIDASM_HOOK(bdOutputResScreenWf11Hook, (PPCRegister &f11)) {
  OutputResPatchDim(f11, false);
}
REX_MIDASM_AT(bdOutputResScreenWf11Hook, 0x82134F94, after, f11);
REX_MIDASM_HOOK(bdOutputResScreenHf12Hook, (PPCRegister &f12)) {
  OutputResPatchDim(f12, true);
}
REX_MIDASM_AT(bdOutputResScreenHf12Hook, 0x82134F70, after, f12);
REX_MIDASM_AT(bdOutputResScreenHf12Hook, 0x82134F90, after, f12);

// These map NDC to pixels using the view's screen dims, which the patches above
// filled with the output dims, but every caller reads the result as
// design canvas coordinates. Pin the dim loads back and leave the struct alone.
REX_MIDASM_HOOK(bdWorldToScreenDesignWf3Hook, (PPCRegister &f3)) {
  u32 w, h;
  if (Output::EngineSize(w, h))
    f3.f64 = kDesignCanvasWidth;
}
REX_MIDASM_AT(bdWorldToScreenDesignWf3Hook, 0x821354D0, after, f3);
REX_MIDASM_AT(bdWorldToScreenDesignWf3Hook, 0x821356EC, after, f3);
REX_MIDASM_AT(bdWorldToScreenDesignWf3Hook, 0x8213578C, after, f3);
REX_MIDASM_HOOK(bdWorldToScreenDesignHf4Hook, (PPCRegister &f4)) {
  u32 w, h;
  if (Output::EngineSize(w, h))
    f4.f64 = kDesignCanvasHeight;
}
REX_MIDASM_AT(bdWorldToScreenDesignHf4Hook, 0x821354C8, after, f4);
REX_MIDASM_AT(bdWorldToScreenDesignHf4Hook, 0x821356E4, after, f4);
REX_MIDASM_AT(bdWorldToScreenDesignHf4Hook, 0x82135784, after, f4);

// The pinned design canvas basis suits every quad authored against it, but the
// camera composite background quad is authored at the camera target texture
// size, which output res enlarged: under the pinned viewport constant only its
// top-left quarter shows. Walk the command nodes before the drain and rescale
// any quad whose extent matches the output dims.
struct Bd2DCommandNode {
  be_u32 pad_00[2]; // +0x00
  be_u32 next;      // +0x08
  be_u32 pad_0C[2]; // +0x0C
  be_u32 vertCount; // +0x14
  be_u32 vertData;  // +0x18  inline vertex payload VA (0 for text nodes)
  be_u32 stride;    // +0x1C
};
static_assert(offsetof(Bd2DCommandNode, next) == 0x08);
static_assert(offsetof(Bd2DCommandNode, vertCount) == 0x14);
static_assert(offsetof(Bd2DCommandNode, vertData) == 0x18);
static_assert(offsetof(Bd2DCommandNode, stride) == 0x1C);
struct Bd2DVertex {
  be_f32 x;
  be_f32 y;
}; // first 8 of the 0x14 stride

namespace {
// Authored extents this far apart still count as the same edge.
constexpr float kEdgeTolerance = 8.0f;

bool MaxMatches(float max_x, float max_y, double w, double h) {
  return std::fabs(max_x - static_cast<float>(w)) <= kEdgeTolerance &&
         std::fabs(max_y - static_cast<float>(h)) <= kEdgeTolerance;
}

void RenormalizeSizedQuads(u32 node, u32 out_w, u32 out_h) {
  for (int guard = 0; node && guard < 4096; ++guard) {
    const auto *n = bd::mem::at<const Bd2DCommandNode>(node);
    if (!n) {
      REXLOG_WARN(bd, "RenormalizeSizedQuads: unmapped 2D node VA 0x{:08X}, "
              "stopping "
              "walk",
              node);
      break;
    }
    const u32 count = n->vertCount;
    const u32 data = n->vertData;
    const u32 stride = n->stride;
    if (data && stride == 0x14 && count >= 3 && count <= 64) {
      float max_x = -999999.0f, max_y = -999999.0f;
      float min_x = 999999.0f, min_y = 999999.0f;
      for (u32 i = 0; i < count; ++i) {
        const auto *v = bd::mem::at<const Bd2DVertex>(data + i * stride);
        min_x = std::min(min_x, static_cast<float>(v->x));
        min_y = std::min(min_y, static_cast<float>(v->y));
        max_x = std::max(max_x, static_cast<float>(v->x));
        max_y = std::max(max_y, static_cast<float>(v->y));
      }
      const bool spans_surface = std::fabs(min_x) <= kEdgeTolerance &&
                                 std::fabs(min_y) <= kEdgeTolerance &&
                                 MaxMatches(max_x, max_y, out_w, out_h);
      if (spans_surface) {
        // Onto the canvas the pinned basis expects, flush to its edges, so
        // the drain's per-draw fit reads it as a backdrop.
        for (u32 i = 0; i < count; ++i) {
          auto *v = bd::mem::at<Bd2DVertex>(data + i * stride);
          v->x = kDesignCanvasWidth * (static_cast<float>(v->x) / max_x);
          v->y = kDesignCanvasHeight * (static_cast<float>(v->y) / max_y);
        }
      }
    }
    node = n->next;
  }
}
} // namespace

// The 2D overlay list is baked at the design canvas but drained after
// bdRenderFrame has set the viewport globals to the output res surface size.
// Pin the canvas basis for the drain, so text, HUD graphics and the background
// quad fill the surface as one unit, and restore after. Marking the drain rides
// along: under the pinned basis every draw inside it is in canvas coordinates,
// so the upload path can fit each one.
REX_DETOUR(bdRenderSubmitList, void, (u32 list)) {
  u32 w, h;
  auto *vw = bd::mem::at<be_f32>(kViewportWidthEA);
  auto *vh = bd::mem::at<be_f32>(kViewportHeightEA);
  const bool drain = Output::LogicalSize(w, h);
  const bool pin = Output::EngineSize(w, h) && vw && vh;

  float saved_w = 0.0f, saved_h = 0.0f;
  if (pin) {
    saved_w = *vw;
    saved_h = *vh;
    RenormalizeSizedQuads(list, w, h);
    *vw = kDesignCanvasWidth;
    *vh = kDesignCanvasHeight;
  }
  if (drain)
    bd::gpu::Video::SetDesignCanvasDrain(true);
  original(list);
  if (drain)
    bd::gpu::Video::SetDesignCanvasDrain(false);
  if (pin) {
    *vw = saved_w;
    *vh = saved_h;
  }
}
