/**
 * @file    platform/key_capture.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "platform/key_capture.h"

#include <memory>

#include <rex/input_keys.h>
#include <rex/hid/binding.h>
#include <rex/hid/input_system.h>

#include "platform/hid.h"

namespace bd::platform {
namespace {

// Must not outlive the input system, so ShutdownKeyCapture drops it.
std::unique_ptr<rex::hid::IKeyCapture> g_capture;

rex::hid::IKeyCapture &Capture() {
  if (!g_capture)
    g_capture = HID().CreateKeyCapture({});
  return *g_capture;
}

} // namespace

void ShutdownKeyCapture() { g_capture.reset(); }

void SetCapturePadButtons(u32 buttons) { Capture().SetPadButtons(buttons); }

void BeginKeyCapture() { Capture().Begin(); }

std::string PollKeyCapture() {
  const auto source = Capture().Poll();
  return source ? bd::platform::HID().InputSourceToString(*source) : std::string();
}

bool KeyCapturePending() { return Capture().IsPending(); }

bool KeyCaptureCanceled() { return Capture().WasCanceled(); }

} // namespace bd::platform
