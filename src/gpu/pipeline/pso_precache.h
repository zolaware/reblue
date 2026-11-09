/**
 * @file    gpu/pipeline/pso_precache.h
 * @brief   Background worker pool that compiles PipelineState requests
 *          via GetOrCreatePipeline off the render thread.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <rex/types.h>

#include "gpu/pipeline/pipeline_state.h"

namespace bd::gpu {

// Outstanding compiles for one load batch. Decremented after every build
// attempt, success OR failure, so a failed compile cannot gate a load forever.
class CompileToken {
public:
  u32 Total() const { return total_.load(std::memory_order_acquire); }

  void AddPending() {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    total_.fetch_add(1, std::memory_order_acq_rel);
  }
  void ReleasePending() { pending_.fetch_sub(1, std::memory_order_acq_rel); }

private:
  std::atomic<u32> pending_{0};
  std::atomic<u32> total_{0};
};

using TokenPtr = std::shared_ptr<CompileToken>;

bool PrecacheEnabled();

// 'state' MUST already be sanitized (SanitizePipelineState) so its dedup hash
// matches the cache key. Auto-attaches to the thread's active load capture
// (BeginLoadCapture), which routes the work to the priority lane so it
// compiles ahead of background coverage. No-op until host device.
void EnqueuePipeline(const PipelineState &state);

// Tokenless priority lane enqueue for work about to be needed, e.g.
// boot residual entries drained right before first use.
void EnqueuePipelinePriority(const PipelineState &state);

// Load capture bracket, thread-local so concurrent loads capture independently.
// Between Begin and End, EnqueuePipeline calls auto-attach to this load's
// token and enter the priority lane. EndLoadCapture is non-blocking:
// it does NOT hold the load thread waiting on compiles. Any PSO not warm by its
// first draw compiles lazily on the render thread (pipeline_cache).
void BeginLoadCapture();
void EndLoadCapture();

// Stops the compiler threads and joins them. Queued work is dropped.
void StopPSOPrecache();

} // namespace bd::gpu
