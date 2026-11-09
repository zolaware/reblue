/**
 * @file    gpu/pipeline/vulkan_pipeline_cache.h
 * @brief   Persists the Vulkan VkPipelineCache between runs, for drivers with
 *          no implicit disk cache.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

namespace plume {
struct RenderDevice;
}

namespace bd::gpu {

// Loads the saved cache into the device and starts the periodic saver. Call
// after device creation and before any pipeline is compiled.
void InitVulkanPipelineCache(plume::RenderDevice *device);

// Stops the saver and writes a final save if the cache grew.
void ShutdownVulkanPipelineCache();

} // namespace bd::gpu
