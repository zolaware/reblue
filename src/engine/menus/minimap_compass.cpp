/**
 * @file    engine/menus/minimap_compass.cpp
 * @brief   The area map's markers drawn on the field compass, through the
 *          compass widget's own transform.
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/field.h"
#include "engine/gimmicks.h"
#include "engine/hud_fade.h"
#include "engine/menus/map_markers.h"
#include "engine/settings.h"
#include "engine/state_layout.h"
#include "gpu/gpu.h"

REX_EXTERN(__imp__MiniMapTask__DrawWidget);

namespace bd::engine {

namespace {

// The compass widget, as MiniMapTask__DrawWidget lays it out: a 256-wide
// masked crop centered at 1120,588 with the player pinned to its center. A
// marker's screen offset is its world delta over MapScale, times TexSize over
// DispSize, times the 128 half-width. Its own destination marker hides past 70
// widget pixels, fading over the last tenth, and ours keep its numbers so
// nothing pops at a different rim.
constexpr float kCompassX = 1120.0f;
constexpr float kCompassY = 588.0f;
constexpr float kCompassHalf = 128.0f;
constexpr float kCompassCull = 70.0f;
constexpr float kCompassFade = 0.9f;
// In front of the widget's own 20.1 band, stepping down toward the redrawn
// arrow.
constexpr float kCompassMarkerZ = 20.098f;
constexpr float kCompassMarkerZFloor = 20.09f;
constexpr float kCompassArrowZ = 20.088f;
constexpr u32 kCompassArrowColor = 0xB0FFFFFFu; // the widget's own arrow tint
// The widget stands down on the maps past this id, and so do the markers.
constexpr u32 kCompassMapIdCap = 1810;
// A marker this close to the center can stand on the arrow, which earns the
// arrow a second draw on top, the way the area map keeps the player above its
// dots.
constexpr float kArrowCover = 18.0f;
constexpr int kMarkerRefreshFrames = 30;

// The area map's markers on the field compass, through the compass widget's
// own transform.
class MiniMapMarkers {
public:
  static MiniMapMarkers &Get() {
    static MiniMapMarkers m;
    return m;
  }

  void Draw(u32 miniMap);

private:
  std::string stem_;
  std::vector<Marker> markers_;
  int refresh_ = 0;
};

void MiniMapMarkers::Draw(u32 miniMap) {
  if (!Settings::Get().MapGimmickMarkers())
    return;
  // The widget under these already faded through its own hooks.
  const float fade = HudFade::Get().Alpha();
  if (fade <= 0.0f)
    return;
  const u32 fsc = mem::load<u32>(addr::kFieldSceneCtl);
  if (mem::try_field<u32>(fsc, offsetof(FieldSceneCtl_t, mapId)) >=
      kCompassMapIdCap)
    return;
  const u32 db = mem::try_field<u32>(miniMap, offsetof(MiniMapTask_t, floor));
  if (!FloorReady(db))
    return;
  const Field field;
  if (!field.HasPlayer())
    return;

  // Chests open and points get taken while the compass is up, so the cache
  // follows the stage and refreshes on a short cadence rather than once.
  const std::string stem = field.Stage().Name();
  if (stem != stem_ || --refresh_ <= 0) {
    stem_ = stem;
    refresh_ = kMarkerRefreshFrames;
    markers_ = Gimmicks::Get().Markers(stem_);
  }
  if (markers_.empty())
    return;

  const auto *m = mem::try_at<const MiniMapDB_t>(db);
  if (float(m->dispW) <= 0.0f || float(m->dispH) <= 0.0f)
    return;
  const Vec3 player = field.Position();

  const float rot = (float(m->texRot) + float(m->offsetRot)) * kDegToRad;
  const float cosA = std::cos(rot);
  const float sinA = std::sin(rot);
  const float spanX =
      float(m->texW) / float(m->dispW) * kCompassHalf / float(m->scaleX);
  const float spanY =
      float(m->texH) / float(m->dispH) * kCompassHalf / float(m->scaleZ);

  const float compassX = kCompassX + gpu::Output::DesignOverscanX();
  const float compassY = kCompassY + gpu::Output::DesignOverscanY();

  PrimSelectTexture(kChromeMarker,
                    miniMap + offsetof(MiniMapTask_t, chromeTex));
  const u32 shapeTex = mem::load<u32>(PrimState() + kPrim_Texture);
  QuadWriter quads{shapeTex, kCompassMarkerZ, kCompassMarkerZFloor};

  bool onArrow = false;
  for (const Marker &mk : markers_) {
    if (!MarkerVisible(mk))
      continue;
    const float dx = (mk.x - player[0]) * spanX;
    const float dy = (mk.z - player[2]) * spanY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist >= kCompassCull)
      continue;
    const float t = dist / kCompassCull;
    const float rim =
        t < kCompassFade ? 1.0f
                         : 1.0f - (t - kCompassFade) / (1.0f - kCompassFade);
    quads.alpha = u32(rim * fade * 255.0f);
    onArrow = onArrow || dist < kArrowCover;

    DrawMarkerShape(quads, mk.kind, compassX + dx * cosA + dy * sinA,
                    compassY - dx * sinA + dy * cosA, MarkerPulse(mk),
                    kMarkerHalf);
  }

  if (!onArrow)
    return;
  const float heading = (float(m->texRot) + float(m->plyRot)) * kDegToRad +
                        field.Rotation()[1] - kHalfPi;
  PrimSelectTexture(kChromeArrow, miniMap + offsetof(MiniMapTask_t, chromeTex));
  const u32 arrow = (u32(f32(kCompassArrowColor >> 24) * fade) << 24) |
                    (kCompassArrowColor & 0x00FFFFFFu);
  PrimDrawRectRotated(compassX, compassY, kCompassArrowZ, kMarkerSize,
                      kMarkerSize, heading, 0, 0, 0, 0, 0, 0, arrow);
}

} // namespace

} // namespace bd::engine

REX_HOOK_RAW(MiniMapTask__DrawWidget) {
  const u32 miniMap = ctx.r3.u32;
  __imp__MiniMapTask__DrawWidget(ctx, base);
  bd::engine::MiniMapMarkers::Get().Draw(miniMap);
}
