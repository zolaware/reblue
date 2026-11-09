/**
 * @file    gpu/device_lost.cpp
 * @brief   Device removal detection: how each backend notices, and the one-shot
 *          report that follows.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/device.h"

#include <atomic>
#include <format>

#include <plume_render_interface.h>
#if defined(REBLUE_D3D12)
#include <plume_d3d12.h>
#else
#include <plume_vulkan.h>
#endif

#include "core/logging.h"
#include <rex/app.h>
#include "gpu/dred.h"
#include "platform/platform.h"

namespace bd::gpu {

namespace {

#if defined(REBLUE_D3D12)
const char *DeviceRemovedReasonName(long hr) {
  switch (static_cast<unsigned long>(hr)) {
  case 0x887A0005ul:
    return "DXGI_ERROR_DEVICE_REMOVED";
  case 0x887A0006ul:
    return "DXGI_ERROR_DEVICE_HUNG";
  case 0x887A0007ul:
    return "DXGI_ERROR_DEVICE_RESET";
  case 0x887A0020ul:
    return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
  case 0x887A0001ul:
    return "DXGI_ERROR_INVALID_CALL";
  case 0x80070057ul:
    return "E_INVALIDARG";
  default:
    return "unknown";
  }
}
#endif

// Latched once, process-wide, so only the first detector reports + terminates.
std::atomic<bool> g_device_lost_reported{false};

// Shared tail once a backend has confirmed the loss. Always returns true, so a
// detector can tail-call it.
bool ReportDeviceLost(const char *context, const char *reason, u32 code) {
  // First detector wins, and the rest just see "lost" and bail without a second
  // dialog.
  bool expected = false;
  if (!g_device_lost_reported.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return true;
  }

  REXLOG_CRITICAL(bd, "graphics device removed (at {}): {:#010x} ({})",
              context ? context : "?", code, reason);
  // Before the dialog: this is the only window in which the runtime still holds
  // the breadcrumb and page fault data for the dead device.
  LogDredReport(context);
  rex::log::Flush();
  bd::platform::ShowFatal(
      "Graphics Device Lost",
      std::format("The graphics device was lost, so reblue has to close.\n\n"
                  "Reason: {} ({:#010x})",
                  reason, code));
  rex::RequestShutdown(1);
  return true;
}

} // namespace

#if defined(REBLUE_D3D12)
bool CheckDeviceRemoved(const char *context) {
  if (g_device_lost_reported.load(std::memory_order_acquire))
    return true;

  auto &s = state();
  auto *dev = static_cast<plume::D3D12Device *>(s.device.get());
  if (!dev || !dev->d3d)
    return false; // no device yet, nothing to lose
  const long hr = dev->d3d->GetDeviceRemovedReason();
  if (hr == 0 /*S_OK*/)
    return false; // device healthy, caller's failure is OOM/other

  return ReportDeviceLost(context, DeviceRemovedReasonName(hr),
                          static_cast<u32>(hr));
}
#else
// Vulkan exposes no removed-reason to poll, and a caller's own failure proves
// nothing: present() also returns false for the VK_ERROR_OUT_OF_DATE_KHR every
// resize produces. vkGetFenceStatus never blocks and reports VK_ERROR_DEVICE_LOST
// as its only non-signaled/unsignaled result.
bool CheckDeviceRemoved(const char *context) {
  if (g_device_lost_reported.load(std::memory_order_acquire))
    return true;

  auto &s = state();
  auto *dev = static_cast<plume::VulkanDevice *>(s.device.get());
  if (!dev || dev->vk == VK_NULL_HANDLE)
    return false; // no device yet
  for (const auto &fence : s.fences) {
    if (!fence)
      continue;
    const VkFence vk_fence =
        static_cast<plume::VulkanCommandFence *>(fence.get())->vk;
    if (vk_fence == VK_NULL_HANDLE)
      continue;
    if (vkGetFenceStatus(dev->vk, vk_fence) == VK_ERROR_DEVICE_LOST) {
      return ReportDeviceLost(context, "VK_ERROR_DEVICE_LOST",
                              static_cast<u32>(VK_ERROR_DEVICE_LOST));
    }
  }
  return false; // device healthy, caller's failure is out-of-date/OOM/other
}
#endif

bool DeviceIsLost() {
  return g_device_lost_reported.load(std::memory_order_acquire);
}

} // namespace bd::gpu
