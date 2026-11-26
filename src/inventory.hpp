#pragma once

#include "world_types.hpp"

#include <algorithm>
#include <utility>
#include <vector>

struct ItemStack
{
    BlockType type = BlockType::Air;
    int count = 0;
};

inline constexpr int maxStackSize = 64;
inline constexpr int hotbarSize = 9;

inline int moveItems(ItemStack &source, ItemStack &destination, int limit)
{
    if (&source == &destination || source.count == 0 ||
        (destination.count != 0 && destination.type != source.type))
        return 0;
    int moved = std::min({source.count, maxStackSize - destination.count, limit});
    if (moved <= 0)
        return 0;
    destination.type = source.type;
    destination.count += moved;
    source.count -= moved;
    if (source.count == 0)
        source = {};
    return moved;
}

inline void storeItems(ItemStack &source, std::vector<ItemStack> &slots)
{
    for (ItemStack &slot : slots)
        if (slot.count)
            moveItems(source, slot, maxStackSize);
    for (ItemStack &slot : slots)
        if (!slot.count)
            moveItems(source, slot, maxStackSize);
}

inline void clickStack(ItemStack &held, ItemStack &slot, bool right)
{
    if (!held.count)
        moveItems(slot, held, right ? (slot.count + 1) / 2 : maxStackSize);
    else if (!right && slot.count && slot.type != held.type)
        std::swap(held, slot);
    else
        moveItems(held, slot, right ? 1 : maxStackSize);
}

inline void distributeItems(ItemStack &held, const std::vector<ItemStack *> &slots, bool right)
{
    if (slots.empty())
        return;
    const int each = right ? 1 : held.count / static_cast<int>(slots.size());
    for (ItemStack *slot : slots)
        moveItems(held, *slot, each);
}
