/**
 * @file    engine/visual_render.h
 * @brief   Visual::Render, which owns the screen dimensions every render
 *          target derives from, the brightness and screen position mirrors,
 *          and the scene render scale.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#pragma once

#include <rex/types.h>

#include "engine/object.h"

namespace bd::engine {

class VisualRender : public Object {
public:
  VisualRender() = default;
  explicit VisualRender(u32 address) : Object(address) {}

  static VisualRender Get();

  static constexpr u32 kBrightnessChannels = 3;

  f32 ScreenW() const;
  bool SetScreenW(f32 v);
  bool SetScreenH(f32 v);
  bool SetBrightness(u32 channel, f32 v);
  bool SetScreenPosX(f32 v);
  bool SetScreenPosY(f32 v);
  bool SetSafeRate(f32 v);
  f32 RenderRate() const;
  bool SetRenderRate(f32 v);
  bool SetFSAA(bool on);
};

} // namespace bd::engine
