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
    {BlockType::Led, {"LED", true, {0.95f, 0.9f, 0.2f}}},
    {BlockType::Button, {"Button", true, {0.6f, 0.2f, 0.2f}}},
    {BlockType::Wire, {"Wire", true, {0.55f, 0.55f, 0.58f}}},
};

const std::vector<BlockType> HOTBAR = {BlockType::Dirt, BlockType::Grass, BlockType::Wood,
                                       BlockType::Stone, BlockType::Glass, BlockType::NotGate};
const std::vector<BlockType> INVENTORY_ALLOWED = {BlockType::Dirt,     BlockType::Grass, BlockType::Wood,
                                                  BlockType::Stone,    BlockType::Glass, BlockType::AndGate,
                                                  BlockType::OrGate,   BlockType::NotGate, BlockType::Led,
                                                  BlockType::Button,   BlockType::Wire};

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
      buttonState(w * h * d, 0)
{
}

BlockType World::get(int x, int y, int z) const { return tiles[index(x, y, z)]; }

void World::set(int x, int y, int z, BlockType b)
{
    int idx = index(x, y, z);
    tiles[idx] = b;
    power[idx] = 0;
    if (b != BlockType::Button)
        buttonState[idx] = 0;
}

uint8_t World::getPower(int x, int y, int z) const { return power[index(x, y, z)]; }

void World::setPower(int x, int y, int z, uint8_t v) { power[index(x, y, z)] = v; }

uint8_t World::getButtonState(int x, int y, int z) const { return buttonState[index(x, y, z)]; }

void World::setButtonState(int x, int y, int z, uint8_t v) { buttonState[index(x, y, z)] = v; }

void World::toggleButton(int x, int y, int z)
{
    int idx = index(x, y, z);
    buttonState[idx] = buttonState[idx] ? 0 : 1;
}

int World::index(int x, int y, int z) const { return (y * depth + z) * width + x; }

int World::totalSize() const { return static_cast<int>(tiles.size()); }

void World::overwritePower(const std::vector<uint8_t> &next) { power = next; }

int World::getWidth() const { return width; }
int World::getHeight() const { return height; }
int World::getDepth() const { return depth; }

void World::generate(unsigned seed)
{
    std::mt19937 rng(seed);
    (void)rng;

    int surface = height / 4;
    std::fill(power.begin(), power.end(), 0);
    std::fill(buttonState.begin(), buttonState.end(), 0);
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

        if (world.inside(x, y, z) && isSolid(world.get(x, y, z)))
        {
            return {x, y, z, nx, ny, nz, true};
        }
    }
    return {0, 0, 0, 0, 0, 0, false};
}

void updateLogic(World &world)
{
    int total = world.totalSize();
    std::vector<uint8_t> next(total, 0);
    std::vector<uint8_t> sources(total, 0);
    std::vector<std::array<int, 3>> gateOutputs;
    std::vector<std::array<int, 3>> notOutputs;
    gateOutputs.reserve(total / 16);
    notOutputs.reserve(total / 16);

    auto idx = [&](int x, int y, int z)
    { return world.index(x, y, z); };
    auto powerAt = [&](int x, int y, int z) -> uint8_t
    {
        if (!world.inside(x, y, z))
            return 0;
        return world.getPower(x, y, z);
    };

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
                    int inA = powerAt(x - 1, y, z) ? 1 : 0;
                    int inB = powerAt(x + 1, y, z) ? 1 : 0;
                    out = (inA && inB) ? 1 : 0;
                    if (out)
                        gateOutputs.push_back({x, y, z});
                    break;
                }
                case BlockType::OrGate:
                {
                    int inA = powerAt(x - 1, y, z) ? 1 : 0;
                    int inB = powerAt(x + 1, y, z) ? 1 : 0;
                    out = (inA || inB) ? 1 : 0;
                    if (out)
                        gateOutputs.push_back({x, y, z});
                    break;
                }
                case BlockType::NotGate:
                {
                    int inA = powerAt(x + 1, y, z) ? 1 : 0; // input on +X
                    out = inA ? 0 : 1;
                    if (out)
                        notOutputs.push_back({x, y, z});
                    break;
                }
                case BlockType::Button:
                    out = world.getButtonState(x, y, z) ? 1 : 0;
                    break;
                default:
                    out = 0;
                    break;
                }
                if (out && b == BlockType::Button)
                {
                    sources[idx(x, y, z)] = 1;
                    next[idx(x, y, z)] = 1;
                }
            }
        }
    }

    std::vector<int> queue;
    queue.reserve(total / 4);
    for (int i = 0; i < total; ++i)
        if (sources[i])
            queue.push_back(i);

    auto setPower = [&](int x, int y, int z)
    {
        if (!world.inside(x, y, z))
            return;
        int i = idx(x, y, z);
        if (next[i] == 0)
            next[i] = 1;
    };

    auto pushWire = [&](int x, int y, int z)
    {
        if (!world.inside(x, y, z))
            return;
        int i = idx(x, y, z);
        if (next[i] == 0)
        {
            next[i] = 1;
            queue.push_back(i);
        }
    };

    for (const auto &g : gateOutputs)
    {
        int ox = g[0];
        int oy = g[1];
        int oz = g[2] + 1;
        if (!world.inside(ox, oy, oz))
            continue;
        int outIdx = idx(ox, oy, oz);
        BlockType outB = world.get(ox, oy, oz);
        if (outB == BlockType::Wire)
        {
            if (next[outIdx] == 0)
                next[outIdx] = 1;
            queue.push_back(outIdx);
        }
        else
        {
            setPower(ox, oy, oz);
        }
    }

    // NOT outputs go toward -X only (input on +X)
    for (const auto &g : notOutputs)
    {
        int ox = g[0] - 1;
        int oy = g[1];
        int oz = g[2];
        if (!world.inside(ox, oy, oz))
            continue;
        int outIdx = idx(ox, oy, oz);
        BlockType outB = world.get(ox, oy, oz);
        if (outB == BlockType::Wire)
        {
            if (next[outIdx] == 0)
                next[outIdx] = 1;
            queue.push_back(outIdx);
        }
        else
        {
            setPower(ox, oy, oz);
        }
    }

    while (!queue.empty())
    {
        int i = queue.back();
        queue.pop_back();
        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();

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
                pushWire(xx, yy, zz);
        }
    }

    auto nextAt = [&](int x, int y, int z) -> uint8_t
    {
        if (!world.inside(x, y, z))
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
                uint8_t lit = nextAt(x, y, z);
                for (int i = 0; i < 6 && !lit; ++i)
                    lit = nextAt(nx[i], ny[i], nz[i]) ? 1 : 0;
                next[idx(x, y, z)] = lit;
            }
        }
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
    world.overwritePower(next);
}
