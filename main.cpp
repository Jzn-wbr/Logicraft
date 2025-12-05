#define SDL_MAIN_HANDLED
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#include <algorithm>
#include <array>
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
    Sand
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

static const std::map<BlockType, BlockInfo> BLOCKS = {
    {BlockType::Air, {"Air", false, {0.7f, 0.85f, 1.0f}}},
    {BlockType::Grass, {"Grass", true, {0.2f, 0.7f, 0.2f}}},
    {BlockType::Dirt, {"Dirt", true, {0.45f, 0.25f, 0.1f}}},
    {BlockType::Stone, {"Stone", true, {0.5f, 0.5f, 0.5f}}},
    {BlockType::Wood, {"Wood", true, {0.55f, 0.35f, 0.2f}}},
    {BlockType::Leaves, {"Leaves", true, {0.25f, 0.6f, 0.25f}}},
    {BlockType::Water, {"Water", false, {0.2f, 0.4f, 0.9f}}},
    {BlockType::Plank, {"Plank", true, {0.75f, 0.6f, 0.4f}}},
    {BlockType::Sand, {"Sand", true, {0.9f, 0.8f, 0.6f}}},
};

static const std::vector<BlockType> HOTBAR = {BlockType::Dirt, BlockType::Stone, BlockType::Wood,
                                              BlockType::Plank, BlockType::Sand};

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
    World(int w, int h, int d) : width(w), height(h), depth(d), tiles(w * h * d, BlockType::Air) {}

    BlockType get(int x, int y, int z) const { return tiles[(y * depth + z) * width + x]; }
    void set(int x, int y, int z, BlockType b) { tiles[(y * depth + z) * width + x] = b; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getDepth() const { return depth; }

    void generate(unsigned seed)
    {
        std::mt19937 rng(seed);
        (void)rng; // seed kept for future randomness if needed

        int surface = height / 4; // flat world height
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
};

bool isSolid(BlockType b) { return BLOCKS.at(b).solid; }

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

bool isTransparent(BlockType b) { return !isSolid(b); }

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

struct Vec3
{
    float x, y, z;
};

// Mesh par chunk (VBO)
const int CHUNK_SIZE = 16;
int CHUNK_X_COUNT = 0;
int CHUNK_Y_COUNT = 0;
int CHUNK_Z_COUNT = 0;
std::vector<ChunkMesh> chunkMeshes;
GLuint gAtlasTex = 0;
const int ATLAS_COLS = 4;
const int ATLAS_ROWS = 4;
const int ATLAS_TILE_SIZE = 32;
std::map<BlockType, int> gBlockTile;
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

// ---------- Texture atlas generation ----------
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
                A = 180;
            writePixel(pix, texW, x0 + x, y0 + y, R, G, B, A);
        }
    }
}

void createAtlasTexture()
{
    gBlockTile = {{BlockType::Grass, 0}, {BlockType::Dirt, 1}, {BlockType::Stone, 2}, {BlockType::Wood, 3}, {BlockType::Leaves, 4}, {BlockType::Water, 5}, {BlockType::Plank, 6}, {BlockType::Sand, 7}, {BlockType::Air, 8}};

    int texW = ATLAS_COLS * ATLAS_TILE_SIZE;
    int texH = ATLAS_ROWS * ATLAS_TILE_SIZE;
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

bool pointInRect(float mx, float my, float x, float y, float w, float h)
{
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
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
                        int pendingSlot, bool pendingIsHotbar, int mouseX, int mouseY)
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
                int tIdx = tileIndexFor(b);
                if (x == 0 || !isSolid(world.get(x - 1, y, z)))
                    addFace(x, y, z, -1, 0, 0, color, tIdx);
                if (x == world.getWidth() - 1 || !isSolid(world.get(x + 1, y, z)))
                    addFace(x, y, z, 1, 0, 0, color, tIdx);
                if (y == 0 || !isSolid(world.get(x, y - 1, z)))
                    addFace(x, y, z, 0, -1, 0, color, tIdx);
                if (y == world.getHeight() - 1 || !isSolid(world.get(x, y + 1, z)))
                    addFace(x, y, z, 0, 1, 0, color, tIdx);
                if (z == 0 || !isSolid(world.get(x, y, z - 1)))
                    addFace(x, y, z, 0, 0, -1, color, tIdx);
                if (z == world.getDepth() - 1 || !isSolid(world.get(x, y, z + 1)))
                    addFace(x, y, z, 0, 0, 1, color, tIdx);
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
    std::string title = "MiniCraft 3D";
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

    int selected = 0;
    std::vector<ItemStack> hotbarSlots(HOTBAR.size());
    for (size_t i = 0; i < HOTBAR.size(); ++i)
    {
        hotbarSlots[i].type = HOTBAR[i];
        hotbarSlots[i].count = 50;
    }
    std::vector<ItemStack> inventorySlots(INV_COLS * INV_ROWS);
    {
        int idx = 0;
        for (const auto &kv : BLOCKS)
        {
            if (kv.first == BlockType::Air)
                continue;
            if (idx >= static_cast<int>(inventorySlots.size()))
                break;
            inventorySlots[idx].type = kv.first;
            inventorySlots[idx].count = 32;
            ++idx;
        }
    }
    bool inventoryOpen = false;
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
                 "1-5 changer de bloc, Space saut, Shift descendre, R regen, X save (non implemente), Esc quitter.\n";

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
        if (!inventoryOpen)
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
                    running = false;
                }
                else if (e.key.keysym.sym == SDLK_e)
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
                if (!inventoryOpen)
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
                if (inventoryOpen)
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
                                int left = addToInventory(bt, 1, hotbarSlots, inventorySlots);
                                (void)left;
                                world.set(hit.x, hit.y, hit.z, BlockType::Air);
                                markNeighborsDirty(hit.x, hit.y, hit.z);
                            }
                        }
                        else if (e.button.button == SDL_BUTTON_RIGHT)
                        {
                            int nx = hit.x + hit.nx;
                            int ny = hit.y + hit.ny;
                            int nz = hit.z + hit.nz;
                            if (world.inside(nx, ny, nz) && !isSolid(world.get(nx, ny, nz)) &&
                                !blockIntersectsPlayer(player, nx, ny, nz, PLAYER_HEIGHT))
                            {
                                ItemStack &slot = hotbarSlots[selected];
                                if (slot.count > 0)
                                {
                                    BlockType toPlace = slot.type;
                                    if (toPlace != BlockType::Air)
                                    {
                                        slot.count--;
                                        if (slot.count == 0)
                                            slot.type = BlockType::Air;
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

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        Vec3 fwd = forwardVec(player.yaw, player.pitch);
        Vec3 right{std::cos(player.yaw), 0.0f, std::sin(player.yaw)};
        float moveSpeed = SPEED * (sprinting ? SPRINT_MULT : 1.0f);
        bool forwardHeld = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_Z];

        if (!inventoryOpen)
        {
            if (forwardHeld)
            {
                player.vx += fwd.x * moveSpeed * dt;
                player.vz += fwd.z * moveSpeed * dt;
            }
            if (keys[SDL_SCANCODE_S])
            {
                player.vx -= fwd.x * moveSpeed * dt;
                player.vz -= fwd.z * moveSpeed * dt;
            }
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_Q])
            {
                player.vx -= right.x * moveSpeed * dt;
                player.vz -= right.z * moveSpeed * dt;
            }
            if (keys[SDL_SCANCODE_D])
            {
                player.vx += right.x * moveSpeed * dt;
                player.vz += right.z * moveSpeed * dt;
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
        if (!forwardHeld || inventoryOpen)
        {
            sprinting = false;
        }
        player.vy += GRAVITY * dt;

        float nextY = player.y + player.vy * dt;
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

        float nextX = player.x + player.vx * dt;
        nextX = std::clamp(nextX, 1.0f, WIDTH - 2.0f);
        if (collidesAt(world, nextX, nextY, player.z, PLAYER_HEIGHT))
        {
            nextX = player.x;
            player.vx = 0.0f;
        }

        float nextZ = player.z + player.vz * dt;
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

        Vec3 fwdCast = forwardVec(player.yaw, player.pitch);
        float eyeY = player.y + EYE_HEIGHT;
        HitInfo hit = raycast(world, player.x, eyeY, player.z, fwdCast.x, fwdCast.y, fwdCast.z, 8.0f);
        if (hit.hit)
        {
            drawFaceHighlight(hit);
        }

        beginHud(winW, winH);
        if (!inventoryOpen)
            drawCrosshair(winW, winH);
        drawInventoryBar(winW, winH, hotbarSlots, selected);
        if (inventoryOpen)
            drawInventoryPanel(winW, winH, inventorySlots, hotbarSlots, pendingSlot, pendingIsHotbar, mouseX, mouseY);
        endHud();

        SDL_GL_SwapWindow(window);
        updateTitle(window);
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
