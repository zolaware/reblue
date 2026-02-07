/**
 * @file        bdengine/vfs.cpp
 * @brief       Virtual file system - content handler registry + file I/O hooks.
 *
 *              Intercepts bdFileExistsCheck and bdFileReadStart to serve:
 *              1. Virtual files registered via ContentProvider callbacks
 *              2. Loose mod file overrides (from ModManager)
 *              3. Original game files (fallthrough to engine)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause -- see LICENSE
 */
#include "bdengine/mods/vfs.h"
#include "bdengine/common/logging.h"
#include "bdengine/mods/mod_manager.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <rex/types.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>

REXCVAR_DECLARE(bool, bd_mod_log);

namespace {
constexpr u32 kHeapHandleAddr = 0x827A8580;
}  // namespace

namespace ReadReq {
constexpr u32 kStatus    = 0x00;
constexpr u32 kAllocType = 0x06;
constexpr u32 kBuffer    = 0x08;
constexpr u32 kFileSize  = 0x0C;
constexpr u32 kBusyFlag  = 0x10;
}  // namespace ReadReq

REX_IMPORT(__imp__hcHeapAlloc, hcHeapAlloc, u32(u32, u32, u32));

namespace {

struct VfsRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, bd::vfs::ContentProvider> handlers;
};

VfsRegistry& GetRegistry() {
    static VfsRegistry reg;
    return reg;
}

}  // namespace

namespace bd::vfs {

void RegisterContentHandler(const std::string& path, ContentProvider provider) {
    auto& reg = GetRegistry();
    std::lock_guard lock(reg.mutex);
    reg.handlers[path] = std::move(provider);
    BD_INFO("[vfs] registered handler for '{}'", path);
}

void UnregisterContentHandler(const std::string& path) {
    auto& reg = GetRegistry();
    std::lock_guard lock(reg.mutex);
    auto it = reg.handlers.find(path);
    if (it != reg.handlers.end()) {
        reg.handlers.erase(it);
        BD_INFO("[vfs] unregistered handler for '{}'", path);
    }
}

bool Exists(const std::string& path) {
    auto& reg = GetRegistry();
    std::lock_guard lock(reg.mutex);
    return reg.handlers.count(path) > 0;
}

std::string NormalizePath(const char* raw_path) {
    return bd::GetModManager().NormalizePath(raw_path);
}

}  // namespace bd::vfs

namespace {

/**
 * @brief Try to invoke a VFS content provider for the given normalized path.
 *        Returns the content if a handler is registered, empty vector otherwise.
 */
std::vector<u8> TryVfsContent(const std::string& normalized_path) {
    auto& reg = GetRegistry();
    std::lock_guard lock(reg.mutex);
    auto it = reg.handlers.find(normalized_path);
    if (it != reg.handlers.end()) {
        return it->second();
    }
    return {};
}

/**
 * @brief Allocate guest memory via the game's heap allocator.
 */
u32 AllocGuestMemory(u32 size) {
    auto* base = rex::system::kernel_state()->memory()->virtual_membase();
    u32 heap = rex::memory::load_and_swap<u32>(base + kHeapHandleAddr);
    return hcHeapAlloc(heap, size, 0u);
}

/**
 * @brief Fill a guest ReadRequest struct with completed-read fields.
 */
void FillReadRequest(u8* base, u32 req_addr, u32 guest_buf, u32 size) {
    auto* req = base + req_addr;
    rex::memory::store_and_swap<u32>(req + ReadReq::kStatus,    2u);
    rex::memory::store_and_swap<u16>(req + ReadReq::kAllocType, 1u);
    rex::memory::store_and_swap<u32>(req + ReadReq::kBuffer,    guest_buf);
    rex::memory::store_and_swap<u32>(req + ReadReq::kFileSize,  size);
    rex::memory::store_and_swap<u32>(req + ReadReq::kBusyFlag,  0u);
}

}  // namespace

/**
 * @brief Hook for bdFileExistsCheck at guest address 0x822711E8.
 */
REX_EXTERN(__imp__bdFileExistsCheck);

REX_HOOK_RAW(bdFileExistsCheck) {
    auto& mgr = bd::GetModManager();
    mgr.EnsureInitialized();

    const char* raw_path = reinterpret_cast<const char*>(base + ctx.r3.u32);
    auto normalized = mgr.NormalizePath(raw_path);
    auto localized = bd::ModManager::LocalizePath(normalized);

    // Check VFS.
    auto content = TryVfsContent(localized);
    if (!content.empty()) {
        ctx.r3.u64 = static_cast<u32>(content.size());
        BD_INFO("[vfs:exists] '{}' ({}b)", localized, content.size());
        return;
    }

    // Check mod file overrides.
    auto* override_path = mgr.FindOverride(localized);
    if (override_path) {
        std::error_code ec;
        auto size = std::filesystem::file_size(*override_path, ec);
        if (!ec && size > 0) {
            BD_INFO("[mods:exists] HIT '{}' -> '{}' ({}b)",
                    localized, override_path->string(), size);
            ctx.r3.u64 = static_cast<u32>(size);
            if (REXCVAR_GET(bd_mod_log))
                mgr.RecordAccess(localized, bd::FileSource::Mod);
            return;
        }
        BD_ERROR("[mods:exists] override file unreadable: {}",
                 override_path->string());
    }

    // Fall through to the original engine handler.
    __imp__bdFileExistsCheck(ctx, base);

    if (REXCVAR_GET(bd_mod_log)) {
        auto source = ctx.r3.u64 > 0
            ? mgr.ClassifyOriginal(localized)
            : bd::FileSource::Missing;
        mgr.RecordAccess(localized, source);
    }
}

/**
 * @brief Hook for bdFileReadStart at guest address 0x82271520.
 */
REX_EXTERN(__imp__bdFileReadStart);

REX_HOOK_RAW(bdFileReadStart) {
    auto& mgr = bd::GetModManager();
    mgr.EnsureInitialized();

    const char* raw_path = reinterpret_cast<const char*>(base + ctx.r3.u32);
    auto normalized = mgr.NormalizePath(raw_path);
    auto localized = bd::ModManager::LocalizePath(normalized);

    // Check VFS.
    auto content = TryVfsContent(localized);
    if (!content.empty()) {
        u32 size = static_cast<u32>(content.size());
        u32 guest_buf = AllocGuestMemory(size);
        if (guest_buf) {
            std::memcpy(base + guest_buf, content.data(), size);
            FillReadRequest(base, ctx.r4.u32, guest_buf, size);
            ctx.r3.u64 = 1;
            BD_INFO("[vfs:read] '{}' ({}b)", localized, size);
        } else {
            BD_ERROR("[vfs] alloc failed for '{}' ({}b)", localized, size);
            ctx.r3.u64 = 0;
        }
        return;
    }

    // Check mod file overrides.
    auto* override_path = mgr.FindOverride(localized);
    if (!override_path) {
        // Fall through to the original engine handler.
        __imp__bdFileReadStart(ctx, base);
        if (REXCVAR_GET(bd_mod_log)) {
            auto source = ctx.r3.u64
                ? mgr.ClassifyOriginal(localized)
                : bd::FileSource::Missing;
            mgr.RecordAccess(localized, source);
        }
        return;
    }

    if (REXCVAR_GET(bd_mod_log))
        mgr.RecordAccess(localized, bd::FileSource::Mod);

    // Native file read for loose mod override.
    FILE* f = fopen(override_path->string().c_str(), "rb");
    if (!f) {
        BD_ERROR("[mods] failed to open: {}", override_path->string());
        __imp__bdFileReadStart(ctx, base);
        return;
    }

    fseek(f, 0, SEEK_END);
    auto file_size = static_cast<u32>(ftell(f));
    fseek(f, 0, SEEK_SET);

    u32 guest_buffer = AllocGuestMemory(file_size);
    if (!guest_buffer) {
        BD_ERROR("[mods] allocation failed for {} bytes", file_size);
        fclose(f);
        __imp__bdFileReadStart(ctx, base);
        return;
    }

    size_t bytes_read = fread(base + guest_buffer, 1, file_size, f);
    fclose(f);

    if (bytes_read != file_size) {
        BD_ERROR("[mods] partial read: {} of {} bytes", bytes_read, file_size);
        __imp__bdFileReadStart(ctx, base);
        return;
    }

    BD_INFO("[mods:read] HIT '{}' -> '{}' ({}b)",
            localized, override_path->string(), file_size);

    FillReadRequest(base, ctx.r4.u32, guest_buffer, file_size);
    ctx.r3.u64 = 1;
}
