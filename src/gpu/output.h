/**
 * @file    gpu/output.h
 * @brief   The render rectangle BD targets, and the design canvas its 2D layer
 *          is authored against.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/types.h>

namespace rex::ui {
class Window;
}

namespace bd::gpu {

// Every HUD coordinate, debug text glyph position and 2D quad in the game is
// authored against these dims. Output res changes what the renderer targets,
// never this.
constexpr float kDesignCanvasWidth = 1280.0f;
constexpr float kDesignCanvasHeight = 720.0f;
constexpr double kDesignCanvasAspect = kDesignCanvasWidth / kDesignCanvasHeight;

// Ratios within this of each other count as the same. Wide enough for the
// multiple-of-8 rounding of the fit rect, far under the gap to the nearest real
// ratio (16:10 is 0.18 away).
constexpr double kDesignCanvasAspectEpsilon = 0.01;

class Output {
public:
  static void Init(rex::ui::Window *window);
  static bool RenderSize(u32 &w, u32 &h);

  static double RenderDensity();
  static double RenderFraction();

  // The ratio bd_aspect_ratio asks for, or 0 to take whatever the window is.
  static double ConfiguredAspect();

  static bool StretchToFill();

  // The render rect's own ratio, not the configured one: multiple-of-8
  // rounding moves it, and a live bd_aspect_ratio change does not move the rect.
  static double RenderAspect();

  // The ratio the picture is composed for, which the projection is widened to
  // and the 2D fit works against. RenderAspect except in stretch mode, which
  // composes at the design canvas while the rasterizer covers the window.
  static double ProjectionAspect();

  // Whether the composed ratio is far enough off the design canvas to be worth
  // re-fitting the 2D layer into its centered sub-rect.
  static bool DesignFitActive();

  // What a design canvas coordinate is scaled by, about the canvas center, to
  // keep BD's authored 2D layout on a render rect of another ratio. 1 on the
  // axis the ratio left alone.
  static float DesignScaleX();
  static float DesignScaleY();

  // How far past the design canvas, in canvas units, an element has to be moved
  // for that scale to put it on the surface edge rather than the canvas edge.
  // Zero on the axis the ratio left alone.
  static float DesignOverscanX();
  static float DesignOverscanY();

  // fitW/fitH round down to a multiple of 8 for BD buffer alignment. An aspect
  // of 0 fills the rectangle, leaving no bars.
  static void ComputeFit(u32 swapW, u32 swapH, double aspect, u32 &fitW,
                         u32 &fitH, i32 &offX, i32 &offY);
};

} // namespace bd::gpu
