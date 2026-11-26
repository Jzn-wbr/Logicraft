#include "edit_history.hpp"
#include "inventory.hpp"

int main()
{
    using B = BlockType;
    ItemStack held;
    ItemStack source{B::Dirt, 5};
    clickStack(held, source, true);
    if (held.count != 3 || source.count != 2)
        return 1;
    clickStack(held, source, true);
    if (held.count != 2 || source.count != 3)
        return 2;

    ItemStack stone{B::Stone, 64};
    clickStack(held, stone, false);
    if (held.type != B::Stone || held.count != 64 || stone.type != B::Dirt || stone.count != 2)
        return 3;

    ItemStack a{B::Stone, 62}, b, c;
    distributeItems(held, {&a, &b, &c}, false);
    if (held.count != 20 || a.count != 64 || b.count != 21 || c.count != 21)
        return 4;
    distributeItems(held, {&a, &b, &c}, true);
    if (held.count != 18 || a.count != 64 || b.count != 22 || c.count != 22)
        return 5;

    std::vector<ItemStack> destination{{B::Stone, 63}, {B::Dirt, 64}};
    storeItems(held, destination);
    if (held.count != 17 || destination[0].count != 64)
        return 6;
    destination.push_back({});
    storeItems(held, destination);
    if (held.count || held.type != B::Air || destination.back().count != 17)
        return 7;

    World world(2, 1, 1);
    EditHistory history;
    BlockPosition position{0, 0, 0}, second{1, 0, 0};
    auto before = BlockState::read(world, position);
    auto secondBefore = BlockState::read(world, second);
    world.set(0, 0, 0, B::Button);
    world.setButtonWidth(0, 0, 0, 5);
    world.setButtonValue(0, 0, 0, 17);
    world.set(1, 0, 0, B::NotGate);
    world.setOrientation(1, 0, 0, 3);
    history.record({{position, before, BlockState::read(world, position)},
                    {second, secondBefore, BlockState::read(world, second)}});
    auto allowed = [](BlockPosition, BlockType) { return true; };
    auto changed = [](int, int, int) {};
    if (!history.apply(world, false, allowed, changed) ||
        world.get(0, 0, 0) != B::Air || world.get(1, 0, 0) != B::Air)
        return 8;
    if (history.apply(world, true, [](BlockPosition p, BlockType) { return p[0] == 0; }, changed) ||
        world.get(0, 0, 0) != B::Air)
        return 9;
    if (!history.apply(world, true, allowed, changed) ||
        world.getButtonValue(0, 0, 0) != 17 || world.getOrientation(1, 0, 0) != 3)
        return 10;
    return 0;
}
