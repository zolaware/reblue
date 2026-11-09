/**
 * @file    core/memory_helpers.h
 * @brief   The Xbox 360 address space accessor.
 *
 *          A raw membase+va cast skips the 0xE0-heap 0x1000-byte compensation
 *          that TranslateVirtual applies. bd::mem is the one spelling that
 *          always routes through it.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <optional>
#include <string>
#include <type_traits>

#include <rex/kernel/guest_memory.h>
#include <rex/kernel/memory_view.h>
#include <rex/types.h>

namespace rex {
class GuestMemory;
class TitleRuntime;
} // namespace rex

namespace bd {

// rex/types.h exports be_u32 etc globally but not the be<> template itself.
template <typename T> using be = rex::BE<T>;

namespace mem {

void Bind(rex::kernel::TitleRuntime *title);
rex::kernel::GuestMemory *memory();
std::optional<rex::kernel::MemoryView> view();

// System heap blocks, which the title never allocates from. Zero on failure.
u32 SystemAlloc(u32 size, u32 alignment);
void SystemFree(u32 address);
void Zero(u32 address, u32 length);

// T is the type as stored in memory, a be<> scalar for byte-swapped fields.
// at() swaps nothing itself. Null when the kernel is down or va is 0.
template <typename T> inline T *at(u32 va) {
  const std::optional<rex::kernel::MemoryView> v = view();
  if (!v || !va)
    return nullptr;
  return v->template TranslateVirtual<T *>(va);
}

// Scalar read through be<T>, so multi-byte types come back host-order.
template <typename T> inline T load(u32 va, T fallback = T{}) {
  auto *p = at<be<T>>(va);
  return p ? static_cast<T>(*p) : fallback;
}

template <typename T> inline bool store(u32 va, T value) {
  auto *p = at<be<T>>(va);
  if (!p)
    return false;
  *p = value;
  return true;
}

// Most bytes a guest C string read copies out, MAX_PATH.
inline constexpr size_t kMaxCStringBytes = 260;

// A guest C string copied out, empty when va is 0 or unreadable. The copy ends
// at max_length bytes, at the terminator, or where readable memory ends.
inline std::string cstr(u32 va, size_t max_length = kMaxCStringBytes) {
  const std::optional<rex::kernel::MemoryView> v = view();
  if (!v)
    return {};
  const auto text = v->TryGetCString(va, max_length);
  return text ? std::string(*text) : std::string();
}

// ---- checked accessors ----
//
// at() translates whatever it is handed, since TranslateVirtual never fails, so
// a chained guest pointer freed and reused as unrelated data still resolves to
// a host address and faults on read. The try_ forms validate first, checking
// for non-null, aligned for T, and inside a committed readable heap, then
// return null or the fallback. Use them for anything reading a pointer chain
// off the guest thread: the console, the UI, the state readers. Guest thread
// sites that know their pointer is live can stay on at()/load().
//
// These are defined out of line, since the validation needs the title memory and
// this header is included nearly everywhere.

template <typename T> inline T *try_at(u32 va) {
  const std::optional<rex::kernel::MemoryView> v = view();
  return v ? v->template TryTranslateVirtual<T *>(va) : nullptr;
}

template <typename T> inline T try_load(u32 va, T fallback = T{}) {
  auto *p = try_at<const be<T>>(va);
  return p ? static_cast<T>(*p) : fallback;
}

// Validates readability, not writability, matching the accessor this replaced:
// the heaps it writes into are committed read-write, so a write-protect check
// would reject nothing it accepts today.
template <typename T> inline bool try_store(u32 va, T value) {
  auto *p = try_at<be<T>>(va);
  if (!p)
    return false;
  *p = value;
  return true;
}

// Separate from try_load(base + off) for the null base guard: a null base would
// otherwise read the offset as an address.
template <typename T> inline T try_field(u32 base, u32 off, T fallback = T{}) {
  return base ? try_load<T>(base + off, fallback) : fallback;
}

} // namespace mem
} // namespace bd
