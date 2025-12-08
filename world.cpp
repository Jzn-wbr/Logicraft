#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <random>

// Forward declaration for render dirty marking
void markChunkFromBlock(int x, int y, int z);

const std::map<BlockType, BlockInfo> BLOCKS = {
    {BlockType::Air, {"Air", false, {0.7f, 0.85f, 1.0f}}},
    {BlockType::Grass, {"Grass", true, {0.2f, 0.7f, 0.2f}}},
    {BlockType::Dirt, {"Dirt", true, {0.45f, 0.25f, 0.1f}}},
    {BlockType::Stone, {"Stone", true, {0.5f, 0.5f, 0.5f}}},
    {BlockType::Wood, {"Wood", true, {0.8f, 0.65f, 0.45f}}},
    {BlockType::Leaves, {"Leaves", true, {0.25f, 0.6f, 0.25f}}},
    {BlockType::Water, {"Water", false, {0.2f, 0.4f, 0.9f}}},
    {BlockType::Plank, {"Plank", true, {0.75f, 0.6f, 0.4f}}},
    {BlockType::Sand, {"Sand", true, {0.9f, 0.8f, 0.6f}}},
    {BlockType::Glass, {"Glass", true, {0.82f, 0.93f, 0.98f}}},
    {BlockType::AndGate, {"AND", true, {0.18f, 0.7f, 0.32f}}},
    {BlockType::OrGate, {"OR", true, {0.92f, 0.56f, 0.18f}}},
    {BlockType::NotGate, {"NOT", true, {0.45f, 0.25f, 0.7f}}},
    {BlockType::XorGate, {"XOR", true, {0.2f, 0.5f, 0.9f}}},
    {BlockType::DFlipFlop, {"FLIPFLOP D", true, {0.2f, 0.78f, 0.72f}}},
    {BlockType::AddGate, {"ADD", true, {0.9f, 0.42f, 0.3f}}},
    {BlockType::Counter, {"Counter", true, {0.8f, 0.8f, 0.25f}}},
    {BlockType::Led, {"LED", true, {0.95f, 0.9f, 0.2f}}},
    {BlockType::Button, {"Button", true, {0.6f, 0.2f, 0.2f}}},
    {BlockType::Wire, {"Wire", true, {0.55f, 0.55f, 0.58f}}},
    {BlockType::Sign, {"Sign", false, {0.85f, 0.7f, 0.45f}}},
    {BlockType::Splitter, {"Splitter", true, {0.2f, 0.75f, 0.7f}}},
    {BlockType::Merger, {"Merger", true, {0.75f, 0.4f, 0.85f}}},
};

const std::vector<BlockType> HOTBAR = {BlockType::Dirt, BlockType::Grass, BlockType::Wood,
                                       BlockType::Stone, BlockType::Glass, BlockType::NotGate,
                                       BlockType::Splitter, BlockType::Merger};
const std::vector<BlockType> INVENTORY_ALLOWED = {BlockType::Dirt, BlockType::Grass, BlockType::Wood,
                                                  BlockType::Stone, BlockType::Glass, BlockType::AndGate,
                                                  BlockType::OrGate, BlockType::NotGate, BlockType::XorGate,
                                                  BlockType::DFlipFlop, BlockType::AddGate, BlockType::Counter,
                                                  BlockType::Led,      BlockType::Button,   BlockType::Wire,
                                                  BlockType::Sign,     BlockType::Splitter, BlockType::Merger};

bool isSolid(BlockType b) { return BLOCKS.at(b).solid; }

bool occludesFaces(BlockType b)
{
    if (b == BlockType::Wire)
        return false;
    if (b == BlockType::Glass)
        return false;
    return isSolid(b);
}

bool isTransparent(BlockType b)
{
    if (b == BlockType::Wire)
        return true;
    if (b == BlockType::Glass)
        return true;
    return !isSolid(b);
}

World::World(int w, int h, int d)
    : width(w), height(h), depth(d), tiles(w * h * d, BlockType::Air), power(w * h * d, 0),
      powerWidth(w * h * d, 8), buttonState(w * h * d, 0), buttonValue(w * h * d, 0),
      buttonWidth(w * h * d, 0), splitterWidth(w * h * d, 1), splitterOrder(w * h * d, 0),
      signText(w * h * d)
{
}

BlockType World::get(int x, int y, int z) const { return tiles[index(x, y, z)]; }

void World::set(int x, int y, int z, BlockType b)
{
    int idx = index(x, y, z);
    tiles[idx] = b;
    power[idx] = 0;
    powerWidth[idx] = 8;
    if (b != BlockType::Button)
    {
        buttonState[idx] = 0;
        buttonValue[idx] = 0;
        buttonWidth[idx] = 0;
    }
    else
    {
        buttonValue[idx] = 1; // default button payload is 1 (1-bit)
        buttonWidth[idx] = 1; // default to 1-bit bus
    }
    if (b != BlockType::Splitter)
    {
        splitterWidth[idx] = 1;
        splitterOrder[idx] = 0;
    }
    if (b != BlockType::Merger)
    {
        splitterWidth[idx] = 1;
        splitterOrder[idx] = 0;
    }
    if (b != BlockType::Sign)
        signText[idx].clear();
}

uint8_t World::getPower(int x, int y, int z) const { return power[index(x, y, z)]; }

uint8_t World::getPowerWidth(int x, int y, int z) const { return powerWidth[index(x, y, z)]; }

void World::setPower(int x, int y, int z, uint8_t v) { power[index(x, y, z)] = v; }

void World::setPowerWidth(int x, int y, int z, uint8_t w) { powerWidth[index(x, y, z)] = w; }

uint8_t World::getButtonState(int x, int y, int z) const { return buttonState[index(x, y, z)]; }

void World::setButtonState(int x, int y, int z, uint8_t v) { buttonState[index(x, y, z)] = v; }

uint8_t World::getButtonValue(int x, int y, int z) const { return buttonValue[index(x, y, z)]; }

void World::setButtonValue(int x, int y, int z, uint8_t v)
{
    int i = index(x, y, z);
    uint8_t width = buttonWidth[i];
    if (width == 0)
        width = 8;
    uint8_t mask = (width >= 8) ? 0xFFu : static_cast<uint8_t>((1u << width) - 1u);
    buttonValue[i] = static_cast<uint8_t>(v & mask);
}

uint8_t World::getButtonWidth(int x, int y, int z) const { return buttonWidth[index(x, y, z)]; }

void World::setButtonWidth(int x, int y, int z, uint8_t bits)
{
    int i = index(x, y, z);
    uint8_t clamped = static_cast<uint8_t>(std::clamp<int>(bits, 1, 8));
    buttonWidth[i] = clamped;
    uint8_t mask = (clamped >= 8) ? 0xFFu : static_cast<uint8_t>((1u << clamped) - 1u);
    buttonValue[i] &= mask; // trim stored payload to the new width
}

uint8_t World::getSplitterWidth(int x, int y, int z) const { return splitterWidth[index(x, y, z)]; }
void World::setSplitterWidth(int x, int y, int z, uint8_t bits)
{
    int i = index(x, y, z);
    splitterWidth[i] = static_cast<uint8_t>(std::clamp<int>(bits, 1, 7));
}
uint8_t World::getSplitterOrder(int x, int y, int z) const { return splitterOrder[index(x, y, z)]; }
void World::setSplitterOrder(int x, int y, int z, uint8_t order)
{
    int i = index(x, y, z);
    splitterOrder[i] = static_cast<uint8_t>(order & 0x1u);
}

void World::toggleButton(int x, int y, int z)
{
    int idx = index(x, y, z);
    buttonState[idx] = buttonState[idx] ? 0 : 1;
}

int World::index(int x, int y, int z) const { return (y * depth + z) * width + x; }

int World::totalSize() const { return static_cast<int>(tiles.size()); }

void World::overwritePower(const std::vector<uint8_t> &next, const std::vector<uint8_t> &nextW)
{
    power = next;
    powerWidth = nextW;
}

int World::getWidth() const { return width; }
int World::getHeight() const { return height; }
int World::getDepth() const { return depth; }

void World::generate(unsigned seed)
{
    std::mt19937 rng(seed);
    (void)rng;

    int surface = height / 4;
    std::fill(power.begin(), power.end(), 0);
    std::fill(powerWidth.begin(), powerWidth.end(), 8);
    std::fill(buttonState.begin(), buttonState.end(), 0);
    std::fill(buttonWidth.begin(), buttonWidth.end(), 0);
    std::fill(splitterWidth.begin(), splitterWidth.end(), 1);
    std::fill(splitterOrder.begin(), splitterOrder.end(), 0);
    for (int z = 0; z < depth; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int y = 0; y < height; ++y)
            {
                BlockType b = BlockType::Air;
                if (y == 0)
                {
                    b = BlockType::Stone;
                }
                else if (y < surface - 2)
                {
                    b = BlockType::Stone;
                }
                else if (y < surface - 1)
                {
                    b = BlockType::Dirt;
                }
                else if (y == surface - 1)
                {
                    b = BlockType::Grass;
                }
                set(x, y, z, b);
            }
        }
    }
}

bool World::inside(int x, int y, int z) const
{
    return x >= 0 && x < width && y >= 0 && y < height && z >= 0 && z < depth;
}

int World::surfaceY(int x, int z) const
{
    for (int y = height - 1; y >= 0; --y)
    {
        if (isSolid(get(x, y, z)))
        {
            return y + 1;
        }
    }
    return height / 2;
}

const std::string &World::getSignText(int x, int y, int z) const
{
    static const std::string empty;
    int idx = index(x, y, z);
    if (idx < 0 || idx >= static_cast<int>(signText.size()))
        return empty;
    return signText[idx];
}

void World::setSignText(int x, int y, int z, const std::string &text)
{
    int idx = index(x, y, z);
    if (idx < 0 || idx >= static_cast<int>(signText.size()))
        return;
    signText[idx] = text;
    markChunkFromBlock(x, y, z);
}

bool collidesAt(const World &world, float px, float py, float pz, float playerHeight)
{
    float halfWidth = 0.3f;
    int minX = static_cast<int>(std::floor(px - halfWidth));
    int maxX = static_cast<int>(std::floor(px + halfWidth));
    int minZ = static_cast<int>(std::floor(pz - halfWidth));
    int maxZ = static_cast<int>(std::floor(pz + halfWidth));
    int minY = static_cast<int>(std::floor(py));
    int maxY = static_cast<int>(std::floor(py + playerHeight));
    for (int x = minX; x <= maxX; ++x)
    {
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                if (world.inside(x, y, z) && isSolid(world.get(x, y, z)))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool blockIntersectsPlayer(const Player &player, int bx, int by, int bz, float playerHeight)
{
    float halfWidth = 0.3f;
    float minX = player.x - halfWidth;
    float maxX = player.x + halfWidth;
    float minZ = player.z - halfWidth;
    float maxZ = player.z + halfWidth;
    float minY = player.y;
    float maxY = player.y + playerHeight;
    return (bx + 1 > minX && bx < maxX && bz + 1 > minZ && bz < maxZ && by + 1 > minY && by < maxY);
}

HitInfo raycast(const World &world, float ox, float oy, float oz, float dx, float dy, float dz, float maxDist)
{
    const float epsilon = 1e-6f;
    ox += dx * 0.01f;
    oy += dy * 0.01f;
    oz += dz * 0.01f;

    int x = static_cast<int>(std::floor(ox));
    int y = static_cast<int>(std::floor(oy));
    int z = static_cast<int>(std::floor(oz));

    float invDx = (std::abs(dx) < epsilon) ? 1e30f : 1.0f / dx;
    float invDy = (std::abs(dy) < epsilon) ? 1e30f : 1.0f / dy;
    float invDz = (std::abs(dz) < epsilon) ? 1e30f : 1.0f / dz;

    int stepX = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    int stepY = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
    int stepZ = (dz > 0) ? 1 : (dz < 0 ? -1 : 0);

    float nextX = (dx > 0) ? (x + 1.0f) : static_cast<float>(x);
    float nextY = (dy > 0) ? (y + 1.0f) : static_cast<float>(y);
    float nextZ = (dz > 0) ? (z + 1.0f) : static_cast<float>(z);

    float tMaxX = (stepX != 0) ? (nextX - ox) * invDx : 1e30f;
    float tMaxY = (stepY != 0) ? (nextY - oy) * invDy : 1e30f;
    float tMaxZ = (stepZ != 0) ? (nextZ - oz) * invDz : 1e30f;

    float tDeltaX = (stepX != 0) ? std::abs(invDx) : 1e30f;
    float tDeltaY = (stepY != 0) ? std::abs(invDy) : 1e30f;
    float tDeltaZ = (stepZ != 0) ? std::abs(invDz) : 1e30f;

    float t = 0.0f;
    int nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < 4096 && t <= maxDist; ++i)
    {
        if (tMaxX < tMaxY)
        {
            if (tMaxX < tMaxZ)
            {
                x += stepX;
                t = tMaxX;
                tMaxX += tDeltaX;
                nx = (stepX > 0) ? -1 : 1;
                ny = 0;
                nz = 0;
            }
            else
            {
                z += stepZ;
                t = tMaxZ;
                tMaxZ += tDeltaZ;
                nx = 0;
                ny = 0;
                nz = (stepZ > 0) ? -1 : 1;
            }
        }
        else
        {
            if (tMaxY < tMaxZ)
            {
                y += stepY;
                t = tMaxY;
                tMaxY += tDeltaY;
                nx = 0;
                ny = (stepY > 0) ? -1 : 1;
                nz = 0;
            }
            else
            {
                z += stepZ;
                t = tMaxZ;
                tMaxZ += tDeltaZ;
                nx = 0;
                ny = 0;
                nz = (stepZ > 0) ? -1 : 1;
            }
        }

        if (world.inside(x, y, z))
        {
            BlockType b = world.get(x, y, z);
            if (isSolid(b) || b == BlockType::Sign)
            {
                return {x, y, z, nx, ny, nz, true};
            }
        }
    }
    return {0, 0, 0, 0, 0, 0, false};
}

void updateLogic(World &world)
{
    int total = world.totalSize();
    std::vector<uint8_t> next(total, 0);
    std::vector<uint8_t> nextWidth(total, 8);
    std::vector<uint8_t> sourcesVal(total, 0);
    std::vector<std::array<int, 3>> gateOutputs;
    std::vector<std::array<int, 3>> notOutputs;
    std::vector<std::array<int, 3>> addSumOutputs;
    std::vector<std::array<int, 3>> addCoutOutputs;
    std::vector<uint8_t> gateOutVal;
    std::vector<uint8_t> gateOutWidth;
    std::vector<uint8_t> notOutVal;
    std::vector<uint8_t> addSumVal;
    std::vector<uint8_t> addCoutVal;
    std::vector<uint8_t> notOutWidth;
    std::vector<uint8_t> addSumWidth;
    std::vector<uint8_t> addCoutWidth;
    std::vector<int> dffIndices;
    std::vector<uint8_t> dffNextClk(total, 0);
    gateOutputs.reserve(total / 16);
    notOutputs.reserve(total / 16);
    addSumOutputs.reserve(total / 16);
    addCoutOutputs.reserve(total / 16);
    gateOutVal.reserve(total / 16);
    gateOutWidth.reserve(total / 16);
    notOutVal.reserve(total / 16);
    addSumVal.reserve(total / 16);
    addCoutVal.reserve(total / 16);
    notOutWidth.reserve(total / 16);
    addSumWidth.reserve(total / 16);
    addCoutWidth.reserve(total / 16);

    auto idx = [&](int x, int y, int z)
    { return world.index(x, y, z); };
    auto powerAt = [&](int x, int y, int z) -> uint8_t
    {
        if (!world.inside(x, y, z))
            return 0;
        return world.getPower(x, y, z);
    };
    auto widthAt = [&](int x, int y, int z) -> uint8_t
    {
        if (!world.inside(x, y, z))
            return 8;
        return world.getPowerWidth(x, y, z);
    };
    auto setPower = [&](int x, int y, int z, uint8_t val, uint8_t width)
    {
        if (!world.inside(x, y, z))
            return;
        int i = idx(x, y, z);
        if (next[i] == 0)
        {
            next[i] = val;
            nextWidth[i] = width == 0 ? 8 : width;
        }
        else
        {
            next[i] |= val;
            nextWidth[i] = std::max<uint8_t>(nextWidth[i], width == 0 ? 8 : width);
        }
    };
    std::vector<int> queue;
    queue.reserve(total / 4);
    auto pushWire = [&](int x, int y, int z, uint8_t val, uint8_t width)
    {
        if (!world.inside(x, y, z))
            return;
        int i = idx(x, y, z);
        uint8_t clampedW = width == 0 ? 8 : width;
        if (next[i] == 0)
        {
            next[i] = val;
            nextWidth[i] = clampedW;
            queue.push_back(i);
        }
        else if ((next[i] | val) != next[i])
        {
            next[i] |= val;
            nextWidth[i] = std::max<uint8_t>(nextWidth[i], clampedW);
            queue.push_back(i);
        }
        else if (clampedW > nextWidth[i])
        {
            nextWidth[i] = clampedW;
            queue.push_back(i);
        }
    };

    for (int y = 0; y < world.getHeight(); ++y)
        for (int z = 0; z < world.getDepth(); ++z)
            for (int x = 0; x < world.getWidth(); ++x)
                nextWidth[idx(x, y, z)] = world.getPowerWidth(x, y, z);
    for (int y = 0; y < world.getHeight(); ++y)
        for (int z = 0; z < world.getDepth(); ++z)
            for (int x = 0; x < world.getWidth(); ++x)
                nextWidth[idx(x, y, z)] = world.getPowerWidth(x, y, z);

    for (int y = 0; y < world.getHeight(); ++y)
    {
        for (int z = 0; z < world.getDepth(); ++z)
        {
            for (int x = 0; x < world.getWidth(); ++x)
            {
                BlockType b = world.get(x, y, z);
                uint8_t out = 0;
                switch (b)
                {
                case BlockType::AndGate:
                {
                    uint8_t inA = powerAt(x - 1, y, z);
                    uint8_t inB = powerAt(x + 1, y, z);
                    uint8_t w = static_cast<uint8_t>(std::min(widthAt(x - 1, y, z), widthAt(x + 1, y, z)));
                    if (w == 0)
                        w = 8;
                    uint8_t mask = w >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w) - 1u);
                    out = static_cast<uint8_t>((inA & mask) & (inB & mask));
                    if (out)
                    {
                        gateOutputs.push_back({x, y, z});
                        gateOutVal.push_back(out);
                        gateOutWidth.push_back(w);
                    }
                    break;
                }
                case BlockType::OrGate:
                {
                    uint8_t inA = powerAt(x - 1, y, z);
                    uint8_t inB = powerAt(x + 1, y, z);
                    uint8_t w = static_cast<uint8_t>(std::min(widthAt(x - 1, y, z), widthAt(x + 1, y, z)));
                    if (w == 0)
                        w = 8;
                    uint8_t mask = w >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w) - 1u);
                    out = static_cast<uint8_t>((inA & mask) | (inB & mask));
                    if (out)
                    {
                        gateOutputs.push_back({x, y, z});
                        gateOutVal.push_back(out);
                        gateOutWidth.push_back(w);
                    }
                    break;
                }
                case BlockType::XorGate:
                {
                    uint8_t inA = powerAt(x - 1, y, z);
                    uint8_t inB = powerAt(x + 1, y, z);
                    uint8_t w = static_cast<uint8_t>(std::min(widthAt(x - 1, y, z), widthAt(x + 1, y, z)));
                    if (w == 0)
                        w = 8;
                    uint8_t mask = w >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w) - 1u);
                    out = static_cast<uint8_t>((inA & mask) ^ (inB & mask));
                    if (out)
                    {
                        gateOutputs.push_back({x, y, z});
                        gateOutVal.push_back(out);
                        gateOutWidth.push_back(w);
                    }
                    break;
                }
                case BlockType::DFlipFlop:
                {
                    uint8_t storedQ = world.getPower(x, y, z);
                    uint8_t storedW = world.getPowerWidth(x, y, z);
                    if (storedW == 0)
                        storedW = 8;
                    uint8_t storedMask = storedW >= 8 ? 0xFFu : static_cast<uint8_t>((1u << storedW) - 1u);
                    storedQ &= storedMask;
                    uint8_t dIn = powerAt(x + 1, y, z);        // D on +X
                    uint8_t dW = widthAt(x + 1, y, z);
                    if (dW == 0)
                        dW = 8;
                    uint8_t clk = powerAt(x - 1, y, z) ? 1 : 0; // CLK on -X (bool)
                    uint8_t prevClk = world.getButtonState(x, y, z) ? 1 : 0;
                    uint8_t nextQ = storedQ;
                    uint8_t nextW = storedW ? storedW : dW;
                    if (clk && !prevClk)
                    {
                        uint8_t mask = dW >= 8 ? 0xFFu : static_cast<uint8_t>((1u << dW) - 1u);
                        nextQ = static_cast<uint8_t>(dIn & mask); // latch on rising edge
                        nextW = dW;
                    }

                    if (nextQ)
                    {
                        gateOutputs.push_back({x, y, z});
                        gateOutVal.push_back(nextQ);
                        gateOutWidth.push_back(nextW);
                    }

                    next[idx(x, y, z)] = nextQ;
                    nextWidth[idx(x, y, z)] = nextW;
                    int flat = idx(x, y, z);
                    dffNextClk[flat] = clk ? 1 : 0;
                    dffIndices.push_back(flat);
                    out = nextQ;
                    break;
                }
                case BlockType::AddGate:
                {
                    uint8_t wP = widthAt(x - 1, y, z);
                    uint8_t wQ = widthAt(x + 1, y, z);
                    uint8_t bitWidth = static_cast<uint8_t>(std::min<uint8_t>(std::max<uint8_t>(wP ? wP : 1, wQ ? wQ : 1), 8));
                    if (bitWidth == 0)
                        bitWidth = 1;
                    uint8_t mask = bitWidth >= 8 ? 0xFFu : static_cast<uint8_t>((1u << bitWidth) - 1u);

                    uint8_t p = powerAt(x - 1, y, z) & mask;   // P on -X
                    uint8_t q = powerAt(x + 1, y, z) & mask;   // Q on +X
                    uint8_t cin = powerAt(x, y, z - 1) ? 1 : 0; // Cin on -Z (bool)
                    uint16_t res = static_cast<uint16_t>(p) + static_cast<uint16_t>(q) +
                                   static_cast<uint16_t>(cin);
                    uint8_t sum = static_cast<uint8_t>(res & mask);
                    uint8_t cout = (res >> bitWidth) ? 0xFF : 0x00;
                    if (sum)
                    {
                        addSumOutputs.push_back({x, y, z});
                        addSumVal.push_back(sum);
                        addSumWidth.push_back(bitWidth);
                    }
                    if (cout)
                    {
                        addCoutOutputs.push_back({x, y, z});
                        addCoutVal.push_back(cout);
                        addCoutWidth.push_back(1);
                    }
                    out = 0;
                    break;
                }
                case BlockType::NotGate:
                {
                    uint8_t w = widthAt(x + 1, y, z);
                    if (w == 0)
                        w = 8;
                    uint8_t mask = w >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w) - 1u);
                    uint8_t inA = powerAt(x + 1, y, z) & mask; // input on +X (right)
                    out = static_cast<uint8_t>(~inA & mask);
                    if (out)
                    {
                        notOutputs.push_back({x, y, z});
                        notOutVal.push_back(out);
                        notOutWidth.push_back(w);
                    }
                    break;
                }
                case BlockType::Button:
                {
                    uint8_t active = world.getButtonState(x, y, z) ? 1 : 0;
                    uint8_t width = world.getButtonWidth(x, y, z);
                    if (width == 0)
                        width = 8;
                    uint8_t mask = (width >= 8) ? 0xFFu : static_cast<uint8_t>((1u << width) - 1u);
                    out = active ? static_cast<uint8_t>(world.getButtonValue(x, y, z) & mask) : 0;
                    if (out)
                        nextWidth[idx(x, y, z)] = width;
                    break;
                }
                case BlockType::Counter:
                {
                    // Single input on +X
                    uint8_t val = powerAt(x + 1, y, z);
                    next[idx(x, y, z)] = val;
                    nextWidth[idx(x, y, z)] = widthAt(x + 1, y, z);
                    out = 0;
                    break;
                }
                case BlockType::Splitter:
                {
                    uint8_t busW = widthAt(x, y, z - 1);
                    if (busW == 0)
                        busW = 1;
                    uint8_t busMask = busW >= 8 ? 0xFFu : static_cast<uint8_t>((1u << busW) - 1u);
                    uint8_t busVal = powerAt(x, y, z - 1) & busMask;
                    uint8_t wParam = world.getSplitterWidth(x, y, z);
                    uint8_t order = world.getSplitterOrder(x, y, z);
                    uint8_t w1 = std::clamp<uint8_t>(wParam, 1, static_cast<uint8_t>(std::max<int>(1, busW - 1)));
                    uint8_t w2 = static_cast<uint8_t>(std::max<int>(1, busW - w1));
                    uint8_t mask1 = w1 >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w1) - 1u);
                    uint8_t mask2 = w2 >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w2) - 1u);
                    uint8_t out1 = 0, out2 = 0;
                    if (order == 0)
                    {
                        out1 = busVal & mask1;                     // LSB chunk -> B1 (-X)
                        out2 = static_cast<uint8_t>((busVal >> w1) & mask2); // remaining -> B2 (+X)
                    }
                    else
                    {
                        out2 = busVal & mask2;                     // LSB chunk -> B2 (+X)
                        out1 = static_cast<uint8_t>((busVal >> w2) & mask1); // MSB chunk -> B1 (-X)
                    }
                    if (out1)
                        pushWire(x - 1, y, z, out1, w1); // B1 on -X
                    if (out2)
                        pushWire(x + 1, y, z, out2, w2); // B2 on +X
                    out = 0;
                    break;
                }
                case BlockType::Merger:
                {
                    uint8_t wParam = world.getSplitterWidth(x, y, z);
                    uint8_t order = world.getSplitterOrder(x, y, z);
                    uint8_t inW1 = widthAt(x - 1, y, z);
                    uint8_t inW2 = widthAt(x + 1, y, z);
                    if (inW1 == 0)
                        inW1 = 8;
                    if (inW2 == 0)
                        inW2 = 8;
                    uint8_t w1 = std::clamp<uint8_t>(wParam, 1, 7);
                    uint8_t w2 = static_cast<uint8_t>(std::max<int>(1, std::min<int>(8 - w1, inW2)));
                    uint8_t mask1 = w1 >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w1) - 1u);
                    uint8_t mask2 = w2 >= 8 ? 0xFFu : static_cast<uint8_t>((1u << w2) - 1u);
                    uint8_t in1 = powerAt(x - 1, y, z) & mask1;
                    uint8_t in2 = powerAt(x + 1, y, z) & mask2;
                    uint8_t busW = static_cast<uint8_t>(std::min<int>(8, w1 + w2));
                    uint8_t busVal = 0;
                    if (order == 0)
                        busVal = static_cast<uint8_t>(in1 | static_cast<uint8_t>(in2 << w1)); // B1 in LSB
                    else
                        busVal = static_cast<uint8_t>((in1 << w2) | in2); // B1 in MSB
                    if (busVal)
                        pushWire(x, y, z + 1, busVal, busW); // BUS out on +Z
                    out = 0;
                    break;
                }
                default:
                    out = 0;
                    break;
                }
                if (out && b == BlockType::Button)
                {
                    sourcesVal[idx(x, y, z)] = out;
                    next[idx(x, y, z)] = out;
                    nextWidth[idx(x, y, z)] = world.getButtonWidth(x, y, z);
                }
            }
        }
    }

    for (int i = 0; i < total; ++i)
        if (sourcesVal[i])
            queue.push_back(i);

    for (size_t idxOut = 0; idxOut < gateOutputs.size(); ++idxOut)
    {
        const auto &g = gateOutputs[idxOut];
        uint8_t val = gateOutVal[idxOut];
        uint8_t w = gateOutWidth[idxOut];
        int ox = g[0];
        int oy = g[1];
        int oz = g[2] + 1;
        if (!world.inside(ox, oy, oz))
            continue;
        BlockType outB = world.get(ox, oy, oz);
        if (outB == BlockType::Wire)
        {
            pushWire(ox, oy, oz, val, w);
        }
        else
        {
            setPower(ox, oy, oz, val, w);
        }
    }

    for (size_t idxOut = 0; idxOut < addSumOutputs.size(); ++idxOut)
    {
        const auto &g = addSumOutputs[idxOut];
        uint8_t val = addSumVal[idxOut];
        uint8_t w = addSumWidth[idxOut];
        int ox = g[0];
        int oy = g[1];
        int oz = g[2] + 1; // S toward +Z
        if (!world.inside(ox, oy, oz))
            continue;
        BlockType outB = world.get(ox, oy, oz);
        if (outB == BlockType::Wire)
        {
            pushWire(ox, oy, oz, val, w);
        }
        else
        {
            setPower(ox, oy, oz, val, w);
        }
    }

    for (size_t idxOut = 0; idxOut < addCoutOutputs.size(); ++idxOut)
    {
        const auto &g = addCoutOutputs[idxOut];
        uint8_t val = addCoutVal[idxOut];
        uint8_t w = addCoutWidth[idxOut];
        int ox = g[0];
        int oy = g[1] - 1; // Cout downward
        int oz = g[2];
        if (!world.inside(ox, oy, oz))
            continue;
        BlockType outB = world.get(ox, oy, oz);
        if (outB == BlockType::Wire)
        {
            pushWire(ox, oy, oz, val, w);
        }
        else
        {
            setPower(ox, oy, oz, val, w);
        }
    }

    // NOT outputs go toward -X only (input on +X)
    for (size_t idxOut = 0; idxOut < notOutputs.size(); ++idxOut)
    {
        const auto &g = notOutputs[idxOut];
        uint8_t val = notOutVal[idxOut];
        uint8_t w = notOutWidth[idxOut];
        int ox = g[0] - 1;
        int oy = g[1];
        int oz = g[2];
        if (!world.inside(ox, oy, oz))
            continue;
        BlockType outB = world.get(ox, oy, oz);
        if (outB == BlockType::Wire)
        {
            pushWire(ox, oy, oz, val, w);
        }
        else
        {
            setPower(ox, oy, oz, val, w);
        }
    }

    while (!queue.empty())
    {
        int i = queue.back();
        queue.pop_back();
        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();
        uint8_t valHere = next[i];

        int nx[6] = {x + 1, x - 1, x, x, x, x};
        int ny[6] = {y, y, y + 1, y - 1, y, y};
        int nz[6] = {z, z, z, z, z + 1, z - 1};
        for (int k = 0; k < 6; ++k)
        {
            int xx = nx[k], yy = ny[k], zz = nz[k];
            if (!world.inside(xx, yy, zz))
                continue;
            BlockType nb = world.get(xx, yy, zz);
            if (nb == BlockType::Wire)
                pushWire(xx, yy, zz, valHere, nextWidth[i]);
        }
    }

    auto nextAtNonLed = [&](int x, int y, int z) -> uint8_t
    {
        if (!world.inside(x, y, z))
            return 0;
        if (world.get(x, y, z) == BlockType::Led)
            return 0;
        return next[idx(x, y, z)];
    };

    for (int y = 0; y < world.getHeight(); ++y)
    {
        for (int z = 0; z < world.getDepth(); ++z)
        {
            for (int x = 0; x < world.getWidth(); ++x)
            {
                if (world.get(x, y, z) != BlockType::Led)
                    continue;
                int nx[6] = {x + 1, x - 1, x, x, x, x};
                int ny[6] = {y, y, y + 1, y - 1, y, y};
                int nz[6] = {z, z, z, z, z + 1, z - 1};
                uint8_t lit = nextAtNonLed(x, y, z);
                for (int i = 0; i < 6 && !lit; ++i)
                    lit = nextAtNonLed(nx[i], ny[i], nz[i]) ? 1 : 0;
                next[idx(x, y, z)] = lit;
            }
        }
    }

    for (int flat : dffIndices)
    {
        int x = flat % world.getWidth();
        int y = (flat / world.getWidth()) / world.getDepth();
        int z = (flat / world.getWidth()) % world.getDepth();
        world.setButtonState(x, y, z, dffNextClk[flat]);
    }

    for (int y = 0; y < world.getHeight(); ++y)
    {
        for (int z = 0; z < world.getDepth(); ++z)
        {
            for (int x = 0; x < world.getWidth(); ++x)
            {
                uint8_t old = world.getPower(x, y, z);
                uint8_t nw = next[idx(x, y, z)];
                if (old != nw)
                    markChunkFromBlock(x, y, z);
            }
        }
    }
    world.overwritePower(next, nextWidth);
}
