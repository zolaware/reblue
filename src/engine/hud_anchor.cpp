/**
 * @file    engine/hud_anchor.cpp
 * @brief   Pin the few HUD elements that belong in a screen corner to the
 *          render surface rather than to BD's 2D design canvas.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/memory_helpers.h"
#include "engine/d2anime/d2anime_task.h"
#include "engine/loader.h"
#include "gpu/gpu.h"

#include <cstddef>

#include <rex/ppc.h>
#include <rex/types.h>

// The 2D layer is scaled about the canvas center to fit a render rect of
// another ratio, which floats a corner element inward by the width of the
// letterbox. Moving one out past the canvas first, by what that scale will take
// back off, puts it on the surface edge. Zero overscan at 16:9, so every
// coordinate below is untouched there.

namespace {

void AnchorBottomRight(PPCRegister &x, PPCRegister &y) {
  x.f64 += bd::gpu::Output::DesignOverscanX();
  y.f64 += bd::gpu::Output::DesignOverscanY();
}

struct BdPrim2DVertex {
  be_f32 x;
  be_f32 y;
  be_f32 u;
  be_f32 v;
  be_u32 color;
};
static_assert(sizeof(BdPrim2DVertex) == 0x14);
static_assert(offsetof(BdPrim2DVertex, u) == 0x08);
static_assert(offsetof(BdPrim2DVertex, color) == 0x10);

// FreeDfsTask's close-up view holds the window as four points, read by the loop
// that copies them into the camera view and by the one that miters the frame.
constexpr u32 kCloseUpView_Outline = 0x1E8;

struct CloseUpOutline {
  BdPrim2DVertex points[4];
};

// Extreme points per axis, uv included: a bounding box would not do, since uv
// can run either way against position and the pairing is what says which.
struct OutlineAxis {
  float near_pos, near_uv, far_pos, far_uv;
};

struct Outline {
  OutlineAxis x, y;
};

void Widen(OutlineAxis &a, float pos, float uv) {
  if (pos < a.near_pos) {
    a.near_pos = pos;
    a.near_uv = uv;
  } else if (pos > a.far_pos) {
    a.far_pos = pos;
    a.far_uv = uv;
  }
}

bool ReadOutline(u32 view, Outline &out) {
  const auto *o = bd::mem::at<const CloseUpOutline>(view + kCloseUpView_Outline);
  if (!o)
    return false;
  const auto &first = o->points[0];
  out.x.near_pos = out.x.far_pos = first.x;
  out.x.near_uv = out.x.far_uv = first.u;
  out.y.near_pos = out.y.far_pos = first.y;
  out.y.near_uv = out.y.far_uv = first.v;
  for (const auto &p : o->points) {
    Widen(out.x, p.x, p.u);
    Widen(out.y, p.y, p.v);
  }
  return true;
}

// The close-up is shared: the battle portrait is a window in a corner, the
// summon's cinematic the same points zoomed past the screen. Judged over the
// whole outline, since a point-at-a-time test keeps the cinematic's two corners
// at the canvas origin and drops the rest, pulling the quad apart.
bool IsWindowed(const Outline &o) {
  return o.x.near_pos >= 0.0f && o.x.far_pos <= bd::gpu::kDesignCanvasWidth &&
         o.y.near_pos >= 0.0f && o.y.far_pos <= bd::gpu::kDesignCanvasHeight;
}

// bd_gamedat/fix/cam_fix.csv authors the window twice, one wedge against the
// canvas' left edge and its mirror against the right, and the battle picks by
// the side the actor stands on. Anchoring both to the top-left carries the
// mirrored one out of its corner and leaves it floating mid-screen, so take the
// direction per axis from the edge the outline was authored against.
void AnchorOutwards(const Outline &o, PPCRegister &x, PPCRegister &y) {
  const double over_x = bd::gpu::Output::DesignOverscanX();
  const double over_y = bd::gpu::Output::DesignOverscanY();
  x.f64 += bd::gpu::kDesignCanvasWidth - o.x.far_pos < o.x.near_pos ? over_x
                                                                    : -over_x;
  y.f64 += bd::gpu::kDesignCanvasHeight - o.y.far_pos < o.y.near_pos ? over_y
                                                                     : -over_y;
}

// The cinematic overruns the screen and lets the edge cut it: at the canvas
// border its uv is exactly 1. A wider render rect makes that overrun picture,
// which with nothing left to sample repeats the source's last row across it, so
// cut where the console's edge stood and take uv along.
void CropAxis(const OutlineAxis &a, float limit, double &pos, double &uv) {
  const double cropped = pos < 0.0 ? 0.0 : (pos > limit ? limit : pos);
  if (cropped == pos || a.far_pos == a.near_pos)
    return;
  uv = a.near_uv +
       (cropped - a.near_pos) * (a.far_uv - a.near_uv) / (a.far_pos - a.near_pos);
  pos = cropped;
}

// The wheel's authored corner, from bd_system/loader/Nowloading.csv. Written
// back absolute every frame, so a repeat is not a drift.
constexpr double kNowLoadingX = 80.0;
constexpr double kNowLoadingY = 580.0;

} // namespace

// The field compass in the canvas' bottom-right corner: needle and target arm
// about their shared center, dial from its top-left corner. All three move by
// the same amount, so they stay one piece. f1/f2 are the copy already staged
// for the first draw, f28/f29 the pair the second reads.
void bdCompassNeedleAnchorHook(PPCRegister &f1, PPCRegister &f2,
                               PPCRegister &f28, PPCRegister &f29) {
  AnchorBottomRight(f1, f2);
  AnchorBottomRight(f28, f29);
}

void bdCompassDialAnchorHook(PPCRegister &f1, PPCRegister &f2) {
  AnchorBottomRight(f1, f2);
}

// The dungeon minimap, a second widget in the same corner: disc, frame and
// markers all placed off one center. f25/f26 hold it for the whole draw, f1/f2
// are the copy already staged for the first call.
void bdMiniMapAnchorHook(PPCRegister &f1, PPCRegister &f2, PPCRegister &f25,
                         PPCRegister &f26) {
  AnchorBottomRight(f1, f2);
  AnchorBottomRight(f25, f26);
}

// The map layer under that frame is pushed by a helper that ignores the center
// it is handed and rebuilds it from constants of its own, in f29 and f30.
void bdMiniMapLayerAnchorHook(PPCRegister &f29, PPCRegister &f30) {
  AnchorBottomRight(f29, f30);
}

// The composite quad the camera view draws the close-up through. Both loops
// rebuild from the outline rather than editing in place, so nothing compounds.
void bdCloseUpQuadAnchorHook(PPCRegister &r3, PPCRegister &f11,
                             PPCRegister &f12, PPCRegister &f13,
                             PPCRegister &f0) {
  Outline outline;
  if (!ReadOutline(r3.u32, outline))
    return;
  if (IsWindowed(outline)) {
    AnchorOutwards(outline, f11, f12);
    return;
  }
  CropAxis(outline.x, bd::gpu::kDesignCanvasWidth, f11.f64, f13.f64);
  CropAxis(outline.y, bd::gpu::kDesignCanvasHeight, f12.f64, f0.f64);
}

// The window's border, which the cinematic does not draw.
void bdCloseUpFrameAnchorHook(PPCRegister &r31, PPCRegister &f0,
                              PPCRegister &f13) {
  Outline outline;
  if (!ReadOutline(r31.u32, outline) || !IsWindowed(outline))
    return;
  AnchorOutwards(outline, f0, f13);
}

// The party cards along the canvas' bottom edge. The row of panels is a d2anime
// whose pos.y is driven from the placement its mode setter pulls out of the bag,
// so moving the two ends of the slide and the base they are laid out from
// carries the whole row down together.
void bdPartyPanelSlideAnchorHook(PPCRegister &f30, PPCRegister &f31) {
  const double over_y = bd::gpu::Output::DesignOverscanY();
  f30.f64 += over_y;
  f31.f64 += over_y;
}

void bdPartyPanelBaseAnchorHook(PPCRegister &f13) {
  f13.f64 += bd::gpu::Output::DesignOverscanY();
}

// The now-loading wheel belongs in the screen's bottom-left corner, not the
// canvas'.
void bdNowLoadingAnchorHook(PPCRegister &r31) {
  const float over_x = bd::gpu::Output::DesignOverscanX();
  const float over_y = bd::gpu::Output::DesignOverscanY();
  if (over_x == 0.0f && over_y == 0.0f)
    return;
  bd::engine::AnimeData bag =
      bd::engine::Loader(r31.u32).NowLoadingAnime().AnimeData();
  if (!bag)
    return;
  bag.SetFloat("pos.x", kNowLoadingX - over_x);
  bag.SetFloat("pos.y", kNowLoadingY + over_y);
}

void bdFieldEncounterMenuAnchorHook(PPCRegister &f30) {
  f30.f64 += bd::gpu::Output::DesignOverscanX();
}

void bdFieldEncounterPanelAnchorHook(PPCRegister &f1) {
  constexpr double mid = bd::gpu::kDesignCanvasWidth * 0.5;
  f1.f64 = mid + (f1.f64 - mid) * bd::gpu::Output::DesignScaleX();
}
