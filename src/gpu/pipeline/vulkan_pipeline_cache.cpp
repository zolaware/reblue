/**
 * @file    gpu/pipeline/vulkan_pipeline_cache.cpp
 * @brief   Persists the Vulkan VkPipelineCache between runs, for drivers with
 *          no implicit disk cache.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/pipeline/vulkan_pipeline_cache.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include <plume_render_interface.h>
#include <rex/platform/file.h>
#include <rex/kernel/title_runtime.h>
#include <rex/types.h>

#include "core/logging.h"
#include "platform/host_ui.h"
#include "core/cvar_table.h"

namespace bd::gpu {

namespace {

constexpr auto kSaveInterval = std::chrono::seconds(60);

std::mutex g_mutex;
plume::RenderDevice *g_device = nullptr;
size_t g_savedSize = 0;
std::jthread *g_saver = nullptr;

std::filesystem::path CacheFilePath() {
  std::filesystem::path root = bd::CacheRoot();
  if (root.empty())
    root = rex::platform::GetUserFolder();
  return root / "vulkan_pipeline_cache.bin";
}

void SaveIfGrown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<u8> data;
  if (!g_device || !g_device->getPipelineCacheData(data) ||
      data.size() <= g_savedSize)
    return;

  const auto path = CacheFilePath();
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      REXBD_WARN("vulkan pipeline cache: cannot open {}", tmp.string());
      return;
    }
    f.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
    if (!f) {
      REXBD_WARN("vulkan pipeline cache: write failed: {}", tmp.string());
      return;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    REXBD_WARN("vulkan pipeline cache: rename failed: {}", ec.message());
    std::filesystem::remove(tmp, ec);
    return;
  }
  g_savedSize = data.size();
  REXBD_DEBUG("vulkan pipeline cache: saved {} bytes to {}", data.size(),
               path.string());
}

void SaverLoop(const std::stop_token &stop) {
  std::mutex waitMutex;
  std::condition_variable_any wake;
  std::unique_lock<std::mutex> lock(waitMutex);
  while (!stop.stop_requested()) {
    wake.wait_for(lock, stop, kSaveInterval, [] { return false; });
    if (stop.stop_requested())
      break;
    SaveIfGrown();
  }
}

void StopSaver() {
  std::jthread *saver;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    saver = g_saver;
    g_saver = nullptr;
  }
  if (!saver)
    return;
  saver->request_stop();
  saver->join();
  delete saver;
}

} // namespace

void InitVulkanPipelineCache(plume::RenderDevice *device) {
  StopSaver();

  const auto path = CacheFilePath();
  std::vector<u8> blob;
  if (std::ifstream f(path, std::ios::binary | std::ios::ate); f) {
    blob.resize(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char *>(blob.data()),
           static_cast<std::streamsize>(blob.size()));
    if (!f)
      blob.clear();
  }
  if (blob.empty()) {
    REXBD_INFO("vulkan pipeline cache: none at {}", path.string());
  } else {
    const bool accepted =
        device->loadPipelineCacheData(blob.data(), blob.size());
    REXBD_INFO("vulkan pipeline cache: {} {} bytes from {}",
                accepted ? "accepted" : "rejected", blob.size(), path.string());
  }

  std::vector<u8> current;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_device = device;
    g_savedSize = device->getPipelineCacheData(current) ? current.size() : 0;
    g_saver = new std::jthread(SaverLoop);
  }
}

void ShutdownVulkanPipelineCache() {
  StopSaver();
  SaveIfGrown();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_device = nullptr;
}

} // namespace bd::gpu
