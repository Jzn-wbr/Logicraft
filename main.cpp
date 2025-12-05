#define SDL_MAIN_HANDLED
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

enum class BlockType
{
    Air,
    Grass,
    Dirt,
    Stone,
    Wood,
    Leaves,
    Water,
    Plank,
    Sand,
    Glass,
    AndGate,
    OrGate,
    Led,
    Button,
    Wire
};

struct Player
{
    float x = 0.0f;
    float y = 20.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

struct NPC
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float width = 0.8f;
    float height = 1.9f;
    GLuint texture = 0;
    float dirX = 0.0f;
    float dirZ = 0.0f;
    float timeUntilTurn = 0.0f;
};

struct BlockInfo
{
    std::string name;
    bool solid;
    std::array<float, 3> color;
};

struct ItemStack
{
    BlockType type = BlockType::Air;
    int count = 0;
};

struct PauseMenuLayout
{
    float panelX = 0.0f, panelY = 0.0f, panelW = 0.0f, panelH = 0.0f;
    float resumeX = 0.0f, resumeY = 0.0f, resumeW = 0.0f, resumeH = 0.0f;
    float quitX = 0.0f, quitY = 0.0f, quitW = 0.0f, quitH = 0.0f;
};

struct HoverLabel
{
    bool valid = false;
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
};

static const std::map<BlockType, BlockInfo> BLOCKS = {
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
    {BlockType::Led, {"LED", true, {0.95f, 0.9f, 0.2f}}},
    {BlockType::Button, {"Button", true, {0.6f, 0.2f, 0.2f}}},
    {BlockType::Wire, {"Wire", true, {0.55f, 0.55f, 0.58f}}},
};

static const std::vector<BlockType> HOTBAR = {BlockType::Dirt, BlockType::Grass, BlockType::Wood,
                                              BlockType::Stone, BlockType::Glass};
static const std::vector<BlockType> INVENTORY_ALLOWED = {BlockType::Dirt, BlockType::Grass, BlockType::Wood,
                                                         BlockType::Stone, BlockType::Glass, BlockType::AndGate,
                                                         BlockType::OrGate, BlockType::Led, BlockType::Button,
                                                         BlockType::Wire};

struct Vertex
{
    float x, y, z;
    float u, v;
    float r, g, b;
};

struct ChunkMesh
{
    std::vector<Vertex> verts;
    GLuint vbo = 0;
    bool dirty = true;
};

class World
{
public:
    World(int w, int h, int d)
        : width(w), height(h), depth(d), tiles(w * h * d, BlockType::Air), power(w * h * d, 0),
          buttonState(w * h * d, 0)
    {
    }

    BlockType get(int x, int y, int z) const { return tiles[index(x, y, z)]; }
    void set(int x, int y, int z, BlockType b)
    {
        int idx = index(x, y, z);
        tiles[idx] = b;
        power[idx] = 0;
        if (b != BlockType::Button)
            buttonState[idx] = 0;
    }
    uint8_t getPower(int x, int y, int z) const { return power[index(x, y, z)]; }
    void setPower(int x, int y, int z, uint8_t v) { power[index(x, y, z)] = v; }
    uint8_t getButtonState(int x, int y, int z) const { return buttonState[index(x, y, z)]; }
    void setButtonState(int x, int y, int z, uint8_t v) { buttonState[index(x, y, z)] = v; }
    void toggleButton(int x, int y, int z)
    {
        int idx = index(x, y, z);
        buttonState[idx] = buttonState[idx] ? 0 : 1;
    }
    int index(int x, int y, int z) const { return (y * depth + z) * width + x; }
    int totalSize() const { return static_cast<int>(tiles.size()); }
    void overwritePower(const std::vector<uint8_t> &next) { power = next; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getDepth() const { return depth; }

    void generate(unsigned seed)
    {
        std::mt19937 rng(seed);
        (void)rng; // seed kept for future randomness if needed

        int surface = height / 4; // flat world height
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

    bool inside(int x, int y, int z) const { return x >= 0 && x < width && y >= 0 && y < height && z >= 0 && z < depth; }

    int surfaceY(int x, int z) const;

private:
    int width;
    int height;
    int depth;
    std::vector<BlockType> tiles;
    std::vector<uint8_t> power;
    std::vector<uint8_t> buttonState;
};

bool isSolid(BlockType b) { return BLOCKS.at(b).solid; }

bool occludesFaces(BlockType b)
{
    if (b == BlockType::Wire)
        return false;
    if (b == BlockType::Glass)
        return false;
    return isSolid(b);
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

struct HitInfo
{
    int x, y, z;    // bloc touch??
    int nx, ny, nz; // normale de la face touch??e (-1,0,1)
    bool hit;
};

HitInfo raycast(const World &world, float ox, float oy, float oz, float dx, float dy, float dz, float maxDist)
{
    const float epsilon = 1e-6f;
    // D??cale l??g??rement l'origine pour ??viter de toucher le bloc du joueur
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

bool isTransparent(BlockType b)
{
    if (b == BlockType::Wire)
        return true;
    if (b == BlockType::Glass)
        return true;
    return !isSolid(b);
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

void markChunkFromBlock(int x, int y, int z);

struct Vec3
{
    float x, y, z;
};

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalizeVec(const Vec3 &v)
{
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f)
        return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

// Mesh par chunk (VBO)
const int CHUNK_SIZE = 16;
int CHUNK_X_COUNT = 0;
int CHUNK_Y_COUNT = 0;
int CHUNK_Z_COUNT = 0;
std::vector<ChunkMesh> chunkMeshes;
GLuint gAtlasTex = 0;
const int ATLAS_COLS = 4;
const int ATLAS_ROWS = 5;
const int ATLAS_TILE_SIZE = 32;
std::map<BlockType, int> gBlockTile;
int gAndTopTile = 0;
int gOrTopTile = 0;
const int MAX_STACK = 64;
const int INV_COLS = 5;
const int INV_ROWS = 3;

Vec3 forwardVec(float yaw, float pitch)
{
    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    float sy = std::sin(yaw);
    float cy = std::cos(yaw);
    return {sy * cp, sp, -cy * cp};
}

// RNG ultra simple et peu coûteux, suffisant pour un déplacement bête.
float cheapRand01()
{
    static uint32_t state = 0x12345678u;
    state = 1664525u * state + 1013904223u;
    return static_cast<float>((state >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

void updateNpc(NPC &npc, const World &world, float dt)
{
    npc.timeUntilTurn -= dt;
    if (npc.timeUntilTurn <= 0.0f)
    {
        // Décide rarement un mouvement, sinon il reste immobile pour être moins présent.
        if (cheapRand01() < 0.4f)
        {
            float angle = cheapRand01() * 6.2831853f; // 2pi
            npc.dirX = std::cos(angle);
            npc.dirZ = std::sin(angle);
        }
        else
        {
            npc.dirX = 0.0f;
            npc.dirZ = 0.0f;
        }
        npc.timeUntilTurn = 2.5f + cheapRand01() * 3.5f; // reste longtemps dans le même état
    }

    const float npcSpeed = 1.2f;
    float nextX = npc.x + npc.dirX * npcSpeed * dt;
    float nextZ = npc.z + npc.dirZ * npcSpeed * dt;
    nextX = std::clamp(nextX, 1.0f, static_cast<float>(world.getWidth() - 2));
    nextZ = std::clamp(nextZ, 1.0f, static_cast<float>(world.getDepth() - 2));

    int tileX = static_cast<int>(std::floor(nextX));
    int tileZ = static_cast<int>(std::floor(nextZ));
    if (world.inside(tileX, 0, tileZ))
    {
        npc.x = nextX;
        npc.z = nextZ;
        npc.y = static_cast<float>(world.surfaceY(tileX, tileZ));
    }
}

void updateLogic(World &world)
{
    int total = world.totalSize();
    std::vector<uint8_t> next(total, 0);
    std::vector<uint8_t> sources(total, 0);
    std::vector<std::array<int, 3>> gateOutputs;
    gateOutputs.reserve(total / 16);

    auto idx = [&](int x, int y, int z)
    { return world.index(x, y, z); };
    auto powerAt = [&](int x, int y, int z) -> uint8_t
    {
        if (!world.inside(x, y, z))
            return 0;
        return world.getPower(x, y, z);
    };

    // 1) evaluate sources (buttons, directional gates)
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

    // 2) propagate via wires (BFS from sources)
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

    // gate outputs: fixed direction toward +Z
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
            queue.push_back(outIdx); // always explore from the gate output wire
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

    // 3) LEDs light up if a neighbour is powered (or direct pre-set)
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

    // 4) mark dirty if changed then overwrite state
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

void drawBlockFaces(const World &world, int x, int y, int z, float s, const std::array<float, 3> &color)
{
    float hs = s * 0.5f;
    float vx[8][3] = {{x - hs, y - hs, z - hs}, {x + hs, y - hs, z - hs}, {x + hs, y + hs, z - hs}, {x - hs, y + hs, z - hs}, {x - hs, y - hs, z + hs}, {x + hs, y - hs, z + hs}, {x + hs, y + hs, z + hs}, {x - hs, y + hs, z + hs}};

    auto neighborTransparent = [&](int nx, int ny, int nz)
    {
        if (!world.inside(nx, ny, nz))
        {
            return true;
        }
        return isTransparent(world.get(nx, ny, nz));
    };

    glColor3f(color[0], color[1], color[2]);
    glBegin(GL_QUADS);
    // back (-z)
    if (neighborTransparent(x, y, z - 1))
    {
        glVertex3fv(vx[0]);
        glVertex3fv(vx[1]);
        glVertex3fv(vx[2]);
        glVertex3fv(vx[3]);
    }
    // front (+z)
    if (neighborTransparent(x, y, z + 1))
    {
        glVertex3fv(vx[4]);
        glVertex3fv(vx[5]);
        glVertex3fv(vx[6]);
        glVertex3fv(vx[7]);
    }
    // left (-x)
    if (neighborTransparent(x - 1, y, z))
    {
        glVertex3fv(vx[0]);
        glVertex3fv(vx[4]);
        glVertex3fv(vx[7]);
        glVertex3fv(vx[3]);
    }
    // right (+x)
    if (neighborTransparent(x + 1, y, z))
    {
        glVertex3fv(vx[1]);
        glVertex3fv(vx[5]);
        glVertex3fv(vx[6]);
        glVertex3fv(vx[2]);
    }
    // bottom (-y)
    if (neighborTransparent(x, y - 1, z))
    {
        glVertex3fv(vx[0]);
        glVertex3fv(vx[1]);
        glVertex3fv(vx[5]);
        glVertex3fv(vx[4]);
    }
    // top (+y)
    if (neighborTransparent(x, y + 1, z))
    {
        glVertex3fv(vx[3]);
        glVertex3fv(vx[2]);
        glVertex3fv(vx[6]);
        glVertex3fv(vx[7]);
    }
    glEnd();

    glColor3f(0.05f, 0.05f, 0.05f);
    glLineWidth(1.0f);
    // back (-z)
    if (neighborTransparent(x, y, z - 1))
    {
        glBegin(GL_LINE_LOOP);
        glVertex3fv(vx[0]);
        glVertex3fv(vx[1]);
        glVertex3fv(vx[2]);
        glVertex3fv(vx[3]);
        glEnd();
    }
    // front (+z)
    if (neighborTransparent(x, y, z + 1))
    {
        glBegin(GL_LINE_LOOP);
        glVertex3fv(vx[4]);
        glVertex3fv(vx[5]);
        glVertex3fv(vx[6]);
        glVertex3fv(vx[7]);
        glEnd();
    }
    // left (-x)
    if (neighborTransparent(x - 1, y, z))
    {
        glBegin(GL_LINE_LOOP);
        glVertex3fv(vx[0]);
        glVertex3fv(vx[4]);
        glVertex3fv(vx[7]);
        glVertex3fv(vx[3]);
        glEnd();
    }
    // right (+x)
    if (neighborTransparent(x + 1, y, z))
    {
        glBegin(GL_LINE_LOOP);
        glVertex3fv(vx[1]);
        glVertex3fv(vx[5]);
        glVertex3fv(vx[6]);
        glVertex3fv(vx[2]);
        glEnd();
    }
    // bottom (-y)
    if (neighborTransparent(x, y - 1, z))
    {
        glBegin(GL_LINE_LOOP);
        glVertex3fv(vx[0]);
        glVertex3fv(vx[1]);
        glVertex3fv(vx[5]);
        glVertex3fv(vx[4]);
        glEnd();
    }
    // top (+y)
    if (neighborTransparent(x, y + 1, z))
    {
        glBegin(GL_LINE_LOOP);
        glVertex3fv(vx[3]);
        glVertex3fv(vx[2]);
        glVertex3fv(vx[6]);
        glVertex3fv(vx[7]);
        glEnd();
    }
}

void drawOutlinedCube(float x, float y, float z, float s)
{
    float hs = s * 0.5f;
    float vx[8][3] = {{x - hs, y - hs, z - hs}, {x + hs, y - hs, z - hs}, {x + hs, y + hs, z - hs}, {x - hs, y + hs, z - hs}, {x - hs, y - hs, z + hs}, {x + hs, y - hs, z + hs}, {x + hs, y + hs, z + hs}, {x - hs, y + hs, z + hs}};
    glColor3f(1.0f, 0.9f, 0.2f);
    glBegin(GL_LINES);
    int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto &e : edges)
    {
        glVertex3fv(vx[e[0]]);
        glVertex3fv(vx[e[1]]);
    }
    glEnd();
}

void drawBlockOutlineExact(int bx, int by, int bz)
{
    float minX = static_cast<float>(bx);
    float maxX = static_cast<float>(bx + 1);
    float minY = static_cast<float>(by);
    float maxY = static_cast<float>(by + 1);
    float minZ = static_cast<float>(bz);
    float maxZ = static_cast<float>(bz + 1);
    glColor3f(1.0f, 0.9f, 0.2f);
    glBegin(GL_LINES);
    // bottom square
    glVertex3f(minX, minY, minZ);
    glVertex3f(maxX, minY, minZ);

    glVertex3f(maxX, minY, minZ);
    glVertex3f(maxX, minY, maxZ);

    glVertex3f(maxX, minY, maxZ);
    glVertex3f(minX, minY, maxZ);

    glVertex3f(minX, minY, maxZ);
    glVertex3f(minX, minY, minZ);

    // top square
    glVertex3f(minX, maxY, minZ);
    glVertex3f(maxX, maxY, minZ);

    glVertex3f(maxX, maxY, minZ);
    glVertex3f(maxX, maxY, maxZ);

    glVertex3f(maxX, maxY, maxZ);
    glVertex3f(minX, maxY, maxZ);

    glVertex3f(minX, maxY, maxZ);
    glVertex3f(minX, maxY, minZ);

    // verticals
    glVertex3f(minX, minY, minZ);
    glVertex3f(minX, maxY, minZ);

    glVertex3f(maxX, minY, minZ);
    glVertex3f(maxX, maxY, minZ);

    glVertex3f(maxX, minY, maxZ);
    glVertex3f(maxX, maxY, maxZ);

    glVertex3f(minX, minY, maxZ);
    glVertex3f(minX, maxY, maxZ);
    glEnd();
}

void drawBlockHighlight(int bx, int by, int bz)
{
    float minX = static_cast<float>(bx);
    float maxX = static_cast<float>(bx + 1);
    float minY = static_cast<float>(by);
    float maxY = static_cast<float>(by + 1);
    float minZ = static_cast<float>(bz);
    float maxZ = static_cast<float>(bz + 1);
    glColor4f(0.3f, 0.3f, 0.3f, 0.35f);
    glBegin(GL_QUADS);
    // back (-z)
    glVertex3f(minX, minY, minZ);
    glVertex3f(maxX, minY, minZ);
    glVertex3f(maxX, maxY, minZ);
    glVertex3f(minX, maxY, minZ);
    // front (+z)
    glVertex3f(minX, minY, maxZ);
    glVertex3f(maxX, minY, maxZ);
    glVertex3f(maxX, maxY, maxZ);
    glVertex3f(minX, maxY, maxZ);
    // left (-x)
    glVertex3f(minX, minY, minZ);
    glVertex3f(minX, minY, maxZ);
    glVertex3f(minX, maxY, maxZ);
    glVertex3f(minX, maxY, minZ);
    // right (+x)
    glVertex3f(maxX, minY, minZ);
    glVertex3f(maxX, minY, maxZ);
    glVertex3f(maxX, maxY, maxZ);
    glVertex3f(maxX, maxY, minZ);
    // bottom (-y)
    glVertex3f(minX, minY, minZ);
    glVertex3f(maxX, minY, minZ);
    glVertex3f(maxX, minY, maxZ);
    glVertex3f(minX, minY, maxZ);
    // top (+y)
    glVertex3f(minX, maxY, minZ);
    glVertex3f(maxX, maxY, minZ);
    glVertex3f(maxX, maxY, maxZ);
    glVertex3f(minX, maxY, maxZ);
    glEnd();
}

void drawFaceHighlight(const HitInfo &hit)
{
    if (!hit.hit)
        return;
    glColor4f(0.3f, 0.3f, 0.3f, 0.45f);

    Vec3 n{static_cast<float>(hit.nx), static_cast<float>(hit.ny), static_cast<float>(hit.nz)};
    Vec3 u{0.0f, 0.0f, 0.0f};
    Vec3 v{0.0f, 0.0f, 0.0f};

    // choose two perpendicular axes lying on the face
    if (std::abs(n.x) > 0.5f)
    {
        u = {0.0f, 1.0f, 0.0f};
        v = {0.0f, 0.0f, 1.0f};
    }
    else if (std::abs(n.y) > 0.5f)
    {
        u = {1.0f, 0.0f, 0.0f};
        v = {0.0f, 0.0f, 1.0f};
    }
    else
    {
        u = {1.0f, 0.0f, 0.0f};
        v = {0.0f, 1.0f, 0.0f};
    }

    auto add = [](const Vec3 &a, const Vec3 &b)
    { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; };
    auto scale = [](const Vec3 &a, float s)
    { return Vec3{a.x * s, a.y * s, a.z * s}; };

    const float eps = 0.001f;
    Vec3 center{hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f};
    center = add(center, scale(n, 0.5f + eps));
    Vec3 uHalf = scale(u, 0.5f);
    Vec3 vHalf = scale(v, 0.5f);

    Vec3 p1 = add(add(center, uHalf), vHalf);
    Vec3 p2 = add(add(center, uHalf), scale(vHalf, -1.0f));
    Vec3 p3 = add(add(center, scale(uHalf, -1.0f)), scale(vHalf, -1.0f));
    Vec3 p4 = add(add(center, scale(uHalf, -1.0f)), vHalf);

    glBegin(GL_QUADS);
    glVertex3f(p1.x, p1.y, p1.z);
    glVertex3f(p2.x, p2.y, p2.z);
    glVertex3f(p3.x, p3.y, p3.z);
    glVertex3f(p4.x, p4.y, p4.z);
    glEnd();
}

void drawNpcBlocky(const NPC &npc)
{
    // Blocks are quarter-size vs world blocks.
    const float s = 0.25f;
    float baseY = npc.y;

    auto drawColoredCube = [](float cx, float cy, float cz, float size, float r, float g, float b)
    {
        float hx = size * 0.5f;
        float x0 = cx - hx, x1 = cx + hx;
        float y0 = cy - hx, y1 = cy + hx;
        float z0 = cz - hx, z1 = cz + hx;
        glDisable(GL_TEXTURE_2D);
        glColor3f(r, g, b);
        glBegin(GL_QUADS);
        // front
        glVertex3f(x0, y0, z1);
        glVertex3f(x1, y0, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x0, y1, z1);
        // back
        glVertex3f(x1, y0, z0);
        glVertex3f(x0, y0, z0);
        glVertex3f(x0, y1, z0);
        glVertex3f(x1, y1, z0);
        // left
        glVertex3f(x0, y0, z0);
        glVertex3f(x0, y0, z1);
        glVertex3f(x0, y1, z1);
        glVertex3f(x0, y1, z0);
        // right
        glVertex3f(x1, y0, z1);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y1, z0);
        glVertex3f(x1, y1, z1);
        // top
        glVertex3f(x0, y1, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x1, y1, z0);
        glVertex3f(x0, y1, z0);
        // bottom
        glVertex3f(x0, y0, z0);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y0, z1);
        glVertex3f(x0, y0, z1);
        glEnd();
    };

    auto drawHeadCubeFaceOnly = [](float cx, float cy, float cz, float size, GLuint tex)
    {
        float hx = size * 0.5f;
        float x0 = cx - hx, x1 = cx + hx;
        float y0 = cy - hx, y1 = cy + hx;
        float z0 = cz - hx, z1 = cz + hx;

        // colored faces
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.9f, 0.9f, 0.9f);
        glBegin(GL_QUADS);
        // back
        glVertex3f(x1, y0, z0);
        glVertex3f(x0, y0, z0);
        glVertex3f(x0, y1, z0);
        glVertex3f(x1, y1, z0);
        // left
        glVertex3f(x0, y0, z0);
        glVertex3f(x0, y0, z1);
        glVertex3f(x0, y1, z1);
        glVertex3f(x0, y1, z0);
        // right
        glVertex3f(x1, y0, z1);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y1, z0);
        glVertex3f(x1, y1, z1);
        // top
        glVertex3f(x0, y1, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x1, y1, z0);
        glVertex3f(x0, y1, z0);
        // bottom
        glVertex3f(x0, y0, z0);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y0, z1);
        glVertex3f(x0, y0, z1);
        glEnd();

        // front face textured (pointing +Z)
        if (tex != 0)
        {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor3f(1.0f, 1.0f, 1.0f);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
            glColor3f(0.8f, 0.8f, 0.8f);
        }
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(x0, y0, z1);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(x1, y0, z1);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(x1, y1, z1);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(x0, y1, z1);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    };

    // Stack of 4 cubes (height s each)
    for (int i = 0; i < 4; ++i)
    {
        float cy = baseY + s * (0.5f + i);
        if (i < 3)
        {
            drawColoredCube(npc.x, cy, npc.z, s, 0.2f, 0.35f, 0.55f); // body color
        }
        else
        {
            drawHeadCubeFaceOnly(npc.x, cy, npc.z, s, npc.texture);
        }
    }

    // Arms on the 3rd block height (index 2, y center at s*(0.5+2))
    float armY = baseY + s * (0.5f + 2);
    float armOffset = s * 0.6f;
    drawColoredCube(npc.x - armOffset, armY, npc.z, s, 0.2f, 0.35f, 0.55f);
    drawColoredCube(npc.x + armOffset, armY, npc.z, s, 0.2f, 0.35f, 0.55f);
}

// HUD helpers for 2D overlay
void beginHud(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void endHud()
{
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void drawQuad(float x, float y, float w, float h, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void drawOutline(float x, float y, float w, float h, float r, float g, float b, float a, float thickness = 2.0f)
{
    drawQuad(x, y, w, thickness, r, g, b, a);
    drawQuad(x, y + h - thickness, w, thickness, r, g, b, a);
    drawQuad(x, y, thickness, h, r, g, b, a);
    drawQuad(x + w - thickness, y, thickness, h, r, g, b, a);
}

// simple seven-segment digits for HUD numbers
void drawDigit(float x, float y, float size, int digit, float r, float g, float b, float a)
{
    // segments: 0 top, 1 top-right, 2 bottom-right, 3 bottom, 4 bottom-left, 5 top-left, 6 middle
    static const int segMap[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1}, {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1}, {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 1, 1}};

    const float w = size;
    const float h = size * 1.6f;
    const float t = size * 0.18f;

    auto seg = [&](float sx, float sy, float sw, float sh)
    { drawQuad(x + sx, y + sy, sw, sh, r, g, b, a); };

    if (segMap[digit][0])
        seg(t, 0, w - 2 * t, t);
    if (segMap[digit][1])
        seg(w - t, t, t, h / 2 - t * 1.1f);
    if (segMap[digit][2])
        seg(w - t, h / 2 + t * 0.1f, t, h / 2 - t * 1.1f);
    if (segMap[digit][3])
        seg(t, h - t, w - 2 * t, t);
    if (segMap[digit][4])
        seg(0, h / 2 + t * 0.1f, t, h / 2 - t * 1.1f);
    if (segMap[digit][5])
        seg(0, t, t, h / 2 - t * 1.1f);
    if (segMap[digit][6])
        seg(t, h / 2 - t * 0.5f, w - 2 * t, t);
}

void drawNumber(float x, float y, int value, float size, float r, float g, float b, float a)
{
    value = std::max(0, value);
    int digits[4];
    int count = 0;
    do
    {
        digits[count++] = value % 10;
        value /= 10;
    } while (value > 0 && count < 4);

    float totalWidth = count * (size + size * 0.35f) - size * 0.35f;
    float startX = x - totalWidth;
    for (int i = count - 1; i >= 0; --i)
    {
        drawDigit(startX, y, size, digits[i], r, g, b, a);
        startX += size + size * 0.35f;
    }
}

void drawDigitBillboard(const Vec3 &pos, float size, int digit, const Vec3 &right, const Vec3 &up, float r, float g,
                        float b, float a)
{
    static const int segMap[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1}, {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1}, {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 1, 1}};

    const float w = size;
    const float h = size * 1.6f;
    const float t = size * 0.18f;

    auto quad = [&](float ox, float oy, float ow, float oh)
    {
        Vec3 p0{pos.x + (ox - w * 0.5f) * right.x + (oy - h * 0.5f) * up.x,
                pos.y + (ox - w * 0.5f) * right.y + (oy - h * 0.5f) * up.y,
                pos.z + (ox - w * 0.5f) * right.z + (oy - h * 0.5f) * up.z};
        Vec3 p1{p0.x + ow * right.x, p0.y + ow * right.y, p0.z + ow * right.z};
        Vec3 p3{p0.x + oh * up.x, p0.y + oh * up.y, p0.z + oh * up.z};
        Vec3 p2{p1.x + oh * up.x, p1.y + oh * up.y, p1.z + oh * up.z};

        glBegin(GL_QUADS);
        glColor4f(r, g, b, a);
        glVertex3f(p0.x, p0.y, p0.z);
        glVertex3f(p1.x, p1.y, p1.z);
        glVertex3f(p2.x, p2.y, p2.z);
        glVertex3f(p3.x, p3.y, p3.z);
        glEnd();
    };

    if (segMap[digit][0])
        quad(t, 0, w - 2 * t, t);
    if (segMap[digit][1])
        quad(w - t, t, t, h / 2 - t * 1.1f);
    if (segMap[digit][2])
        quad(w - t, h / 2 + t * 0.1f, t, h / 2 - t * 1.1f);
    if (segMap[digit][3])
        quad(t, h - t, w - 2 * t, t);
    if (segMap[digit][4])
        quad(0, h / 2 + t * 0.1f, t, h / 2 - t * 1.1f);
    if (segMap[digit][5])
        quad(0, t, t, h / 2 - t * 1.1f);
    if (segMap[digit][6])
        quad(t, h / 2 - t * 0.5f, w - 2 * t, t);
}

// ---------- Texture atlas generation ----------
static const std::map<char, std::array<uint8_t, 5>> FONT5x4 = {
    {'A', {0b0110, 0b1001, 0b1111, 0b1001, 0b1001}},
    {'B', {0b1110, 0b1001, 0b1110, 0b1001, 0b1110}},
    {'C', {0b0111, 0b1000, 0b1000, 0b1000, 0b0111}},
    {'D', {0b1110, 0b1001, 0b1001, 0b1001, 0b1110}},
    {'E', {0b1111, 0b1000, 0b1110, 0b1000, 0b1111}},
    {'G', {0b0111, 0b1000, 0b1011, 0b1001, 0b0111}},
    {'I', {0b1110, 0b0100, 0b0100, 0b0100, 0b1110}},
    {'L', {0b1000, 0b1000, 0b1000, 0b1000, 0b1111}},
    {'N', {0b1001, 0b1101, 0b1011, 0b1001, 0b1001}},
    {'O', {0b0110, 0b1001, 0b1001, 0b1001, 0b0110}},
    {'P', {0b1110, 0b1001, 0b1110, 0b1000, 0b1000}},
    {'R', {0b1110, 0b1001, 0b1110, 0b1010, 0b1001}},
    {'S', {0b0111, 0b1000, 0b0110, 0b0001, 0b1110}},
    {'T', {0b1111, 0b0100, 0b0100, 0b0100, 0b0100}},
    {'U', {0b1001, 0b1001, 0b1001, 0b1001, 0b0110}},
    {'V', {0b1001, 0b1001, 0b1001, 0b0110, 0b0110}},
    {'W', {0b1001, 0b1001, 0b1011, 0b1101, 0b1001}},
    {'Y', {0b1001, 0b1001, 0b0110, 0b0100, 0b0100}},
};

void writePixel(std::vector<uint8_t> &pix, int texW, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int idx = (y * texW + x) * 4;
    pix[idx + 0] = r;
    pix[idx + 1] = g;
    pix[idx + 2] = b;
    pix[idx + 3] = a;
}

float hashNoise(int x, int y, int seed)
{
    uint32_t n = static_cast<uint32_t>(x) * 374761u + static_cast<uint32_t>(y) * 668265263u +
                 static_cast<uint32_t>(seed) * 915488749u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n = n ^ (n >> 16);
    return (n & 0xFFFF) / 65535.0f;
}

void fillTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor, int styleSeed)
{
    int tileX = tileIdx % ATLAS_COLS;
    int tileY = tileIdx / ATLAS_COLS;
    int x0 = tileX * ATLAS_TILE_SIZE;
    int y0 = tileY * ATLAS_TILE_SIZE;
    for (int y = 0; y < ATLAS_TILE_SIZE; ++y)
    {
        for (int x = 0; x < ATLAS_TILE_SIZE; ++x)
        {
            float n = hashNoise(x, y + tileIdx * 17, styleSeed);
            float shade = 0.85f + n * 0.25f;
            float r = std::clamp(baseColor[0] * shade, 0.0f, 1.0f);
            float g = std::clamp(baseColor[1] * shade, 0.0f, 1.0f);
            float b = std::clamp(baseColor[2] * shade, 0.0f, 1.0f);

            // subtle lines for some styles
            if ((styleSeed % 3 == 0) && (y % 8 == 0))
                shade *= 0.92f;
            if ((styleSeed % 4 == 1) && (x % 6 == 0))
                shade *= 0.9f;
            r = std::clamp(baseColor[0] * shade, 0.0f, 1.0f);
            g = std::clamp(baseColor[1] * shade, 0.0f, 1.0f);
            b = std::clamp(baseColor[2] * shade, 0.0f, 1.0f);

            // waves for water
            if (styleSeed == 99)
            {
                float wave = std::sin((x + y * 0.6f) * 0.2f) * 0.04f;
                r = std::clamp(r + wave, 0.0f, 1.0f);
                g = std::clamp(g + wave, 0.0f, 1.0f);
                b = std::clamp(b + wave * 1.6f, 0.0f, 1.0f);
            }

            uint8_t R = static_cast<uint8_t>(r * 255.0f);
            uint8_t G = static_cast<uint8_t>(g * 255.0f);
            uint8_t B = static_cast<uint8_t>(b * 255.0f);
            uint8_t A = 255;
            if (styleSeed == 99)
                A = 180; // eau semi-transparente
            else if (styleSeed == 88)
                A = 120;              // verre transparent
            else if (styleSeed == 17) // LED légère transparence
                A = 220;
            else if (styleSeed == 19) // fil fin
                A = 255;
            writePixel(pix, texW, x0 + x, y0 + y, R, G, B, A);
        }
    }
}

int tinyTextWidthOnTile(const std::string &text, int scale)
{
    if (text.empty())
        return 0;
    int spacing = scale;
    int letters = static_cast<int>(text.size());
    return letters * (4 * scale) + (letters - 1) * spacing;
}

void fillRect(std::vector<uint8_t> &pix, int texW, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
              uint8_t a)
{
    int texH = ATLAS_ROWS * ATLAS_TILE_SIZE;
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(texW, x + w);
    int y1 = std::min(texH, y + h);
    for (int yy = y0; yy < y1; ++yy)
    {
        for (int xx = x0; xx < x1; ++xx)
        {
            writePixel(pix, texW, xx, yy, r, g, b, a);
        }
    }
}

void blitTinyCharToTile(std::vector<uint8_t> &pix, int texW, int tileIdx, int x, int y, char c, int scale, uint8_t r,
                        uint8_t g, uint8_t b, uint8_t a)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = FONT5x4.find(c);
    if (it == FONT5x4.end())
        return;
    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int texH = ATLAS_ROWS * ATLAS_TILE_SIZE;
    const auto &rows = it->second;
    for (int row = 0; row < 5; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            if ((rows[row] & (1 << (3 - col))) == 0)
                continue;
            for (int dy = 0; dy < scale; ++dy)
            {
                for (int dx = 0; dx < scale; ++dx)
                {
                    int px = tileX + x + col * scale + dx;
                    int py = tileY + y + row * scale + dy;
                    if (px < 0 || px >= texW || py < 0 || py >= texH)
                        continue;
                    writePixel(pix, texW, px, py, r, g, b, a);
                }
            }
        }
    }
}

void blitTinyTextToTile(std::vector<uint8_t> &pix, int texW, int tileIdx, int x, int y, const std::string &text,
                        int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int cursor = x;
    int spacing = scale;
    for (char c : text)
    {
        blitTinyCharToTile(pix, texW, tileIdx, cursor, y, c, scale, r, g, b, a);
        cursor += 4 * scale + spacing;
    }
}

void drawGateTopLabels(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                       const std::string &gateLabel)
{
    auto toByte = [](float v, float mul)
    {
        return static_cast<uint8_t>(std::clamp(v * mul, 0.0f, 1.0f) * 255.0f);
    };

    uint8_t accentR = toByte(baseColor[0], 1.25f);
    uint8_t accentG = toByte(baseColor[1], 1.25f);
    uint8_t accentB = toByte(baseColor[2], 1.25f);
    uint8_t bgR = toByte(baseColor[0], 0.55f);
    uint8_t bgG = toByte(baseColor[1], 0.55f);
    uint8_t bgB = toByte(baseColor[2], 0.55f);
    uint8_t textR = 245;
    uint8_t textG = 245;
    uint8_t textB = 240;

    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int centerX = ATLAS_TILE_SIZE / 2;
    int midY = ATLAS_TILE_SIZE / 2 + 2;

    // simple routing sketch: inputs on X edges, output toward +Z (texture bottom)
    fillRect(pix, texW, tileX + centerX - 2, tileY, 4, midY - 1, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + 8, tileY + midY - 1, ATLAS_TILE_SIZE - 16, 2, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + 2, tileY + midY - 2, 5, 4, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - 7, tileY + midY - 2, 5, 4, accentR, accentG, accentB, 255);

    // small arrow head on the output side
    for (int i = 0; i < 3; ++i)
    {
        int width = 5 - i * 2;
        int startX = centerX - width / 2;
        fillRect(pix, texW, tileX + startX, tileY + i, width, 1, textR, textG, textB, 255);
    }

    int labelScale = 1;
    int labelHeight = 5 * labelScale;
    int inWidth = tinyTextWidthOnTile("IN", labelScale);
    int outWidth = tinyTextWidthOnTile("OUT", labelScale);
    int gateWidth = tinyTextWidthOnTile(gateLabel, labelScale);

    int inY = ATLAS_TILE_SIZE - labelHeight - 3;
    int outY = 4;
    int gateY = midY + 4;

    fillRect(pix, texW, tileX + 1, tileY + inY - 1, inWidth + 4, labelHeight + 2, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - inWidth - 5, tileY + inY - 1, inWidth + 4, labelHeight + 2, bgR,
             bgG, bgB, 255);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - outWidth) / 2 - 2, tileY + outY - 1, outWidth + 4,
             labelHeight + 2, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - gateWidth) / 2 - 2, tileY + gateY - 1, gateWidth + 4,
             labelHeight + 2, bgR, bgG, bgB, 255);

    blitTinyTextToTile(pix, texW, tileIdx, 2, inY, "IN", labelScale, textR, textG, textB, 255);
    int rightX = ATLAS_TILE_SIZE - inWidth - 2;
    blitTinyTextToTile(pix, texW, tileIdx, rightX, inY, "IN", labelScale, textR, textG, textB, 255);
    int outX = (ATLAS_TILE_SIZE - outWidth) / 2;
    blitTinyTextToTile(pix, texW, tileIdx, outX, outY, "OUT", labelScale, textR, textG, textB, 255);
    int gateX = (ATLAS_TILE_SIZE - gateWidth) / 2;
    blitTinyTextToTile(pix, texW, tileIdx, gateX, gateY, gateLabel, labelScale, textR, textG, textB, 255);
}

void fillGateTileWithLabels(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                            int styleSeed, const std::string &gateLabel)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    drawGateTopLabels(pix, texW, tileIdx, baseColor, gateLabel);
}

GLuint loadTextureFromBMP(const std::string &path)
{
    SDL_Surface *surf = SDL_LoadBMP(path.c_str());
    if (!surf)
    {
        std::cerr << "Failed to load BMP \"" << path << "\": " << SDL_GetError() << "\n";
        return 0;
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!rgba)
    {
        std::cerr << "Failed to convert BMP \"" << path << "\" to RGBA: " << SDL_GetError() << "\n";
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_FreeSurface(rgba);
    return tex;
}

void createAtlasTexture()
{
    gBlockTile = {{BlockType::Grass, 0}, {BlockType::Dirt, 1}, {BlockType::Stone, 2}, {BlockType::Wood, 3}, {BlockType::Leaves, 4}, {BlockType::Water, 5}, {BlockType::Plank, 6}, {BlockType::Sand, 7}, {BlockType::Air, 8}, {BlockType::Glass, 9}, {BlockType::AndGate, 10}, {BlockType::OrGate, 11}, {BlockType::Led, 12}, {BlockType::Button, 13}, {BlockType::Wire, 14}};

    int nextTile = static_cast<int>(gBlockTile.size());
    gAndTopTile = nextTile++;
    gOrTopTile = nextTile++;

    int texW = ATLAS_COLS * ATLAS_TILE_SIZE;
    int texH = ATLAS_ROWS * ATLAS_TILE_SIZE;
    int atlasCapacity = ATLAS_COLS * ATLAS_ROWS;
    int maxTileIdx = 0;
    for (const auto &kv : gBlockTile)
        maxTileIdx = std::max(maxTileIdx, kv.second);
    maxTileIdx = std::max(maxTileIdx, std::max(gAndTopTile, gOrTopTile));
    if (maxTileIdx >= atlasCapacity)
    {
        std::cerr << "Atlas capacity too small for gate labels.\n";
        return;
    }
    std::vector<uint8_t> pixels(texW * texH * 4, 0);

    auto base = [&](BlockType b)
    { return BLOCKS.at(b).color; };

    fillTile(pixels, texW, gBlockTile[BlockType::Grass], {0.2f, 0.8f, 0.25f}, 3);
    fillTile(pixels, texW, gBlockTile[BlockType::Dirt], base(BlockType::Dirt), 4);
    fillTile(pixels, texW, gBlockTile[BlockType::Stone], base(BlockType::Stone), 7);
    fillTile(pixels, texW, gBlockTile[BlockType::Wood], base(BlockType::Wood), 1);
    fillTile(pixels, texW, gBlockTile[BlockType::Leaves], base(BlockType::Leaves), 5);
    fillTile(pixels, texW, gBlockTile[BlockType::Water], base(BlockType::Water), 99);
    fillTile(pixels, texW, gBlockTile[BlockType::Plank], base(BlockType::Plank), 0);
    fillTile(pixels, texW, gBlockTile[BlockType::Sand], base(BlockType::Sand), 2);
    fillTile(pixels, texW, gBlockTile[BlockType::Air], {0.7f, 0.85f, 1.0f}, 6);
    fillTile(pixels, texW, gBlockTile[BlockType::Glass], {0.85f, 0.9f, 0.95f}, 88);
    fillTile(pixels, texW, gBlockTile[BlockType::AndGate], base(BlockType::AndGate), 15);
    fillTile(pixels, texW, gBlockTile[BlockType::OrGate], base(BlockType::OrGate), 16);
    fillTile(pixels, texW, gBlockTile[BlockType::Led], base(BlockType::Led), 17);
    fillTile(pixels, texW, gBlockTile[BlockType::Button], base(BlockType::Button), 18);
    fillTile(pixels, texW, gBlockTile[BlockType::Wire], base(BlockType::Wire), 19);
    fillGateTileWithLabels(pixels, texW, gAndTopTile, base(BlockType::AndGate), 15, "AND");
    fillGateTileWithLabels(pixels, texW, gOrTopTile, base(BlockType::OrGate), 16, "OR");

    if (gAtlasTex == 0)
    {
        glGenTextures(1, &gAtlasTex);
    }
    glBindTexture(GL_TEXTURE_2D, gAtlasTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void drawInventoryBar(int winW, int winH, const std::vector<ItemStack> &hotbar, int selected)
{
    const int slotCount = static_cast<int>(HOTBAR.size());
    const float slotSize = 54.0f;
    const float gap = 10.0f;
    const float padding = 16.0f;
    const float barWidth = padding * 2 + gap * (slotCount + 1) + slotSize * slotCount;
    const float barHeight = slotSize + padding * 2;
    const float barX = (winW - barWidth) * 0.5f;
    const float barY = winH - barHeight - 20.0f;

    // glassy background
    drawQuad(barX, barY, barWidth, barHeight, 0.05f, 0.05f, 0.08f, 0.65f);
    drawOutline(barX, barY, barWidth, barHeight, 1.0f, 1.0f, 1.0f, 0.08f, 3.0f);
    drawOutline(barX + 4.0f, barY + 4.0f, barWidth - 8.0f, barHeight - 8.0f, 0.0f, 0.0f, 0.0f, 0.35f, 2.0f);

    for (int i = 0; i < slotCount; ++i)
    {
        float x = barX + padding + gap * (i + 1) + slotSize * i;
        float y = barY + padding;
        BlockType b = hotbar[i].type;
        if (!BLOCKS.count(b))
            b = BlockType::Air;
        auto col = BLOCKS.at(b).color;

        // slot shell
        drawQuad(x - 4.0f, y - 4.0f, slotSize + 8.0f, slotSize + 8.0f, 0.0f, 0.0f, 0.0f, 0.25f);
        drawOutline(x - 4.0f, y - 4.0f, slotSize + 8.0f, slotSize + 8.0f, 1.0f, 1.0f, 1.0f, 0.08f, 2.0f);

        // block color tile
        float alpha = hotbar[i].count > 0 ? 0.95f : 0.35f;
        drawQuad(x, y, slotSize, slotSize, col[0], col[1], col[2], alpha);
        drawOutline(x, y, slotSize, slotSize, 0.0f, 0.0f, 0.0f, 0.45f, 2.0f);

        // stack count
        int count = hotbar[i].count;
        drawNumber(x + slotSize - 6.0f, y + slotSize - 24.0f, count, 10.0f, 1.0f, 0.98f, 0.9f, 0.95f);

        // selection glow
        if (i == selected)
        {
            drawOutline(x - 6.0f, y - 6.0f, slotSize + 12.0f, slotSize + 12.0f, 1.0f, 0.9f, 0.2f, 0.9f, 3.5f);
            drawQuad(x - 2.0f, y - 2.0f, slotSize + 4.0f, slotSize + 4.0f, 1.0f, 0.85f, 0.35f, 0.12f);
        }
    }
}

void drawCrosshair(int winW, int winH)
{
    float cx = winW * 0.5f;
    float cy = winH * 0.5f;
    const float len = 8.0f;
    const float thick = 2.0f;
    drawQuad(cx - len, cy - thick * 0.5f, len * 2.0f, thick, 1.0f, 1.0f, 1.0f, 0.9f);
    drawQuad(cx - thick * 0.5f, cy - len, thick, len * 2.0f, 1.0f, 1.0f, 1.0f, 0.9f);
}

PauseMenuLayout computePauseLayout(int winW, int winH)
{
    PauseMenuLayout l;
    l.panelW = 360.0f;
    l.panelH = 220.0f;
    l.panelX = (winW - l.panelW) * 0.5f;
    l.panelY = (winH - l.panelH) * 0.5f;

    l.resumeW = l.panelW - 80.0f;
    l.resumeH = 50.0f;
    l.resumeX = l.panelX + (l.panelW - l.resumeW) * 0.5f;
    l.resumeY = l.panelY + 60.0f;

    l.quitW = l.resumeW;
    l.quitH = 50.0f;
    l.quitX = l.resumeX;
    l.quitY = l.resumeY + 70.0f;
    return l;
}

void drawPauseMenu(int winW, int winH, const PauseMenuLayout &l, bool hoverResume, bool hoverQuit)
{
    drawQuad(0.0f, 0.0f, static_cast<float>(winW), static_cast<float>(winH), 0.0f, 0.0f, 0.0f, 0.55f);
    drawQuad(l.panelX, l.panelY, l.panelW, l.panelH, 0.05f, 0.05f, 0.08f, 0.92f);
    drawOutline(l.panelX, l.panelY, l.panelW, l.panelH, 1.0f, 1.0f, 1.0f, 0.08f, 3.0f);

    auto drawButton = [](float x, float y, float w, float h, float r, float g, float b, bool hover)
    {
        float a = hover ? 0.95f : 0.8f;
        drawQuad(x, y, w, h, r, g, b, a);
        drawOutline(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.45f, 3.0f);
    };

    drawButton(l.resumeX, l.resumeY, l.resumeW, l.resumeH, 0.16f, 0.55f, 0.25f, hoverResume);
    drawButton(l.quitX, l.quitY, l.quitW, l.quitH, 0.65f, 0.18f, 0.12f, hoverQuit);

    // resume icon (triangle play)
    glColor4f(1.0f, 1.0f, 1.0f, hoverResume ? 0.95f : 0.85f);
    glBegin(GL_TRIANGLES);
    float rx0 = l.resumeX + l.resumeW * 0.36f;
    float ry0 = l.resumeY + l.resumeH * 0.22f;
    float rx1 = l.resumeX + l.resumeW * 0.36f;
    float ry1 = l.resumeY + l.resumeH * 0.78f;
    float rx2 = l.resumeX + l.resumeW * 0.74f;
    float ry2 = l.resumeY + l.resumeH * 0.5f;
    glVertex2f(rx0, ry0);
    glVertex2f(rx1, ry1);
    glVertex2f(rx2, ry2);
    glEnd();

    // quit icon (X)
    glLineWidth(4.0f);
    glColor4f(1.0f, 1.0f, 1.0f, hoverQuit ? 0.95f : 0.85f);
    float qx0 = l.quitX + l.quitW * 0.3f;
    float qy0 = l.quitY + l.quitH * 0.3f;
    float qx1 = l.quitX + l.quitW * 0.7f;
    float qy1 = l.quitY + l.quitH * 0.7f;
    glBegin(GL_LINES);
    glVertex2f(qx0, qy0);
    glVertex2f(qx1, qy1);
    glVertex2f(qx0, qy1);
    glVertex2f(qx1, qy0);
    glEnd();
    glLineWidth(1.0f);
}

bool pointInRect(float mx, float my, float x, float y, float w, float h)
{
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

void drawCharTiny(float x, float y, float size, char c, float r, float g, float b, float a)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = FONT5x4.find(c);
    if (it == FONT5x4.end())
        return;
    glColor4f(r, g, b, a);
    const auto &rows = it->second;
    for (int row = 0; row < 5; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            if (rows[row] & (1 << (3 - col)))
            {
                drawQuad(x + col * size, y + row * size, size, size, r, g, b, a);
            }
        }
    }
}

void drawTextTiny(float x, float y, float size, const std::string &text, float r, float g, float b, float a)
{
    float cursor = x;
    const float spacing = size * 0.8f;
    for (char c : text)
    {
        if (c == ' ')
        {
            cursor += 4 * size + spacing;
            continue;
        }
        drawCharTiny(cursor, y, size, c, r, g, b, a);
        cursor += 4 * size + spacing;
    }
}

void drawTooltip(float mx, float my, int winW, int winH, const std::string &text)
{
    if (text.empty())
        return;
    float size = 9.0f;
    float padding = 6.0f;
    float width = static_cast<float>(text.size()) * (4 * size + size * 0.8f) - size * 0.8f + padding * 2;
    float height = 5 * size + padding * 2;
    float tx = mx + 18.0f;
    float ty = my - height - 12.0f;
    tx = std::clamp(tx, 4.0f, static_cast<float>(winW) - width - 4.0f);
    ty = std::clamp(ty, 4.0f, static_cast<float>(winH) - height - 4.0f);
    drawQuad(tx, ty, width, height, 0.05f, 0.05f, 0.08f, 0.9f);
    drawOutline(tx, ty, width, height, 1.0f, 1.0f, 1.0f, 0.12f, 2.0f);
    drawTextTiny(tx + padding, ty + padding, size, text, 1.0f, 0.95f, 0.85f, 1.0f);
}

void drawButtonStateLabels(const World &world, const Player &player, float radius)
{
    Vec3 fwd = forwardVec(player.yaw, player.pitch);
    Vec3 right = normalizeVec(Vec3{fwd.z, 0.0f, -fwd.x});
    if (std::abs(right.x) < 1e-4f && std::abs(right.z) < 1e-4f)
        right = {1.0f, 0.0f, 0.0f};
    Vec3 up = normalizeVec(cross(right, fwd));
    float size = 0.22f;

    int minX = std::max(0, static_cast<int>(std::floor(player.x - radius)));
    int maxX = std::min(world.getWidth() - 1, static_cast<int>(std::ceil(player.x + radius)));
    int minY = std::max(0, static_cast<int>(std::floor(player.y - radius)));
    int maxY = std::min(world.getHeight() - 1, static_cast<int>(std::ceil(player.y + radius)));
    int minZ = std::max(0, static_cast<int>(std::floor(player.z - radius)));
    int maxZ = std::min(world.getDepth() - 1, static_cast<int>(std::ceil(player.z + radius)));

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    for (int y = minY; y <= maxY; ++y)
    {
        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                if (world.get(x, y, z) != BlockType::Button)
                    continue;
                Vec3 pos{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 1.2f, static_cast<float>(z) + 0.5f};
                pos.x += up.x * 0.02f;
                pos.y += up.y * 0.02f;
                pos.z += up.z * 0.02f;
                int state = world.getButtonState(x, y, z) ? 1 : 0;
                float alpha = 0.95f;
                drawDigitBillboard(pos, size, state, right, up, 1.0f, 0.95f, 0.2f, alpha);
            }
        }
    }
}
void drawSlotBox(float x, float y, float slotSize, const ItemStack &slot, bool selected, bool hovered)
{
    auto col = BLOCKS.at(slot.count > 0 ? slot.type : BlockType::Air).color;
    float alpha = slot.count > 0 ? 0.95f : 0.28f;
    drawQuad(x - 4.0f, y - 4.0f, slotSize + 8.0f, slotSize + 8.0f, 0.0f, 0.0f, 0.0f, 0.25f);
    drawOutline(x - 4.0f, y - 4.0f, slotSize + 8.0f, slotSize + 8.0f, 1.0f, 1.0f, 1.0f, 0.08f, 2.0f);
    drawQuad(x, y, slotSize, slotSize, col[0], col[1], col[2], alpha);
    drawOutline(x, y, slotSize, slotSize, 0.0f, 0.0f, 0.0f, 0.45f, 2.0f);
    if (slot.count > 0)
        drawNumber(x + slotSize - 6.0f, y + slotSize - 24.0f, slot.count, 10.0f, 1.0f, 0.98f, 0.9f, 0.95f);
    if (selected)
        drawOutline(x - 6.0f, y - 6.0f, slotSize + 12.0f, slotSize + 12.0f, 1.0f, 0.9f, 0.2f, 0.9f, 3.5f);
    else if (hovered)
        drawOutline(x - 6.0f, y - 6.0f, slotSize + 12.0f, slotSize + 12.0f, 1.0f, 1.0f, 1.0f, 0.35f, 2.0f);
}

struct SlotHit
{
    bool valid = false;
    bool isHotbar = false;
    int idx = -1;
};

SlotHit hitTestInventoryUI(int mx, int my, int winW, int winH)
{
    const float slotSize = 54.0f;
    const float gap = 10.0f;
    const float padding = 16.0f;

    float invWidth = padding * 2 + INV_COLS * slotSize + (INV_COLS + 1) * gap;
    float invHeight = padding * 2 + INV_ROWS * slotSize + (INV_ROWS + 1) * gap;
    float invX = (winW - invWidth) * 0.5f;
    float invY = (winH - invHeight) * 0.5f - 40.0f;

    for (int row = 0; row < INV_ROWS; ++row)
    {
        for (int col = 0; col < INV_COLS; ++col)
        {
            float x = invX + padding + gap * (col + 1) + slotSize * col;
            float y = invY + padding + gap * (row + 1) + slotSize * row;
            if (pointInRect(static_cast<float>(mx), static_cast<float>(my), x, y, slotSize, slotSize))
                return {true, false, row * INV_COLS + col};
        }
    }

    float hbWidth = padding * 2 + static_cast<float>(HOTBAR.size()) * slotSize + (HOTBAR.size() + 1) * gap;
    float hbX = (winW - hbWidth) * 0.5f;
    float hbY = invY + invHeight + 40.0f;
    for (int i = 0; i < static_cast<int>(HOTBAR.size()); ++i)
    {
        float x = hbX + padding + gap * (i + 1) + slotSize * i;
        float y = hbY + padding;
        if (pointInRect(static_cast<float>(mx), static_cast<float>(my), x, y, slotSize, slotSize))
            return {true, true, i};
    }
    return {};
}

void drawInventoryPanel(int winW, int winH, const std::vector<ItemStack> &inventory, const std::vector<ItemStack> &hotbar,
                        int pendingSlot, bool pendingIsHotbar, int mouseX, int mouseY, HoverLabel &hoverLabel)
{
    const float slotSize = 54.0f;
    const float gap = 10.0f;
    const float padding = 16.0f;

    float invWidth = padding * 2 + INV_COLS * slotSize + (INV_COLS + 1) * gap;
    float invHeight = padding * 2 + INV_ROWS * slotSize + (INV_ROWS + 1) * gap;
    float invX = (winW - invWidth) * 0.5f;
    float invY = (winH - invHeight) * 0.5f - 40.0f;

    drawQuad(invX - 18.0f, invY - 22.0f, invWidth + 36.0f, invHeight + 130.0f, 0.03f, 0.03f, 0.06f, 0.85f);
    drawOutline(invX - 18.0f, invY - 22.0f, invWidth + 36.0f, invHeight + 130.0f, 1.0f, 1.0f, 1.0f, 0.08f, 3.0f);

    for (int row = 0; row < INV_ROWS; ++row)
    {
        for (int col = 0; col < INV_COLS; ++col)
        {
            float x = invX + padding + gap * (col + 1) + slotSize * col;
            float y = invY + padding + gap * (row + 1) + slotSize * row;
            int idx = row * INV_COLS + col;
            bool hovered = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), x, y, slotSize, slotSize);
            bool selected = (!pendingIsHotbar && pendingSlot == idx);
            drawSlotBox(x, y, slotSize, inventory[idx], selected, hovered);
            if (hovered && inventory[idx].count > 0 && BLOCKS.count(inventory[idx].type))
            {
                hoverLabel.valid = true;
                hoverLabel.text = BLOCKS.at(inventory[idx].type).name;
                hoverLabel.x = static_cast<float>(mouseX);
                hoverLabel.y = static_cast<float>(mouseY);
            }
        }
    }

    float hbWidth = padding * 2 + static_cast<float>(HOTBAR.size()) * slotSize + (HOTBAR.size() + 1) * gap;
    float hbX = (winW - hbWidth) * 0.5f;
    float hbY = invY + invHeight + 40.0f;
    drawQuad(hbX - 10.0f, hbY - 10.0f, hbWidth + 20.0f, slotSize + padding * 2 + 20.0f, 0.05f, 0.05f, 0.08f, 0.75f);
    drawOutline(hbX - 10.0f, hbY - 10.0f, hbWidth + 20.0f, slotSize + padding * 2 + 20.0f, 1.0f, 1.0f, 1.0f, 0.06f, 2.0f);

    for (int i = 0; i < static_cast<int>(HOTBAR.size()); ++i)
    {
        float x = hbX + padding + gap * (i + 1) + slotSize * i;
        float y = hbY + padding;
        bool hovered = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), x, y, slotSize, slotSize);
        bool selected = (pendingIsHotbar && pendingSlot == i);
        drawSlotBox(x, y, slotSize, hotbar[i], selected, hovered);
        if (hovered && hotbar[i].count > 0 && BLOCKS.count(hotbar[i].type))
        {
            hoverLabel.valid = true;
            hoverLabel.text = BLOCKS.at(hotbar[i].type).name;
            hoverLabel.x = static_cast<float>(mouseX);
            hoverLabel.y = static_cast<float>(mouseY);
        }
    }
}

void setup3D(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, static_cast<double>(w) / static_cast<double>(h), 0.1, 500.0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
}

int chunkIndex(int cx, int cy, int cz)
{
    if (cx < 0 || cy < 0 || cz < 0 || cx >= CHUNK_X_COUNT || cy >= CHUNK_Y_COUNT || cz >= CHUNK_Z_COUNT)
        return -1;
    return cx + CHUNK_X_COUNT * (cz + CHUNK_Z_COUNT * cy);
}

void markAllChunksDirty()
{
    for (auto &c : chunkMeshes)
        c.dirty = true;
}

void markChunkFromBlock(int x, int y, int z)
{
    int cx = x / CHUNK_SIZE;
    int cy = y / CHUNK_SIZE;
    int cz = z / CHUNK_SIZE;
    int idx = chunkIndex(cx, cy, cz);
    if (idx >= 0)
        chunkMeshes[idx].dirty = true;
}

void markNeighborsDirty(int x, int y, int z)
{
    static const int offs[7][3] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto &o : offs)
    {
        markChunkFromBlock(x + o[0], y + o[1], z + o[2]);
    }
}

void ensureVbo(ChunkMesh &m)
{
    if (m.vbo == 0)
    {
        glGenBuffers(1, &m.vbo);
    }
}

int tileIndexFor(BlockType b)
{
    auto it = gBlockTile.find(b);
    if (it != gBlockTile.end())
        return it->second;
    return gBlockTile[BlockType::Air];
}

int addToSlots(std::vector<ItemStack> &slots, BlockType b, int amount)
{
    for (auto &s : slots)
    {
        if (s.count > 0 && s.type == b && s.count < MAX_STACK)
        {
            int add = std::min(amount, MAX_STACK - s.count);
            s.count += add;
            amount -= add;
            if (amount <= 0)
                return 0;
        }
    }
    for (auto &s : slots)
    {
        if (s.count == 0)
        {
            s.type = b;
            int add = std::min(amount, MAX_STACK);
            s.count = add;
            amount -= add;
            if (amount <= 0)
                return 0;
        }
    }
    return amount;
}

int addToInventory(BlockType b, int amount, std::vector<ItemStack> &hotbarSlots, std::vector<ItemStack> &inventorySlots)
{
    amount = addToSlots(hotbarSlots, b, amount);
    amount = addToSlots(inventorySlots, b, amount);
    return amount;
}

void buildChunkMesh(const World &world, int cx, int cy, int cz)
{
    int idx = chunkIndex(cx, cy, cz);
    if (idx < 0)
        return;
    ChunkMesh &mesh = chunkMeshes[idx];
    mesh.verts.clear();
    int x0 = cx * CHUNK_SIZE;
    int y0 = cy * CHUNK_SIZE;
    int z0 = cz * CHUNK_SIZE;
    int x1 = std::min(world.getWidth(), x0 + CHUNK_SIZE);
    int y1 = std::min(world.getHeight(), y0 + CHUNK_SIZE);
    int z1 = std::min(world.getDepth(), z0 + CHUNK_SIZE);

    auto addFace = [&](int x, int y, int z, const int nx, const int ny, const int nz, const std::array<float, 3> &col,
                       int tile)
    {
        float bx = static_cast<float>(x);
        float by = static_cast<float>(y);
        float bz = static_cast<float>(z);
        float br = col[0];
        float bg = col[1];
        float bb = col[2];

        float du = 1.0f / ATLAS_COLS;
        float dv = 1.0f / ATLAS_ROWS;
        int tx = tile % ATLAS_COLS;
        int ty = tile / ATLAS_COLS;
        const float pad = 0.0015f; // avoid bleeding
        float u0 = tx * du + pad;
        float v0 = ty * dv + pad;
        float u1 = (tx + 1) * du - pad;
        float v1 = (ty + 1) * dv - pad;

        auto push = [&](float px, float py, float pz, float u, float v)
        { mesh.verts.push_back(Vertex{px, py, pz, u, v, br, bg, bb}); };

        if (nx == 1)
        {
            push(bx + 1, by, bz, u1, v1);
            push(bx + 1, by + 1, bz, u1, v0);
            push(bx + 1, by + 1, bz + 1, u0, v0);
            push(bx + 1, by, bz + 1, u0, v1);
        }
        else if (nx == -1)
        {
            push(bx, by, bz, u1, v1);
            push(bx, by, bz + 1, u0, v1);
            push(bx, by + 1, bz + 1, u0, v0);
            push(bx, by + 1, bz, u1, v0);
        }
        else if (ny == 1)
        {
            push(bx, by + 1, bz, u1, v1);
            push(bx + 1, by + 1, bz, u0, v1);
            push(bx + 1, by + 1, bz + 1, u0, v0);
            push(bx, by + 1, bz + 1, u1, v0);
        }
        else if (ny == -1)
        {
            push(bx, by, bz, u1, v1);
            push(bx + 1, by, bz, u0, v1);
            push(bx + 1, by, bz + 1, u0, v0);
            push(bx, by, bz + 1, u1, v0);
        }
        else if (nz == 1)
        {
            push(bx, by, bz + 1, u1, v1);
            push(bx + 1, by, bz + 1, u0, v1);
            push(bx + 1, by + 1, bz + 1, u0, v0);
            push(bx, by + 1, bz + 1, u1, v0);
        }
        else if (nz == -1)
        {
            push(bx, by, bz, u1, v1);
            push(bx, by + 1, bz, u1, v0);
            push(bx + 1, by + 1, bz, u0, v0);
            push(bx + 1, by, bz, u0, v1);
        }
    };
    auto addBox = [&](float minX, float minY, float minZ, float maxX, float maxY, float maxZ, const std::array<float, 3> &col,
                      int tile)
    {
        float br = col[0];
        float bg = col[1];
        float bb = col[2];
        float du = 1.0f / ATLAS_COLS;
        float dv = 1.0f / ATLAS_ROWS;
        int tx = tile % ATLAS_COLS;
        int ty = tile / ATLAS_COLS;
        const float pad = 0.0015f;
        float u0 = tx * du + pad;
        float v0 = ty * dv + pad;
        float u1 = (tx + 1) * du - pad;
        float v1 = (ty + 1) * dv - pad;

        auto push = [&](float px, float py, float pz, float u, float v)
        { mesh.verts.push_back(Vertex{px, py, pz, u, v, br, bg, bb}); };

        // +X
        push(maxX, minY, minZ, u1, v1);
        push(maxX, maxY, minZ, u1, v0);
        push(maxX, maxY, maxZ, u0, v0);
        push(maxX, minY, maxZ, u0, v1);
        // -X
        push(minX, minY, minZ, u1, v1);
        push(minX, minY, maxZ, u0, v1);
        push(minX, maxY, maxZ, u0, v0);
        push(minX, maxY, minZ, u1, v0);
        // +Y
        push(minX, maxY, minZ, u1, v1);
        push(maxX, maxY, minZ, u0, v1);
        push(maxX, maxY, maxZ, u0, v0);
        push(minX, maxY, maxZ, u1, v0);
        // -Y
        push(minX, minY, minZ, u1, v1);
        push(maxX, minY, minZ, u0, v1);
        push(maxX, minY, maxZ, u0, v0);
        push(minX, minY, maxZ, u1, v0);
        // +Z
        push(minX, minY, maxZ, u1, v1);
        push(maxX, minY, maxZ, u0, v1);
        push(maxX, maxY, maxZ, u0, v0);
        push(minX, maxY, maxZ, u1, v0);
        // -Z
        push(minX, minY, minZ, u1, v1);
        push(minX, maxY, minZ, u1, v0);
        push(maxX, maxY, minZ, u0, v0);
        push(maxX, minY, minZ, u0, v1);
    };

    for (int y = y0; y < y1; ++y)
    {
        for (int z = z0; z < z1; ++z)
        {
            for (int x = x0; x < x1; ++x)
            {
                BlockType b = world.get(x, y, z);
                if (b == BlockType::Air)
                    continue;
                auto color = BLOCKS.at(b).color;
                float brightness = 0.9f - (y / float(world.getHeight())) * 0.3f;
                color[0] *= brightness;
                color[1] *= brightness;
                color[2] *= brightness;
                if (b == BlockType::Led && world.getPower(x, y, z))
                {
                    color[0] = std::min(color[0] * 1.6f, 1.0f);
                    color[1] = std::min(color[1] * 1.4f, 1.0f);
                    color[2] = std::min(color[2] * 1.1f, 1.0f);
                }
                else if (b == BlockType::Led)
                {
                    const float desat = 0.12f;
                    color[0] = std::min(color[0] * 0.25f + desat, 1.0f);
                    color[1] = std::min(color[1] * 0.25f + desat, 1.0f);
                    color[2] = std::min(color[2] * 0.25f + desat, 1.0f);
                }
                else if (b == BlockType::Wire && world.getPower(x, y, z))
                {
                    color[0] = std::min(color[0] + 0.3f, 1.0f);
                    color[1] = std::min(color[1] + 0.05f, 1.0f);
                    color[2] = std::min(color[2] + 0.05f, 1.0f);
                }
                int tIdx = tileIndexFor(b);
                auto faceTile = [&](int nx, int ny, int nz)
                {
                    (void)nx;
                    (void)nz;
                    if (ny == 1)
                    {
                        if (b == BlockType::AndGate)
                            return gAndTopTile;
                        if (b == BlockType::OrGate)
                            return gOrTopTile;
                    }
                    return tIdx;
                };
                if (b == BlockType::Wire)
                {
                    auto connects = [&](int dx, int dy, int dz)
                    {
                        int xx = x + dx;
                        int yy = y + dy;
                        int zz = z + dz;
                        if (!world.inside(xx, yy, zz))
                            return false;
                        BlockType nb = world.get(xx, yy, zz);
                        return nb == BlockType::Wire || nb == BlockType::Button || nb == BlockType::Led ||
                               nb == BlockType::AndGate || nb == BlockType::OrGate;
                    };

                    float cx = static_cast<float>(x) + 0.5f;
                    float cy = static_cast<float>(y) + 0.5f;
                    float cz = static_cast<float>(z) + 0.5f;
                    const float half = 0.12f;
                    const float margin = 0.04f;

                    addBox(cx - half, cy - half, cz - half, cx + half, cy + half, cz + half, color, tIdx);
                    if (connects(1, 0, 0))
                        addBox(cx, cy - half, cz - half, static_cast<float>(x + 1) - margin, cy + half, cz + half, color,
                               tIdx);
                    if (connects(-1, 0, 0))
                        addBox(static_cast<float>(x) + margin, cy - half, cz - half, cx, cy + half, cz + half, color, tIdx);
                    if (connects(0, 1, 0))
                        addBox(cx - half, cy, cz - half, cx + half, static_cast<float>(y + 1) - margin, cz + half, color,
                               tIdx);
                    if (connects(0, -1, 0))
                        addBox(cx - half, static_cast<float>(y) + margin, cz - half, cx + half, cy, cz + half, color, tIdx);
                    if (connects(0, 0, 1))
                        addBox(cx - half, cy - half, cz, cx + half, cy + half, static_cast<float>(z + 1) - margin, color,
                               tIdx);
                    if (connects(0, 0, -1))
                        addBox(cx - half, cy - half, static_cast<float>(z) + margin, cx + half, cy + half, cz, color, tIdx);
                    continue;
                }

                if (x == 0 || !occludesFaces(world.get(x - 1, y, z)))
                    addFace(x, y, z, -1, 0, 0, color, faceTile(-1, 0, 0));
                if (x == world.getWidth() - 1 || !occludesFaces(world.get(x + 1, y, z)))
                    addFace(x, y, z, 1, 0, 0, color, faceTile(1, 0, 0));
                if (y == 0 || !occludesFaces(world.get(x, y - 1, z)))
                    addFace(x, y, z, 0, -1, 0, color, faceTile(0, -1, 0));
                if (y == world.getHeight() - 1 || !occludesFaces(world.get(x, y + 1, z)))
                    addFace(x, y, z, 0, 1, 0, color, faceTile(0, 1, 0));
                if (z == 0 || !occludesFaces(world.get(x, y, z - 1)))
                    addFace(x, y, z, 0, 0, -1, color, faceTile(0, 0, -1));
                if (z == world.getDepth() - 1 || !occludesFaces(world.get(x, y, z + 1)))
                    addFace(x, y, z, 0, 0, 1, color, faceTile(0, 0, 1));
            }
        }
    }

    ensureVbo(mesh);
    if (!mesh.verts.empty())
    {
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.verts.size() * sizeof(Vertex), mesh.verts.data(), GL_STATIC_DRAW);
    }
    mesh.dirty = false;
}
void updateTitle(SDL_Window *window)
{
    std::string title = "Messercraft";
    SDL_SetWindowTitle(window, title.c_str());
}

int main()
{
    const int WIDTH = 96;
    const int HEIGHT = 48;
    const int DEPTH = 96;
    const float PLAYER_HEIGHT = 1.7f;
    const float EYE_HEIGHT = PLAYER_HEIGHT * 0.8f;
    const float SPEED = 32.0f;
    const float JUMP = 8.0f;
    const float GRAVITY = -48.0f;
    const float SPRINT_MULT = 1.6f;
    const float SPRINT_DOUBLE_TAP = 0.3f;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL init error: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("MiniCraft 3D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::cerr << "SDL window error: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx)
    {
        std::cerr << "OpenGL context error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "GLEW init error\n";
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    createAtlasTexture();
    GLuint npcTexture = loadTextureFromBMP("images/npc_head.bmp");
    if (npcTexture == 0)
    {
        std::cerr << "Could not load NPC texture (images/npc_head.bmp). Using flat color.\n";
    }

    World world(WIDTH, HEIGHT, DEPTH);
    CHUNK_X_COUNT = (WIDTH + CHUNK_SIZE - 1) / CHUNK_SIZE;
    CHUNK_Y_COUNT = (HEIGHT + CHUNK_SIZE - 1) / CHUNK_SIZE;
    CHUNK_Z_COUNT = (DEPTH + CHUNK_SIZE - 1) / CHUNK_SIZE;
    chunkMeshes.assign(CHUNK_X_COUNT * CHUNK_Y_COUNT * CHUNK_Z_COUNT, {});
    unsigned seed = static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count());
    world.generate(seed);
    markAllChunksDirty();

    Player player;
    player.x = WIDTH / 2.0f;
    player.z = DEPTH / 2.0f;
    player.y = world.surfaceY(static_cast<int>(player.x), static_cast<int>(player.z)) + 0.2f;
    NPC npc;
    npc.texture = npcTexture;
    npc.width = 0.95f;
    npc.height = 1.9f;
    int npcGridX = static_cast<int>(player.x) + 2;
    int npcGridZ = static_cast<int>(player.z) + 2;
    npc.x = npcGridX + 0.5f;
    npc.z = npcGridZ + 0.5f;
    npc.y = world.surfaceY(npcGridX, npcGridZ);

    int selected = 0;
    std::vector<ItemStack> hotbarSlots(HOTBAR.size());
    for (size_t i = 0; i < HOTBAR.size(); ++i)
    {
        hotbarSlots[i].type = HOTBAR[i];
        hotbarSlots[i].count = 64;
    }
    std::vector<ItemStack> inventorySlots(INV_COLS * INV_ROWS);
    {
        int idx = 0;
        for (BlockType b : INVENTORY_ALLOWED)
        {
            if (idx >= static_cast<int>(inventorySlots.size()))
                break;
            inventorySlots[idx].type = b;
            inventorySlots[idx].count = 64;
            ++idx;
        }
    }
    bool inventoryOpen = false;
    bool pauseMenuOpen = false;
    int pendingSlot = -1;
    bool pendingIsHotbar = false;
    int mouseX = 0, mouseY = 0;

    bool running = true;
    Uint64 prev = SDL_GetPerformanceCounter();
    float fps = 60.0f;
    float smoothDX = 0.0f;
    float smoothDY = 0.0f;
    float elapsedTime = 0.0f;
    float lastForwardTap = -1.0f;
    bool sprinting = false;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_ShowCursor(SDL_FALSE);

    std::cout << "Commandes: WASD/ZQSD deplacement, souris pour la camera, clic gauche miner, clic droit placer, "
                 "1-5 changer de bloc, Space saut, Shift descendre, R regen, X save (non implemente), Esc menu pause.\n";

    int winW = 1280, winH = 720;
    setup3D(winW, winH);

    while (running)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - prev) / SDL_GetPerformanceFrequency();
        prev = now;
        elapsedTime += dt;
        if (dt > 0.0001f)
        {
            fps = fps * 0.9f + (1.0f / dt) * 0.1f; // lissage simple
        }

        // Applique la souris liss??e ici pour stabiliser la camera
        if (!inventoryOpen && !pauseMenuOpen)
        {
            const float sensitivity = 0.011f;
            player.yaw += smoothDX * sensitivity;
            player.pitch -= smoothDY * sensitivity;
            smoothDX *= 0.5f;
            smoothDY *= 0.5f;
            player.pitch = std::clamp(player.pitch, -1.5f, 1.5f);
        }
        else
        {
            smoothDX = 0.0f;
            smoothDY = 0.0f;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
            {
                running = false;
            }
            else if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_ESCAPE)
                {
                    if (pauseMenuOpen)
                    {
                        pauseMenuOpen = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                    }
                    else
                    {
                        pauseMenuOpen = true;
                        inventoryOpen = false;
                        pendingSlot = -1;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                        smoothDX = smoothDY = 0.0f;
                    }
                }
                else if (e.key.keysym.sym == SDLK_e && !pauseMenuOpen)
                {
                    inventoryOpen = !inventoryOpen;
                    pendingSlot = -1;
                    SDL_SetRelativeMouseMode(inventoryOpen ? SDL_FALSE : SDL_TRUE);
                    SDL_ShowCursor(inventoryOpen ? SDL_TRUE : SDL_FALSE);
                    smoothDX = smoothDY = 0.0f;
                }
                else if (e.key.keysym.sym == SDLK_r)
                {
                    world.generate(seed + 1);
                    seed += 1337;
                    player.x = WIDTH / 2.0f;
                    player.z = DEPTH / 2.0f;
                    player.y = world.surfaceY(static_cast<int>(player.x), static_cast<int>(player.z)) + 0.2f;
                    markAllChunksDirty();
                }
                else if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_5)
                {
                    selected = static_cast<int>(e.key.keysym.sym - SDLK_1);
                }
                else if ((e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_z) && e.key.repeat == 0)
                {
                    if (lastForwardTap >= 0.0f && (elapsedTime - lastForwardTap) <= SPRINT_DOUBLE_TAP)
                    {
                        sprinting = true;
                    }
                    lastForwardTap = elapsedTime;
                }
            }
            else if (e.type == SDL_MOUSEMOTION)
            {
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                if (!inventoryOpen && !pauseMenuOpen)
                {
                    // Filtre de la souris pour lisser les mouvements et limiter les saccades
                    smoothDX = smoothDX * 0.6f + static_cast<float>(e.motion.xrel) * 0.4f;
                    smoothDY = smoothDY * 0.6f + static_cast<float>(e.motion.yrel) * 0.4f;
                }
            }
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                winW = e.window.data1;
                winH = e.window.data2;
                setup3D(winW, winH);
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (pauseMenuOpen)
                {
                    PauseMenuLayout l = computePauseLayout(winW, winH);
                    bool hoverResume = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.resumeX,
                                                   l.resumeY, l.resumeW, l.resumeH);
                    bool hoverQuit = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.quitX, l.quitY,
                                                 l.quitW, l.quitH);
                    if (hoverQuit)
                    {
                        running = false;
                    }
                    else if (hoverResume)
                    {
                        pauseMenuOpen = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                        smoothDX = smoothDY = 0.0f;
                    }
                }
                else if (inventoryOpen)
                {
                    if (e.button.button == SDL_BUTTON_LEFT)
                    {
                        SlotHit hit = hitTestInventoryUI(mouseX, mouseY, winW, winH);
                        if (!hit.valid)
                        {
                            pendingSlot = -1;
                        }
                        else if (pendingSlot < 0)
                        {
                            pendingSlot = hit.idx;
                            pendingIsHotbar = hit.isHotbar;
                        }
                        else
                        {
                            ItemStack *a =
                                pendingIsHotbar ? &hotbarSlots[pendingSlot] : &inventorySlots[pendingSlot];
                            ItemStack *b = hit.isHotbar ? &hotbarSlots[hit.idx] : &inventorySlots[hit.idx];
                            if (a && b)
                            {
                                std::swap(*a, *b);
                            }
                            pendingSlot = -1;
                        }
                    }
                }
                else
                {
                    Vec3 fwd = forwardVec(player.yaw, player.pitch);
                    float eyeY = player.y + EYE_HEIGHT;
                    HitInfo hit = raycast(world, player.x, eyeY, player.z, fwd.x, fwd.y, fwd.z, 8.0f);
                    if (hit.hit)
                    {
                        if (e.button.button == SDL_BUTTON_LEFT)
                        {
                            BlockType bt = world.get(hit.x, hit.y, hit.z);
                            if (bt != BlockType::Air && bt != BlockType::Water)
                            {
                                world.set(hit.x, hit.y, hit.z, BlockType::Air);
                                markNeighborsDirty(hit.x, hit.y, hit.z);
                            }
                        }
                        else if (e.button.button == SDL_BUTTON_RIGHT)
                        {
                            BlockType target = world.get(hit.x, hit.y, hit.z);
                            if (target == BlockType::Button)
                            {
                                world.toggleButton(hit.x, hit.y, hit.z);
                            }
                            else
                            {
                                int nx = hit.x + hit.nx;
                                int ny = hit.y + hit.ny;
                                int nz = hit.z + hit.nz;
                                if (world.inside(nx, ny, nz) && !isSolid(world.get(nx, ny, nz)) &&
                                    !blockIntersectsPlayer(player, nx, ny, nz, PLAYER_HEIGHT))
                                {
                                    ItemStack &slot = hotbarSlots[selected];
                                    if (slot.type != BlockType::Air)
                                    {
                                        BlockType toPlace = slot.type;
                                        world.set(nx, ny, nz, toPlace);
                                        markNeighborsDirty(nx, ny, nz);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        float simDt = pauseMenuOpen ? 0.0f : dt;

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        Vec3 fwd = forwardVec(player.yaw, player.pitch);
        Vec3 right{std::cos(player.yaw), 0.0f, std::sin(player.yaw)};
        float moveSpeed = SPEED * (sprinting ? SPRINT_MULT : 1.0f);
        bool forwardHeld = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_Z];

        if (!inventoryOpen && !pauseMenuOpen)
        {
            if (forwardHeld)
            {
                player.vx += fwd.x * moveSpeed * simDt;
                player.vz += fwd.z * moveSpeed * simDt;
            }
            if (keys[SDL_SCANCODE_S])
            {
                player.vx -= fwd.x * moveSpeed * simDt;
                player.vz -= fwd.z * moveSpeed * simDt;
            }
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_Q])
            {
                player.vx -= right.x * moveSpeed * simDt;
                player.vz -= right.z * moveSpeed * simDt;
            }
            if (keys[SDL_SCANCODE_D])
            {
                player.vx += right.x * moveSpeed * simDt;
                player.vz += right.z * moveSpeed * simDt;
            }
            if (keys[SDL_SCANCODE_SPACE])
            {
                player.vy = JUMP;
            }
            if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            {
                player.vy = -JUMP;
            }
        }
        if (!forwardHeld || inventoryOpen || pauseMenuOpen)
        {
            sprinting = false;
        }
        player.vy += GRAVITY * simDt;

        float nextY = player.y + player.vy * simDt;
        nextY = std::clamp(nextY, PLAYER_HEIGHT * 0.5f, HEIGHT - 2.0f);
        if (collidesAt(world, player.x, nextY, player.z, PLAYER_HEIGHT))
        {
            if (player.vy < 0.0f)
            {
                nextY = std::floor(player.y) + 0.001f;
            }
            else if (player.vy > 0.0f)
            {
                nextY = std::floor(player.y + PLAYER_HEIGHT) - PLAYER_HEIGHT - 0.001f;
            }
            player.vy = 0.0f;
        }

        float nextX = player.x + player.vx * simDt;
        nextX = std::clamp(nextX, 1.0f, WIDTH - 2.0f);
        if (collidesAt(world, nextX, nextY, player.z, PLAYER_HEIGHT))
        {
            nextX = player.x;
            player.vx = 0.0f;
        }

        float nextZ = player.z + player.vz * simDt;
        nextZ = std::clamp(nextZ, 1.0f, DEPTH - 2.0f);
        if (collidesAt(world, nextX, nextY, nextZ, PLAYER_HEIGHT))
        {
            nextZ = player.z;
            player.vz = 0.0f;
        }

        player.x = nextX;
        player.y = nextY;
        player.z = nextZ;

        player.vx *= 0.85f;
        player.vy *= 0.85f;
        player.vz *= 0.85f;

        updateNpc(npc, world, simDt);
        updateLogic(world);

        glClearColor(0.55f, 0.75f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glLoadIdentity();
        Vec3 fwdView = forwardVec(player.yaw, player.pitch);
        gluLookAt(player.x, player.y + EYE_HEIGHT, player.z, player.x + fwdView.x, player.y + EYE_HEIGHT + fwdView.y,
                  player.z + fwdView.z, 0.0f, 1.0f, 0.0f);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gAtlasTex);
        const float chunkView = 28.0f;
        for (int cY = 0; cY < CHUNK_Y_COUNT; ++cY)
        {
            for (int cZ = 0; cZ < CHUNK_Z_COUNT; ++cZ)
            {
                for (int cX = 0; cX < CHUNK_X_COUNT; ++cX)
                {
                    float cxCenter = (cX + 0.5f) * CHUNK_SIZE;
                    float cyCenter = (cY + 0.5f) * CHUNK_SIZE;
                    float czCenter = (cZ + 0.5f) * CHUNK_SIZE;
                    float dx = cxCenter - player.x;
                    float dy = cyCenter - player.y;
                    float dz = czCenter - player.z;
                    if (dx * dx + dy * dy + dz * dz > chunkView * chunkView)
                        continue;
                    int idx = chunkIndex(cX, cY, cZ);
                    if (idx < 0)
                        continue;
                    ChunkMesh &cm = chunkMeshes[idx];
                    if (cm.dirty)
                    {
                        buildChunkMesh(world, cX, cY, cZ);
                    }
                    if (cm.verts.empty() || cm.vbo == 0)
                        continue;
                    glBindBuffer(GL_ARRAY_BUFFER, cm.vbo);
                    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(0));
                    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(sizeof(float) * 3));
                    glColorPointer(3, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(sizeof(float) * 5));
                    glDrawArrays(GL_QUADS, 0, static_cast<GLsizei>(cm.verts.size()));
                }
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

        // NPC blocky model
        drawNpcBlocky(npc);

        // bouton 0/1 affiché directement sur le bloc proche du joueur
        drawButtonStateLabels(world, player, 10.0f);

        Vec3 fwdCast = forwardVec(player.yaw, player.pitch);
        float eyeY = player.y + EYE_HEIGHT;
        HitInfo hit = raycast(world, player.x, eyeY, player.z, fwdCast.x, fwdCast.y, fwdCast.z, 8.0f);
        if (hit.hit)
        {
            drawFaceHighlight(hit);
        }

        HoverLabel hoverLabel;
        beginHud(winW, winH);
        if (!inventoryOpen && !pauseMenuOpen)
            drawCrosshair(winW, winH);
        drawInventoryBar(winW, winH, hotbarSlots, selected);
        if (inventoryOpen)
            drawInventoryPanel(winW, winH, inventorySlots, hotbarSlots, pendingSlot, pendingIsHotbar, mouseX, mouseY,
                               hoverLabel);
        if (pauseMenuOpen)
        {
            PauseMenuLayout l = computePauseLayout(winW, winH);
            bool hoverResume = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.resumeX, l.resumeY,
                                           l.resumeW, l.resumeH);
            bool hoverQuit = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.quitX, l.quitY, l.quitW,
                                         l.quitH);
            drawPauseMenu(winW, winH, l, hoverResume, hoverQuit);
        }
        if (hoverLabel.valid)
            drawTooltip(hoverLabel.x, hoverLabel.y, winW, winH, hoverLabel.text);
        endHud();

        SDL_GL_SwapWindow(window);
        updateTitle(window);
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
