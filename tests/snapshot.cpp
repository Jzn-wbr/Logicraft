#include "snapshot.hpp"
#include "world.hpp"
#include <sstream>
#include <iostream>

int main(int argc, char **argv)
{
    World world(2, 1, 1);
    world.set(0, 0, 0, BlockType::Button);
    world.setButtonWidth(0, 0, 0, 5);
    world.setButtonValue(0, 0, 0, 3);
    world.set(1, 0, 0, BlockType::Wire);
    world.setPowerWidth(1, 0, 0, 5);
    world.setTick(1234);
    std::stringstream bytes;
    uint32_t seed = 0;
    if (!writeSnapshot(bytes, world, 17))
        return 1;
    World restored(2, 1, 1);
    if (!readSnapshot(bytes, restored, seed) || seed != 17 || restored.tick() != 1234 ||
        restored.getButtonWidth(0, 0, 0) != 5 || restored.getButtonValue(0, 0, 0) != 3 ||
        restored.getPowerWidth(1, 0, 0) != 5)
        return 2;

    std::stringstream truncated(bytes.str().substr(0, bytes.str().size() - 1));
    seed = 99;
    restored.set(0, 0, 0, BlockType::Glass);
    if (readSnapshot(truncated, restored, seed) || seed != 99 || restored.get(0, 0, 0) != BlockType::Glass)
        return 3;

    for (unsigned char version : {8, 9})
    {
        // Handcrafted legacy button, five bits carrying the value three.
        std::string fixture("BULLDOG\0", 8);
        for (unsigned char n : {version, (unsigned char)1, (unsigned char)1, (unsigned char)1, (unsigned char)0})
        {
            fixture.push_back(n);
            fixture.append(3, '\0');
        }
        fixture.append("\x0f\x03\x01\x03\x05", 5);
        if (version == 9)
            fixture.append("\x01\x00", 2);
        fixture.append(4, '\0');
        std::istringstream in(fixture);
        World old(1, 1, 1);
        if (!readSnapshot(in, old, seed) || old.getButtonWidth(0, 0, 0) != 5)
            return 4;
    }

    World legacy(96, 48, 96);
    for (int i = 1; i < argc; ++i)
        if (!loadWorldFromFile(legacy, argv[i], seed))
            return 5;

    for (uint8_t turns = 0; turns < 4; ++turns)
    {
        World circuit(5, 1, 5);
        circuit.set(2, 0, 2, BlockType::NotGate);
        circuit.setOrientation(2, 0, 2, turns);
        auto [dx, dz] = rotateHorizontal(1, 0, turns);
        circuit.set(2 + dx, 0, 2 + dz, BlockType::Wire);
        circuit.setPower(2 + dx, 0, 2 + dz, 1);
        circuit.setPowerWidth(2 + dx, 0, 2 + dz, 3);
        circuit.set(2 - dx, 0, 2 - dz, BlockType::Wire);

        std::stringstream encoded;
        World decoded(5, 1, 5);
        if (!writeSnapshot(encoded, circuit, 42) || !readSnapshot(encoded, decoded, seed) ||
            decoded.getOrientation(2, 0, 2) != turns ||
            static_cast<unsigned char>(encoded.str()[8]) != (turns ? 12 : 11))
            return 6;
        updateLogic(decoded);
        if (decoded.getPower(2 - dx, 0, 2 - dz) != 6 || decoded.getPowerWidth(2 - dx, 0, 2 - dz) != 3)
            return 7;

        if (turns)
        {
            std::string invalid = encoded.str();
            invalid[36 + 12 * 10 + 9] = 4; // orientation of the center cell
            std::istringstream corrupted(invalid);
            if (readSnapshot(corrupted, decoded, seed) || decoded.getOrientation(2, 0, 2) != turns)
                return 8;
        }
    }

    // A zero-valued bus retains the width of its driver, including after a width-only edit.
    World bus(5, 1, 1);
    bus.set(0, 0, 0, BlockType::Button);
    for (int x = 1; x < 5; ++x)
        bus.set(x, 0, 0, BlockType::Wire);
    for (uint8_t width = 1; width <= 8; ++width)
    {
        bus.setButtonWidth(0, 0, 0, width);
        int notified = 0;
        updateLogic(bus, {}, [&](int x, int, int) { notified += x > 0; });
        if (notified != 4)
            return 9;
        for (int x = 1; x < 5; ++x)
            if (bus.getPower(x, 0, 0) != 0 || bus.getPowerWidth(x, 0, 0) != width)
                return 10;
    }

    World splitter(3, 1, 3);
    splitter.set(1, 0, 1, BlockType::Splitter);
    splitter.setSplitterWidth(1, 0, 1, 2);
    splitter.set(1, 0, 0, BlockType::Wire);
    splitter.setPowerWidth(1, 0, 0, 5);
    splitter.set(0, 0, 1, BlockType::Wire);
    splitter.set(2, 0, 1, BlockType::Wire);
    updateLogic(splitter);
    if (splitter.getPowerWidth(0, 0, 1) != 2 || splitter.getPowerWidth(2, 0, 1) != 3)
        return 11;
    return 0;
}
