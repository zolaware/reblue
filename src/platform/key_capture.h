/**
 * @file    platform/key_capture.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <cstddef>
#include <string>

#include <rex/types.h>
#include <rex/input_keys.h>

namespace bd::platform {

using VK = rex::VirtualKey;

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


// Drops the capture before the input system goes away.
void ShutdownKeyCapture();

void SetCapturePadButtons(u32 buttons);

// Snapshot current key state. Keys already held are ignored until released.
void BeginKeyCapture();

// The next input as binding text ("Space", "Shift+W", "LMB", "WheelUp",
// "PadA"), or empty while none. Reported once released, so a key still down
// cannot re-fire the row just bound. Escape held or both triggers cancel,
// because nothing is reserved. Ends the capture on a hit.
std::string PollKeyCapture();

// True from the hit's key-down until PollKeyCapture reports it. The pad's
// cancel has to stand down through this window: the driver may synthesize a
// cancel press out of the very key being captured.
bool KeyCapturePending();

bool KeyCaptureCanceled();

} // namespace bd::platform
