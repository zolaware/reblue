/**
 * @file    platform/key_capture.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <cstddef>
#include <string>

#include <rex/types.h>

namespace bd::platform {

// Names must match the rex keybind names (rex::ui::ParseVirtualKey) so a
// captured name is directly storable as a keybind_* cvar value. Bare modifiers
// are absent on purpose: they are captured as prefixes, and a bind that is only
// a modifier can never fire under exact-match modifier semantics.
inline constexpr const char *kBindableKeys[] = {
    "LMB",         "RMB",       "MMB",        "A",           "B",
    "C",           "D",         "E",          "F",           "G",
    "H",           "I",         "J",          "K",           "L",
    "M",           "N",         "O",          "P",           "Q",
    "R",           "S",         "T",          "U",           "V",
    "W",           "X",         "Y",          "Z",           "0",
    "1",           "2",         "3",          "4",           "5",
    "6",           "7",         "8",          "9",           "F1",
    "F2",          "F3",        "F4",         "F5",          "F6",
    "F7",          "F8",        "F9",         "F10",         "F11",
    "F12",         "Backtick",  "Minus",      "Plus",        "Comma",
    "Period",      "Semicolon", "Slash",      "Backslash",   "LBracket",
    "RBracket",    "Quote",     "Escape",     "Return",      "Space",
    "Tab",         "Backspace", "Delete",     "Insert",      "Home",
    "End",         "PageUp",    "PageDown",   "Left",        "Right",
    "Up",          "Down",      "Numpad0",    "Numpad1",     "Numpad2",
    "Numpad3",     "Numpad4",   "Numpad5",    "Numpad6",     "Numpad7",
    "Numpad8",     "Numpad9",   "NumpadPlus", "NumpadMinus", "NumpadStar",
    "NumpadSlash", "CapsLock",
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
