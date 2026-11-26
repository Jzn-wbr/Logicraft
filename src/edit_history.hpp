#pragma once

#include "world.hpp"

#include <array>
#include <deque>
#include <utility>

using BlockPosition = std::array<int, 3>;

struct BlockState
{
    BlockType type;
    uint8_t power, powerWidth, memory, value, width, busWidth, order, frequency, orientation;
    std::string text;

    static BlockState read(const World &world, BlockPosition position)
    {
        const auto [x, y, z] = position;
        return {world.get(x, y, z), world.getPower(x, y, z), world.getPowerWidth(x, y, z),
                world.getButtonState(x, y, z), world.getButtonValue(x, y, z), world.getButtonWidth(x, y, z),
                world.getSplitterWidth(x, y, z), world.getSplitterOrder(x, y, z), world.getClockFreq(x, y, z),
                world.getOrientation(x, y, z), world.getSignText(x, y, z)};
    }

    void write(World &world, BlockPosition position, bool restoreSignal = true) const
    {
        const auto [x, y, z] = position;
        world.set(x, y, z, type);
        world.setOrientation(x, y, z, orientation);
        world.setPower(x, y, z, restoreSignal ? power : 0);
        world.setPowerWidth(x, y, z, powerWidth);
        world.setButtonState(x, y, z, restoreSignal ? memory : 0);
        if (type == BlockType::Button)
        {
            world.setButtonWidth(x, y, z, width);
            world.setButtonValue(x, y, z, value);
        }
        if (type == BlockType::Splitter || type == BlockType::Merger)
        {
            world.setSplitterWidth(x, y, z, busWidth);
            world.setSplitterOrder(x, y, z, order);
        }
        if (type == BlockType::Clock)
            world.setClockFreq(x, y, z, frequency);
        if (type == BlockType::Sign)
            world.setSignText(x, y, z, text);
    }
};

struct BlockEdit
{
    BlockPosition position;
    BlockState before;
    BlockState after;
};

class EditHistory
{
public:
    void record(std::vector<BlockEdit> edits)
    {
        undone.clear();
        if (done.size() == 128)
            done.pop_front();
        done.push_back(std::move(edits));
    }

    template<class Allows, class Changed>
    bool apply(World &world, bool redo, Allows allows, Changed changed)
    {
        auto &from = redo ? undone : done;
        auto &to = redo ? done : undone;
        if (from.empty())
            return false;
        for (const BlockEdit &edit : from.back())
            if (!allows(edit.position, redo ? edit.after.type : edit.before.type))
                return false;
        for (const BlockEdit &edit : from.back())
        {
            (redo ? edit.after : edit.before).write(world, edit.position);
            auto [x, y, z] = edit.position;
            changed(x, y, z);
        }
        to.push_back(std::move(from.back()));
        from.pop_back();
        return true;
    }

private:
    std::deque<std::vector<BlockEdit>> done;
    std::deque<std::vector<BlockEdit>> undone;
};
