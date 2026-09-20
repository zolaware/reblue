#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/input/action_state.h"
#include "engine/input/actions.h"
#include "engine/input/binding_store.h"
#include "engine/input/button_map.h"
#include "engine/input/input_sources.h"

REX_EXTERN(__imp__bdPadInputPoll);

REXCVAR_DEFINE_BOOL(bd_input_bindings, true, "Input",
                    "Route input through reblue's binding layer");

namespace bd::engine {

namespace addr {
inline constexpr u32 kPadZeroBlock = 0x82DDA768;
inline constexpr u32 kAttractMode = 0x82DDA860;
} // namespace addr

namespace {

constexpr u32 kHeldOffset = 0x0C;
constexpr u32 kPressedOffset = 0x10;
constexpr u32 kReleasedOffset = 0x14;
constexpr u32 kRepeatOffset = 0x18;

constexpr int kAxisPairs = 2;
constexpr int kAxisComponents = 2;
constexpr u32 kAnalogOffset[kAxisPairs * kAxisComponents] = {0x1C, 0x20, 0x24,
                                                             0x28};

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
  return source.axisSlot != 0 || source.kind == SourceKind::PadAxes ||
         source.kind == SourceKind::MouseAxes;
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

void StoreWord(u32 va, u32 value, u32 claimed) {
  const u32 kept = mem::try_load<u32>(va) & ~claimed;
  mem::try_store<u32>(va, kept | value);
}

void WritePadZero() {
  const u32 attract = mem::try_load<u32>(addr::kAttractMode);
  if (attract == kAttractRecord || attract == kAttractReplay)
    return;

  InputSources::Get().Sample(addr::kPadZeroBlock);
  ButtonMap::Get().Rebuild();
  InputActions::Get().Resolve();

  const AxisState axes = ResolveAxes();
  PadWords words = ResolveWords();
  AddStickDirections(axes, words);

  StoreWord(addr::kPadZeroBlock + kHeldOffset, words.held, words.claimed);
  StoreWord(addr::kPadZeroBlock + kPressedOffset, words.pressed,
            words.claimed);
  StoreWord(addr::kPadZeroBlock + kReleasedOffset, words.released,
            words.claimed);
  StoreWord(addr::kPadZeroBlock + kRepeatOffset, words.repeat, words.claimed);

  for (int pair = 0; pair < kAxisPairs; ++pair) {
    for (int component = 0; component < kAxisComponents; ++component) {
      const int slot = pair * kAxisComponents + component;
      mem::try_store<f32>(addr::kPadZeroBlock + kAnalogOffset[slot],
                          axes.value[pair][component]);
    }
  }
}

} // namespace

} // namespace bd::engine

REX_HOOK_RAW(bdPadInputPoll) {
  const bool route = REXCVAR_GET(bd_input_bindings);
  __imp__bdPadInputPoll(ctx, base);
  if (route)
    bd::engine::WritePadZero();
}
