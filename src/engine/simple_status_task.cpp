/**
 * @file    engine/simple_status_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/simple_status_task.h"

#include <cstddef>

#include "core/memory_helpers.h"

namespace bd::engine {

namespace {

constexpr size_t kRowCount = 5;

// One party card, holding the d2anime variable bag its row draws through.
struct SimpleStatusRow_t {
  /* 0x000 */ u8 _pad000[0x144];
  /* 0x144 */ be_u32 varBag;
  /* 0x148 */ u8 _pad148[0x160 - 0x148];
};
static_assert(sizeof(SimpleStatusRow_t) == 0x160);
static_assert(offsetof(SimpleStatusRow_t, varBag) == 0x144);

struct SimpleStatusTask_t {
  /* 0x000 */ u8 _pad000[0x70];
  /* 0x070 */ SimpleStatusRow_t rows[kRowCount];
  /* 0x750 */ u8 _pad750[0x7FC - 0x750];
  /* 0x7FC */ be_u32 anime;
  /* 0x800 */ be_u32 mode;
};
static_assert(offsetof(SimpleStatusTask_t, rows) == 0x070);
static_assert(offsetof(SimpleStatusTask_t, anime) == 0x7FC);
static_assert(offsetof(SimpleStatusTask_t, mode) == 0x800);

} // namespace

u32 SimpleStatusTask::Mode() const {
  const auto *self = Self<SimpleStatusTask_t>();
  return self ? static_cast<u32>(self->mode) : 0;
}

D2AnimeTask SimpleStatusTask::Anime() const {
  const auto *self = Self<SimpleStatusTask_t>();
  return D2AnimeTask(self ? static_cast<u32>(self->anime) : 0);
}

AnimeData SimpleStatusTask::RowVarBag(size_t row) const {
  const auto *self = Self<SimpleStatusTask_t>();
  if (!self || row >= kRowCount)
    return AnimeData();
  return AnimeData(static_cast<u32>(self->rows[row].varBag));
}

} // namespace bd::engine
