/**
 * @file    platform/keyboard_input.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <array>
#include <atomic>

#include <rex/types.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window_listener.h>

namespace rex::ui {
class Window;
}

namespace bd::platform {

// Window thread keyboard latch. SDL key events arrive on the main thread while
// the guest keyboard bridge polls from a guest thread, so state crosses that
// boundary through an atomic bitset instead of a live SDL query.
//
// Registered at z 32, above the MnK driver and ReXApp (both z 0) and below the
// ImGui drawer (z 64). Window listeners run highest z first and stop at the
// first handled event, so this slot lets the overlays keep their keys while
// still consuming key-downs before MnK can turn them into pad state.
class KeyboardInput final : public rex::ui::WindowInputListener,
                            public rex::ui::WindowListener {
public:
  static constexpr size_t kZOrder = 32;

  // Modifier bits returned by Modifiers().
  static constexpr u8 kModShift = 1u << 0;
  static constexpr u8 kModCtrl = 1u << 1;
  static constexpr u8 kModAlt = 1u << 2;

  void Attach(rex::ui::Window *window);
  void Detach();

  bool IsDown(rex::ui::VirtualKey vk) const;

  // Whether any key at all is held. Non-consuming, unlike the mouse's
  // MovedSince, so several readers can ask in the same frame.
  bool AnyDown() const;

  u8 Modifiers() const;

  bool WindowFocused() const;

  // WindowInputListener
  void OnKeyDown(rex::ui::KeyEvent &e) override;
  void OnKeyUp(rex::ui::KeyEvent &e) override;

  // WindowListener
  void OnGotFocus(rex::ui::UISetupEvent &e) override;
  void OnLostFocus(rex::ui::UISetupEvent &e) override;
  void OnClosing(rex::ui::UIEvent &e) override;

private:
  bool ShouldSwallow() const;
  void Set(rex::ui::VirtualKey vk, bool down);

  std::array<std::atomic<u64>, 4> keys_{};
  std::atomic<bool> focused_{true};
  rex::ui::Window *window_ = nullptr;
};

KeyboardInput &Keyboard();

} // namespace bd::platform
