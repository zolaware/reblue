/**
 * @file    platform/mouse_input.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/mouse_input.h"

#include <cstdlib>

#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "core/logging.h"
#include "engine/engine.h"

namespace bd::platform {
namespace {

MouseInput g_mouse;

} // namespace

MouseInput &Mouse() { return g_mouse; }

void MouseInput::Attach(rex::ui::Window *window) {
  if (!window || window_.load(std::memory_order_relaxed))
    return;
  window_.store(window, std::memory_order_relaxed);
  window->AddInputListener(this, kZOrder);
  window->AddListener(this);
  QueuePointerUpdate();
}

void MouseInput::Detach() {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window)
    return;
  gameCursor_.store(false, std::memory_order_relaxed);
  look_.store(false, std::memory_order_relaxed);
  ApplyPointerMode();
  window->RemoveInputListener(this);
  window->RemoveListener(this);
  window_.store(nullptr, std::memory_order_relaxed);
  hasPosition_.store(false, std::memory_order_relaxed);
  moved_.store(false, std::memory_order_relaxed);
  wheelAccum_.store(0, std::memory_order_relaxed);
  wheelTaken_.store(0, std::memory_order_relaxed);
  lookDx_.store(0.0f, std::memory_order_relaxed);
  lookDy_.store(0.0f, std::memory_order_relaxed);
  buttons_.store(0, std::memory_order_relaxed);
}

bool MouseInput::Position(f32 &x, f32 &y) const {
  if (!hasPosition_.load(std::memory_order_relaxed))
    return false;
  x = x_.load(std::memory_order_relaxed);
  y = y_.load(std::memory_order_relaxed);
  return true;
}

bool MouseInput::MovedSince() {
  return moved_.exchange(false, std::memory_order_relaxed);
}

int MouseInput::TakeWheelDetents() {
  const int taken = wheelAccum_.exchange(0, std::memory_order_relaxed);
  wheelTaken_.store(taken, std::memory_order_relaxed);
  return taken;
}

int MouseInput::WheelDetents() const {
  return wheelTaken_.load(std::memory_order_relaxed);
}

bool MouseInput::TakeLookDelta(f32 &dx, f32 &dy) {
  const f32 x = lookDx_.exchange(0.0f, std::memory_order_relaxed);
  const f32 y = lookDy_.exchange(0.0f, std::memory_order_relaxed);
  if (x == 0.0f && y == 0.0f)
    return false;
  dx = x;
  dy = y;
  return true;
}

void MouseInput::PeekLookDelta(f32 &dx, f32 &dy) const {
  dx = lookDx_.load(std::memory_order_relaxed);
  dy = lookDy_.load(std::memory_order_relaxed);
}

bool MouseInput::WindowSize(f32 &w, f32 &h) const {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window)
    return false;
  const u32 width = window->GetActualPhysicalWidth();
  const u32 height = window->GetActualPhysicalHeight();
  if (width == 0 || height == 0)
    return false;
  w = f32(width);
  h = f32(height);
  return true;
}

void MouseInput::SetGameCursorActive(bool active) {
  if (gameCursor_.exchange(active, std::memory_order_relaxed) != active)
    QueuePointerUpdate();
}

void MouseInput::SetLookActive(bool active) {
  if (look_.exchange(active, std::memory_order_relaxed) != active)
    QueuePointerUpdate();
}

void MouseInput::QueuePointerUpdate() {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window || updateQueued_.exchange(true, std::memory_order_relaxed))
    return;
  window->app_context().CallInUIThreadDeferred([this] {
    updateQueued_.store(false, std::memory_order_relaxed);
    ApplyPointerMode();
  });
}

void MouseInput::ApplyPointerMode() {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window)
    return;
  PointerMode want = PointerMode::Free;
  if (focused_.load(std::memory_order_relaxed)) {
    if (look_.load(std::memory_order_relaxed))
      want = PointerMode::Locked;
    else if (gameCursor_.load(std::memory_order_relaxed))
      want = PointerMode::Hidden;
  }
  if (want == mode_)
    return;

  if (mode_ == PointerMode::Locked) {
    window->SetRelativeMouseMode(false);
    relative_ = false;
  }
  if (mode_ == PointerMode::Free)
    arrowVisibility_ = window->GetCursorVisibility();

  if (want == PointerMode::Free) {
    window->SetCursorVisibility(arrowVisibility_);
  } else {
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
  }
  if (want == PointerMode::Locked) {
    relative_ = window->SetRelativeMouseMode(true);
    if (!relative_)
      BD_WARN("[mouse] pointer lock unavailable, mouse look recenters the "
              "pointer instead");
    lockX_ = i32(x_.load(std::memory_order_relaxed));
    lockY_ = i32(y_.load(std::memory_order_relaxed));
  }
  lookDx_.store(0.0f, std::memory_order_relaxed);
  lookDy_.store(0.0f, std::memory_order_relaxed);
  mode_ = want;
}

void MouseInput::RecenterLockedPointer(i32 x, i32 y) {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window)
    return;
  const i32 width = i32(window->GetActualPhysicalWidth());
  const i32 height = i32(window->GetActualPhysicalHeight());
  if (width <= 0 || height <= 0)
    return;
  if (std::abs(x - width / 2) < width / 4 &&
      std::abs(y - height / 2) < height / 4)
    return;
  i32 centerX = 0;
  i32 centerY = 0;
  if (window->WarpMouseToCenter(centerX, centerY)) {
    lockX_ = centerX;
    lockY_ = centerY;
  }
}

void MouseInput::OnMouseMove(rex::ui::MouseEvent &e) {
  if (mode_ == PointerMode::Locked) {
    if (relative_) {
      lookDx_.fetch_add(e.dx(), std::memory_order_relaxed);
      lookDy_.fetch_add(e.dy(), std::memory_order_relaxed);
      return;
    }
    lookDx_.fetch_add(f32(e.x() - lockX_), std::memory_order_relaxed);
    lookDy_.fetch_add(f32(e.y() - lockY_), std::memory_order_relaxed);
    lockX_ = e.x();
    lockY_ = e.y();
    RecenterLockedPointer(e.x(), e.y());
    return;
  }
  const f32 x = f32(e.x());
  const f32 y = f32(e.y());
  const f32 prevX = x_.load(std::memory_order_relaxed);
  const f32 prevY = y_.load(std::memory_order_relaxed);
  x_.store(x, std::memory_order_relaxed);
  y_.store(y, std::memory_order_relaxed);
  const bool hadPosition =
      hasPosition_.exchange(true, std::memory_order_relaxed);
  if (!hadPosition || x != prevX || y != prevY)
    moved_.store(true, std::memory_order_relaxed);
}

void MouseInput::OnMouseWheel(rex::ui::MouseEvent &e) {
  if (engine::HostOverlayOwnsPointer())
    return;
  const int detents =
      e.scroll_y() / int(rex::ui::MouseEvent::kScrollPerDetent);
  if (detents != 0)
    wheelAccum_.fetch_add(detents, std::memory_order_relaxed);
}

void MouseInput::OnMouseDown(rex::ui::MouseEvent &e) {
  if (engine::HostOverlayOwnsPointer()) {
    e.set_handled(true);
    return;
  }
  buttons_.fetch_or(1u << u32(e.button()), std::memory_order_relaxed);
}

void MouseInput::OnMouseUp(rex::ui::MouseEvent &e) {
  buttons_.fetch_and(~(1u << u32(e.button())), std::memory_order_relaxed);
}

bool MouseInput::IsButtonDown(rex::ui::MouseEvent::Button button) const {
  return (buttons_.load(std::memory_order_relaxed) & (1u << u32(button))) != 0;
}

bool MouseInput::AnyButtonDown() const {
  return buttons_.load(std::memory_order_relaxed) != 0;
}

void MouseInput::OnGotFocus(rex::ui::UISetupEvent &) {
  focused_.store(true, std::memory_order_relaxed);
  ApplyPointerMode();
}

void MouseInput::OnLostFocus(rex::ui::UISetupEvent &) {
  focused_.store(false, std::memory_order_relaxed);
  ApplyPointerMode();
  moved_.store(false, std::memory_order_relaxed);
  wheelAccum_.store(0, std::memory_order_relaxed);
  wheelTaken_.store(0, std::memory_order_relaxed);
  lookDx_.store(0.0f, std::memory_order_relaxed);
  lookDy_.store(0.0f, std::memory_order_relaxed);
  hasPosition_.store(false, std::memory_order_relaxed);
  buttons_.store(0, std::memory_order_relaxed);
}

// A normal close never reaches ReblueApp::OnShutdown, so detach here or the
// window tears down with us still on its listener lists. Window tolerates a
// listener removing itself mid-dispatch, which is how MnkInputDriver does it.
void MouseInput::OnClosing(rex::ui::UIEvent &) { Detach(); }

} // namespace bd::platform
