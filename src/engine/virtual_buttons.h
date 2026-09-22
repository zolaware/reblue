/**
 * @file    engine/virtual_buttons.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

namespace bd::engine {

// True while a d2anime menu has input focus. Published by the AnimeMenu_Update
// hook and read by the button layer, so a key can mean one thing in the field
// and another inside a menu.
bool MenuOwnsInput();
void SetMenuOwnsInput(bool owns);

// True while one of reblue's own ImGui surfaces is up. Those need the system
// pointer, and the SDK's input-active callback only knows about the overlays
// reblue replaced, so reblue has to speak for its own. Everything the pointer
// layer does stands down for it: mouse look lets the cursor go, the drawn
// cursor is not drawn, the arrow is not hidden, and hover stops following the
// mouse onto whatever engine list happens to be behind the window.
bool HostOverlayOwnsPointer();

// Held for as long as a dialog needs the system pointer. An overlay that only
// draws must not take one, or the game cursor never comes back.
class HostPointerClaim {
public:
  HostPointerClaim();
  ~HostPointerClaim();
  HostPointerClaim(const HostPointerClaim &) = delete;
  HostPointerClaim &operator=(const HostPointerClaim &) = delete;
};

// Records that the engine pad reported a press this frame, and reads that back
// draining. True for a key the MnK driver turned into pad state too, so a
// caller telling the devices apart has to test the keyboard first.
void PadInputSeen();
bool TakePadInputSeen();

// owns input.
void UpdateMouseLook();

} // namespace bd::engine
