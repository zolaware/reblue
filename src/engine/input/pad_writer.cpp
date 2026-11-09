#include <rex/ppc/hook.h>
#include <rex/kernel/memory_view.h>
#include <rex/types.h>

#include "core/cvar_table.h"
#include "core/guest_global.h"
#include "core/memory_helpers.h"
#include "engine/cutscene_pause.h"
#include "engine/input/action_state.h"
#include "engine/input/actions.h"
#include "engine/input/binding_registry.h"
#include "engine/input/button_map.h"
#include "engine/input/input_sources.h"
#include "engine/input/pad_block.h"

namespace bd::engine {

namespace addr {
REX_GUEST_GLOBAL(PadBlock_t, kPadZeroBlock, 0x82DDA768);
REX_GUEST_GLOBAL(be_u32, kAttractMode, 0x82DDA860);
} // namespace addr

namespace {

constexpr int kAxisPairs = 2;
constexpr int kAxisComponents = 2;
static_assert(sizeof(PadBlock_t::analog) ==
              kAxisPairs * kAxisComponents * sizeof(be_f32));

constexpr u32 kFirstStickBit = 16;
constexpr int kStickDirections = 4;

constexpr f32 kStickDirection = (16384.0f - 9830.1f) * 0.000043597869f;

constexpr u32 kAttractRecord = 1;
constexpr u32 kAttractReplay = 2;

struct PadWords {
  u32 claimed = 0;
  u32 held = 0;
  u32 pressed = 0;
  u32 released = 0;
  u32 repeat = 0;
};

struct AxisState {
  f32 value[kAxisPairs][kAxisComponents] = {};
  bool claimed[kAxisPairs] = {};
  bool repeat[kAxisPairs] = {};
};

u32 g_prevStick = 0;
bool g_driving = false;

PadWords ResolveWords() {
  const ButtonMap &map = ButtonMap::Get();
  const InputActions &actions = InputActions::Get();
  PadWords out;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const u32 bit = map.Mask(action);
    if (!bit)
      continue;
    out.claimed |= bit;
    if (actions.Held(action))
      out.held |= bit;
    if (actions.Pressed(action))
      out.pressed |= bit;
    if (actions.Released(action))
      out.released |= bit;
    if (actions.Repeat(action))
      out.repeat |= bit;
  }
  return out;
}

bool DrivesAxis(const Source &source) {
  return source.axis_slot != 0 || source.type == SourceKind::kPadAxes;
}

AxisState ResolveAxes() {
  const InputActions &actions = InputActions::Get();
  AxisState out;
  for (int i = 0; i < kActionCount; ++i) {
    const auto action = static_cast<Action>(i);
    const ActionDesc &desc = Describe(action);
    const int pair = static_cast<int>(desc.axis);
    if (desc.kind != ActionKind::Mapped || pair < 0 || pair >= kAxisPairs)
      continue;
    bool drives = false;
    for (const Source &source : Bindings::Get().Sources(action))
      drives = drives || DrivesAxis(source);
    if (!drives)
      continue;
    out.claimed[pair] = true;
    out.repeat[pair] = out.repeat[pair] || actions.Repeat(action);
  }
  for (int pair = 0; pair < kAxisPairs; ++pair) {
    const auto which = static_cast<AxisPair>(pair);
    for (int component = 0; component < kAxisComponents; ++component) {
      const int slot = pair * kAxisComponents + component;
      out.value[pair][component] =
          out.claimed[pair] ? actions.Axis(which, component)
                            : InputSources::Get().PadAnalog(slot);
    }
  }
  return out;
}

u32 PairBits(int pair) {
  return ((1u << kStickDirections) - 1u)
         << (kFirstStickBit + u32(pair) * u32(kStickDirections));
}

void AddStickDirections(const AxisState &axes, PadWords &words) {
  u32 mask = 0;
  u32 now = 0;
  u32 repeating = 0;
  for (int pair = 0; pair < kAxisPairs; ++pair) {
    if (!axes.claimed[pair])
      continue;
    mask |= PairBits(pair);
    if (axes.repeat[pair])
      repeating |= PairBits(pair);
    const u32 base = kFirstStickBit + u32(pair) * u32(kStickDirections);
    const f32 x = axes.value[pair][0];
    const f32 y = axes.value[pair][1];
    if (y > kStickDirection)
      now |= 1u << base;
    if (y < -kStickDirection)
      now |= 1u << (base + 1);
    if (x > kStickDirection)
      now |= 1u << (base + 2);
    if (x < -kStickDirection)
      now |= 1u << (base + 3);
  }

  const u32 prev = g_prevStick & mask;
  words.claimed |= mask;
  words.held |= now;
  words.pressed |= now & ~prev;
  words.released |= prev & ~now;
  words.repeat |= (now & repeating) | (now & ~prev);
  g_prevStick = (g_prevStick & ~mask) | now;
}

void StoreWord(be_u32 &word, u32 value, u32 claimed) {
  word = (u32(word) & ~claimed) | value;
}

void WritePadZero(bool route) {
  if (!route) {
    g_driving = false;
    return;
  }

  const u32 attract = mem::global_load(addr::kAttractMode);
  if (attract == kAttractRecord || attract == kAttractReplay)
    return;

  InputSources::Get().Sample(addr::kPadZeroBlock.GetAddress());
  ButtonMap::Get().Rebuild();
  InputActions::Get().Resolve();

  AxisState axes = ResolveAxes();
  PadWords words = ResolveWords();
  AddStickDirections(axes, words);
  if (CutscenePause::Get().Active()) {
    axes = AxisState{};
    words = PadWords{.claimed = ~0u};
  }

  const auto view = mem::view();
  auto *pad = view ? addr::kPadZeroBlock.TryGetHost(*view) : nullptr;
  if (!pad)
    return;

  StoreWord(pad->held, words.held, words.claimed);
  StoreWord(pad->pressed, words.pressed, words.claimed);
  StoreWord(pad->released, words.released, words.claimed);
  StoreWord(pad->repeat, words.repeat, words.claimed);

  for (int pair = 0; pair < kAxisPairs; ++pair) {
    for (int component = 0; component < kAxisComponents; ++component) {
      const int slot = pair * kAxisComponents + component;
      pad->analog[slot] = axes.value[pair][component];
    }
  }

  if (!g_driving)
    pad->connectEdge = 1;
  g_driving = true;
  pad->connected = 1;
  pad->disconnectEdge = 0;
}

} // namespace

} // namespace bd::engine

REX_DETOUR(bdPadInputPoll, void, ()) {
  const bool route = bd::cvars::Get().input_bindings.Get();
  original();
  bd::engine::WritePadZero(route);
}
