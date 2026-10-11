/**
 * @file    engine/mouse_cursor.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#include <atomic>
#include <chrono>
#include <cmath>

#include <rex/hook.h>
#include <rex/types.h>

#include "engine/d2anime/anime_hittest.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/guest_prim.h"
#include "engine/guest_texlist.h"
#include "engine/mouse_cursor.h"
#include "engine/settings.h"
#include "engine/virtual_buttons.h"
#include "platform/platform.h"

// ReXGlue numbers GPR ordinals over integer parameters only, so every register a
// float argument reserves has to be spelled out as a placeholder for the ones
// behind it to sit where the engine reads them.

namespace bd::engine {

namespace {

// d2anime\res\wrp_tgrcur_cir.dds and wrp_tgrcur_sec.dds, the two halves of the
// world map's warp cursor. L_wrp_cur.csv layers the arc ring over the glow, and
// this is the same pair in the same order.
constexpr const char *kCursorDir = "d2anime\\res\\";
constexpr const char *kGlowTexture = "wrp_tgrcur_cir";
constexpr const char *kRingTexture = "wrp_tgrcur_sec";
constexpr u32 kSlotGlow = 0;
constexpr u32 kSlotRing = 1;

Texlist g_cursor{kCursorDir, {kGlowTexture, kRingTexture}};

// As L_wrp_cur.csv sizes them, in design canvas pixels: a 64 glow with a 56 and
// a 48 arc ring over it. It spins the outer ring 712 degrees and the inner one
// 356 across the same 180 frames, so the two never line up twice in a turn.
constexpr f32 kGlowSize = 64.0f;
constexpr f32 kOuterRingSize = 56.0f;
constexpr f32 kInnerRingSize = 48.0f;
constexpr f32 kSpinSeconds = 6.0f; // the CSV's 180 frames at 30Hz
constexpr f32 kOuterTurnDegrees = -712.0f;
constexpr f32 kInnerTurnDegrees = -356.0f;
constexpr f32 kOuterDegreesPerSecond = kOuterTurnDegrees / kSpinSeconds;
constexpr f32 kInnerDegreesPerSecond = kInnerTurnDegrees / kSpinSeconds;
constexpr f32 kDegToRad = 0.017453292f;
constexpr f32 kTwoPi = 6.2831855f;

// Brighter than the stock cursor, which leans on a pulsing ring stack and a
// pair of screen-spanning crosshair lines this one drops. A pointer that has to
// be hunted for is worse than one that is a shade too loud.
constexpr u32 kGlowColor = 0xFFFFFFFFu;
constexpr u32 kOuterRingColor = 0xE0FFFFFFu;
constexpr u32 kInnerRingColor = 0x90FFFFFFu;

// wrp_tgrcur_sec is white art, and white on the title screen's white is nothing
// at all: only the cyan glow survived there. A dark copy of each ring a few
// pixels wider, drawn behind both of them, gives the arcs an edge to read
// against. On the dark backgrounds the rest of the menus sit on it disappears,
// the way a drop shadow should. The vertex color modulates the texture,
// so a black one turns the same white arcs into their own outline.
constexpr f32 kRingOutline = 6.0f;
constexpr u32 kRingOutlineColor = 0xB0000000u;

// A d2anime pri column and a prim z sort against the same key, and the smaller
// number draws in front. The stock warp cursor sits at -1, which clears the
// menu bands but not the confirm popup: RBDEL_PS in engine/menus/config_layout.h
// puts that at -100 so it covers the list behind it, and a popup that buries
// the pointer is a pointer the user has to hunt for. Both outlines sit behind
// both rings rather than each behind its own: the two ring radii are close
// enough that an outline between them would darken the other ring as they
// cross.
constexpr f32 kCursorZ = -101.0f;
constexpr f32 kGlowZ = kCursorZ;
constexpr f32 kRingOutlineZ = kCursorZ - 0.05f;
constexpr f32 kOuterRingZ = kCursorZ - 0.1f;
constexpr f32 kInnerRingZ = kCursorZ - 0.2f;

// Tick thread.
bool g_loadStarted = false;

// Read by the draw hook, which runs per rendered frame and may not be on the
// thread the tick runs on.
std::atomic<bool> g_visible{false};

// One scale over every layer, so the setting thins the whole pointer rather
// than flattening the balance the ring alphas carry between them.
u32 Fade(u32 color, i32 percent) {
  const u32 alpha = ((color >> 24) * u32(percent)) / 100u;
  return (alpha << 24) | (color & 0x00FFFFFFu);
}

// Wall time rather than the engine tick, so the spin stays smooth above 30fps.
f32 SpinSeconds() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<f32>(std::chrono::steady_clock::now() - start)
      .count();
}

// The angle accumulates rather than replaying the CSV's timeline on a loop.
// Neither ring turns a whole number of times across its 180 frames, so
// restarting the tween snaps it by the remainder: 8 degrees on the outer ring
// every six seconds, 4 on the inner. Same rates, no seam.
f32 SpinAngle(f32 seconds, f32 degreesPerSecond) {
  return std::fmod(seconds * degreesPerSecond * kDegToRad, kTwoPi);
}

void DrawMouseCursor() {
  if (!g_cursor.Ready() || !g_visible.load(std::memory_order_relaxed))
    return;

  // Sampled here rather than on the tick: this hook runs per rendered frame, so
  // the pointer sits where the mouse is now instead of where it was when the
  // logic last stepped.
  f32 x = 0.0f;
  f32 y = 0.0f;
  if (!CursorInMenuSpace(x, y))
    return;

  const f32 seconds = SpinSeconds();
  const f32 outerAngle = SpinAngle(seconds, kOuterDegreesPerSecond);
  const f32 innerAngle = SpinAngle(seconds, kInnerDegreesPerSecond);
  const i32 opacity = Settings::Get().MouseCursorOpacity();

  g_cursor.Select(kSlotGlow);
  PrimDrawRectRotated(x, y, kGlowZ, kGlowSize, kGlowSize, 0.0, 0, 0, 0, 0, 0,
                        0, Fade(kGlowColor, opacity));

  const auto ring = [&](f32 size, f32 z, f32 angle, u32 color) {
    PrimDrawRectRotated(x, y, z, size, size, angle, 0, 0, 0, 0, 0, 0,
                          Fade(color, opacity));
  };
  g_cursor.Select(kSlotRing);
  ring(kOuterRingSize + kRingOutline, kRingOutlineZ, outerAngle,
       kRingOutlineColor);
  ring(kInnerRingSize + kRingOutline, kRingOutlineZ, innerAngle,
       kRingOutlineColor);
  ring(kOuterRingSize, kOuterRingZ, outerAngle, kOuterRingColor);
  ring(kInnerRingSize, kInnerRingZ, innerAngle, kInnerRingColor);
}

} // namespace

void MouseCursorTick() {
  // Stands down for reblue's own ImGui surfaces, keeping the system arrow on
  // screen and the drawn one off it while the debug menu is up. The
  // title rows publish MenuOwnsInput now, so without this the pointer opening
  // that menu is the one the game is still holding.
  const bool wanted = Settings::Get().MouseInput() && MenuOwnsInput() &&
                      !HostOverlayOwnsPointer();
  // The pad takes the pointer off screen with it, mouse motion brings it back.
  g_visible.store(wanted && MenuMouse::Get().MouseHasCursor(),
                  std::memory_order_relaxed);

  // Nothing loads until a menu wants a pointer, so a session that never opens
  // one never pays for the textures. Once started the load carries on while no
  // menu is up, so the cursor is there the moment one opens.
  g_loadStarted = g_loadStarted || wanted;
  const bool ready = g_loadStarted && g_cursor.Poll();

  // The arrow only goes away once there is something to replace it with, so a
  // texlist that never resolves leaves the pointer usable rather than invisible.
  platform::Mouse().SetGameCursorActive(wanted && ready);
}

} // namespace bd::engine

void bdMouseCursorDrawHook() { bd::engine::DrawMouseCursor(); }
