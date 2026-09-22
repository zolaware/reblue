/**
 * @file    platform/mouse_input.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <atomic>

#include <rex/types.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

namespace bd::platform {

// Mouse events arrive on the window thread and the menu hooks read them from a
class MouseInput final : public rex::ui::WindowInputListener,
                          public rex::ui::WindowListener {
public:
  static constexpr size_t kZOrder = 32;

  void Attach(rex::ui::Window *window);
  void Detach();

  // Physical window pixels. False before the first motion event.
  bool Position(f32 &x, f32 &y) const;

  // True when the pointer moved since the last call, then false until it moves
  // again, so the pad can take the cursor back from a resting mouse.
  bool MovedSince();

  // Signed wheel detents accumulated since the last call, drained by the read.
  int TakeWheelDetents();

  int WheelDetents() const;

  bool TakeLookDelta(f32 &dx, f32 &dy);

  void PeekLookDelta(f32 &dx, f32 &dy) const;

  bool IsButtonDown(rex::ui::MouseEvent::Button button) const;

  // Buttons only, never motion: a hand resting on the mouse must not claim the
  // input device from a pad the other hand is holding.
  bool AnyButtonDown() const;

  bool WindowSize(f32 &w, f32 &h) const;

  // The game is drawing a pointer of its own, so the arrow would be a second
  void SetGameCursorActive(bool active);

  void SetLookActive(bool active);

  // WindowInputListener
  void OnMouseDown(rex::ui::MouseEvent &e) override;
  void OnMouseUp(rex::ui::MouseEvent &e) override;
  void OnMouseMove(rex::ui::MouseEvent &e) override;
  void OnMouseWheel(rex::ui::MouseEvent &e) override;

  // WindowListener
  void OnGotFocus(rex::ui::UISetupEvent &e) override;
  void OnLostFocus(rex::ui::UISetupEvent &e) override;
  void OnClosing(rex::ui::UIEvent &e) override;

private:
  enum class PointerMode : u8 { Free, Hidden, Locked };

  void QueuePointerUpdate();
  // Window thread only.
  void ApplyPointerMode();
  void RecenterLockedPointer(i32 x, i32 y);

  std::atomic<f32> x_{0.0f};
  std::atomic<f32> y_{0.0f};
  std::atomic<bool> hasPosition_{false};
  std::atomic<bool> moved_{false};
  std::atomic<int> wheelAccum_{0};
  std::atomic<int> wheelTaken_{0};
  std::atomic<f32> lookDx_{0.0f};
  std::atomic<f32> lookDy_{0.0f};
  std::atomic<u32> buttons_{0}; // bit per MouseEvent::Button value
  std::atomic<bool> gameCursor_{false};
  std::atomic<bool> look_{false};
  std::atomic<bool> focused_{true};
  std::atomic<bool> updateQueued_{false};
  std::atomic<rex::ui::Window *> window_{nullptr};

  // Window thread only. The arrow goes back to whatever it was when the game
  // took it, so cursor_hide_seconds keeps the mode it chose.
  PointerMode mode_ = PointerMode::Free;
  bool relative_ = false;
  i32 lockX_ = 0;
  i32 lockY_ = 0;
  rex::ui::Window::CursorVisibility arrowVisibility_ =
      rex::ui::Window::CursorVisibility::kVisible;
};

MouseInput &Mouse();

} // namespace bd::platform
