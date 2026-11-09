/**
 * @file    vfs/disc_io.cpp
 * @brief   Disc handling: multi-DVD removal, IO channel scratch buffers, the
 *          shipped pack fallback, and the file load failure dialog.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "generated/reblue_init.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/ppc/detour.h>
#include <rex/runtime.h>
#include <rex/types.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include <rex/app.h>
#include <rex/ppc/midasm.h>
#include "platform/platform.h"
#include "vfs/access_log.h"
#include "vfs/file_system.h"
#include "vfs/vfs.h"

// bdGetDiscForStage(a1): a1=0 used as a bool (non-zero requests disc change),
// a1=1 compared against current disc / stored as save metadata. Return 0 for
// a1=0 (no GameDiscChange) and 1 for a1=1 (saves show "Disc 1").
REX_MIDASM_BRANCH(bdSkipDiscNumberStore, ()) { return true; }
REX_MIDASM_AT(bdSkipDiscNumberStore, 0x820CEAA8, before, 0x820CEAAC);

u32 bdGetDiscForStage_hook(u32 a1) { return (a1 == 0) ? 0 : 1; }

REX_HOOK(bdGetDiscForStage, bdGetDiscForStage_hook);

// BD routes every fatal file load failure into bdFileErrorHandler: black
// screen plus XAM dirty disc UI, and the XAM calls are stubbed, so natively it
// hangs. bdIOThreadReadFile returns 0 for both failure modes and feeds all
// three of its callers, so record the failed path there and replace
// bdFileErrorHandler with a host messagebox naming the file.

namespace {

// Localized full path of the most recent file the IO worker failed to read.
// Written on the IO thread, read on the thread that hits the fatal handler.
std::mutex g_failedFileMutex;
std::string g_lastFailedFile;

constexpr size_t kPathMax = 260;

// BdIoOp_t::mode, picked by bdFileOpenReadSync. Only the resident pack mode is
// one reblue ever writes.
constexpr u32 kIoModePackResident = 2;
constexpr u32 kIoNotCompressed = 0;
constexpr u32 kIoNoPackNode = 0;
constexpr u32 kIoNothingLeftToRead = 0;

// The engine's IO op, allocated by bdFileReadStart. Not the caller's
// ReadRequest, which that function takes as its own argument and parks at
// +0x144. bdPathLocalize writes the path inline.
struct BdIoOp_t {
  be_u32 mode;
  be_u32 packNode;     // released once the read finishes
  be_u32 allocType;    // bdFilePhysicalMallocRead key
  be_u32 priority;     // IO channel load weight
  char path[kPathMax]; // localized, NUL-terminated
  be_u32 handle;
  be_u32 buffer;   // destination, allocated by the open
  be_u32 source;   // resident-pack source
  be_u32 readSize; // bytes still to read or copy
  be_u32 fileSize; // uncompressed size, reported to the caller
  be_u32 compressed;
  be_u32 retryFlag;
  be_u32 offset;
  be_u32 chunkSize;
  be_u32 doneSize;
  be_u32 callback; // cb(request, callbackCtx)
  be_u32 callbackCtx;
  be_u32 request; // caller's ReadRequest, 0 once cancelled
  be_u32 next;    // channel queue link
};
static_assert(offsetof(BdIoOp_t, path) == 0x010);
static_assert(offsetof(BdIoOp_t, handle) == 0x114);
static_assert(offsetof(BdIoOp_t, buffer) == 0x118);
static_assert(offsetof(BdIoOp_t, readSize) == 0x120);
static_assert(offsetof(BdIoOp_t, fileSize) == 0x124);
static_assert(offsetof(BdIoOp_t, compressed) == 0x128);
static_assert(offsetof(BdIoOp_t, request) == 0x144);
static_assert(sizeof(BdIoOp_t) == 0x14C);

std::string OpPath(u32 op_addr) {
  const auto *op = bd::mem::at<const BdIoOp_t>(op_addr);
  if (!op)
    return {};
  return std::string(op->path, ::strnlen(op->path, kPathMax));
}

} // namespace

// bdFileSystemInit gives only IO channel 0 a scratch buffer. Compressed pack
// reads stage through it and each ReadFile chunk is clamped to
// min(remaining, scratchSize), so a compressed read routed to channels 1-3
// computes chunk = 0 and spins the worker forever. An area transition can drop
// a read's enqueue-time Priority pack hit and route it there. Reproduces on
// Xenia too, so it is the engine's race and not the recompilation's.
//
// The scratch comes from the engine's own read buffer allocator and is never
// freed, channels living for the process. Installed lazily on the channel's own
// worker thread, so nothing races the field and the engine heap is live.

REX_IMPORT(__imp__bdFilePhysicalMallocRead, guest_bdFilePhysicalMallocRead,
           u32(u32, u32));

namespace {

struct IoChannel_t {
  u8 _pad000[0x30];
  be_u32 index;       // logged for diagnostics
  be_u32 scratch_ptr; // guest VA of the channel scratch buffer
  be_u32 scratch_size;
};
static_assert(offsetof(IoChannel_t, index) == 0x30);
static_assert(offsetof(IoChannel_t, scratch_ptr) == 0x34);
static_assert(offsetof(IoChannel_t, scratch_size) == 0x38);

constexpr u32 kIoScratchSize = 0x200000; // match channel 0

void AllocateChannelScratch(u32 channel) {
  auto *ch = bd::mem::at<IoChannel_t>(channel);
  if (!ch || ch->scratch_ptr)
    return;
  const u32 buf = guest_bdFilePhysicalMallocRead(1, kIoScratchSize);
  const u32 idx = ch->index;
  if (!buf) {
    REXLOG_ERROR(bd, "disc IO channel {} scratch alloc failed, compressed reads "
             "on this channel would hang",
             idx);
    return;
  }
  ch->scratch_ptr = buf;
  ch->scratch_size = kIoScratchSize;
}

} // namespace

// bdIOThreadReadFile (worker channel, IO op). Returns 0 on
// file-not-found or read error, 1 on success.
REX_DETOUR(bdIOThreadReadFile, u32, (u32 channel, u32 op)) {
  AllocateChannelScratch(channel);

  const u32 result = original(channel, op);
  if (result || !op)
    return result;

  std::string path = OpPath(op);
  {
    std::lock_guard lock(g_failedFileMutex);
    g_lastFailedFile = path;
  }
  REXLOG_ERROR(bd, "disc file read failed: '{}' ({})", path,
           bd::platform::ResourceUse());
  return result;
}

// A request of total size 0 reaches ReadFile with r5 = 0 (0-byte files ship on
// disc, e.g. pack\game_startse.ipk) and the read loop's retry bail covers only
// pack reads, so a direct file spins the IO worker forever. Returning true
// jumps to the success continuation, where bytesRead is already 0.
//
// Only safe when the request TOTAL is 0. A chunk clamped to a zero scratch size
// also arrives with r5 = 0 and would spin on that jump, but
// AllocateChannelScratch above kills it at the source.
REX_MIDASM_BRANCH(bdZeroLengthReadGuard, (PPCRegister &r5, PPCRegister &r31)) {
  if (r5.u32 != 0)
    return false;
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true))
    REXLOG_WARN(bd, "disc zero-length read guarded (recovering): '{}'",
            OpPath(r31.u32));
  return true;
}
REX_MIDASM_AT(bdZeroLengthReadGuard, 0x82271AE0, before, 0x82271AEC, r5, r31);

// bdFileOpenReadSync returns 0 only when the path is in no registered pack and
// opens as no file, the last point before bdIOThreadReadFile turns that into
// the fatal handler above, so the Fallback tier belongs here.
REX_DETOUR(bdFileOpenReadSync, u32, (u32 opAddr)) {
  auto *op = bd::mem::at<BdIoOp_t>(opAddr);

  const u32 result = original(opAddr);
  if (result)
    return result;

  if (!op || !op->request)
    return result; // cancelled, nothing left to complete

  const auto key = bd::vfs::Key::FromGuestPath(op->path).Localized();
  auto hit = bd::vfs::VFS::Get().Files().Read(key, bd::vfs::Tier::Fallback);
  if (!hit)
    return result;

  const u32 size = static_cast<u32>(hit->bytes.size());
  const u32 buffer = guest_bdFilePhysicalMallocRead(op->allocType, size);
  if (!buffer) {
    REXLOG_ERROR(bd, "disc no buffer for '{}' ({} bytes) from mount '{}'", key.str(),
             size, hit->mount);
    return result;
  }
  std::memcpy(bd::mem::at<u8>(buffer), hit->bytes.data(), size);

  // Handed back as an already-resident pack read: the bytes are in place, so
  // there is nothing left for the worker to copy and no pack node to release.
  op->mode = kIoModePackResident;
  op->packNode = kIoNoPackNode;
  op->buffer = buffer;
  op->source = buffer;
  op->readSize = kIoNothingLeftToRead;
  op->fileSize = size;
  op->compressed = kIoNotCompressed;

  bd::vfs::VFS::Get().Log().Record(key.string(), bd::vfs::FileSource::Pack);
  return 1;
}

// bdFileErrorHandler, natively a black screen thread then
// XamShowDirtyDiscErrorUI + reboot. Fully replaced.
void bdFileErrorHandler_hook() {
  std::string file;
  {
    std::lock_guard lock(g_failedFileMutex);
    file = g_lastFailedFile;
  }

  REXLOG_ERROR(bd, "disc file-load fatal, failed file: '{}'",
           file.empty() ? "<unknown>" : file);

  std::string body =
      file.empty()
          ? "Failed to load a required game file.\n\n"
            "Your installation may be incomplete or the data may be corrupt."
          : "Failed to load a required game file:\n  " + file +
                "\n\nYour installation may be incomplete or the data may be "
                "corrupt.";

  bd::platform::ShowFatal("File Load Error", body);
  rex::RequestShutdown(1);
}
REX_HOOK(bdFileErrorHandler, bdFileErrorHandler_hook);
