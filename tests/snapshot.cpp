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
    return 0;
}
