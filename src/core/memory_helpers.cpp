/**
 * @file    core/memory_helpers.cpp
 * @brief   The validating half of the Xbox 360 address space accessor.
 * @license BSD 3-Clause, see LICENSE
 */
#include "core/memory_helpers.h"

#include <atomic>
#include <optional>

#include <cstring>

#include <rex/kernel/title_runtime.h>

namespace bd::mem {

namespace {

std::atomic<rex::kernel::GuestMemory *> s_memory{nullptr};

} // namespace

void Bind(rex::kernel::TitleRuntime *title) {
  s_memory.store(title ? &title->GetGuestMemory() : nullptr,
                 std::memory_order_release);
}

rex::kernel::GuestMemory *memory() { return s_memory.load(std::memory_order_acquire); }

std::optional<rex::kernel::MemoryView> view() {
  rex::kernel::GuestMemory *mem = memory();
  if (!mem)
    return std::nullopt;
  return mem->GetView();
}

u32 SystemAlloc(u32 size, u32 alignment) {
  rex::kernel::GuestMemory *mem = memory();
  if (!mem)
    return 0;
  auto block = mem->Allocate(size, alignment, rex::kernel::GuestProtect::kReadWrite,
                                rex::kernel::GuestHeap::kSystem);
  return block ? *block : 0;
}

void SystemFree(u32 address) {
  if (rex::kernel::GuestMemory *mem = memory(); mem && address)
    (void)mem->Free(address);
}

void Zero(u32 address, u32 length) {
  if (auto *p = at<u8>(address))
    std::memset(p, 0, length);
}

} // namespace bd::mem
