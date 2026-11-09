/**
 * @file    platform/key_capture.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/key_capture.h"

#include <rex/hid/input_source.h>
#include <rex/hid/key_capture.h>

#include "platform/hid.h"

namespace bd::platform {
namespace {

rex::hid::KeyCapture &Capture() {
  static rex::hid::KeyCapture capture(HID());
  return capture;
}

} // namespace

void SetCapturePadButtons(u32 buttons) { Capture().SetPadButtons(buttons); }

void BeginKeyCapture() { Capture().Begin(); }

std::string PollKeyCapture() {
  const auto source = Capture().Poll();
  return source ? rex::hid::InputSourceToString(*source) : std::string();
}

bool KeyCapturePending() { return Capture().IsPending(); }

bool KeyCaptureCanceled() { return Capture().WasCanceled(); }

} // namespace bd::platform
