/**
 * @file    engine/ene_task.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/ene_task.h"

#include <cstddef>

namespace bd::engine {

namespace {

struct EneTask_t {
  /* 0x0000 */ u8 _pad0000[0x70];
  /* 0x0070 */ u8 chara[0x1B40];
};
static_assert(offsetof(EneTask_t, chara) == 0x0070);

} // namespace

EneTask EneTask::FromChara(const engine::Chara &chara) {
  return chara ? EneTask(chara.Address() - offsetof(EneTask_t, chara))
               : EneTask();
}

List<EneTask> EneTask::GroupFrom(u32 head) {
  return List<EneTask>(head, offsetof(EneTask_t, chara) +
                                 Enemy::NextInGroupOffset());
}

Enemy EneTask::Chara() const {
  return Address() ? Enemy(Address() + offsetof(EneTask_t, chara)) : Enemy();
}

} // namespace bd::engine
