/**
 * @file    core/memory_helpers.cpp
 * @brief   The validating half of the Xbox 360 address space accessor.
 * @license BSD 3-Clause, see LICENSE
 */
#include "core/memory_helpers.h"

#include <atomic>
#include <optional>

#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/runtime/xmemory.h>

namespace bd::mem {

namespace {

std::atomic<rex::Runtime *> s_runtime{nullptr};

} // namespace

void Bind(rex::Runtime *runtime) {
  s_runtime.store(runtime && runtime->GetMemory() ? runtime : nullptr,
                  std::memory_order_release);
}

rex::Runtime *runtime() { return s_runtime.load(std::memory_order_acquire); }

rex::runtime::Memory *memory() {
  rex::Runtime *rt = runtime();
  return rt ? rt->GetMemory() : nullptr;
}

std::optional<rex::runtime::MemoryView> view() {
  rex::runtime::Memory *mem = memory();
  if (!mem)
    return std::nullopt;
  return rex::runtime::MemoryView(*mem);
}

bool ready() { return runtime() != nullptr; }

void *try_translate(u32 va, u32 align) {
  rex::runtime::Memory *mem = memory();
  if (!mem || !va || (va & (align - 1)))
    return nullptr;

  auto *heap = mem->LookupHeap(va);
  u32 protect = 0;
  if (!heap || !heap->QueryProtect(va, &protect) ||
      !(protect & rex::runtime::kMemoryProtectRead)) {
    return nullptr;
  }
  return mem->TranslateVirtual<void *>(va);
}

} // namespace bd::mem
