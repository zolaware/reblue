/**
 * @file    engine/virtual_buttons.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/virtual_buttons.h"

#include <atomic>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/types.h>
#include <rex/ui/virtual_key.h>

#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mouse.h"
#include "platform/platform.h"
#include "reblue_init.h"

REX_EXTERN(__imp__bdInputCheckButton);

REXCVAR_DECLARE(bool, bd_input_bindings);

namespace bd::engine {

namespace {

// Plain arrows are the right stick by SDK default and the D-pad only with
// shift held, which is the right split while walking around a field. Inside a
// menu it leaves the arrow keys doing nothing at all: navigation and, on the
// settings rows, changing a value both read the D-pad.
struct MenuArrow {
  rex::ui::VirtualKey key;
  Button button;
};

constexpr MenuArrow kMenuArrows[] = {
    {rex::ui::VirtualKey::kUp, Button::Up},
    {rex::ui::VirtualKey::kDown, Button::Down},
    {rex::ui::VirtualKey::kLeft, Button::Left},
    {rex::ui::VirtualKey::kRight, Button::Right},
};
constexpr int kMenuArrowCount = int(sizeof(kMenuArrows) / sizeof(kMenuArrows[0]));

std::atomic<bool> g_arrowEdge[kMenuArrowCount]{};
bool g_arrowPrevDown[kMenuArrowCount] = {};

std::atomic<bool> g_menuOwnsInput{false};
std::atomic<bool> g_padSawInput{false};
std::atomic<int> g_hostPointerClaims{0};

// Both written and read on the engine thread: queued during a task update,
// activated at the next frame's SampleButtonEdges.
int g_pressQueued = -1;
int g_pressActive = -1;

} // namespace

void PressButton(int padButton) { g_pressQueued = padButton; }

void PadInputSeen() { g_padSawInput.store(true, std::memory_order_relaxed); }

bool TakePadInputSeen() {
  return g_padSawInput.exchange(false, std::memory_order_relaxed);
}

bool MenuOwnsInput() {
  return g_menuOwnsInput.load(std::memory_order_relaxed);
}

void SetMenuOwnsInput(bool owns) {
  g_menuOwnsInput.store(owns, std::memory_order_relaxed);
}

// Unconditional so a release that happens while no menu owns input still
// clears the latched level, gating this on MenuOwnsInput() would let a press
// made just as the last menu closes go stale and swallow the next menu's first
// cursor move.
void SampleButtonEdges() {
  g_pressActive = g_pressQueued;
  g_pressQueued = -1;
  if (REXCVAR_GET(bd_input_bindings))
    return;
  for (int i = 0; i < kMenuArrowCount; ++i) {
    const bool down = platform::Keyboard().IsDown(kMenuArrows[i].key);
    g_arrowEdge[i].store(down && !g_arrowPrevDown[i],
                         std::memory_order_relaxed);
    g_arrowPrevDown[i] = down;
  }
}

// bdInputCheckButton is polled by several independent handlers in the same
// frame, so this only reads the edges SampleButtonEdges already latched, it
// must never mutate state, or whichever handler polls first would consume
// the press and starve the rest.
//
// The arrow keys are the one mapping that cannot be an ordinary bind: they
// already carry the right stick, so a menu borrows them for the D-pad while it
// owns input.
bool SynthesizedButton(Button btn) {
  if (static_cast<int>(btn) == g_pressActive)
    return true;

  if (REXCVAR_GET(bd_input_bindings))
    return false;

  if (!MenuOwnsInput())
    return false;

  // Edges rather than levels, because this is added after bdInputCheckButton
  // has already applied the engine's repeat cooldown. A level would skip that
  // gate and run the cursor at one row per frame.
  for (int i = 0; i < kMenuArrowCount; ++i)
    if (btn == kMenuArrows[i].button)
      return g_arrowEdge[i].load(std::memory_order_relaxed);

  return false;
}

// bdInputIsPressed reads the engine pad's level, and the plain arrow keys are
// the right stick rather than the D-pad, so a held arrow never reaches it and
// every sweep-while-held reader moves one step per press. Read live rather than
// off a latch: this answers 'is it down now', with nothing to consume.
bool SynthesizedButtonHeld(Button btn) {
  if (REXCVAR_GET(bd_input_bindings))
    return false;
  if (!MenuOwnsInput())
    return false;
  for (int i = 0; i < kMenuArrowCount; ++i)
    if (btn == kMenuArrows[i].button)
      return platform::Keyboard().IsDown(kMenuArrows[i].key);
  return false;
}

bool HostOverlayOwnsPointer() {
  return g_hostPointerClaims.load(std::memory_order_relaxed) > 0;
}

HostPointerClaim::HostPointerClaim() {
  g_hostPointerClaims.fetch_add(1, std::memory_order_relaxed);
}

HostPointerClaim::~HostPointerClaim() {
  g_hostPointerClaims.fetch_sub(1, std::memory_order_relaxed);
}

// The SDK owns the other half: it takes the cvar and this gate together before
// it captures the cursor or feeds the stick, and drains its delta every frame
// either way, so a gap in looking cannot dump a backlog into the camera.
void UpdateMouseLook() {
  rex::input::mnk::SetMouseLookActive(!MenuOwnsInput() &&
                                      !HostOverlayOwnsPointer());
}

} // namespace bd::engine

// r3 is the input manager, r4 the pad index, r5 the button id, and the result
// comes back in r3. Only host-synthesized presses are added, so an engine press
// still wins on its own. The button id is taken before the call, since r5 is a
// volatile argument register and the original is free to leave anything in it.
REX_HOOK_RAW(bdInputCheckButton) {
  const u32 button = ctx.r5.u32;
  __imp__bdInputCheckButton(ctx, base);
  // The click that holds a scrollbar is spent on the bar. Without this the
  // screen behind it reads the same press as a confirm and acts on whichever
  // row the cursor happens to hold.
  if (button == u32(bd::engine::Button::A) &&
      bd::engine::MenuMouse::Get().DraggingScrollbar()) {
    ctx.r3.u32 = 0;
    return;
  }
  if (ctx.r3.u32 != 0) {
    bd::engine::PadInputSeen();
    return;
  }
  if (bd::engine::SynthesizedButton(static_cast<bd::engine::Button>(button)))
    ctx.r3.u32 = 1;
}
