/**
 * @file    gpu/pipeline/pso_precache.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/pipeline/pso_precache.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/logging.h"
#include "core/threading.h"
#include "gpu/device.h"
#include "gpu/pipeline/pipeline_cache.h"
#include "gpu/settings.h"

namespace bd::gpu {

namespace {

struct WorkItem {
  PipelineState state; // value copy, carries live GuestShader*/decl*
  TokenPtr token;      // null for ungated work
};

std::mutex g_queueMutex;
std::condition_variable_any g_queueCv;
// Two lanes share one mutex/cv. Priority holds a load's own predicted PSOs so
// they compile ahead of the background backlog (boot replay residual + global
// coverage), the set a draw in the first frames after a load needs warm.
std::deque<WorkItem> g_priorityQueue;
std::deque<WorkItem> g_queue;

std::mutex g_dedupMutex;
std::unordered_set<u64> g_queuedOrDone;

std::atomic<size_t> g_buildFailures{0};

std::once_flag g_startOnce;

// Thread-local so a loader thread brackets its own loads without disturbing
// other threads or the boot replay path.
thread_local TokenPtr t_currentLoadToken;

void ProcessItem(WorkItem &item) {
  try {
    if (!GetOrCreatePipeline(item.state)) {
      const size_t n =
          g_buildFailures.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n == 1 || (n & 0x3FF) == 0)
        REXLOG_WARN(bd, "pso_precache: GetOrCreatePipeline returned null ({} total, "
                "possible device-loss/transient-alloc failure)",
                n);
    }
  } catch (const std::exception &e) {
    REXLOG_ERROR(bd, "pso_precache GetOrCreatePipeline exception: {}", e.what());
  } catch (...) {
    REXLOG_ERROR(bd, "pso_precache GetOrCreatePipeline unknown exception");
  }

  // Release even on a nullptr build or thrown compile so the gate cannot hang.
  if (item.token)
    item.token->ReleasePending();
}

std::vector<std::jthread> g_workers;

void WorkerLoop(const std::stop_token &stop) {
  // A load hands every worker an 8ms link at once, and at normal priority that
  // convoy preempts the render thread and the frame stretches to 300ms.
  DemoteThreadToBackground();
  while (!stop.stop_requested()) {
    WorkItem item;
    {
      std::unique_lock<std::mutex> lock(g_queueMutex);
      if (!g_queueCv.wait(lock, stop, [] {
            return !g_priorityQueue.empty() || !g_queue.empty();
          }))
        return;
      if (!g_priorityQueue.empty()) {
        item = std::move(g_priorityQueue.front());
        g_priorityQueue.pop_front();
      } else if (!g_queue.empty()) {
        item = std::move(g_queue.front());
        g_queue.pop_front();
      } else {
        continue;
      }
    }
    try {
      ProcessItem(item);
    } catch (const std::exception &e) {
      REXLOG_ERROR(bd, "pso_precache worker exception: {}", e.what());
    } catch (...) {
      REXLOG_ERROR(bd, "pso_precache worker unknown exception");
    }
  }
}

void StartWorkerPool() {
  std::call_once(g_startOnce, [] {
    const unsigned hw = std::thread::hardware_concurrency();
    unsigned count = hw > 3 ? (hw * 2u) / 3u : 2u;
    if (hw > 5u && count > hw - 3u)
      count = hw - 3u; // leave 3 for the game
    if (count < 2u)
      count = 2u;
    std::lock_guard<std::mutex> lock(g_queueMutex);
    for (unsigned i = 0; i < count; ++i)
      g_workers.emplace_back(WorkerLoop);
    REXLOG_DEBUG(bd, "pso_precache: started {} compiler thread(s)", count);
  });
}

} // namespace

void StopPSOPrecache() {
  std::vector<std::jthread> workers;
  {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    workers.swap(g_workers);
  }
  for (auto &worker : workers)
    worker.request_stop();
  g_queueCv.notify_all();
}

bool PrecacheEnabled() { return Settings::Get().PSOPrecache(); }

namespace {

// Callers gate on master switch/device readiness so a disabled precache never
// starts the pool or poisons the dedup set.
void EnqueueResolved(const PipelineState &state, TokenPtr token,
                     bool priority) {
  const u64 key = HashPipelineState(state);
  {
    std::lock_guard<std::mutex> lock(g_dedupMutex);
    if (!g_queuedOrDone.insert(key).second)
      return;
  }

  StartWorkerPool();

  if (token)
    token->AddPending();
  {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (priority)
      g_priorityQueue.push_back(WorkItem{state, std::move(token)});
    else
      g_queue.push_back(WorkItem{state, std::move(token)});
  }
  g_queueCv.notify_one();
}

} // namespace

void EnqueuePipeline(const PipelineState &state) {
  if (!PrecacheEnabled() || !Video::HostDevice())
    return;
  TokenPtr token = t_currentLoadToken; // auto-attach to active load capture
  // Capture before the move: argument evaluation order is unspecified, and
  // std::move(token) below would otherwise race static_cast<bool>(token).
  const bool priority = static_cast<bool>(token);
  EnqueueResolved(state, std::move(token), priority);
}

void EnqueuePipelinePriority(const PipelineState &state) {
  if (!PrecacheEnabled() || !Video::HostDevice())
    return;
  EnqueueResolved(state, nullptr, true); // ignore t_currentLoadToken
}

void BeginLoadCapture() {
  t_currentLoadToken = std::make_shared<CompileToken>();
}

void EndLoadCapture() {
  TokenPtr token = std::move(t_currentLoadToken);
  t_currentLoadToken.reset();
  if (!token)
    return;
  const u32 enqueued = token->Total();
  if (enqueued == 0)
    return; // load reused only warm pipelines, nothing to do

  // Pushed to the priority lane and warm ahead of background work, so a heavy
  // load spawn still can't freeze the load thread for hundreds of ms.
  REXLOG_DEBUG(bd, "pso_precache: load queued {} priority pipeline(s) (cache total {})",
           enqueued, PipelineCacheSize());
}

} // namespace bd::gpu
