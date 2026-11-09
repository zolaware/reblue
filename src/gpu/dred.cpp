/**
 * @file    gpu/dred.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "gpu/dred.h"

#if !defined(REBLUE_D3D12)

namespace bd::gpu {
bool EnableDred() { return false; }
bool PrepareDredCommandObjects() { return false; }
void LogDredReport(const char *) {}
} // namespace bd::gpu

#else

#include <algorithm>
#include <format>
#include <iterator>
#include <rex/string/utf8.h>
#include <rex/types.h>
#include <string>

#include <plume_d3d12.h>

#include "core/logging.h"
#include "gpu/device.h"
#include "gpu/settings.h"

namespace bd::gpu {

namespace {

// The ops the GPU had not finished are the interesting ones, so the dump is a
// window ending at the failure point rather than the whole history.
constexpr u32 kOpsBefore = 24;
constexpr u32 kOpsAfter = 4;

// Indexed by D3D12_AUTO_BREADCRUMB_OP. A value past the end prints numerically.
constexpr const char *kOpNames[] = {
    "SetMarker",
    "BeginEvent",
    "EndEvent",
    "DrawInstanced",
    "DrawIndexedInstanced",
    "ExecuteIndirect",
    "Dispatch",
    "CopyBufferRegion",
    "CopyTextureRegion",
    "CopyResource",
    "CopyTiles",
    "ResolveSubresource",
    "ClearRenderTargetView",
    "ClearUnorderedAccessView",
    "ClearDepthStencilView",
    "ResourceBarrier",
    "ExecuteBundle",
    "Present",
    "ResolveQueryData",
    "BeginSubmission",
    "EndSubmission",
    "DecodeFrame",
    "ProcessFrames",
    "AtomicCopyBufferUint",
    "AtomicCopyBufferUint64",
    "ResolveSubresourceRegion",
    "WriteBufferImmediate",
    "DecodeFrame1",
    "SetProtectedResourceSession",
    "DecodeFrame2",
    "ProcessFrames1",
    "BuildRaytracingAccelerationStructure",
    "EmitRaytracingAccelerationStructurePostbuildInfo",
    "CopyRaytracingAccelerationStructure",
    "DispatchRays",
    "InitializeMetaCommand",
    "ExecuteMetaCommand",
    "EstimateMotion",
    "ResolveMotionVectorHeap",
    "SetPipelineState1",
    "InitializeExtensionCommand",
    "ExecuteExtensionCommand",
    "DispatchMesh",
    "EncodeFrame",
    "ResolveEncoderOutputMetadata",
    "Barrier",
    "BeginCommandList",
    "DispatchGraph",
    "SetProgram",
};

std::string OpName(D3D12_AUTO_BREADCRUMB_OP op) {
  const auto i = static_cast<size_t>(op);
  if (i < std::size(kOpNames))
    return kOpNames[i];
  return std::format("op#{}", i);
}

const char *AllocationTypeName(D3D12_DRED_ALLOCATION_TYPE type) {
  switch (type) {
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:
    return "CommandQueue";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:
    return "CommandAllocator";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:
    return "PipelineState";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:
    return "CommandList";
  case D3D12_DRED_ALLOCATION_TYPE_FENCE:
    return "Fence";
  case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:
    return "DescriptorHeap";
  case D3D12_DRED_ALLOCATION_TYPE_HEAP:
    return "Heap";
  case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP:
    return "QueryHeap";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE:
    return "CommandSignature";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY:
    return "PipelineLibrary";
  case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:
    return "Resource";
  case D3D12_DRED_ALLOCATION_TYPE_PASS:
    return "Pass";
  case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT:
    return "StateObject";
  case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND:
    return "MetaCommand";
  case D3D12_DRED_ALLOCATION_TYPE_INVALID:
    return "Invalid";
  default:
    return "other";
  }
}

std::string NodeName(const char *narrow, const wchar_t *wide) {
  if (narrow && *narrow)
    return narrow;
  if (wide && *wide)
    return rex::ToUTF8(wide);
  return "(unnamed)";
}

void LogBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE1 *head) {
  // Retired lists leave the node list, so an idle GPU and one that never
  // recorded look the same from here.
  if (!head) {
    REXLOG_CRITICAL(bd, "dred no breadcrumb nodes: nothing in flight at removal, or "
                "breadcrumbs never recorded");
    return;
  }

  u32 total = 0;
  u32 stalled = 0;
  for (const auto *node = head; node; node = node->pNext, ++total) {
    const bool has_history = node->pLastBreadcrumbValue != nullptr &&
                             node->pCommandHistory != nullptr;
    // A count of zero never started and a full count fully retired, so only a
    // list caught mid-execution can be the one that hung.
    const u32 done = has_history ? *node->pLastBreadcrumbValue : 0;
    const bool mid = has_history && done > 0 && done < node->BreadcrumbCount;
    const char *state = !has_history ? "no history"
                        : done == 0  ? "not started"
                        : mid        ? "MID-EXECUTION"
                                     : "retired";
    REXLOG_CRITICAL(bd, 
        "dred node {}: list '{}' queue '{}' {}/{} ops - {}", total,
        NodeName(node->pCommandListDebugNameA, node->pCommandListDebugNameW),
        NodeName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW),
        done, node->BreadcrumbCount, state);
    if (!mid)
      continue;

    ++stalled;
    REXLOG_CRITICAL(bd, "dred   stopped on {}", OpName(node->pCommandHistory[done]));

    const u32 first = done > kOpsBefore ? done - kOpsBefore : 0;
    const u32 last = std::min(node->BreadcrumbCount, done + kOpsAfter + 1);
    for (u32 i = first; i < last; ++i) {
      REXLOG_CRITICAL(bd, "dred   [{:5}] {}{}", i, OpName(node->pCommandHistory[i]),
                  i == done ? "  <-- did not complete" : "");
    }
    for (u32 c = 0; c < node->BreadcrumbContextsCount; ++c) {
      const auto &ctx = node->pBreadcrumbContexts[c];
      REXLOG_CRITICAL(bd, "dred   context @{}: {}", ctx.BreadcrumbIndex,
                  ctx.pContextString ? rex::ToUTF8(ctx.pContextString) : "");
    }
  }
  if (stalled == 0)
    REXLOG_CRITICAL(bd, "dred {} node(s), none mid-execution: the removal did not "
                "interrupt an op",
                total);
}

void LogAllocations(const char *what, const D3D12_DRED_ALLOCATION_NODE1 *head) {
  u32 n = 0;
  for (const auto *node = head; node; node = node->pNext, ++n) {
    REXLOG_CRITICAL(bd, "dred   {} [{}] {} '{}'", what, n,
                AllocationTypeName(node->AllocationType),
                NodeName(node->ObjectNameA, node->ObjectNameW));
  }
  if (n == 0)
    REXLOG_CRITICAL(bd, "dred   {}: none", what);
}

ID3D12Device *D3D12Device() {
  auto *dev = static_cast<plume::D3D12Device *>(state().device.get());
  return dev ? dev->d3d : nullptr;
}

} // namespace

bool EnableDred() {
  if (!Settings::Get().DRED())
    return false;
  ID3D12DeviceRemovedExtendedDataSettings1 *settings = nullptr;
  const HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&settings));
  if (FAILED(hr) || !settings) {
    REXLOG_WARN(bd, "dred settings interface unavailable ({:#010x})",
            static_cast<u32>(hr));
    return false;
  }
  settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  settings->Release();
  return true;
}

bool PrepareDredCommandObjects() {
  auto *d3d = D3D12Device();
  if (!d3d)
    return false;

  auto &s = state();
  if (s.queue) {
    if (auto *q = static_cast<plume::D3D12CommandQueue *>(s.queue.get())->d3d)
      q->SetName(L"reblue direct queue");
  }
  for (u32 i = 0; i < kNumFrames; ++i) {
    if (!s.command_lists[i])
      continue;
    auto *list =
        static_cast<plume::D3D12CommandList *>(s.command_lists[i].get());
    if (list->d3d)
      list->d3d->SetName((L"reblue frame list " + std::to_wstring(i)).c_str());
  }

  // Breadcrumbs ride on WriteBufferImmediate, so a queue type missing from
  // these flags records nothing whatever the settings say.
  D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3 = {};
  if (FAILED(d3d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &o3,
                                      sizeof(o3))))
    return false;
  return (o3.WriteBufferImmediateSupportFlags &
          D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT) != 0;
}

void LogDredReport(const char *context) {
  auto *d3d = D3D12Device();
  if (!d3d)
    return;
  if (!context)
    context = "?";

  ID3D12DeviceRemovedExtendedData1 *dred = nullptr;
  if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(&dred))) || !dred) {
    REXLOG_CRITICAL(bd, "dred unavailable at {}", context);
    return;
  }

  REXLOG_CRITICAL(bd, "dred ==== device removed extended data ({}) ====", context);

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
  if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
    LogBreadcrumbs(breadcrumbs.pHeadAutoBreadcrumbNode);
  else
    REXLOG_CRITICAL(bd, "dred breadcrumb output unavailable");

  D3D12_DRED_PAGE_FAULT_OUTPUT1 fault = {};
  if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&fault))) {
    if (fault.PageFaultVA)
      REXLOG_CRITICAL(bd, "dred page fault at GPU VA {:#018x}", fault.PageFaultVA);
    else
      REXLOG_CRITICAL(bd, "dred no page fault recorded (engine timeout)");
    // A name under 'freed' answers what the GPU still referenced after we
    // destroyed it.
    LogAllocations("live", fault.pHeadExistingAllocationNode);
    LogAllocations("freed", fault.pHeadRecentFreedAllocationNode);
  } else {
    REXLOG_CRITICAL(bd, "dred page-fault output unavailable");
  }

  REXLOG_CRITICAL(bd, "dred ==== end ====");
  dred->Release();
}

} // namespace bd::gpu

#endif // REBLUE_D3D12
