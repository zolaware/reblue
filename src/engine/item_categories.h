/**
 * @file    engine/item_categories.h
 * @brief   Which kind each inventory id is, taken from the category column of
 *          the shipped designer sheet bd_gamedat\\fix\\itm_drop.csv.
 *
 * The runtime record carries a category too, but in a field nobody has
 * decoded. The sheet ships with the game, its ITM numbers are the inventory
 * ids one for one, and it is the same data, so it is read at author time and
 * baked here rather than guessed at from names.
 *
 * Rows the sheet marks 廃止 (abolished) are left kNone: the id is real but the
 * content is dead, and those are the entries that read "Worthless Junk".
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

enum class ItemCategory : u8 {
  kNone = 0,
  kHeal,      // REC
  kUsable,    // USE
  kSpellbook, // SCR
  kArm,       // EQU:arm
  kFinger,    // EQU:finger
  kEar,       // EQU:ear
  kNeck,      // EQU:neck
  kChest,     // EQU:chest
  kValuable,  // EVT and GEM
};
inline constexpr u32 kItemCategoryMaxId = 437;

// Indexed by inventory id; [0] is unused so an id indexes directly.
inline constexpr ItemCategory kItemCategories[] = {
    ItemCategory::kNone, ItemCategory::kHeal, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kHeal, ItemCategory::kValuable, 
    ItemCategory::kUsable, ItemCategory::kHeal, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kHeal, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kHeal, 
    ItemCategory::kUsable, ItemCategory::kHeal, ItemCategory::kHeal, 
    ItemCategory::kUsable, ItemCategory::kHeal, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kHeal, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kHeal, ItemCategory::kUsable, 
    ItemCategory::kValuable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kValuable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kEar, ItemCategory::kEar, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kHeal, ItemCategory::kUsable, ItemCategory::kValuable, 
    ItemCategory::kUsable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kHeal, ItemCategory::kValuable, ItemCategory::kUsable, 
    ItemCategory::kValuable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kUsable, ItemCategory::kValuable, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kValuable, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kValuable, ItemCategory::kHeal, 
    ItemCategory::kHeal, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kUsable, ItemCategory::kArm, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kUsable, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, ItemCategory::kNone, 
    ItemCategory::kValuable, ItemCategory::kUsable, ItemCategory::kUsable, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kNone, ItemCategory::kChest, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kEar, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kValuable, ItemCategory::kChest, 
    ItemCategory::kNone, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kArm, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kNeck, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kEar, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kEar, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kChest, 
    ItemCategory::kFinger, ItemCategory::kChest, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kNeck, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kEar, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kEar, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kArm, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kArm, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kNeck, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kNeck, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kFinger, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kNone, ItemCategory::kFinger, 
    ItemCategory::kArm, ItemCategory::kEar, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kArm, ItemCategory::kNeck, ItemCategory::kNeck, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kNeck, 
    ItemCategory::kNeck, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kArm, ItemCategory::kArm, ItemCategory::kArm, 
    ItemCategory::kEar, ItemCategory::kEar, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kEar, ItemCategory::kEar, 
    ItemCategory::kEar, ItemCategory::kChest, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kChest, ItemCategory::kChest, 
    ItemCategory::kChest, ItemCategory::kArm, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kNeck, ItemCategory::kEar, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kHeal, 
    ItemCategory::kEar, ItemCategory::kFinger, ItemCategory::kFinger, 
    ItemCategory::kFinger, ItemCategory::kFinger, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kSpellbook, 
    ItemCategory::kSpellbook, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kNone, ItemCategory::kNone, ItemCategory::kNone, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable, ItemCategory::kValuable, ItemCategory::kValuable, 
    ItemCategory::kValuable,
};

inline ItemCategory CategoryOf(u32 id) {
  return id <= kItemCategoryMaxId ? kItemCategories[id] : ItemCategory::kNone;
}

} // namespace bd::engine
