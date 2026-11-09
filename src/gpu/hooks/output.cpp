/**
 * @file    gpu/hooks/output.cpp
 * @brief   Widen BD's projection to the window's aspect, and keep the 2D layer
 *          authored against the design canvas.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/output.h"

#include <cmath>

#include <rex/ppc/hook.h>
#include <rex/ppc/context.h>
#include <rex/types.h>
#include <rex/ppc/midasm.h>

#include "engine/camera_view.h"
#include "engine/engine.h"
#include "engine/free_dfs_task.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/settings.h"
#include "gpu/hooks/tweaks.h"

// The hook bodies below are global symbols the recompiler dispatch externs by
// name, so they use these names unqualified.
using bd::gpu::kDesignCanvasAspect;
using bd::gpu::kDesignCanvasAspectEpsilon;
using bd::gpu::kDesignCanvasHeight;
using bd::gpu::kDesignCanvasWidth;
using bd::gpu::Output;

namespace {

constexpr f32 kViewFitSlack = 8.0f;

// An authored sequence sizes its screen-covering effect quads to just span the
// fov the game frames itself at, so a wider frame leaves them short of the
// edges. Battle carries the summon and corporeal sequences, which run off the
// battle action steps rather than an .evt scene.
bool AuthoredFraming() {
  return bd::engine::IssEvent::LiveCount() > 0 ||
         static_cast<bool>(bd::engine::Game::Get().BattleCameraTask());
}

// The engine sees the design canvas, so a full-frame view is as wide as it. A
// width that already carries the scene rate is divided by it first.
bool IsFullFrameView(f64 width) {
  return width + kViewFitSlack >= kDesignCanvasWidth;
}

} // namespace

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

// The sub-view render rate is a supersampling cost cap: only the full-frame
// view takes the scene rate.
REX_MIDASM_HOOK(bdSubViewRenderScaleHook,
                (bd::engine::CameraView_t * view)) {
  if (!view || bd::gpu::Settings::Get().SuperSampling() <= 1 ||
      IsFullFrameView(static_cast<float>(view->renderSize[0])))
    return;
  view->rate = 1.0f;
}
REX_MIDASM_AT(bdSubViewRenderScaleHook, 0x8213BA7C, before, r31);

// This site takes its aspect from the view's own width over height, which for
// the full-frame view is already the render rect's. It still gets the framing
// every other full-frame camera does, so it is handed in as the design ratio. A
// sub-view is drawn into a rect of its own ratio and keeps it.
REX_MIDASM_HOOK(bdViewProjectionAspectHook,
                (const bd::engine::FreeDfsTask_t *task, PPCRegister &fov_half,
                 PPCRegister &aspect)) {
  if (!task)
    return;
  const f32 width = static_cast<float>(task->width);
  if (!IsFullFrameView(width / bd::gpu::SceneRenderScale()))
    return;
  aspect.f64 = kDesignCanvasAspect;
  bdProjectionAspectHook(fov_half, aspect);
}
REX_MIDASM_AT(bdViewProjectionAspectHook, 0x8228C17C, after, r31, f1, f30);

// The 2D overlay list is baked at the design canvas. Marking the drain lets the
// upload path fit each draw into the aspect-correct sub-rect.
REX_DETOUR(bdRenderSubmitList, void, (u32 list)) {
  bd::gpu::Video::SetDesignCanvasDrain(true);
  original(list);
  bd::gpu::Video::SetDesignCanvasDrain(false);
}
