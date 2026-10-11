/**
 * @file    engine/gimmicks.cpp
 * @brief   The rows come from the stage the engine has loaded, the live state
 *          from the field's script variable block.
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "engine/gimmicks.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "engine/events.h"
#include "engine/game.h"
#include "engine/scene_file.h"
#include "engine/script.h"
#include "engine/script_vars.h"
#include "engine/state_layout.h"

namespace bd::engine {

namespace {

// A search point with this flag carries none, and respawns every map load.
constexpr u32 kNoFlag = 0xFFFF;

constexpr u32 kFlagDumpThreshold = 8;

constexpr u32 kMaxSceneEntries = 8192;
constexpr u32 kMaxEntryBlocks = 512;
constexpr u32 kMaxSearchPoints = 1024;
constexpr u32 kMaxFlagListCount = 4096;

constexpr u32 kChestTypeDataSmall = 0x10001;
constexpr u32 kChestTypeDataLarge = 0xC0001;

constexpr u32 kGiveItemAdd = 1;
constexpr u32 kGiveAmountAdd = 0x10000;

constexpr u32 kSetVariableParamCount = 4;
constexpr u32 kSetVariableAssign = 0;
constexpr u32 kSetVariableLiteral = 0;

constexpr u32 kOpensAtMin = 1;
constexpr u32 kOpensAtMax = 255;

struct FlagList_t {
  /* 0x00 */ be_u32 count;
  /* 0x04 */ be_u32 ids;
  /* 0x08 */ be_u32 colors;
};
static_assert(offsetof(FlagList_t, count) == 0x00);
static_assert(offsetof(FlagList_t, ids) == 0x04);
static_assert(offsetof(FlagList_t, colors) == 0x08);

struct FlagEntry {
  u32 flag = 0;
  u32 color = 0;
};

struct PointRow {
  u32 flag = kNoFlag;
  GimmickKind kind = GimmickKind::Nothing;
  u32 opensAt = 0;
  Vec3 pos{};
};

struct ChestRow {
  u32 flag = 0;
  u32 opensAt = kOpensAtMin;
  Vec3 pos{};
};

struct BarrierRow {
  u32 flag = 0;
  BarrierColor color = BarrierColor::Blue;
  Vec3 pos{};
};

struct EntryFacts {
  Vec3 pos{};
  SceneEntryType type = SceneEntryType::Spawn;
  bool anyGive = false;
  bool hasLiveGive = false;
  GimmickKind firstLiveGive = GimmickKind::Nothing;
  std::set<u32> sets;
  std::map<u32, u32> assigned;
  std::map<u32, u32> assignedAfterGive;
  std::set<u32> gates;
  std::set<u32> types;
};

struct GiveTrail {
  bool armed = false;
  std::set<u32> written;
};

std::vector<FlagEntry> ReadFlagList(u32 listAddress, bool withColor) {
  std::vector<FlagEntry> out;
  const auto *list = mem::try_at<const FlagList_t>(listAddress);
  if (!list)
    return out;

  const u32 count = static_cast<u32>(list->count);
  const u32 ids = static_cast<u32>(list->ids);
  const u32 colors = static_cast<u32>(list->colors);
  if (!count || count > kMaxFlagListCount || !ids)
    return out;

  out.reserve(count);
  for (u32 i = 0; i < count; ++i) {
    const u32 offset = i * sizeof(u32);
    const auto *id = mem::try_at<const be_u32>(ids + offset);
    if (!id)
      break;
    FlagEntry entry;
    entry.flag = static_cast<u32>(*id);
    if (withColor && colors)
      entry.color = mem::try_load<u32>(colors + offset);
    out.push_back(entry);
  }
  return out;
}

void RecordLiveGive(EntryFacts &facts, GiveTrail &trail, GimmickKind kind) {
  trail.armed = true;
  trail.written.clear();
  if (facts.hasLiveGive)
    return;
  facts.hasLiveGive = true;
  facts.firstLiveGive = kind;
}

void ReadInstruction(EntryFacts &facts, GiveTrail &trail,
                     const SceneInstruction &ins) {
  switch (static_cast<SceneOp>(ins.opcode)) {
  case SceneOp::SetVariable:
  case SceneOp::SetVariableAlt: {
    if (ins.params.size() < kSetVariableParamCount)
      break;
    const u32 dest = static_cast<u32>(ins.params[0]);
    const u32 op = static_cast<u32>(ins.params[1]);
    const u32 source = static_cast<u32>(ins.params[2]);
    const u32 value = static_cast<u32>(ins.params[3]);
    if (dest < kSceneGlobalBase)
      break;
    const u32 global = dest - kSceneGlobalBase;
    facts.sets.insert(global);
    if (op == kSetVariableAssign && source == kSetVariableLiteral) {
      u32 &held = facts.assigned[global];
      held = std::max(held, value);
      if (trail.armed && trail.written.insert(global).second) {
        const auto [it, fresh] =
            facts.assignedAfterGive.try_emplace(global, value);
        if (!fresh)
          it->second = std::min(it->second, value);
      }
    }
    break;
  }
  case SceneOp::GiveItem:
  case SceneOp::GiveItemSpecial:
    facts.anyGive = true;
    if (ins.params.size() >= 2 &&
        static_cast<u32>(ins.params[1]) == kGiveItemAdd)
      RecordLiveGive(facts, trail, GimmickKind::Item);
    break;
  case SceneOp::GiveGold:
    facts.anyGive = true;
    if (!ins.params.empty() &&
        static_cast<u32>(ins.params[0]) == kGiveAmountAdd)
      RecordLiveGive(facts, trail, GimmickKind::Gold);
    break;
  case SceneOp::GiveMedal:
    facts.anyGive = true;
    if (!ins.params.empty() &&
        static_cast<u32>(ins.params[0]) == kGiveAmountAdd)
      RecordLiveGive(facts, trail, GimmickKind::Medal);
    break;
  default:
    break;
  }
}

EntryFacts ReadEntry(const SceneEntry &entry) {
  EntryFacts facts;
  facts.pos = entry.Position();
  facts.type = entry.Type();

  SceneBlock block = entry.FirstBlock();
  for (u32 n = 0; block && n < kMaxEntryBlocks; ++n) {
    facts.types.insert(block.TypeData());
    for (u32 c = 0; c < kSceneConditionCount; ++c) {
      const SceneCondition cond = block.Condition(c);
      if (cond.type == kSceneConditionVariable &&
          cond.operand >= kSceneGlobalBase)
        facts.gates.insert(cond.operand - kSceneGlobalBase);
    }
    GiveTrail trail;
    block.ForEachInstruction([&facts, &trail](const SceneInstruction &ins) {
      ReadInstruction(facts, trail, ins);
    });
    block = block.Next();
  }
  return facts;
}

u32 OpensAtFor(const EntryFacts &facts, u32 flag) {
  u32 value = 0;
  if (const auto it = facts.assignedAfterGive.find(flag);
      it != facts.assignedAfterGive.end())
    value = it->second;
  else if (const auto held = facts.assigned.find(flag);
           held != facts.assigned.end())
    value = held->second;
  return std::clamp(value, kOpensAtMin, kOpensAtMax);
}

bool IsChestType(const EntryFacts &facts) {
  return facts.types.count(kChestTypeDataSmall) != 0 ||
         facts.types.count(kChestTypeDataLarge) != 0;
}

bool PointTrackable(const PointRow &p) {
  return p.opensAt != 0 || p.flag != kNoFlag;
}

bool PointCollected(const ScriptVars &vars, const PointRow &p) {
  if (p.opensAt)
    return vars.Global(p.flag) >= p.opensAt;
  return vars.Flag(p.flag) != 0;
}

u32 PointValue(const ScriptVars &vars, const PointRow &p) {
  return p.opensAt ? vars.Global(p.flag) : vars.Flag(p.flag);
}

} // namespace

struct Gimmicks::Stage {
  engine::Script script;
  std::string stem;
  std::vector<PointRow> points;
  std::vector<ChestRow> chests;
  std::vector<BarrierRow> barriers;
};

const char *ToString(GimmickKind kind) {
  switch (kind) {
  case GimmickKind::Nothing: return "nothing";
  case GimmickKind::Message: return "message";
  case GimmickKind::Item: return "item";
  case GimmickKind::Gold: return "gold";
  case GimmickKind::Heal: return "heal";
  case GimmickKind::Damage: return "damage";
  case GimmickKind::Status: return "status";
  case GimmickKind::Medal: return "medal";
  case GimmickKind::Lock: return "lock";
  case GimmickKind::Grass: return "grass";
  case GimmickKind::Param: return "param";
  case GimmickKind::Chest: return "chest";
  case GimmickKind::Barrier: return "barrier";
  }
  return "?";
}

const char *ToString(BarrierColor color) {
  switch (color) {
  case BarrierColor::Blue: return "blue";
  case BarrierColor::Red: return "red";
  case BarrierColor::Green: return "green";
  case BarrierColor::White: return "white";
  case BarrierColor::Black: return "black";
  }
  return "?";
}

Gimmicks::Gimmicks() = default;
Gimmicks::~Gimmicks() = default;

Gimmicks &Gimmicks::Get() {
  static Gimmicks g;
  return g;
}

void Gimmicks::Init() {
  Events::Subscribe<StageLoaded>(
      [](const StageLoaded &e) { Gimmicks::Get().Build(e.script); });
  Events::Subscribe<StageUnloading>(
      [](const StageUnloading &e) { Gimmicks::Get().Drop(e.script); });
}

void Gimmicks::Build(const Script &script) {
  std::erase_if(stages_, [](const Stage &held) { return !held.script; });

  const std::string stem = script.Name();
  if (stem.empty())
    return;

  Stage stage;
  stage.script = script;
  stage.stem = stem;

  const u32 pointCount = std::min(script.SearchPointCount(), kMaxSearchPoints);
  for (u32 i = 0; i < pointCount; ++i) {
    const SearchPoint point = script.SearchPointAt(i);
    if (!point)
      break;
    const u32 kind = point.Kind();
    if (kind >= kSearchKindCount)
      continue;
    const i32 flag = point.Flag();
    if (flag > static_cast<i32>(kNoFlag))
      continue;
    PointRow row;
    row.flag = flag < 0 ? kNoFlag : static_cast<u32>(flag);
    row.kind = static_cast<GimmickKind>(kind);
    row.pos = point.Position();
    stage.points.push_back(row);
  }

  std::vector<EntryFacts> entries;
  SceneEntry entry = script.Scene().FirstEntry();
  for (u32 n = 0; entry && n < kMaxSceneEntries; ++n) {
    entries.push_back(ReadEntry(entry));
    entry = entry.Next();
  }

  const std::vector<FlagEntry> chestList =
      ReadFlagList(addr::kChestFlagList, false);
  const std::vector<FlagEntry> barrierList =
      ReadFlagList(addr::kBarrierFlagList, true);

  std::set<u32> chestFlags;
  for (const FlagEntry &e : chestList)
    chestFlags.insert(e.flag);

  for (const EntryFacts &facts : entries) {
    if (facts.type != SceneEntryType::Link || !facts.hasLiveGive ||
        facts.assigned.empty())
      continue;

    u32 flag = facts.assigned.begin()->first;
    for (u32 gate : facts.gates) {
      if (facts.assigned.count(gate)) {
        flag = gate;
        break;
      }
    }
    if (chestFlags.count(flag) || IsChestType(facts)) {
      chestFlags.insert(flag);
      continue;
    }

    PointRow row;
    row.flag = flag;
    row.kind = facts.firstLiveGive;
    row.opensAt = OpensAtFor(facts, flag);
    row.pos = facts.pos;
    stage.points.push_back(row);
  }

  for (u32 flag : chestFlags) {
    const EntryFacts *placed = nullptr;
    for (const EntryFacts &facts : entries) {
      if (facts.anyGive && facts.sets.count(flag))
        placed = &facts;
    }
    if (!placed)
      continue;
    ChestRow row;
    row.flag = flag;
    row.opensAt = OpensAtFor(*placed, flag);
    row.pos = placed->pos;
    stage.chests.push_back(row);
  }

  for (const FlagEntry &barrier : barrierList) {
    const EntryFacts *placed = nullptr;
    bool placedGives = false;
    for (const EntryFacts &facts : entries) {
      if (!facts.sets.count(barrier.flag))
        continue;
      if (!facts.anyGive && facts.sets.size() > kFlagDumpThreshold)
        continue;
      if (placedGives && !facts.anyGive)
        continue;
      placed = &facts;
      placedGives = facts.anyGive;
    }
    if (!placed)
      continue;
    BarrierRow row;
    row.flag = barrier.flag;
    row.color = barrier.color < kBarrierColorCount
                    ? static_cast<BarrierColor>(barrier.color)
                    : BarrierColor::Blue;
    row.pos = placed->pos;
    stage.barriers.push_back(row);
  }

  std::erase_if(stages_,
                [&stem](const Stage &held) { return held.stem == stem; });
  BD_INFO("[gimmicks] {}: {} points, {} chests, {} barriers", stem,
          stage.points.size(), stage.chests.size(), stage.barriers.size());
  stages_.push_back(std::move(stage));
}

void Gimmicks::Drop(const Script &script) {
  const u32 address = script.Address();
  if (!address)
    return;
  std::erase_if(stages_, [address](const Stage &held) {
    return held.script.Is(address);
  });
}

const Gimmicks::Stage *Gimmicks::Find(std::string_view stem) const {
  for (auto it = stages_.rbegin(); it != stages_.rend(); ++it) {
    if (it->stem == stem)
      return &*it;
  }
  return nullptr;
}

bool Gimmicks::IsReady() const {
  return static_cast<bool>(Game::Get().ScriptManTask().Vars());
}

bool Gimmicks::Has(std::string_view stem) const {
  return Find(stem) != nullptr;
}

Tally Gimmicks::Points(std::string_view stem,
                       std::optional<GimmickKind> kind) const {
  Tally out;
  const ScriptVars vars = Game::Get().ScriptManTask().Vars();
  const Stage *stage = vars ? Find(stem) : nullptr;
  if (!stage)
    return out;

  for (const PointRow &p : stage->points) {
    if (!PointTrackable(p))
      continue;
    if (kind && p.kind != *kind)
      continue;
    ++out.total;
    if (!PointCollected(vars, p))
      ++out.remaining;
  }
  return out;
}

Tally Gimmicks::Chests(std::string_view stem) const {
  Tally out;
  const ScriptVars vars = Game::Get().ScriptManTask().Vars();
  const Stage *stage = vars ? Find(stem) : nullptr;
  if (!stage)
    return out;

  for (const ChestRow &c : stage->chests) {
    ++out.total;
    if (vars.Global(c.flag) < c.opensAt)
      ++out.remaining;
  }
  return out;
}

Tally Gimmicks::Barriers(std::string_view stem,
                         std::optional<BarrierColor> color) const {
  Tally out;
  const ScriptVars vars = Game::Get().ScriptManTask().Vars();
  const Stage *stage = vars ? Find(stem) : nullptr;
  if (!stage)
    return out;

  for (const BarrierRow &b : stage->barriers) {
    if (color && b.color != *color)
      continue;
    ++out.total;
    if (!vars.Global(b.flag))
      ++out.remaining;
  }
  return out;
}

std::vector<Marker> Gimmicks::Markers(std::string_view stem) const {
  std::vector<Marker> out;
  const Stage *stage = Find(stem);
  if (!stage)
    return out;

  const ScriptVars vars = Game::Get().ScriptManTask().Vars();
  out.reserve(stage->points.size() + stage->chests.size() +
              stage->barriers.size());

  for (const PointRow &p : stage->points) {
    Marker mk;
    mk.kind = p.kind;
    mk.trackable = PointTrackable(p);
    mk.collected = mk.trackable && PointCollected(vars, p);
    mk.flag = static_cast<u16>(p.flag);
    mk.opensAt = static_cast<u8>(p.opensAt);
    mk.value = mk.trackable ? PointValue(vars, p) : 0;
    mk.x = p.pos[0];
    mk.y = p.pos[1];
    mk.z = p.pos[2];
    out.push_back(mk);
  }

  const auto placed = [&](GimmickKind kind, u32 flag, u32 opensAt,
                          const Vec3 &pos) {
    Marker mk;
    mk.kind = kind;
    mk.trackable = true;
    mk.collected = vars.Global(flag) >= opensAt;
    mk.flag = static_cast<u16>(flag);
    mk.opensAt = static_cast<u8>(opensAt);
    mk.value = vars.Global(flag);
    mk.x = pos[0];
    mk.y = pos[1];
    mk.z = pos[2];
    out.push_back(mk);
  };
  for (const ChestRow &c : stage->chests)
    placed(GimmickKind::Chest, c.flag, c.opensAt, c.pos);
  for (const BarrierRow &b : stage->barriers)
    placed(GimmickKind::Barrier, b.flag, kOpensAtMin, b.pos);

  return out;
}

} // namespace bd::engine
