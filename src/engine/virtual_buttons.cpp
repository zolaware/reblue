/**
 * @file    engine/virtual_buttons.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/virtual_buttons.h"

#include <atomic>

#include <rex/hook.h>
#include <rex/ppc/detour.h>
#include <rex/types.h>

#include "engine/d2anime/anime_input.h"
#include "engine/d2anime/anime_mouse.h"
#include "engine/input/action_state.h"
#include "engine/settings.h"
#include "platform/platform.h"
#include "reblue_init.h"

namespace bd::engine {

namespace {

std::atomic<bool> g_menuOwnsInput{false};
std::atomic<bool> g_padSawInput{false};
std::atomic<int> g_hostPointerClaims{0};

} // namespace

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

bool HostOverlayOwnsPointer() {
  return g_hostPointerClaims.load(std::memory_order_relaxed) > 0;
}

HostPointerClaim::HostPointerClaim() {
  g_hostPointerClaims.fetch_add(1, std::memory_order_relaxed);
}

HostPointerClaim::~HostPointerClaim() {
  g_hostPointerClaims.fetch_sub(1, std::memory_order_relaxed);
}

void UpdateMouseLook() {
  platform::SetLookActive(Settings::Get().MouseInput() &&
                                  !MenuOwnsInput() &&
                                  !HostOverlayOwnsPointer());
}

} // namespace bd::engine

REX_DETOUR(bdInputCheckButton, u32, (u32 inputMgr, u32 padIndex, u32 button)) {
  const u32 pressed = original(inputMgr, padIndex, button);
  // The click that holds a scrollbar is spent on the bar. Without this the
  // screen behind it reads the same press as a confirm and acts on whichever
  // row the cursor happens to hold.
  if (button == u32(bd::engine::Button::A) &&
      bd::engine::MenuMouse::Get().DraggingScrollbar()) {
    return 0;
  }
  if (pressed != 0 && !bd::engine::InputActions::Get().Forced())
    bd::engine::PadInputSeen();
  return pressed;
}
