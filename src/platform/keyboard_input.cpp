/**
 * @file    platform/keyboard_input.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/keyboard_input.h"

#include <rex/ui/window.h>

#include "core/settings.h"
#include "engine/engine.h"

namespace bd::platform {
namespace {

KeyboardInput g_keyboard;

} // namespace

KeyboardInput &Keyboard() { return g_keyboard; }

void KeyboardInput::Attach(rex::ui::Window *window) {
  if (!window || window_)
    return;
  window_ = window;
  focused_.store(window_->HasFocus(), std::memory_order_relaxed);
  window_->AddInputListener(this, kZOrder);
  window_->AddListener(this);
}

void KeyboardInput::Detach() {
  if (!window_)
    return;
  window_->RemoveInputListener(this);
  window_->RemoveListener(this);
  window_ = nullptr;
  focused_.store(true, std::memory_order_relaxed);
  for (auto &word : keys_)
    word.store(0, std::memory_order_relaxed);
}

void KeyboardInput::Set(rex::ui::VirtualKey vk, bool down) {
  auto idx = static_cast<u16>(vk);
  if (idx >= 256)
    return;
  const u64 bit = 1ull << (idx & 63);
  if (down)
    keys_[idx >> 6].fetch_or(bit, std::memory_order_relaxed);
  else
    keys_[idx >> 6].fetch_and(~bit, std::memory_order_relaxed);
}

bool KeyboardInput::IsDown(rex::ui::VirtualKey vk) const {
  auto idx = static_cast<u16>(vk);
  if (idx >= 256)
    return false;
  return (keys_[idx >> 6].load(std::memory_order_relaxed) &
          (1ull << (idx & 63))) != 0;
}

bool KeyboardInput::WindowFocused() const {
  return focused_.load(std::memory_order_relaxed);
}

bool KeyboardInput::AnyDown() const {
  for (const auto &word : keys_)
    if (word.load(std::memory_order_relaxed) != 0)
      return true;
  return false;
}

u8 KeyboardInput::Modifiers() const {
  u8 mods = 0;
  if (IsDown(rex::ui::VirtualKey::kShift))
    mods |= kModShift;
  if (IsDown(rex::ui::VirtualKey::kControl))
    mods |= kModCtrl;
  if (IsDown(rex::ui::VirtualKey::kMenu))
    mods |= kModAlt;
  return mods;
}

// Keyboard belongs to Mindows while the overlay is up, so keys must not also
// reach the MnK driver below us and move the party.
bool KeyboardInput::ShouldSwallow() const {
  if (!bd::Settings::Get().Devmode())
    return false;
  const auto &game = bd::engine::Game::Get();
  return game.IsReady() && !game.MindowsHidden();
}

void KeyboardInput::OnKeyDown(rex::ui::KeyEvent &e) {
  Set(e.virtual_key(), true);
  if (ShouldSwallow())
    e.set_handled(true);
}

void KeyboardInput::OnKeyUp(rex::ui::KeyEvent &e) {
  Set(e.virtual_key(), false);
  // Never swallowed. MnK only clears its key_down_ table on focus loss, so
  // eating one key-up latches that key down until the window is defocused.
}

void KeyboardInput::OnGotFocus(rex::ui::UISetupEvent &) {
  focused_.store(true, std::memory_order_relaxed);
}

void KeyboardInput::OnLostFocus(rex::ui::UISetupEvent &) {
  focused_.store(false, std::memory_order_relaxed);
  for (auto &word : keys_)
    word.store(0, std::memory_order_relaxed);
}

// A normal close never reaches ReblueApp::OnShutdown, so detach here or the
// window tears down with us still on its listener lists. Window tolerates a
// listener removing itself mid-dispatch, which is how MnkInputDriver does it.
void KeyboardInput::OnClosing(rex::ui::UIEvent &) { Detach(); }

} // namespace bd::platform
