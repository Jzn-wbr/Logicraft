#pragma once

#include "types.hpp"

#include <map>
#include <vector>

extern const std::map<BlockType, BlockInfo> BLOCKS;
extern const std::vector<BlockType> HOTBAR;
extern const std::vector<BlockType> INVENTORY_ALLOWED;

bool isSolid(BlockType b);
bool isTransparent(BlockType b);
bool occludesFaces(BlockType b);

class World
{
public:
    World(int w, int h, int d);

    BlockType get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockType b);
    uint8_t getPower(int x, int y, int z) const;
    void setPower(int x, int y, int z, uint8_t v);
    uint8_t getButtonState(int x, int y, int z) const;
    void setButtonState(int x, int y, int z, uint8_t v);
    void toggleButton(int x, int y, int z);
    int index(int x, int y, int z) const;
    int totalSize() const;
    void overwritePower(const std::vector<uint8_t> &next);
    int getWidth() const;
    int getHeight() const;
    int getDepth() const;
    void generate(unsigned seed);
    bool inside(int x, int y, int z) const;
    int surfaceY(int x, int z) const;

    const std::string &getSignText(int x, int y, int z) const;
    void setSignText(int x, int y, int z, const std::string &text);

private:
    int width;
    int height;
    int depth;
    std::vector<BlockType> tiles;
    std::vector<uint8_t> power;
    std::vector<uint8_t> buttonState;
    std::vector<std::string> signText;
};

HitInfo raycast(const World &world, float ox, float oy, float oz, float dx, float dy, float dz, float maxDist);

bool collidesAt(const World &world, float px, float py, float pz, float playerHeight);
bool blockIntersectsPlayer(const Player &player, int bx, int by, int bz, float playerHeight);

void updateLogic(World &world);
