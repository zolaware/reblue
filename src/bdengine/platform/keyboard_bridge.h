/**
 * @file        bdengine/keyboard_bridge.h
 *
 * @brief       Per-frame keyboard bridge: polls host keyboard state and writes
 *              into the game's guest keyboard input buffer at 0x82DDA6F0.
 *
 *              Called from a midasm_hook at the top of bdMainGameStep, before
 *              any game code reads the keyboard buffer.
 *
 *              Windows: uses GetAsyncKeyState (thread-safe, no event pump
 * needed). Linux:   uses SDL_GetKeyboardState + SDL_GetModState.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include <SDL3/SDL.h>
#endif

#include <cstdint>

#include <rex/types.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

namespace bd {

using VK = rex::ui::VirtualKey;

inline constexpr u32 kGuestKeyBufferAddr = 0x82DDA6F0;

struct GuestKeyEntry {
  be_u16 charCode; // +0x00
  u16 _pad02;            // +0x02
  be_u16 vkCode;   // +0x04
  u16 _pad06;            // +0x06
  u8 modifiers;          // +0x08
};

inline constexpr VK kPolledKeys[] = {
    VK::kReturn,  VK::kEscape,  VK::kTab,     VK::kBack,    VK::kUp,
    VK::kDown,    VK::kLeft,    VK::kRight,   VK::kAdd,     VK::kSubtract,
    VK::kF1,      VK::kF2,      VK::kF3,      VK::kF4,      VK::kF5,
    VK::kF6,      VK::kF7,      VK::kF8,      VK::kF9,      VK::kF10,
    VK::kF11,     VK::kF12,     VK::kA,       VK::kB,       VK::kC,
    VK::kD,       VK::kE,       VK::kF,       VK::kG,       VK::kH,
    VK::kI,       VK::kJ,       VK::kK,       VK::kL,       VK::kM,
    VK::kN,       VK::kO,       VK::kP,       VK::kQ,       VK::kR,
    VK::kS,       VK::kT,       VK::kU,       VK::kV,       VK::kW,
    VK::kX,       VK::kY,       VK::kZ,       VK::k0,       VK::k1,
    VK::k2,       VK::k3,       VK::k4,       VK::k5,       VK::k6,
    VK::k7,       VK::k8,       VK::k9,       VK::kNumpad0, VK::kNumpad1,
    VK::kNumpad2, VK::kNumpad3, VK::kNumpad4, VK::kNumpad5, VK::kNumpad6,
    VK::kNumpad7, VK::kNumpad8, VK::kNumpad9,
    VK::kInsert,  VK::kHome,
};

inline constexpr size_t kPolledKeyCount =
    sizeof(kPolledKeys) / sizeof(kPolledKeys[0]);

#ifndef _WIN32
// SDL scancode mapping - only needed on non-Windows platforms.
struct KeyMapping {
  VK vk;
  SDL_Scancode scancode;
};

inline constexpr KeyMapping kKeyMappings[] = {
    {VK::kReturn, SDL_SCANCODE_RETURN}, {VK::kEscape, SDL_SCANCODE_ESCAPE},
    {VK::kTab, SDL_SCANCODE_TAB},       {VK::kBack, SDL_SCANCODE_BACKSPACE},
    {VK::kUp, SDL_SCANCODE_UP},         {VK::kDown, SDL_SCANCODE_DOWN},
    {VK::kLeft, SDL_SCANCODE_LEFT},     {VK::kRight, SDL_SCANCODE_RIGHT},
    {VK::kAdd, SDL_SCANCODE_KP_PLUS},   {VK::kSubtract, SDL_SCANCODE_KP_MINUS},
    {VK::kF1, SDL_SCANCODE_F1},         {VK::kF2, SDL_SCANCODE_F2},
    {VK::kF3, SDL_SCANCODE_F3},         {VK::kF4, SDL_SCANCODE_F4},
    {VK::kF5, SDL_SCANCODE_F5},         {VK::kF6, SDL_SCANCODE_F6},
    {VK::kF7, SDL_SCANCODE_F7},         {VK::kF8, SDL_SCANCODE_F8},
    {VK::kF9, SDL_SCANCODE_F9},         {VK::kF10, SDL_SCANCODE_F10},
    {VK::kF11, SDL_SCANCODE_F11},       {VK::kF12, SDL_SCANCODE_F12},
    {VK::kA, SDL_SCANCODE_A},           {VK::kB, SDL_SCANCODE_B},
    {VK::kC, SDL_SCANCODE_C},           {VK::kD, SDL_SCANCODE_D},
    {VK::kE, SDL_SCANCODE_E},           {VK::kF, SDL_SCANCODE_F},
    {VK::kG, SDL_SCANCODE_G},           {VK::kH, SDL_SCANCODE_H},
    {VK::kI, SDL_SCANCODE_I},           {VK::kJ, SDL_SCANCODE_J},
    {VK::kK, SDL_SCANCODE_K},           {VK::kL, SDL_SCANCODE_L},
    {VK::kM, SDL_SCANCODE_M},           {VK::kN, SDL_SCANCODE_N},
    {VK::kO, SDL_SCANCODE_O},           {VK::kP, SDL_SCANCODE_P},
    {VK::kQ, SDL_SCANCODE_Q},           {VK::kR, SDL_SCANCODE_R},
    {VK::kS, SDL_SCANCODE_S},           {VK::kT, SDL_SCANCODE_T},
    {VK::kU, SDL_SCANCODE_U},           {VK::kV, SDL_SCANCODE_V},
    {VK::kW, SDL_SCANCODE_W},           {VK::kX, SDL_SCANCODE_X},
    {VK::kY, SDL_SCANCODE_Y},           {VK::kZ, SDL_SCANCODE_Z},
    {VK::k0, SDL_SCANCODE_0},           {VK::k1, SDL_SCANCODE_1},
    {VK::k2, SDL_SCANCODE_2},           {VK::k3, SDL_SCANCODE_3},
    {VK::k4, SDL_SCANCODE_4},           {VK::k5, SDL_SCANCODE_5},
    {VK::k6, SDL_SCANCODE_6},           {VK::k7, SDL_SCANCODE_7},
    {VK::k8, SDL_SCANCODE_8},           {VK::k9, SDL_SCANCODE_9},
    {VK::kNumpad0, SDL_SCANCODE_KP_0},  {VK::kNumpad1, SDL_SCANCODE_KP_1},
    {VK::kNumpad2, SDL_SCANCODE_KP_2},  {VK::kNumpad3, SDL_SCANCODE_KP_3},
    {VK::kNumpad4, SDL_SCANCODE_KP_4},  {VK::kNumpad5, SDL_SCANCODE_KP_5},
    {VK::kNumpad6, SDL_SCANCODE_KP_6},  {VK::kNumpad7, SDL_SCANCODE_KP_7},
    {VK::kNumpad8, SDL_SCANCODE_KP_8},  {VK::kNumpad9, SDL_SCANCODE_KP_9},
    {VK::kInsert, SDL_SCANCODE_INSERT}, {VK::kHome, SDL_SCANCODE_HOME},
};

inline constexpr size_t kKeyMappingCount =
    sizeof(kKeyMappings) / sizeof(kKeyMappings[0]);
#endif // !_WIN32

/**
 * @brief Map VK codes to the character codes Blue Dragon's debug systems expect.
 */
inline u16 VkToChar(VK vk) {
  auto v = static_cast<u16>(vk);
  switch (vk) {
  case VK::kReturn:
    return 13;
  case VK::kEscape:
    return 27;
  case VK::kTab:
    return 9;
  case VK::kBack:
    return 8;
  case VK::kLeft:
    return 37;
  case VK::kUp:
    return 38;
  case VK::kRight:
    return 39;
  case VK::kDown:
    return 40;
  case VK::kLWin:
    return 91;
  case VK::kRWin:
    return 92;
  case VK::kAdd:
    return 107;
  case VK::kSubtract:
    return 109;
  default:
    if (v >= '0' && v <= '9')
      return v;
    if (v >= 'A' && v <= 'Z')
      return v + 0x20; // lowercase
    if (v >= static_cast<u16>(VK::kNumpad0) &&
        v <= static_cast<u16>(VK::kNumpad9))
      return v;
    return v;
  }
}

/**
 * @brief Poll host keyboard and write the first NEWLY pressed key into the guest
 *        buffer. Uses edge detection so held keys don't repeat every frame. Called
 *        once per frame before bdMainGameStep reads the buffer.
 */
inline void PollKeyboardToGuest() {
  auto *ks = rex::system::kernel_state();
  if (!ks || !ks->memory())
    return;

  auto *window = ks->emulator() ? ks->emulator()->display_window() : nullptr;
  if (!window || !window->HasFocus())
    return;

  auto *entry = reinterpret_cast<GuestKeyEntry *>(
      ks->memory()->virtual_membase() + kGuestKeyBufferAddr);

  static bool prev_down[kPolledKeyCount] = {};

#ifdef _WIN32
  // Windows: GetAsyncKeyState queries hardware state directly from any thread.
  bool now_down[kPolledKeyCount];
  for (size_t i = 0; i < kPolledKeyCount; ++i) {
    now_down[i] =
        (GetAsyncKeyState(static_cast<int>(kPolledKeys[i])) & 0x8000) != 0;
  }

  u8 mods = 0;
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    mods |= 1;
  if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    mods |= 2;
  if (GetAsyncKeyState(VK_MENU) & 0x8000)
    mods |= 4;

  VK found_vk = VK::kNone;
  for (size_t i = 0; i < kPolledKeyCount; ++i) {
    if (now_down[i] && !prev_down[i]) {
      found_vk = kPolledKeys[i];
      break;
    }
  }

  for (size_t i = 0; i < kPolledKeyCount; ++i) {
    prev_down[i] = now_down[i];
  }
#else
  // Linux/SDL: SDL_GetKeyboardState returns cached state updated by event pump.
  const bool *sdl_state = SDL_GetKeyboardState(nullptr);

  u8 mods = 0;
  SDL_Keymod sdl_mods = SDL_GetModState();
  if (sdl_mods & SDL_KMOD_SHIFT)
    mods |= 1;
  if (sdl_mods & SDL_KMOD_CTRL)
    mods |= 2;
  if (sdl_mods & SDL_KMOD_ALT)
    mods |= 4;

  VK found_vk = VK::kNone;
  for (size_t i = 0; i < kKeyMappingCount; ++i) {
    bool now_down = sdl_state[kKeyMappings[i].scancode];
    if (now_down && !prev_down[i]) {
      found_vk = kKeyMappings[i].vk;
      break;
    }
  }

  for (size_t i = 0; i < kKeyMappingCount; ++i) {
    prev_down[i] = sdl_state[kKeyMappings[i].scancode];
  }
#endif

  if (found_vk != VK::kNone) {
    entry->vkCode = static_cast<u16>(found_vk);
    entry->charCode = VkToChar(found_vk);
    entry->modifiers = mods;
  } else {
    entry->vkCode = 0;
    entry->charCode = 0;
    entry->modifiers = 0;
  }
}

} // namespace bd
