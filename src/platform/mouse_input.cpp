/**
 * @file    platform/mouse_input.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/mouse_input.h"

#include <rex/ui/window.h>

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
}

void MouseInput::Detach() {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window)
    return;
  gameCursor_.store(false, std::memory_order_relaxed);
  ApplyGameCursorState();
  window->RemoveInputListener(this);
  window->RemoveListener(this);
  window_.store(nullptr, std::memory_order_relaxed);
  hasPosition_.store(false, std::memory_order_relaxed);
  moved_.store(false, std::memory_order_relaxed);
  wheelAccum_.store(0, std::memory_order_relaxed);
  wheelTaken_.store(0, std::memory_order_relaxed);
  deltaX_.store(0.0f, std::memory_order_relaxed);
  deltaY_.store(0.0f, std::memory_order_relaxed);
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

bool MouseInput::TakeDelta(f32 &dx, f32 &dy) {
  const f32 x = deltaX_.exchange(0.0f, std::memory_order_relaxed);
  const f32 y = deltaY_.exchange(0.0f, std::memory_order_relaxed);
  if (x == 0.0f && y == 0.0f)
    return false;
  dx = x;
  dy = y;
  return true;
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
  gameCursor_.store(active, std::memory_order_relaxed);
}

void MouseInput::ApplyGameCursorState() {
  rex::ui::Window *window = window_.load(std::memory_order_relaxed);
  if (!window)
    return;
  const bool hide = gameCursor_.load(std::memory_order_relaxed);
  if (hide == arrowHidden_)
    return;
  arrowHidden_ = hide;
  if (hide) {
    arrowVisibility_ = window->GetCursorVisibility();
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
  } else {
    window->SetCursorVisibility(arrowVisibility_);
  }
}

void MouseInput::OnMouseMove(rex::ui::MouseEvent &e) {
  ApplyGameCursorState();
  const f32 x = f32(e.x());
  const f32 y = f32(e.y());
  f32 prevX = x_.load(std::memory_order_relaxed);
  f32 prevY = y_.load(std::memory_order_relaxed);
  x_.store(x, std::memory_order_relaxed);
  y_.store(y, std::memory_order_relaxed);
  const bool hadPosition =
      hasPosition_.exchange(true, std::memory_order_relaxed);
  if (!hadPosition || x != prevX || y != prevY)
    moved_.store(true, std::memory_order_relaxed);
  if (hadPosition && (x != prevX || y != prevY)) {
    deltaX_.fetch_add(x - prevX, std::memory_order_relaxed);
    deltaY_.fetch_add(y - prevY, std::memory_order_relaxed);
  }
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
  ApplyGameCursorState();
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

void MouseInput::OnLostFocus(rex::ui::UISetupEvent &) {
  // The arrow belongs to whatever the user alt-tabbed to now.
  gameCursor_.store(false, std::memory_order_relaxed);
  ApplyGameCursorState();
  moved_.store(false, std::memory_order_relaxed);
  wheelAccum_.store(0, std::memory_order_relaxed);
  wheelTaken_.store(0, std::memory_order_relaxed);
  deltaX_.store(0.0f, std::memory_order_relaxed);
  deltaY_.store(0.0f, std::memory_order_relaxed);
  hasPosition_.store(false, std::memory_order_relaxed);
  buttons_.store(0, std::memory_order_relaxed);
}

// A normal close never reaches ReblueApp::OnShutdown, so detach here or the
// window tears down with us still on its listener lists. Window tolerates a
// listener removing itself mid-dispatch, which is how MnkInputDriver does it.
void MouseInput::OnClosing(rex::ui::UIEvent &) { Detach(); }

} // namespace bd::platform
