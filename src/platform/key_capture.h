/**
 * @file    platform/key_capture.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <cstddef>
#include <string>

#include <rex/types.h>
#include <rex/hid/virtual_key.h>

namespace bd::platform {

using VK = rex::hid::VirtualKey;

inline constexpr VK kBindableKeys[] = {
    VK::kLButton, VK::kRButton, VK::kMButton, VK::kA, VK::kB, VK::kC, VK::kD,
    VK::kE, VK::kF, VK::kG, VK::kH, VK::kI, VK::kJ, VK::kK, VK::kL, VK::kM,
    VK::kN, VK::kO, VK::kP, VK::kQ, VK::kR, VK::kS, VK::kT, VK::kU, VK::kV,
    VK::kW, VK::kX, VK::kY, VK::kZ, VK::k0, VK::k1, VK::k2, VK::k3, VK::k4,
    VK::k5, VK::k6, VK::k7, VK::k8, VK::k9, VK::kF1, VK::kF2, VK::kF3,
    VK::kF4, VK::kF5, VK::kF6, VK::kF7, VK::kF8, VK::kF9, VK::kF10, VK::kF11,
    VK::kF12, VK::kOem3, VK::kOemMinus, VK::kOemPlus, VK::kOemComma,
    VK::kOemPeriod, VK::kOem1, VK::kOem2, VK::kOem5, VK::kOem4, VK::kOem6,
    VK::kOem7, VK::kEscape, VK::kReturn, VK::kSpace, VK::kTab, VK::kBack,
    VK::kDelete, VK::kInsert, VK::kHome, VK::kEnd, VK::kPrior, VK::kNext,
    VK::kLeft, VK::kRight, VK::kUp, VK::kDown, VK::kNumpad0, VK::kNumpad1,
    VK::kNumpad2, VK::kNumpad3, VK::kNumpad4, VK::kNumpad5, VK::kNumpad6,
    VK::kNumpad7, VK::kNumpad8, VK::kNumpad9, VK::kAdd, VK::kSubtract,
    VK::kMultiply, VK::kDivide, VK::kCapital,
};

// Declaration order is also the key atlas's cell order, which
// engine/glyph_set.cpp indexes into. Inserting a name shifts every cell after
// it, so res/embed/glyphs has to be redrawn whenever this list changes.
inline constexpr size_t kBindableKeyCount =
    sizeof(kBindableKeys) / sizeof(kBindableKeys[0]);


void SetCapturePadButtons(u32 buttons);

// Snapshot current key state. Keys already held are ignored until released.
void BeginKeyCapture();

// The first key newly pressed since BeginKeyCapture, as a rex keybind name
// ("Space", "LMB", "RMB"), or empty while none. A held modifier becomes a
// prefix, so Shift plus W returns "Shift+W". Ends the capture on a hit.
//
// Reported only once the key is released. Committing while it is still down
// hands the MnK driver a held key that matches the fresh bind, and the
// synthetic press that falls out re-fires the row just bound.
//
// Nothing is reserved. A key swallowed to mean cancel or clear could never be
// bound, so those live on the list behind it instead.
std::string PollKeyCapture();

// True from the hit's key-down until PollKeyCapture reports it. The pad's
// cancel has to stand down through this window: the driver may synthesize a
// cancel press out of the very key being captured.
bool KeyCapturePending();

bool KeyCaptureCanceled();

} // namespace bd::platform
