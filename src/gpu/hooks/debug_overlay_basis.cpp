/**
 * @file    gpu/hooks/debug_overlay_basis.cpp
 * @brief   Pin BD's debug overlays to the design canvas while output res has
 *          the render chain on a larger surface.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/output.h"

#include <rex/types.h>

#include <rex/hook.h>

#include "core/memory_helpers.h"

namespace {

// Guest globals bdBuildDebugGlyphQuad (0x82272460) and bdDrawRectPrimitive read
// to map fixed-pixel coords to NDC: basis = mode ? viewport dims : the display
// float dims at 0x82DDA5E8.
constexpr u32 kViewportModeFlagEA = 0x82DE8914;
constexpr u32 kViewportWidthEA = 0x82DE8918;
constexpr u32 kViewportHeightEA = 0x82DE891C;

// Holds the design canvas basis for the duration of one guest call. Both hooks
// below draw fixed-pixel coordinates, and bdRenderFrame leaves the basis at the
// output surface dims, so above the design canvas the glyphs and the window
// chrome each shrink toward the top-left, and by different amounts, which
// drifts the Mindows boxes away from their own text. Nothing else inside either
// call reads these globals.
class DesignCanvasBasis {
public:
  explicit DesignCanvasBasis(bool active) : active_(active) {
    if (!active_)
      return;
    mode_ = bd::mem::at<be_u32>(kViewportModeFlagEA);
    width_ = bd::mem::at<be_f32>(kViewportWidthEA);
    height_ = bd::mem::at<be_f32>(kViewportHeightEA);
    active_ = mode_ && width_ && height_;
    if (!active_)
      return;
    saved_mode_ = *mode_;
    saved_width_ = *width_;
    saved_height_ = *height_;
    *mode_ = 1u;
    *width_ = bd::gpu::kDesignCanvasWidth;
    *height_ = bd::gpu::kDesignCanvasHeight;
  }

  ~DesignCanvasBasis() {
    if (!active_)
      return;
    *mode_ = saved_mode_;
    *width_ = saved_width_;
    *height_ = saved_height_;
  }

  DesignCanvasBasis(const DesignCanvasBasis &) = delete;
  DesignCanvasBasis &operator=(const DesignCanvasBasis &) = delete;

private:
  bool active_;
  be_u32 *mode_ = nullptr;
  be_f32 *width_ = nullptr;
  be_f32 *height_ = nullptr;
  u32 saved_mode_ = 0;
  float saved_width_ = 0.0f;
  float saved_height_ = 0.0f;
};

bool OutputResActive() {
  u32 w, h;
  return bd::gpu::Output::RenderSize(w, h);
}

} // namespace

// bdRenderDebugText: BD's immediate debug text renderer (the design
// viewer's 'TITLE: LOAD / TYPE / SELECT' / MODEL / MOTION menu, '1/26', etc.),
// called straight from bdRenderFrame, NOT through the bdRenderSubmitList drain.
// Its mode flag is 0 on this path, so the glyph builder divides by the display
// float dims, which bdOutputResDeviceDimsHook set to the output size.
REX_EXTERN(__imp__bdRenderDebugText);
REX_HOOK_RAW(bdRenderDebugText) {
  DesignCanvasBasis pin(OutputResActive());
  __imp__bdRenderDebugText(ctx, base);
}

// bdMindowsDraw: the Mindows debug overlay window chrome (panel,
// title bar, scrollbar filled rects, via bdDrawRectPrimitive). bdRenderFrame
// sets mode=1 with the viewport dims at the output surface size just before
// this call, while the overlay's text goes through the pinned hook above.
REX_EXTERN(__imp__bdMindowsDraw);
REX_HOOK_RAW(bdMindowsDraw) {
  DesignCanvasBasis pin(OutputResActive());
  __imp__bdMindowsDraw(ctx, base);
}
