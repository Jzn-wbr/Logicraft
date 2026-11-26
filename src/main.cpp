#define SDL_MAIN_HANDLED
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "snapshot.hpp"
#include "render.hpp"
#include "types.hpp"
#include "world.hpp"

static std::filesystem::path gResourceRoot = std::filesystem::current_path();
static std::filesystem::path gMapsDir = "maps";

std::filesystem::path executableDirectory()
{
    char *base = SDL_GetBasePath();
    if (!base)
        return std::filesystem::current_path();
    auto path = std::filesystem::u8path(base);
    SDL_free(base);
    return path;
}

std::filesystem::path resolveResourceRoot(const std::filesystem::path &executableDir)
{
    namespace fs = std::filesystem;
    // An installed binary has its assets alongside it. Development builds
    // may be nested several directories below the source tree.
    for (auto dir = executableDir; !dir.empty(); dir = dir.parent_path())
    {
        std::error_code ec;
        if (fs::is_directory(dir / "maps", ec) && fs::is_directory(dir / "images", ec))
            return dir;
        if (dir == dir.root_path())
            break;
    }
    return fs::current_path();
}

std::string assetPath(const std::string &relative)
{
    static const auto executableDir = executableDirectory();
    std::error_code ec;
    auto local = executableDir / relative;
    if (std::filesystem::is_regular_file(local, ec))
        return local.u8string();
    return (gResourceRoot / relative).u8string();
}

void drawTextTiny(float x, float y, float size, const std::string &text, float r, float g, float b, float a);
std::string stemFromPath(const std::string &path);
std::string saveStatus;
extern bool gSaveInputFocus;
extern std::vector<std::string> gSaveList;
extern int gSaveIndex;

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 forwardVec(float yaw, float pitch);

void interactWithTargetBlock(const World &world, const Player &player, float eyeHeight)
{
    Vec3 fwd = forwardVec(player.yaw, player.pitch);
    float eyeY = player.y + eyeHeight;
    HitInfo hit = raycast(world, player.x, eyeY, player.z, fwd.x, fwd.y, fwd.z, 8.0f);
    if (!hit.hit)
        return;
}

Vec3 normalizeVec(const Vec3 &v)
{
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-6f)
        return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(0.35f, 0.35f, 0.35f, 0.34f);

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
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
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
const std::map<char, std::array<uint8_t, 5>> FONT5x4 = {
    {'0', {0b0110, 0b1001, 0b1001, 0b1001, 0b0110}},
    {'1', {0b0100, 0b1100, 0b0100, 0b0100, 0b1110}},
    {'2', {0b1110, 0b0001, 0b0110, 0b1000, 0b1111}},
    {'3', {0b1110, 0b0001, 0b0110, 0b0001, 0b1110}},
    {'4', {0b1001, 0b1001, 0b1111, 0b0001, 0b0001}},
    {'5', {0b1111, 0b1000, 0b1110, 0b0001, 0b1110}},
    {'6', {0b0111, 0b1000, 0b1110, 0b1001, 0b0110}},
    {'7', {0b1111, 0b0001, 0b0010, 0b0100, 0b0100}},
    {'8', {0b0110, 0b1001, 0b0110, 0b1001, 0b0110}},
    {'9', {0b0110, 0b1001, 0b0111, 0b0001, 0b1110}},
    {'A', {0b0110, 0b1001, 0b1111, 0b1001, 0b1001}},
    {'B', {0b1110, 0b1001, 0b1110, 0b1001, 0b1110}},
    {'C', {0b0111, 0b1000, 0b1000, 0b1000, 0b0111}},
    {'D', {0b1110, 0b1001, 0b1001, 0b1001, 0b1110}},
    {'E', {0b1111, 0b1000, 0b1110, 0b1000, 0b1111}},
    {'F', {0b1111, 0b1000, 0b1110, 0b1000, 0b1000}},
    {'G', {0b0111, 0b1000, 0b1011, 0b1001, 0b0111}},
    {'H', {0b1001, 0b1001, 0b1111, 0b1001, 0b1001}},
    {'I', {0b1110, 0b0100, 0b0100, 0b0100, 0b1110}},
    {'J', {0b0011, 0b0001, 0b0001, 0b1001, 0b0110}},
    {'L', {0b1000, 0b1000, 0b1000, 0b1000, 0b1111}},
    {'M', {0b1111, 0b1001, 0b1001, 0b1001, 0b1001}},
    {'K', {0b1001, 0b1010, 0b1100, 0b1010, 0b1001}},
    {'N', {0b1001, 0b1101, 0b1011, 0b1001, 0b1001}},
    {'O', {0b0110, 0b1001, 0b1001, 0b1001, 0b0110}},
    {'P', {0b1110, 0b1001, 0b1110, 0b1000, 0b1000}},
    {'Q', {0b0110, 0b1001, 0b1001, 0b1010, 0b0111}},
    {'R', {0b1110, 0b1001, 0b1110, 0b1010, 0b1001}},
    {'S', {0b0111, 0b1000, 0b0110, 0b0001, 0b1110}},
    {'T', {0b1111, 0b0100, 0b0100, 0b0100, 0b0100}},
    {'U', {0b1001, 0b1001, 0b1001, 0b1001, 0b0110}},
    {'V', {0b1001, 0b1001, 0b1001, 0b0110, 0b0110}},
    {'W', {0b1001, 0b1001, 0b1011, 0b1101, 0b1001}},
    {'Y', {0b1001, 0b1001, 0b0110, 0b0100, 0b0100}},
    {'X', {0b1001, 0b0110, 0b0100, 0b0110, 0b1001}},
    {'Z', {0b1111, 0b0010, 0b0100, 0b1000, 0b1111}},
    // === PONCTUATION ===
    {'.', {0b0000, 0b0000, 0b0000, 0b0000, 0b0100}},
    {',', {0b0000, 0b0000, 0b0000, 0b0100, 0b1000}},
    {':', {0b0000, 0b0100, 0b0000, 0b0100, 0b0000}},
    {';', {0b0000, 0b0100, 0b0000, 0b0100, 0b1000}},
    {'!', {0b0100, 0b0100, 0b0100, 0b0000, 0b0100}},
    {'?', {0b0110, 0b0001, 0b0010, 0b0000, 0b0010}},
    {'-', {0b0000, 0b0000, 0b0110, 0b0000, 0b0000}},
    {'_', {0b0000, 0b0000, 0b0000, 0b0000, 0b1111}},
    {'(', {0b0010, 0b0100, 0b0100, 0b0100, 0b0010}},
    {')', {0b0100, 0b0010, 0b0010, 0b0010, 0b0100}},
    {'+', {0b0000, 0b0100, 0b1110, 0b0100, 0b0000}},
    {'=', {0b0000, 0b1110, 0b0000, 0b1110, 0b0000}},
    {'/', {0b0001, 0b0010, 0b0100, 0b1000, 0b0000}},
    {'*', {0b0100, 0b1110, 0b0100, 0b1110, 0b0100}},
    {'%', {0b1001, 0b0010, 0b0100, 0b1000, 0b0110}},
    {'\'', {0b0100, 0b0100, 0b0000, 0b0000, 0b0000}},
    {'"', {0b1010, 0b1010, 0b0000, 0b0000, 0b0000}},

    // === SYMBOLS ASCII ===
    {'#', {0b0101, 0b1111, 0b0101, 0b1111, 0b0101}},
    {'$', {0b0110, 0b1010, 0b0110, 0b0101, 0b0110}},
    {'&', {0b0100, 0b1010, 0b0100, 0b1010, 0b0111}},
    {'@', {0b0110, 0b1001, 0b1011, 0b1000, 0b0110}},
    {'<', {0b0010, 0b0100, 0b1000, 0b0100, 0b0010}},
    {'>', {0b1000, 0b0100, 0b0010, 0b0100, 0b1000}},
    {'[', {0b0110, 0b0100, 0b0100, 0b0100, 0b0110}},
    {']', {0b0110, 0b0010, 0b0010, 0b0010, 0b0110}},
    {'{', {0b0010, 0b0100, 0b1000, 0b0100, 0b0010}}, // same shape than '(' but centered
    {'}', {0b0100, 0b0010, 0b0001, 0b0010, 0b0100}},
    {'|', {0b0100, 0b0100, 0b0100, 0b0100, 0b0100}},
    {'\\', {0b1000, 0b0100, 0b0010, 0b0001, 0b0000}},
};

// Forward decl for slot icon rendering
void drawSlotIcon(const ItemStack &slot, float x, float y, float slotSize);

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

    // brushed metal bar shell
    drawQuad(barX + 8.0f, barY + 10.0f, barWidth, barHeight, 0.0f, 0.0f, 0.0f, 0.32f);
    drawQuad(barX, barY, barWidth, barHeight, 0.06f, 0.065f, 0.075f, 0.96f);
    drawQuad(barX, barY, barWidth, barHeight * 0.4f, 0.1f, 0.11f, 0.13f, 0.55f);
    for (int i = 0; i < 6; ++i)
    {
        float y = barY + 14.0f + i * 10.0f;
        drawQuad(barX + 10.0f, y, barWidth - 20.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.04f);
    }
    drawOutline(barX, barY, barWidth, barHeight, 0.95f, 0.95f, 0.95f, 0.2f, 3.0f);
    drawOutline(barX + 3.0f, barY + 3.0f, barWidth - 6.0f, barHeight - 6.0f, 0.0f, 0.0f, 0.0f, 0.55f, 2.0f);

    for (int i = 0; i < slotCount; ++i)
    {
        float x = barX + padding + gap * (i + 1) + slotSize * i;
        float y = barY + padding;
        BlockType b = hotbar[i].type;
        if (!BLOCKS.count(b))
            b = BlockType::Air;
        auto col = BLOCKS.at(b).color;

        // slot shell (metal frame + glass insert)
        drawQuad(x - 3.0f, y - 3.0f, slotSize + 6.0f, slotSize + 6.0f, 0.0f, 0.0f, 0.0f, 0.3f);
        drawQuad(x - 1.0f, y - 1.0f, slotSize + 2.0f, slotSize + 2.0f, 0.09f, 0.095f, 0.11f, 0.7f);
        drawOutline(x - 4.0f, y - 4.0f, slotSize + 8.0f, slotSize + 8.0f, 0.95f, 0.95f, 0.95f, 0.18f, 2.0f);

        // block color tile
        float alpha = hotbar[i].count > 0 ? 0.9f : 0.25f;
        drawQuad(x, y, slotSize, slotSize, col[0], col[1], col[2], alpha);
        drawQuad(x + 2.0f, y + 2.0f, slotSize - 4.0f, slotSize * 0.45f, 1.0f, 1.0f, 1.0f, 0.09f);
        drawOutline(x, y, slotSize, slotSize, 0.0f, 0.0f, 0.0f, 0.55f, 2.0f);
        drawSlotIcon(hotbar[i], x, y, slotSize);

        // stack count
        int count = hotbar[i].count;
        if (count > 0)
            drawNumber(x + slotSize - 4.0f, y + slotSize - 16.0f, count, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f);

        // selection glow
        if (i == selected)
        {
            drawOutline(x - 6.0f, y - 6.0f, slotSize + 12.0f, slotSize + 12.0f, 0.95f, 0.85f, 0.35f, 0.9f, 3.5f);
            drawQuad(x - 2.0f, y - 2.0f, slotSize + 4.0f, slotSize + 4.0f, 0.95f, 0.8f, 0.35f, 0.18f);
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

enum class MenuAction { Normal, Primary, Caution, Disabled };

// Repeat a tile from the world's atlas without stretching its pixel pattern.
void menuMaterial(float x, float y, float width, float height, BlockType material, float light)
{
    const int tile = tileIndexFor(material);
    const float u = float(tile % ATLAS_COLS) / ATLAS_COLS;
    const float v = float(tile / ATLAS_COLS) / ATLAS_ROWS;
    const float pitch = ATLAS_TILE_SIZE * 2.0f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gAtlasTex);
    glColor3f(light, light, light);
    glBegin(GL_QUADS);
    for (float dy = 0; dy < height; dy += pitch)
    {
        for (float dx = 0; dx < width; dx += pitch)
        {
            float w = std::min(pitch, width-dx), h = std::min(pitch, height-dy);
            float right = u + w / (pitch * ATLAS_COLS), bottom = v + h / (pitch * ATLAS_ROWS);
            glTexCoord2f(u,v); glVertex2f(x+dx,y+dy);
            glTexCoord2f(right,v); glVertex2f(x+dx+w,y+dy);
            glTexCoord2f(right,bottom); glVertex2f(x+dx+w,y+dy+h);
            glTexCoord2f(u,bottom); glVertex2f(x+dx,y+dy+h);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void signText(float x, float y, float size, const std::string &label)
{
    drawTextTiny(x, y, size, label, 0.16f, 0.085f, 0.035f, 1);
}

void signBoard(float x, float y, float w, float h, float light = 0.94f)
{
    drawQuad(x+2, y+3, w, h, 0.27f, 0.18f, 0.10f, 1);
    menuMaterial(x, y, w, h, BlockType::Plank, light);
    for (float pin : {x+6, x+w-9})
        drawQuad(pin, y+h/2-1, 3, 3, 0.29f, 0.17f, 0.08f, 1);
}

void signPosts(float x, float y, float w, float h)
{
    for (float post : {x+w*0.12f, x+w*0.86f})
        menuMaterial(post, y+6, 12, h+22, BlockType::Wood, 0.82f);
}

void signHeading(float x, float y, float w, const char *title)
{
    signBoard(x, y, w, 46);
    float textWidth = (4.8f*std::strlen(title)-0.8f)*4;
    signText(std::round(x+(w-textWidth)/2), y+13, 4, title);
}

void menuPanel(float x, float y, float w, float h, const char *title)
{
    signPosts(x,y,w,h);
    signBoard(x+5,y+54,w-10,h-54);
    signHeading(x,y,w,title);
}

void menuButton(float x, float y, float w, float h, const char *label, bool hover,
                MenuAction action = MenuAction::Normal)
{
    bool enabled = action != MenuAction::Disabled;
    signBoard(x,y,w,h,enabled ? 1.04f : 0.65f);
    if (hover && enabled)
        drawOutline(x+1,y+1,w-2,h-2,0.99f,0.85f,0.33f,1,2);
    float size = w < 145 ? 2 : 3;
    float tw = (std::strlen(label)*4.8f-0.8f)*size;
    signText(std::round(x+(w-tw)/2),std::round(y+(h-5*size)/2),size,label);
}

float menuDockX(int winW, float panelW)
{
    float centered = (winW-panelW)*0.5f;
    return winW >= panelW*2 ? winW-panelW-32 : centered;
}

constexpr int saveRows = 6;
constexpr int saveRowHeight = 24;
int saveOffset = 0;

SaveMenuLayout computeSaveMenuLayout(int winW, int winH)
{
    SaveMenuLayout s;
    s.panelW = 560.0f;
    int visibleRows = std::clamp(static_cast<int>(gSaveList.size()), 1, saveRows);
    s.panelH = 400.0f - (saveRows-visibleRows)*saveRowHeight;
    s.panelX = menuDockX(winW, s.panelW);
    s.panelY = (winH - s.panelH) * 0.5f;

    s.listW = s.panelW * 0.86f;
    s.listH = visibleRows * saveRowHeight;
    s.listX = s.panelX + (s.panelW - s.listW) * 0.5f;
    s.listY = s.panelY + 66.0f;

    s.inputW = s.panelW * 0.86f;
    s.inputH = 36.0f;
    s.inputX = s.panelX + (s.panelW - s.inputW) * 0.5f;
    s.inputY = s.listY + s.listH + 34.0f;

    s.overwriteW = (s.listW - 36.0f) / 4.0f;
    s.overwriteH = 52.0f;
    s.createW = s.overwriteW;
    s.createH = s.overwriteH;
    s.loadW = s.overwriteW;
    s.loadH = s.overwriteH;
    s.backW = s.overwriteW;
    s.backH = s.overwriteH;

    float total = s.createW + s.overwriteW + s.loadW + s.backW + 3 * 12.0f;
    float start = s.panelX + (s.panelW - total) * 0.5f;
    float buttonsY = s.inputY + s.inputH + 20.0f;
    s.createX = start;
    s.createY = buttonsY;
    s.overwriteX = s.createX + s.createW + 12.0f;
    s.overwriteY = buttonsY;
    s.loadX = s.overwriteX + s.overwriteW + 12.0f;
    s.loadY = buttonsY;
    s.backX = s.loadX + s.loadW + 12.0f;
    s.backY = buttonsY;
    return s;
}

void drawSaveMenu(int winW, int winH, const SaveMenuLayout &s, int highlightedIndex, bool hoverCreate,
                  bool hoverOverwrite, bool hoverLoad, bool hoverBack, const std::string &inputName)
{
    drawQuad(0.0f, 0.0f, static_cast<float>(winW), static_cast<float>(winH), 0,0,0,0.18f);
    signPosts(s.panelX,s.panelY,s.panelW,s.panelH-44);
    signHeading(s.panelX,s.panelY,s.panelW,"CARTES");
    signBoard(s.inputX-8,s.inputY-26,s.inputW+16,s.inputH+32);
    if (!saveStatus.empty())
        signBoard(s.listX,s.panelY+s.panelH-34,s.listW,26);
    if (gSaveList.empty())
        signBoard(s.listX,s.listY,s.listW,saveRowHeight-3);
    if (gSaveList.empty())
        signText(s.listX+12,s.listY+8,2,"AUCUNE CARTE");
    for (int row = 0; row < saveRows && row + saveOffset < static_cast<int>(gSaveList.size()); ++row)
    {
        int i = row + saveOffset;
        float y = s.listY + row*saveRowHeight;
        signBoard(s.listX,y,s.listW,saveRowHeight-3);
        if (i == highlightedIndex)
            drawOutline(s.listX,y,s.listW,saveRowHeight-3,0.99f,0.85f,0.33f,1,2);
        signText(s.listX+15,y+5,2,stemFromPath(gSaveList[i]).substr(0,45));
    }
    signText(s.inputX,s.inputY-19,2,"NOM DE LA NOUVELLE CARTE");
    signText(s.listX,s.panelY+s.panelH-26,2,saveStatus);
    // input field
    drawQuad(s.inputX, s.inputY, s.inputW, s.inputH, 0.20f, 0.12f, 0.06f, 0.92f);
    float outlineA = gSaveInputFocus ? 0.55f : 0.2f;
    drawOutline(s.inputX, s.inputY, s.inputW, s.inputH, 1.0f, 1.0f, 1.0f, outlineA, 2.0f);
    std::string displayName = inputName.empty() ? "" : inputName;
    float textSizeInput = 2;
    float textX = s.inputX + 10.0f;
    float textY = s.inputY + 9.0f;
    drawTextTiny(textX, textY, textSizeInput, displayName, 1.0f, 1.0f, 1.0f,
                 inputName.empty() ? 0.6f : 0.95f);
    if (gSaveInputFocus)
    {
        float charW = 4.0f * textSizeInput + textSizeInput * 0.8f;
        float caretX = textX + static_cast<float>(inputName.size()) * charW;
        float caretH = textSizeInput * 5.0f;
        Uint32 t = SDL_GetTicks();
        if ((t / 500) % 2 == 0)
            drawQuad(caretX + 2.0f, textY, 2.0f, caretH, 1.0f, 1.0f, 1.0f, 0.9f);
    }

    menuButton(s.createX, s.createY, s.createW, s.createH, "CREER", hoverCreate, MenuAction::Normal);
    menuButton(s.overwriteX, s.overwriteY, s.overwriteW, s.overwriteH, "ECRASER", hoverOverwrite, highlightedIndex < 0 ? MenuAction::Disabled : MenuAction::Caution);
    menuButton(s.loadX, s.loadY, s.loadW, s.loadH, "CHARGER", hoverLoad, highlightedIndex < 0 ? MenuAction::Disabled : MenuAction::Primary);
    menuButton(s.backX, s.backY, s.backW, s.backH, "RETOUR", hoverBack, MenuAction::Normal);
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
    float size = 2.0f;
    float padding = 6.0f;
    float width = static_cast<float>(text.size()) * (4 * size + size * 0.8f) - size * 0.8f + padding * 2;
    float height = 5 * size + padding * 2;
    float tx = mx + 18.0f;
    float ty = my - height - 12.0f;
    tx = std::clamp(tx, 4.0f, static_cast<float>(winW) - width - 4.0f);
    ty = std::clamp(ty, 4.0f, static_cast<float>(winH) - height - 4.0f);
    signBoard(tx,ty,width,height);
    drawOutline(tx, ty, width, height, 1.0f, 1.0f, 1.0f, 0.12f, 2.0f);
    signText(tx+padding,ty+padding,size,text);
}

void drawSignEditBox(int winW, int winH, const std::string &text)
{
    const float w = std::min(560.0f, winW-32.0f);
    const float x = (winW-w)/2, y = (winH-136)/2;
    menuPanel(x,y,w,136,"PANNEAU");
    const std::string display = text.empty() ? "TAPEZ LE TEXTE DU PANNEAU" : text;
    const float size = std::min(3.0f,(w-40)/(4.8f*display.size()));
    signText(x+20,y+74,size,display);
    signText(x+20,y+111,1.5f,"ENTREE VALIDER   ESC ANNULER");
}

struct Config
{
    float mouseSensitivity = 0.011f;
    bool fullscreenDefault = false;
    bool showFps = false;
    bool vsync = true;
};

// Pixel coordinates on the 1280 x 720 captures. The first three boards
// occupy the same positions in both photographs.
struct MenuPoint { float x, y; };
using SignFace = std::array<MenuPoint, 4>;
constexpr SignFace menuSigns[] = {
    {{{334,304},{592,303},{593,348},{338,353}}},
    {{{339,366},{593,360},{594,403},{342,414}}},
    {{{343,427},{594,414},{594,457},{346,473}}},
    {{{347,485},{594,468},{595,509},{350,531}}}
};

struct MenuImageLayout
{
    float scale, x, y;

    MenuImageLayout(int width, int height)
        : scale(std::min(width/1280.0f, height/720.0f)),
          x((width-1280*scale)/2), y((height-720*scale)/2) {}

    int signAt(float mx, float my, bool paused) const
    {
        if (scale <= 0) return -1;
        MenuPoint p{(mx-x)/scale,(my-y)/scale};
        for (int i=0; i<(paused ? 4 : 3); ++i)
        {
            const auto &face = menuSigns[i];
            bool inside = true;
            for (size_t k=0; k<face.size(); ++k)
            {
                auto a = face[k], b = face[(k+1)%face.size()];
                if ((b.x-a.x)*(p.y-a.y) < (b.y-a.y)*(p.x-a.x))
                {
                    inside = false;
                    break;
                }
            }
            if (inside) return i;
        }
        return -1;
    }
};

void drawMenuImage(const MenuImageLayout &layout, GLuint texture, int hovered)
{
    glPushMatrix();
    glTranslatef(layout.x,layout.y,0);
    glScalef(layout.scale,layout.scale,1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,texture);
    glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(0,0);
    glTexCoord2f(1,0); glVertex2f(1280,0);
    glTexCoord2f(1,1); glVertex2f(1280,720);
    glTexCoord2f(0,1); glVertex2f(0,720);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    if (hovered >= 0)
    {
        glColor4f(0.99f,0.85f,0.33f,1);
        glLineWidth(2);
        glBegin(GL_LINE_LOOP);
        for (auto corner : menuSigns[hovered])
            glVertex2f(corner.x,corner.y);
        glEnd();
        glLineWidth(1);
    }
    glPopMatrix();
}

struct SettingsMenuLayout
{
    float panelX = 0, panelY = 0, panelW = 0, panelH = 0;
    float backX = 0, backY = 0, backW = 0, backH = 0;
    float sensMinusX = 0, sensMinusY = 0, sensMinusW = 0, sensMinusH = 0;
    float sensPlusX = 0, sensPlusY = 0, sensPlusW = 0, sensPlusH = 0;
    float fpsBoxX = 0, fpsBoxY = 0, fpsBoxW = 0, fpsBoxH = 0;
    float fullscreenBoxX = 0, fullscreenBoxY = 0, fullscreenBoxW = 0, fullscreenBoxH = 0;
};

SettingsMenuLayout computeSettingsLayout(int winW, int winH)
{
    SettingsMenuLayout s{};
    s.panelW = 520.0f;
    s.panelH = 340.0f;
    s.panelX = menuDockX(winW, s.panelW);
    s.panelY = (static_cast<float>(winH) - s.panelH) * 0.5f;
    s.sensMinusW = 36.0f;
    s.sensMinusH = 36.0f;
    s.sensMinusX = s.panelX + 220.0f;
    s.sensMinusY = s.panelY + 76.0f;
    s.sensPlusW = 36.0f;
    s.sensPlusH = 36.0f;
    s.sensPlusX = s.sensMinusX + 150.0f;
    s.sensPlusY = s.sensMinusY;
    s.fpsBoxW = 136.0f;
    s.fpsBoxH = 32.0f;
    s.fpsBoxX = s.panelX + 220.0f;
    s.fpsBoxY = s.panelY + 138.0f;
    s.fullscreenBoxW = 136.0f;
    s.fullscreenBoxH = 32.0f;
    s.fullscreenBoxX = s.panelX + 220.0f;
    s.fullscreenBoxY = s.panelY + 194.0f;
    s.backW = 140.0f;
    s.backH = 44.0f;
    s.backW = s.panelW - 24;
    s.backX = s.panelX + 12;
    s.backY = s.panelY + s.panelH - s.backH - 20.0f;
    return s;
}

void drawSettingsMenu(int winW, int winH, const SettingsMenuLayout &s, const Config &cfg, bool hoverBack,
                      bool hoverMinus, bool hoverPlus, bool hoverFps, bool hoverFullscreen)
{
    drawQuad(0, 0, winW, winH, 0,0,0,0.20f);
    signPosts(s.panelX,s.panelY,s.panelW,s.panelH-36);
    signHeading(s.panelX,s.panelY,s.panelW,"REGLAGES");
    for (float top : {s.sensMinusY-8, s.fpsBoxY-6, s.fullscreenBoxY-4})
        signBoard(s.panelX+6,top,s.panelW-12,44);
    signText(s.panelX+24, s.sensMinusY+12, 2, "SENSIBILITE SOURIS");
    menuButton(s.sensMinusX,s.sensMinusY,s.sensMinusW,s.sensMinusH,"-",hoverMinus);
    menuButton(s.sensPlusX,s.sensPlusY,s.sensPlusW,s.sensPlusH,"+",hoverPlus);
    char value[16];
    std::snprintf(value, sizeof(value), "%.3f", cfg.mouseSensitivity);
    signText(s.sensMinusX+50, s.sensMinusY+12, 3, value);
    signText(s.panelX+24, s.fpsBoxY+10, 2, "COMPTEUR FPS");
    menuButton(s.fpsBoxX,s.fpsBoxY,s.fpsBoxW,s.fpsBoxH,cfg.showFps ? "ACTIVE" : "DESACTIVE",hoverFps,
               cfg.showFps ? MenuAction::Primary : MenuAction::Normal);
    signText(s.panelX+24, s.fullscreenBoxY+10, 2, "AFFICHAGE");
    menuButton(s.fullscreenBoxX,s.fullscreenBoxY,s.fullscreenBoxW,s.fullscreenBoxH,
               cfg.fullscreenDefault ? "PLEIN ECRAN" : "FENETRE",hoverFullscreen);
    menuButton(s.backX, s.backY, s.backW, s.backH, "RETOUR", hoverBack, MenuAction::Normal);
}

void drawButtonEditBox(int winW, int winH, const std::string &valueText, const std::string &widthText, bool widthFocused)
{
    float boxW = std::min(450.0f, winW-32.0f);
    float boxH = 230.0f;
    float x = (winW - boxW) * 0.5f;
    float y = (winH - boxH) * 0.5f;
    menuPanel(x, y-25, boxW, boxH+50, "BOUTON");
    float textX = x + 18.0f;
    float textY = y + 44.0f;

    auto drawField = [&](const char *label, float yOffset, const std::string &txt, bool focused)
    {
        signText(textX, textY + yOffset, 2.0f, label);
        float boxY = textY + yOffset + 22.0f;
        float outline = focused ? 0.55f : 0.25f;
        drawQuad(textX, boxY, boxW - 36.0f, 36.0f, 0.20f, 0.12f, 0.06f, 0.92f);
        drawOutline(textX, boxY, boxW - 36.0f, 36.0f, 1.0f, 1.0f, 1.0f, outline, 2.0f);
        drawTextTiny(textX + 6.0f, boxY + 10.0f, 2.0f, txt.empty() ? "0" : txt, 1.0f, 1.0f, 1.0f, 1.0f);
    };

    drawField("Valeur du bouton (0-255)", 0.0f, valueText, !widthFocused);
    drawField("Largeur du bus (1-8 bits)", 68.0f, widthText, widthFocused);
    signText(textX, textY + 150.0f, 1.5f, "TAB CHAMP   ENTREE VALIDER   ESC ANNULER");
}

void drawClockEditBox(int winW, int winH, const std::string &freqText)
{
    float boxW = 360.0f;
    float boxH = 190.0f;
    float x = (winW - boxW) * 0.5f;
    float y = (winH - boxH) * 0.5f;
    menuPanel(x, y-25, boxW, boxH+50, "HORLOGE");
    float textX = x + 18.0f;
    float textY = y + 44.0f;

    signText(textX, textY, 2.0f, "Frequence clock (1-255)");
    float boxY = textY + 22.0f;
    drawQuad(textX, boxY, boxW - 36.0f, 36.0f, 0.20f, 0.12f, 0.06f, 0.92f);
    drawOutline(textX, boxY, boxW - 36.0f, 36.0f, 1.0f, 1.0f, 1.0f, 0.45f, 2.0f);
    drawTextTiny(textX + 6.0f, boxY + 10.0f, 2.0f, freqText.empty() ? "0" : freqText, 1.0f, 1.0f, 1.0f, 1.0f);
    signText(textX, textY + 90.0f, 1.5f, "Entrer pour valider, Esc pour annuler");
}

void drawWireInfoBox(int winW, int winH, uint8_t width, uint8_t value)
{
    float boxW = 360.0f;
    float boxH = 200.0f;
    float x = (winW - boxW) * 0.5f;
    float y = (winH - boxH) * 0.5f;
    menuPanel(x, y-25, boxW, boxH+50, "BUS");

    float textX = x + 18.0f;
    float textY = y + 46.0f;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "Largeur du bus : %u bit%s", static_cast<unsigned>(width),
                  width > 1 ? "s" : "");
    signText(textX, textY, 2.2f, buf);

    std::snprintf(buf, sizeof(buf), "Valeur decimale : %u", static_cast<unsigned>(value));
    signText(textX, textY + 28.0f, 2.0f, buf);

    std::string bin;
    bin.reserve(width ? width : 8);
    uint8_t w = width ? width : 8;
    for (int i = static_cast<int>(w) - 1; i >= 0; --i)
        bin.push_back((value & (1u << i)) ? '1' : '0');
    std::snprintf(buf, sizeof(buf), "Valeur binaire : %s", bin.c_str());
    signText(textX, textY + 56.0f, 2.0f, buf);

    signText(textX, textY + 100.0f, 1.5f, "Appuyer sur Esc pour fermer");
}

void drawSplitterEditBox(int winW, int winH, bool isMerger, const std::string &widthText, bool order)
{
    float boxW = std::min(450.0f, winW-32.0f);
    float boxH = 200.0f;
    float x = (winW - boxW) * 0.5f;
    float y = (winH - boxH) * 0.5f;
    menuPanel(x, y-25, boxW, boxH+50, isMerger ? "MERGER" : "SPLITTER");
    float textX = x + 18.0f;
    float textY = y + 18.0f;

    signText(textX, textY + 26.0f, 1.6f,
                 isMerger ? "B1 et B2 reunis vers BUS" : "BUS vers B1 et B2");

    signText(textX, textY + 52.0f, 2.0f, "Largeur B1 (1-7 bits)");
    drawQuad(textX, textY + 68.0f, boxW - 36.0f, 32.0f, 0.20f, 0.12f, 0.06f, 0.92f);
    drawOutline(textX, textY + 68.0f, boxW - 36.0f, 32.0f, 1.0f, 1.0f, 1.0f, 0.25f, 2.0f);
    drawTextTiny(textX + 6.0f, textY + 76.0f, 2.0f, widthText.empty() ? "1" : widthText, 1.0f, 1.0f, 1.0f, 1.0f);

    signText(textX, textY + 110.0f, 2.0f,
                 isMerger ? (order ? "Ordre: B1 en bits de poids fort" : "Ordre: B1 en bits de poids faible")
                          : (order ? "B1 RECOIT LES BITS FORTS" : "B1 RECOIT LES BITS FAIBLES"));

    signText(textX, textY + 140.0f, 1.5f, "Tab:inverse l'ordre, Entrer:valider, Esc:annuler");
}

// ---------- Config ----------
static const char *CONFIG_FILE = "config.cfg";

void loadConfig(Config &cfg)
{
    std::ifstream in(CONFIG_FILE);
    if (!in)
        return;
    std::string line;
    while (std::getline(in, line))
    {
        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if (key == "mouse_sensitivity")
        {
            try
            {
                float v = std::stof(val);
                if (v >= 0.001f && v <= 0.1f)
                    cfg.mouseSensitivity = v;
            }
            catch (...)
            {
            }
        }
        else if (key == "fullscreen")
        {
            cfg.fullscreenDefault = (val == "1" || val == "true" || val == "yes");
        }
        else if (key == "show_fps")
        {
            cfg.showFps = (val == "1" || val == "true" || val == "yes");
        }
        else if (key == "vsync")
        {
            cfg.vsync = (val == "1" || val == "true" || val == "yes");
        }
    }
}

void saveConfig(const Config &cfg)
{
    std::ofstream out(CONFIG_FILE);
    if (!out)
        return;
    out << "mouse_sensitivity=" << cfg.mouseSensitivity << "\n";
    out << "fullscreen=" << (cfg.fullscreenDefault ? 1 : 0) << "\n";
    out << "show_fps=" << (cfg.showFps ? 1 : 0) << "\n";
    out << "vsync=" << (cfg.vsync ? 1 : 0) << "\n";
}

// ---------- Save / Load ----------
std::string timestampSaveName()
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tmStruct{};
#ifdef _WIN32
    localtime_s(&tmStruct, &t);
#else
    localtime_r(&t, &tmStruct);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d.bulldog", tmStruct.tm_year + 1900,
                  tmStruct.tm_mon + 1, tmStruct.tm_mday, tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec);
    return (gMapsDir / buf).string();
}

std::string latestSaveInMaps()
{
    namespace fs = std::filesystem;
    fs::path dir = gMapsDir;
    if (!fs::exists(dir))
        return {};
    fs::file_time_type latestTime;
    fs::path latestPath;
    for (auto &p : fs::directory_iterator(dir))
    {
        if (!p.is_regular_file())
            continue;
        if (p.path().extension() != ".bulldog")
            continue;
        auto t = p.last_write_time();
        if (latestPath.empty() || t > latestTime)
        {
            latestTime = t;
            latestPath = p.path();
        }
    }
    return latestPath.empty() ? std::string{} : latestPath.string();
}

std::vector<std::string> gSaveList;
int gSaveIndex = -1;
std::string gSaveNameInput;
bool gSaveInputFocus = false;
bool gSignEditOpen = false;
int gSignEditX = 0, gSignEditY = 0, gSignEditZ = 0;
std::string gSignEditBuffer;
bool gButtonEditOpen = false;
int gButtonEditX = 0, gButtonEditY = 0, gButtonEditZ = 0;
std::string gButtonEditBuffer;
std::string gButtonWidthBuffer;
bool gButtonEditingWidth = false;
bool gClockEditOpen = false;
int gClockEditX = 0, gClockEditY = 0, gClockEditZ = 0;
std::string gClockEditBuffer;
bool gWireInfoOpen = false;
int gWireInfoX = 0, gWireInfoY = 0, gWireInfoZ = 0;
uint8_t gWireInfoWidth = 0;
uint8_t gWireInfoValue = 0;
bool gSplitterEditOpen = false;
bool gSplitterIsMerger = false;
int gSplitterX = 0, gSplitterY = 0, gSplitterZ = 0;
std::string gSplitterWidthBuffer;
bool gSplitterOrder = false; // false = B1 LSB (split: LSB->B1), true = B1 MSB
bool gMainMenuOpen = true;
bool gSettingsMenuOpen = false;
Config gConfig;

std::string stemFromPath(const std::string &path);

void refreshSaveList()
{
    saveOffset = 0;
    gSaveList.clear();
    gSaveIndex = -1;
    namespace fs = std::filesystem;
    fs::path dir = gMapsDir;
    if (!fs::exists(dir))
        return;
    std::vector<std::pair<fs::file_time_type, std::string>> entries;
    for (auto &p : fs::directory_iterator(dir))
    {
        if (!p.is_regular_file())
            continue;
        if (p.path().extension() != ".bulldog")
            continue;
        entries.push_back({p.last_write_time(), p.path().string()});
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b)
              { return a.first > b.first; });
    for (auto &e : entries)
        gSaveList.push_back(e.second);
    if (!gSaveList.empty())
        gSaveIndex = 0;
}

std::string stemFromPath(const std::string &path)
{
    namespace fs = std::filesystem;
    fs::path p(path);
    std::string stem = p.stem().string();
    for (auto &c : stem)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return stem;
}

std::string normalizeSaveInput(const std::string &input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    const size_t MAX_LEN = 32;
    if (out.size() > MAX_LEN)
        out.resize(MAX_LEN);
    return out;
}

int findSaveIndexByStem(const std::string &stem)
{
    std::string target = normalizeSaveInput(stem);
    for (int i = 0; i < static_cast<int>(gSaveList.size()); ++i)
    {
        if (normalizeSaveInput(stemFromPath(gSaveList[i])) == target)
            return i;
    }
    return -1;
}

std::string buildSavePathFromInput(const std::string &input)
{
    std::string name = normalizeSaveInput(input);
    if (name.empty())
        return timestampSaveName();
    const std::string ext = ".bulldog";
    if (name.size() < ext.size() || name.substr(name.size() - ext.size()) != ext)
        name += ext;
    return (gMapsDir / name).string();
}

void drawButtonStateLabels(const World &world, const Player &player, float radius)
{
    Vec3 fwd = forwardVec(player.yaw, player.pitch);
    Vec3 camRight = normalizeVec(cross(fwd, Vec3{0.0f, 1.0f, 0.0f}));
    if (std::abs(camRight.x) < 1e-4f && std::abs(camRight.z) < 1e-4f)
        camRight = {1.0f, 0.0f, 0.0f};
    Vec3 camUp = normalizeVec(cross(camRight, fwd));
    // Ensure the billboard faces the camera without mirroring: flip both axes if normal points toward camera
    Vec3 normal = cross(camRight, camUp);
    float dot = normal.x * fwd.x + normal.y * fwd.y + normal.z * fwd.z;
    if (dot > 0.0f)
    {
        camRight.x *= -1.0f;
        camRight.y *= -1.0f;
        camRight.z *= -1.0f;
        camUp.x *= -1.0f;
        camUp.y *= -1.0f;
        camUp.z *= -1.0f;
    }
    // Flip vertically to keep digits upright (avoid top/bottom inversion)
    camUp.x *= -1.0f;
    camUp.y *= -1.0f;
    camUp.z *= -1.0f;
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
                BlockType b = world.get(x, y, z);
                if (b != BlockType::Button && b != BlockType::Counter)
                    continue;
                Vec3 pos{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 1.2f, static_cast<float>(z) + 0.5f};
                pos.x += camUp.x * 0.02f;
                pos.y += camUp.y * 0.02f;
                pos.z += camUp.z * 0.02f;
                if (b == BlockType::Button)
                {
                    int state = world.getButtonState(x, y, z) ? 1 : 0;
                    float alpha = 0.95f;
                    drawDigitBillboard(pos, size, state, camRight, camUp, 1.0f, 0.95f, 0.2f, alpha);
                }
                else if (b == BlockType::Counter)
                {
                    uint8_t val = world.getPower(x, y, z);
                    int hundreds = (val / 100) % 10;
                    int tens = (val / 10) % 10;
                    int ones = val % 10;
                    float alpha = 0.95f;
                    float spacing = size * 0.8f; // slight extra gap between digits
                    auto offsetPos = [&](float mul)
                    {
                        return Vec3{pos.x + camRight.x * mul, pos.y + camRight.y * mul, pos.z + camRight.z * mul};
                    };
                    drawDigitBillboard(offsetPos(-spacing), size * 0.7f, hundreds, camRight, camUp, 1.0f, 1.0f, 1.0f,
                                       alpha);
                    // center digit: keep same axes but draw with a tiny offset to avoid overlap
                    drawDigitBillboard(offsetPos(0.0f), size * 0.7f, tens, camRight, camUp, 1.0f, 1.0f, 1.0f, alpha);
                    drawDigitBillboard(offsetPos(spacing), size * 0.7f, ones, camRight, camUp, 1.0f, 1.0f, 1.0f, alpha);
                }
            }
        }
    }
}
inline void drawSlotIcon(const ItemStack &slot, float x, float y, float slotSize)
{
    if (slot.count == 0)
        return;

    float cx = x + slotSize * 0.5f;
    float cy = y + slotSize * 0.5f;

    switch (slot.type)
    {
    case BlockType::Led:
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(x + slotSize * 0.25f, y + slotSize * 0.25f);
        glVertex2f(x + slotSize * 0.25f, y + slotSize * 0.75f);
        glVertex2f(x + slotSize * 0.65f, y + slotSize * 0.5f);
        glEnd();
        glBegin(GL_LINES);
        glVertex2f(x + slotSize * 0.05f, y + slotSize * 0.5f);
        glVertex2f(x + slotSize * 0.25f, y + slotSize * 0.5f);
        glVertex2f(x + slotSize * 0.65f, y + slotSize * 0.5f);
        glVertex2f(x + slotSize * 0.95f, y + slotSize * 0.5f);
        glVertex2f(x + slotSize * 0.72f, y + slotSize * 0.2f);
        glVertex2f(x + slotSize * 0.72f, y + slotSize * 0.8f);
        glEnd();
        glBegin(GL_LINES);
        glVertex2f(x + slotSize * 0.55f, y + slotSize * 0.35f);
        glVertex2f(x + slotSize * 0.4f, y + slotSize * 0.18f);
        glVertex2f(x + slotSize * 0.52f, y + slotSize * 0.32f);
        glVertex2f(x + slotSize * 0.42f, y + slotSize * 0.26f);
        glVertex2f(x + slotSize * 0.6f, y + slotSize * 0.2f);
        glVertex2f(x + slotSize * 0.45f, y + slotSize * 0.05f);
        glVertex2f(x + slotSize * 0.57f, y + slotSize * 0.17f);
        glVertex2f(x + slotSize * 0.47f, y + slotSize * 0.11f);
        glEnd();
        break;
    case BlockType::Button:
        drawQuad(cx - slotSize * 0.25f, cy - slotSize * 0.12f, slotSize * 0.5f, slotSize * 0.24f, 0.75f, 0.25f,
                 0.25f, 0.9f);
        drawOutline(cx - slotSize * 0.25f, cy - slotSize * 0.12f, slotSize * 0.5f, slotSize * 0.24f, 0.1f, 0.1f,
                    0.1f, 0.9f, 2.0f);
        break;
    case BlockType::OrGate:
    case BlockType::AndGate:
    case BlockType::XorGate:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.18f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        const char *label = "XOR";
        if (slot.type == BlockType::AndGate)
            label = "AND";
        else if (slot.type == BlockType::OrGate)
            label = "OR";
        float txtSize = 1.6f;
        float textWidth = static_cast<float>(std::strlen(label)) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.36f;
        drawTextTiny(textX, textY, txtSize, label, 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::AddGate:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float padAdd = slotSize * 0.18f;
        drawOutline(x + padAdd, y + padAdd, slotSize - padAdd * 2, slotSize - padAdd * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.4f;
        float labelWidth =
            static_cast<float>(std::strlen("ADD")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - labelWidth) * 0.5f;
        float textY = y + slotSize * 0.42f;
        drawTextTiny(textX, textY, txtSize, "ADD", 1.0f, 1.0f, 1.0f, 1.0f);
        drawTextTiny(x + padAdd + 2.0f, y + slotSize - padAdd - 8.0f, 1.1f, "P", 1.0f, 1.0f, 1.0f, 0.9f);
        drawTextTiny(x + slotSize - padAdd - 8.0f, y + slotSize - padAdd - 8.0f, 1.1f, "Q", 1.0f, 1.0f, 1.0f, 0.9f);
        drawTextTiny(x + (slotSize * 0.5f) - 4.0f, y + padAdd - 2.0f, 1.1f, "S", 1.0f, 1.0f, 1.0f, 0.9f);
        drawTextTiny(x + padAdd + 2.0f, y + slotSize * 0.28f, 1.0f, "Cin", 1.0f, 1.0f, 1.0f, 0.9f);
        drawTextTiny(x + slotSize * 0.54f, y + slotSize * 0.28f, 1.0f, "Cout", 1.0f, 1.0f, 1.0f, 0.9f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Wire:
        glColor4f(1.0f, 0.9f, 0.3f, 0.95f);
        glBegin(GL_LINE_STRIP);
        glVertex2f(x + slotSize * 0.15f, y + slotSize * 0.65f);
        glVertex2f(x + slotSize * 0.35f, y + slotSize * 0.55f);
        glVertex2f(x + slotSize * 0.55f, y + slotSize * 0.7f);
        glVertex2f(x + slotSize * 0.82f, y + slotSize * 0.4f);
        glEnd();
        break;
    case BlockType::NotGate:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float padNot = slotSize * 0.18f;
        drawOutline(x + padNot, y + padNot, slotSize - padNot * 2, slotSize - padNot * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.6f;
        float textWidth = static_cast<float>(std::strlen("NOT")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.4f;
        drawTextTiny(textX, textY, txtSize, "NOT", 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Counter:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float padCtr = slotSize * 0.25f;
        drawOutline(x + padCtr, y + padCtr, slotSize - padCtr * 2, slotSize - padCtr * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.6f;
        float textWidth =
            static_cast<float>(std::strlen("CNT")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.38f;
        drawTextTiny(textX, textY, txtSize, "CNT", 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Splitter:
    case BlockType::Merger:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.2f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        const char *label = slot.type == BlockType::Splitter ? "SPL" : "MER";
        float txtSize = 1.6f;
        float textWidth = static_cast<float>(std::strlen(label)) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.38f;
        drawTextTiny(textX, textY, txtSize, label, 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Multiplexer:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.2f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.4f;
        float textWidth = static_cast<float>(std::strlen("MUX")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.4f;
        drawTextTiny(textX, textY, txtSize, "MUX", 1.0f, 1.0f, 1.0f, 1.0f);
        drawTextTiny(x + pad + 2.0f, y + slotSize - pad - 10.0f, 1.0f, "SEL", 1.0f, 1.0f, 1.0f, 0.85f);
        drawTextTiny(x + slotSize - pad - 12.0f, y + slotSize - pad - 10.0f, 1.0f, "0", 1.0f, 1.0f, 1.0f, 0.85f);
        drawTextTiny(x + slotSize * 0.35f, y + pad - 4.0f, 1.0f, "OUT", 1.0f, 1.0f, 1.0f, 0.85f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Comparator:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.18f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.5f;
        float textWidth = static_cast<float>(std::strlen("CMP")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.38f;
        drawTextTiny(textX, textY, txtSize, "CMP", 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Clock:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.2f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.5f;
        float textWidth = static_cast<float>(std::strlen("CLK")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.38f;
        drawTextTiny(textX, textY, txtSize, "CLK", 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Decoder:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.2f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.5f;
        float textWidth = static_cast<float>(std::strlen("DEC")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.4f;
        drawTextTiny(textX, textY, txtSize, "DEC", 1.0f, 1.0f, 1.0f, 1.0f);
        drawTextTiny(x + pad + 2.0f, y + slotSize - pad - 10.0f, 1.0f, "SEL", 1.0f, 1.0f, 1.0f, 0.85f);
        drawTextTiny(x + slotSize - pad - 12.0f, y + slotSize - pad - 10.0f, 1.0f, "EN", 1.0f, 1.0f, 1.0f, 0.85f);
        drawTextTiny(x + slotSize * 0.35f, y + pad - 4.0f, 1.0f, "OUT", 1.0f, 1.0f, 1.0f, 0.85f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::DFlipFlop:
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float padDff = slotSize * 0.18f;
        drawOutline(x + padDff, y + padDff, slotSize - padDff * 2, slotSize - padDff * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        float txtSize = 1.4f;
        float labelWidth =
            static_cast<float>(std::strlen("DFF")) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - labelWidth) * 0.5f;
        float textY = y + slotSize * 0.42f;
        drawTextTiny(textX, textY, txtSize, "DFF", 1.0f, 1.0f, 1.0f, 1.0f);
        drawTextTiny(x + padDff + 2.0f, y + slotSize - padDff - 8.0f, 1.2f, "D", 1.0f, 1.0f, 1.0f, 0.9f);
        drawTextTiny(x + slotSize - padDff - 12.0f, y + slotSize - padDff - 8.0f, 1.2f, "CLK", 1.0f, 1.0f, 1.0f, 0.9f);
        drawTextTiny(x + (slotSize * 0.5f) - 4.0f, y + padDff - 2.0f, 1.2f, "Q", 1.0f, 1.0f, 1.0f, 0.9f);
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Sign:
    {
        // simple board + post icon
        float boardW = slotSize * 0.7f;
        float boardH = slotSize * 0.35f;
        float poleW = slotSize * 0.12f;
        float poleH = slotSize * 0.3f;

        float boardX = cx - boardW * 0.5f;
        float boardY = y + slotSize * 0.2f;
        float poleX = cx - poleW * 0.5f;
        float poleY = boardY + boardH;

        drawQuad(boardX, boardY, boardW, boardH, 0.85f, 0.7f, 0.45f, 0.9f);
        drawOutline(boardX, boardY, boardW, boardH, 0.1f, 0.07f, 0.03f, 0.9f, 2.0f);
        drawQuad(poleX, poleY, poleW, poleH, 0.6f, 0.45f, 0.25f, 0.9f);
        break;
    }
    case BlockType::Air:
        break;
    case BlockType::Grass:
    {
        // simple blades of grass (no background square)
        glColor4f(0.12f, 0.55f, 0.18f, 0.35f);
        drawQuad(x + slotSize * 0.1f, y + slotSize * 0.55f, slotSize * 0.8f, slotSize * 0.25f, 0.12f, 0.55f, 0.18f,
                 0.25f);
        glColor4f(0.1f, 0.9f, 0.3f, 0.95f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        float baseY = y + slotSize * 0.78f;
        for (int i = 0; i < 8; ++i)
        {
            float t = static_cast<float>(i) / 7.0f;
            float gx = x + slotSize * 0.15f + t * slotSize * 0.7f;
            float sway = (i % 2 == 0) ? -0.05f : 0.05f;
            glVertex2f(gx, baseY);
            glVertex2f(gx + slotSize * sway, baseY - slotSize * (0.25f + 0.1f * t));
        }
        glEnd();
        glLineWidth(1.0f);
        break;
    }
    case BlockType::Dirt:
    {
        // muddy dirt patch
        drawQuad(x + slotSize * 0.18f, y + slotSize * 0.45f, slotSize * 0.64f, slotSize * 0.28f, 0.36f, 0.23f, 0.12f,
                 0.8f);
        drawQuad(x + slotSize * 0.25f, y + slotSize * 0.52f, slotSize * 0.3f, slotSize * 0.12f, 0.28f, 0.18f, 0.1f,
                 0.8f);
        drawQuad(x + slotSize * 0.52f, y + slotSize * 0.42f, slotSize * 0.16f, slotSize * 0.08f, 0.22f, 0.14f, 0.08f,
                 0.75f);
        break;
    }
    case BlockType::Stone:
    {
        // brick-like pattern without solid square
        glColor4f(0.6f, 0.6f, 0.65f, 0.25f);
        glBegin(GL_LINES);
        // horizontal courses
        float y1 = y + slotSize * 0.35f;
        float y2 = y + slotSize * 0.6f;
        glVertex2f(x + slotSize * 0.1f, y1);
        glVertex2f(x + slotSize * 0.9f, y1);
        glVertex2f(x + slotSize * 0.1f, y2);
        glVertex2f(x + slotSize * 0.9f, y2);
        // vertical offsets (staggered)
        glVertex2f(x + slotSize * 0.3f, y1);
        glVertex2f(x + slotSize * 0.3f, y);
        glVertex2f(x + slotSize * 0.7f, y1);
        glVertex2f(x + slotSize * 0.7f, y);
        glVertex2f(x + slotSize * 0.2f, y2);
        glVertex2f(x + slotSize * 0.2f, y1);
        glVertex2f(x + slotSize * 0.6f, y2);
        glVertex2f(x + slotSize * 0.6f, y1);
        glVertex2f(x + slotSize * 0.9f, y2);
        glVertex2f(x + slotSize * 0.9f, y1);
        glEnd();
        glColor4f(0.25f, 0.25f, 0.3f, 0.4f);
        glBegin(GL_LINES);
        glVertex2f(x + slotSize * 0.1f, y);
        glVertex2f(x + slotSize * 0.1f, y + slotSize);
        glVertex2f(x + slotSize * 0.9f, y);
        glVertex2f(x + slotSize * 0.9f, y + slotSize);
        glEnd();
        break;
    }
    case BlockType::Wood:
    {
        // simple tree trunk + side log, no background square
        float trunkW = slotSize * 0.28f;
        float trunkH = slotSize * 0.62f;
        float trunkX = x + slotSize * 0.22f;
        float trunkY = y + slotSize * 0.25f;
        drawQuad(trunkX, trunkY, trunkW, trunkH, 0.45f, 0.32f, 0.16f, 0.9f);
        glColor4f(0.2f, 0.12f, 0.07f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(trunkX + trunkW * 0.33f, trunkY);
        glVertex2f(trunkX + trunkW * 0.33f, trunkY + trunkH);
        glVertex2f(trunkX + trunkW * 0.66f, trunkY);
        glVertex2f(trunkX + trunkW * 0.66f, trunkY + trunkH);
        glEnd();

        // side log with rings
        float logX = x + slotSize * 0.55f;
        float logY = y + slotSize * 0.35f;
        float logW = slotSize * 0.28f;
        float logH = slotSize * 0.2f;
        drawQuad(logX, logY, logW, logH, 0.55f, 0.4f, 0.22f, 0.9f);
        glColor4f(0.25f, 0.16f, 0.09f, 0.9f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(logX, logY);
        glVertex2f(logX + logW, logY);
        glVertex2f(logX + logW, logY + logH);
        glVertex2f(logX, logY + logH);
        glEnd();
        glBegin(GL_LINES);
        glVertex2f(logX + logW * 0.25f, logY);
        glVertex2f(logX + logW * 0.25f, logY + logH);
        glVertex2f(logX + logW * 0.55f, logY);
        glVertex2f(logX + logW * 0.55f, logY + logH);
        glEnd();
        break;
    }
    case BlockType::Leaves:
    {
        float pad = slotSize * 0.18f;
        drawQuad(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 0.2f, 0.55f, 0.25f, 0.9f);
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 0.0f, 0.0f, 0.0f, 0.35f, 2.0f);
        drawQuad(x + slotSize * 0.2f, y + slotSize * 0.2f, slotSize * 0.6f, slotSize * 0.6f, 0.18f, 0.46f, 0.2f, 0.4f);
        break;
    }
    case BlockType::Water:
    {
        drawQuad(x, y, slotSize, slotSize, 0.1f, 0.35f, 0.85f, 0.55f);
        drawOutline(x, y, slotSize, slotSize, 0.2f, 0.6f, 1.0f, 0.4f, 2.0f);
        glColor4f(0.6f, 0.8f, 1.0f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(x + slotSize * 0.2f, y + slotSize * 0.35f);
        glVertex2f(x + slotSize * 0.8f, y + slotSize * 0.35f);
        glVertex2f(x + slotSize * 0.25f, y + slotSize * 0.55f);
        glVertex2f(x + slotSize * 0.75f, y + slotSize * 0.55f);
        glEnd();
        break;
    }
    case BlockType::Plank:
    {
        float pad = slotSize * 0.18f;
        drawQuad(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 0.75f, 0.6f, 0.35f, 0.9f);
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 0.0f, 0.0f, 0.0f, 0.35f, 2.0f);
        glColor4f(0.35f, 0.25f, 0.15f, 0.7f);
        glBegin(GL_LINES);
        glVertex2f(x + slotSize * 0.2f, y + slotSize * 0.4f);
        glVertex2f(x + slotSize * 0.8f, y + slotSize * 0.4f);
        glVertex2f(x + slotSize * 0.2f, y + slotSize * 0.6f);
        glVertex2f(x + slotSize * 0.8f, y + slotSize * 0.6f);
        glEnd();
        break;
    }
    case BlockType::Sand:
    {
        float pad = slotSize * 0.18f;
        drawQuad(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 0.88f, 0.78f, 0.48f, 0.9f);
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 0.0f, 0.0f, 0.0f, 0.35f, 2.0f);
        drawQuad(x + slotSize * 0.3f, y + slotSize * 0.6f, slotSize * 0.15f, slotSize * 0.08f, 0.8f, 0.7f, 0.4f, 0.6f);
        drawQuad(x + slotSize * 0.55f, y + slotSize * 0.4f, slotSize * 0.18f, slotSize * 0.1f, 0.8f, 0.7f, 0.4f, 0.6f);
        break;
    }
    case BlockType::Glass:
    {
        drawQuad(x, y, slotSize, slotSize, 0.7f, 0.9f, 1.0f, 0.18f);
        drawOutline(x, y, slotSize, slotSize, 0.8f, 0.95f, 1.0f, 0.4f, 2.0f);
        glColor4f(0.4f, 0.7f, 0.9f, 0.5f);
        glBegin(GL_LINES);
        glVertex2f(x + slotSize * 0.2f, y + slotSize * 0.2f);
        glVertex2f(x + slotSize * 0.8f, y + slotSize * 0.8f);
        glEnd();
        break;
    }
    default:
        break;
    }
}

void drawSlotBox(float x, float y, float slotSize, const ItemStack &slot, bool selected, bool hovered)
{
    auto col = BLOCKS.at(slot.count > 0 ? slot.type : BlockType::Air).color;
    float alpha = slot.count > 0 ? 0.95f : 0.25f;
    drawQuad(x - 3.0f, y - 3.0f, slotSize + 6.0f, slotSize + 6.0f, 0.0f, 0.0f, 0.0f, 0.28f);
    drawQuad(x - 1.0f, y - 1.0f, slotSize + 2.0f, slotSize + 2.0f, 0.08f, 0.08f, 0.12f, 0.55f);
    drawOutline(x - 4.0f, y - 4.0f, slotSize + 8.0f, slotSize + 8.0f, 1.0f, 1.0f, 1.0f, 0.12f, 2.0f);
    drawQuad(x, y, slotSize, slotSize, col[0], col[1], col[2], alpha);
    drawQuad(x, y, slotSize, slotSize * 0.5f, 1.0f, 1.0f, 1.0f, 0.06f);
    drawOutline(x, y, slotSize, slotSize, 0.0f, 0.0f, 0.0f, 0.5f, 2.0f);
    drawSlotIcon(slot, x, y, slotSize);
    if (slot.count > 0)
        drawNumber(x + slotSize - 4.0f, y + slotSize - 16.0f, slot.count, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f);
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

constexpr float inventoryBarGap = 24.0f;

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
    float hbY = invY + invHeight + inventoryBarGap;
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

    float hbWidth = padding * 2 + static_cast<float>(hotbar.size()) * slotSize + (hotbar.size() + 1) * gap;
    float hbX = (winW - hbWidth) * 0.5f;
    float hbY = invY + invHeight + inventoryBarGap;
    float panelW = std::max(invWidth, hbWidth) + 32;
    float panelY = invY - 30;
    drawQuad(0, 0, winW, winH, 0,0,0,0.18f);
    menuPanel((winW-panelW)*0.5f, panelY, panelW, hbY+slotSize+2*padding-panelY, "INVENTAIRE");

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

    signText(hbX + padding + gap, hbY-9, 2, "RACCOURCIS");

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

void setup3D(SDL_Window *window, int w, int h)
{
    int pixelWidth, pixelHeight;
    SDL_GL_GetDrawableSize(window, &pixelWidth, &pixelHeight);
    glViewport(0, 0, pixelWidth, pixelHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, static_cast<double>(w) / static_cast<double>(h), 0.1, 500.0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
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

void updateTitle(SDL_Window *window)
{
    std::string title = "Logicraft";
    SDL_SetWindowTitle(window, title.c_str());
}

int main(int argc, char **argv)
{
    const int WIDTH = 96;
    const int HEIGHT = 48;
    const int DEPTH = 96;
    const float PLAYER_HEIGHT = 1.7f;
    const float EYE_HEIGHT = PLAYER_HEIGHT * 0.8f;
    const float SPEED = 32.0f;
    const float JUMP = 20.0f;
    const float GRAVITY = -48.0f;
    const float SPRINT_MULT = 1.6f;
    const float FLY_SPRINT_MULT = 2.4f;
    const float FLY_VERTICAL_MULT = 0.2f;
    const float SPRINT_DOUBLE_TAP = 0.3f;

    gResourceRoot = resolveResourceRoot(executableDirectory());
    gMapsDir = gResourceRoot / "maps";
    {
        std::error_code ec;
        std::filesystem::current_path(gResourceRoot, ec);
    }

    loadConfig(gConfig);

#if defined(_WIN32) && defined(SDL_HINT_WINDOWS_DPI_SCALING)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL init error: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow("Logicraft", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        std::cerr << "SDL window error: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    if (gConfig.fullscreenDefault)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx)
    {
        std::cerr << "OpenGL context error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(gConfig.vsync ? 1 : 0);

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
    GLuint titleImage = loadTextureFromBMP(assetPath("images/menu_title.bmp"), true);
    GLuint pauseImage = loadTextureFromBMP(assetPath("images/menu_pause.bmp"), true);
    if (!titleImage || !pauseImage)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Logicraft","Images du menu introuvables.",window);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    GLuint npcTexture = loadTextureFromBMP(assetPath("images/npc_head.bmp"));
    if (npcTexture == 0)
    {
        std::cerr << "Could not load NPC texture (images/npc_head.bmp). Using flat color.\n";
    }
    GLuint npcTextureAlt = loadTextureFromBMP(assetPath("images/npc_head_alt.bmp"));
    if (npcTextureAlt == 0)
    {
        std::cerr << "Could not load NPC texture (images/npc_head_alt.bmp). Using flat color.\n";
    }
    std::array<std::string, 6> skyboxPaths = {assetPath("images/skybox_night_right.bmp"),
                                              assetPath("images/skybox_night_left.bmp"),
                                              assetPath("images/skybox_night_top.bmp"),
                                              assetPath("images/skybox_night_bottom.bmp"),
                                              assetPath("images/skybox_night_front.bmp"),
                                              assetPath("images/skybox_night_back.bmp")};
    GLuint skyboxTex = loadCubemapFromBMP(skyboxPaths);
    if (skyboxTex == 0)
    {
        std::cerr << "Could not load skybox cubemap. It will be skipped.\n";
    }
    GLuint npcTextureAI = loadTextureFromBMP(assetPath("images/mon_npc.bmp"));
    if (npcTextureAI == 0)
    {
        std::cerr << "Could not load NPC texture (images/mon_npc.bmp). Using flat color.\n";
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

    NPC npc2 = npc;
    npc2.texture = npcTextureAlt ? npcTextureAlt : npcTexture;
    npc2.x = npcGridX - 3.5f;
    npc2.z = npcGridZ - 1.5f;
    npc2.y = world.surfaceY(static_cast<int>(npc2.x), static_cast<int>(npc2.z));

    NPC npc3 = npc;
    npc3.texture = npcTextureAI ? npcTextureAI : npcTexture;
    npc3.x = npcGridX + 4.0f;
    npc3.z = npcGridZ - 2.5f;
    npc3.y = world.surfaceY(static_cast<int>(npc3.x), static_cast<int>(npc3.z));

    int selected = 0;
    std::vector<ItemStack> hotbarSlots(HOTBAR.size());
    // start with empty hotbar
    for (auto &s : hotbarSlots)
        s = {};
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
    bool saveMenuOpen = false;
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
    float lastSpaceTap = -1.0f;
    bool sprinting = false;
    bool flying = false;
    SDL_SetRelativeMouseMode(gMainMenuOpen ? SDL_FALSE : SDL_TRUE);
    SDL_ShowCursor(gMainMenuOpen ? SDL_TRUE : SDL_FALSE);

    std::cout << "Commandes: WASD/ZQSD deplacement, souris pour la camera, clic gauche miner, clic droit placer, "
                 "1-8 changer de bloc, Space saut, Shift descendre, Double jump fly, R teleport to spawn, Esc menu pause/save/load.\n";

    int winW = 1280, winH = 720;
    SDL_GetWindowSize(window, &winW, &winH);
    setup3D(window, winW, winH);

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

        // Applique la souris lissée ici pour stabiliser la camera
        if (!inventoryOpen && !pauseMenuOpen && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen && !gMainMenuOpen && !gSettingsMenuOpen)
        {
            const float sensitivity = gConfig.mouseSensitivity;
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
                if (gMainMenuOpen)
                {
                    if (e.key.keysym.sym == SDLK_ESCAPE)
                    {
                        if (gSettingsMenuOpen)
                        {
                            gSettingsMenuOpen = false;
                        }
                        else
                        {
                            running = false;
                        }
                    }
                    else if (!gSettingsMenuOpen &&
                             (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE))
                    {
                        gMainMenuOpen = false;
                        gSettingsMenuOpen = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                        smoothDX = smoothDY = 0.0f;
                    }
                }
                else if (e.key.keysym.sym == SDLK_ESCAPE)
                {
                    if (gSignEditOpen)
                    {
                        gSignEditOpen = false;
                        SDL_StopTextInput();
                    }
                    else if (gButtonEditOpen)
                    {
                        gButtonEditOpen = false;
                        SDL_StopTextInput();
                    }
                    else if (gSplitterEditOpen)
                    {
                        gSplitterEditOpen = false;
                        SDL_StopTextInput();
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                    }
                    else if (gClockEditOpen)
                    {
                        gClockEditOpen = false;
                        SDL_StopTextInput();
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                    }
                    else if (gWireInfoOpen)
                    {
                        gWireInfoOpen = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                    }
                    else if (gSettingsMenuOpen)
                    {
                        gSettingsMenuOpen = false;
                    }
                    else if (pauseMenuOpen)
                    {
                        pauseMenuOpen = false;
                        saveMenuOpen = false;
                        gSettingsMenuOpen = false;
                        SDL_StopTextInput();
                        gSaveInputFocus = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                    }
                    else
                    {
                        pauseMenuOpen = true;
                        saveMenuOpen = false;
                        gSettingsMenuOpen = false;
                        refreshSaveList();
                        inventoryOpen = false;
                        pendingSlot = -1;
                        SDL_StopTextInput();
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                        smoothDX = smoothDY = 0.0f;
                    }
                }
                else if (e.key.keysym.sym == SDLK_e && !pauseMenuOpen && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen)
                {
                    inventoryOpen = !inventoryOpen;
                    pendingSlot = -1;
                    SDL_SetRelativeMouseMode(inventoryOpen ? SDL_FALSE : SDL_TRUE);
                    SDL_ShowCursor(inventoryOpen ? SDL_TRUE : SDL_FALSE);
                    smoothDX = smoothDY = 0.0f;
                }
                else if (e.key.keysym.sym == SDLK_F11)
                {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    bool isFullscreen =
                        (flags & SDL_WINDOW_FULLSCREEN) || (flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
                    if (!isFullscreen)
                    {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    else
                    {
                        SDL_SetWindowFullscreen(window, 0);
                    }
                }
                else if (e.key.keysym.sym == SDLK_r && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen)
                {
                    float spawnX = WIDTH * 0.5f;
                    float spawnZ = DEPTH * 0.5f;
                    int sx = static_cast<int>(std::floor(spawnX));
                    int sz = static_cast<int>(std::floor(spawnZ));
                    int sy = world.surfaceY(sx, sz);
                    if (sy < 0)
                        sy = 0;
                    // Find the first free spot above surface if blocked
                    int checkY = sy + 1;
                    auto blockedAt = [&](int y)
                    {
                        if (y < 0 || y + 1 >= world.getHeight())
                            return true;
                        return isSolid(world.get(sx, y, sz)) || isSolid(world.get(sx, y + 1, sz));
                    };
                    while (checkY < world.getHeight() - 2 && blockedAt(checkY))
                        ++checkY;
                    if (checkY >= world.getHeight() - 1)
                        checkY = world.getHeight() - 2;
                    player.x = spawnX;
                    player.z = spawnZ;
                    player.y = static_cast<float>(checkY) + 0.2f;
                }
                else if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_8 && !gSignEditOpen &&
                         !gButtonEditOpen && !gWireInfoOpen && !gSplitterEditOpen && !gClockEditOpen)
                {
                    selected = static_cast<int>(e.key.keysym.sym - SDLK_1);
                    if (selected >= static_cast<int>(hotbarSlots.size()))
                        selected = static_cast<int>(hotbarSlots.size()) - 1;
                }
                else if (saveMenuOpen && e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (!gSaveNameInput.empty())
                        gSaveNameInput.pop_back();
                }
                else if (gSignEditOpen && e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (!gSignEditBuffer.empty())
                        gSignEditBuffer.pop_back();
                }
                else if (gButtonEditOpen && e.key.keysym.sym == SDLK_TAB)
                {
                    gButtonEditingWidth = !gButtonEditingWidth;
                }
                else if (gSplitterEditOpen && e.key.keysym.sym == SDLK_TAB)
                {
                    gSplitterOrder = !gSplitterOrder;
                }
                else if (saveMenuOpen && e.key.keysym.sym == SDLK_TAB)
                {
                    std::string suggested = stemFromPath(timestampSaveName());
                    gSaveNameInput = normalizeSaveInput(suggested);
                }
                else if (gSignEditOpen && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                {
                    world.setSignText(gSignEditX, gSignEditY, gSignEditZ, gSignEditBuffer);
                    markChunkFromBlock(gSignEditX, gSignEditY, gSignEditZ);
                    gSignEditOpen = false;
                    SDL_StopTextInput();
                }
                else if (gSignEditOpen && e.key.keysym.sym == SDLK_ESCAPE)
                {
                    gSignEditOpen = false;
                    SDL_StopTextInput();
                }
                else if (gButtonEditOpen && e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (gButtonEditingWidth)
                    {
                        if (!gButtonWidthBuffer.empty())
                            gButtonWidthBuffer.pop_back();
                    }
                    else
                    {
                        if (!gButtonEditBuffer.empty())
                            gButtonEditBuffer.pop_back();
                    }
                }
                else if (gSplitterEditOpen && e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (!gSplitterWidthBuffer.empty())
                        gSplitterWidthBuffer.pop_back();
                }
                else if (gClockEditOpen && e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (!gClockEditBuffer.empty())
                        gClockEditBuffer.pop_back();
                }
                else if (gButtonEditOpen &&
                         (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                {
                    int v = 0;
                    int width = 8;
                    try
                    {
                        v = std::stoi(gButtonEditBuffer.empty() ? "0" : gButtonEditBuffer);
                    }
                    catch (...)
                    {
                        v = 0;
                    }
                    try
                    {
                        width = std::stoi(gButtonWidthBuffer.empty() ? "8" : gButtonWidthBuffer);
                    }
                    catch (...)
                    {
                        width = 8;
                    }
                    width = std::clamp(width, 1, 8);
                    v = std::clamp(v, 0, 255);
                    uint8_t mask = width >= 8 ? 0xFFu : static_cast<uint8_t>((1u << width) - 1u);
                    v &= mask;
                    world.setButtonWidth(gButtonEditX, gButtonEditY, gButtonEditZ, static_cast<uint8_t>(width));
                    world.setButtonValue(gButtonEditX, gButtonEditY, gButtonEditZ, static_cast<uint8_t>(v));
                    gButtonEditOpen = false;
                    SDL_StopTextInput();
                }
                else if (gSplitterEditOpen &&
                         (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                {
                    int width = 1;
                    try
                    {
                        width = std::stoi(gSplitterWidthBuffer.empty() ? "1" : gSplitterWidthBuffer);
                    }
                    catch (...)
                    {
                        width = 1;
                    }
                    width = std::clamp(width, 1, 7);
                    world.setSplitterWidth(gSplitterX, gSplitterY, gSplitterZ, static_cast<uint8_t>(width));
                    world.setSplitterOrder(gSplitterX, gSplitterY, gSplitterZ, gSplitterOrder ? 1 : 0);
                    gSplitterEditOpen = false;
                    SDL_StopTextInput();
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    SDL_ShowCursor(SDL_FALSE);
                }
                else if (gClockEditOpen &&
                         (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                {
                    int freq = 1;
                    try
                    {
                        freq = std::stoi(gClockEditBuffer.empty() ? "1" : gClockEditBuffer);
                    }
                    catch (...)
                    {
                        freq = 1;
                    }
                    freq = std::clamp(freq, 1, 255);
                    world.setClockFreq(gClockEditX, gClockEditY, gClockEditZ, static_cast<uint8_t>(freq));
                    gClockEditOpen = false;
                    SDL_StopTextInput();
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    SDL_ShowCursor(SDL_FALSE);
                }
                else if (gButtonEditOpen && e.key.keysym.sym == SDLK_ESCAPE)
                {
                    gButtonEditOpen = false;
                    SDL_StopTextInput();
                }
                else if (gSplitterEditOpen && e.key.keysym.sym == SDLK_ESCAPE)
                {
                    gSplitterEditOpen = false;
                    SDL_StopTextInput();
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    SDL_ShowCursor(SDL_FALSE);
                }
                else if (gClockEditOpen && e.key.keysym.sym == SDLK_ESCAPE)
                {
                    gClockEditOpen = false;
                    SDL_StopTextInput();
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    SDL_ShowCursor(SDL_FALSE);
                }
                else if (!gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen && !pauseMenuOpen &&
                         e.key.keysym.sym == SDLK_SPACE && e.key.repeat == 0)
                {
                    if (lastSpaceTap >= 0.0f && (elapsedTime - lastSpaceTap) <= SPRINT_DOUBLE_TAP)
                    {
                        flying = !flying;
                        if (flying)
                            player.vy = 0.0f;
                    }
                    lastSpaceTap = elapsedTime;
                }
                else if (!gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen &&
                         (e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_z) && e.key.repeat == 0)
                {
                    if (lastForwardTap >= 0.0f && (elapsedTime - lastForwardTap) <= SPRINT_DOUBLE_TAP)
                    {
                        sprinting = true;
                    }
                    lastForwardTap = elapsedTime;
                }
                else if (e.key.keysym.sym == SDLK_q && !inventoryOpen && !pauseMenuOpen && !gSignEditOpen &&
                         !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen)
                {
                    Vec3 fwd = forwardVec(player.yaw, player.pitch);
                    float eyeY = player.y + EYE_HEIGHT;
                    HitInfo hit = raycast(world, player.x, eyeY, player.z, fwd.x, fwd.y, fwd.z, 8.0f);
                    if (hit.hit && world.get(hit.x, hit.y, hit.z) == BlockType::Button)
                    {
                        gButtonEditOpen = true;
                        gButtonEditX = hit.x;
                        gButtonEditY = hit.y;
                        gButtonEditZ = hit.z;
                        gButtonEditBuffer = std::to_string(world.getButtonValue(hit.x, hit.y, hit.z));
                        gButtonWidthBuffer = std::to_string(world.getButtonWidth(hit.x, hit.y, hit.z));
                        gButtonEditingWidth = false;
                        SDL_StartTextInput();
                    }
                    else if (hit.hit &&
                             (world.get(hit.x, hit.y, hit.z) == BlockType::Splitter ||
                              world.get(hit.x, hit.y, hit.z) == BlockType::Merger))
                    {
                        gSplitterEditOpen = true;
                        gSplitterIsMerger = (world.get(hit.x, hit.y, hit.z) == BlockType::Merger);
                        gSplitterX = hit.x;
                        gSplitterY = hit.y;
                        gSplitterZ = hit.z;
                        gSplitterWidthBuffer = std::to_string(world.getSplitterWidth(hit.x, hit.y, hit.z));
                        gSplitterOrder = world.getSplitterOrder(hit.x, hit.y, hit.z) != 0;
                        SDL_StartTextInput();
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                    }
                    else if (hit.hit && world.get(hit.x, hit.y, hit.z) == BlockType::Wire)
                    {
                        gWireInfoOpen = true;
                        gWireInfoX = hit.x;
                        gWireInfoY = hit.y;
                        gWireInfoZ = hit.z;
                        gWireInfoWidth = world.getPowerWidth(hit.x, hit.y, hit.z);
                        if (gWireInfoWidth == 0)
                            gWireInfoWidth = 8;
                        uint8_t mask = gWireInfoWidth >= 8 ? 0xFFu : static_cast<uint8_t>((1u << gWireInfoWidth) - 1u);
                        gWireInfoValue = static_cast<uint8_t>(world.getPower(hit.x, hit.y, hit.z) & mask);
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                    }
                    else if (hit.hit && world.get(hit.x, hit.y, hit.z) == BlockType::Clock)
                    {
                        gClockEditOpen = true;
                        gClockEditX = hit.x;
                        gClockEditY = hit.y;
                        gClockEditZ = hit.z;
                        gClockEditBuffer = std::to_string(world.getClockFreq(hit.x, hit.y, hit.z));
                        SDL_StartTextInput();
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                    }
                }
            }
            else if (e.type == SDL_MOUSEMOTION)
            {
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                if (!inventoryOpen && !pauseMenuOpen && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen && !gMainMenuOpen && !gSettingsMenuOpen)
                {
                    // Filtre de la souris pour lisser les mouvements et limiter les saccades
                    smoothDX = smoothDX * 0.6f + static_cast<float>(e.motion.xrel) * 0.4f;
                    smoothDY = smoothDY * 0.6f + static_cast<float>(e.motion.yrel) * 0.4f;
                }
            }
            else if (e.type == SDL_MOUSEWHEEL)
            {
                if (saveMenuOpen)
                {
                    int delta = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
                    saveOffset = std::clamp(saveOffset-delta, 0, std::max(0, static_cast<int>(gSaveList.size())-saveRows));
                    continue;
                }
                if (gSplitterEditOpen || gButtonEditOpen || gSignEditOpen || gWireInfoOpen || gClockEditOpen)
                    continue;
                if (!inventoryOpen && !pauseMenuOpen && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen && !gMainMenuOpen && !gSettingsMenuOpen)
                {
                    if (e.wheel.y > 0)
                    {
                        // scroll up: previous slot (wrap)
                        if (selected <= 0)
                            selected = static_cast<int>(hotbarSlots.size()) - 1;
                        else
                            --selected;
                    }
                    else if (e.wheel.y < 0)
                    {
                        // scroll down: next slot (wrap)
                        selected = (selected + 1) % static_cast<int>(hotbarSlots.size());
                    }
                }
            }
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                winW = e.window.data1;
                winH = e.window.data2;
                setup3D(window, winW, winH);
            }
            else if (e.type == SDL_TEXTINPUT)
            {
                if (saveMenuOpen)
                {
                    const char *txt = e.text.text;
                    for (int i = 0; txt[i] != '\0'; ++i)
                    {
                        char c = txt[i];
                        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                        {
                            if (gSaveNameInput.size() < 32)
                                gSaveNameInput.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                        }
                    }
                }
                else if (gSignEditOpen)
                {
                    const char *txt = e.text.text;
                    for (int i = 0; txt[i] != '\0'; ++i)
                    {
                        char c = txt[i];
                        if (c >= 32 && c <= 126 && gSignEditBuffer.size() < 48)
                            gSignEditBuffer.push_back(c);
                    }
                }
                else if (gButtonEditOpen)
                {
                    const char *txt = e.text.text;
                    for (int i = 0; txt[i] != '\0'; ++i)
                    {
                        char c = txt[i];
                        if (c >= '0' && c <= '9')
                        {
                            if (gButtonEditingWidth)
                            {
                                if (gButtonWidthBuffer.size() < 2)
                                    gButtonWidthBuffer.push_back(c);
                            }
                            else
                            {
                                if (gButtonEditBuffer.size() < 3)
                                    gButtonEditBuffer.push_back(c);
                            }
                        }
                    }
                }
                else if (gSplitterEditOpen)
                {
                    const char *txt = e.text.text;
                    for (int i = 0; txt[i] != '\0'; ++i)
                    {
                        char c = txt[i];
                        if (c >= '0' && c <= '9')
                        {
                            if (gSplitterWidthBuffer.size() < 2)
                                gSplitterWidthBuffer.push_back(c);
                        }
                    }
                }
                else if (gClockEditOpen)
                {
                    const char *txt = e.text.text;
                    for (int i = 0; txt[i] != '\0'; ++i)
                    {
                        char c = txt[i];
                        if (c >= '0' && c <= '9')
                        {
                            if (gClockEditBuffer.size() < 3)
                                gClockEditBuffer.push_back(c);
                        }
                    }
                }
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (gMainMenuOpen)
                {

                    if (gSettingsMenuOpen)
                    {
                        SettingsMenuLayout s = computeSettingsLayout(winW, winH);
                        bool hoverMinus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensMinusX, s.sensMinusY, s.sensMinusW, s.sensMinusH);
                        bool hoverPlus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensPlusX, s.sensPlusY, s.sensPlusW, s.sensPlusH);
                        bool hoverFps = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fpsBoxX, s.fpsBoxY, s.fpsBoxW, s.fpsBoxH);
                        bool hoverFullscreen = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fullscreenBoxX, s.fullscreenBoxY, s.fullscreenBoxW, s.fullscreenBoxH);
                        bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.backX, s.backY, s.backW, s.backH);
                        if (hoverMinus)
                        {
                            gConfig.mouseSensitivity = std::max(0.002f, gConfig.mouseSensitivity - 0.001f);
                            saveConfig(gConfig);
                        }
                        else if (hoverPlus)
                        {
                            gConfig.mouseSensitivity = std::min(0.05f, gConfig.mouseSensitivity + 0.001f);
                            saveConfig(gConfig);
                        }
                        else if (hoverFps)
                        {
                            gConfig.showFps = !gConfig.showFps;
                            saveConfig(gConfig);
                        }
                        else if (hoverFullscreen)
                        {
                            gConfig.fullscreenDefault = !gConfig.fullscreenDefault;
                            saveConfig(gConfig);
                            SDL_SetWindowFullscreen(window, gConfig.fullscreenDefault ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        }
                        else if (hoverBack)
                        {
                            gSettingsMenuOpen = false;
                        }
                        continue;
                    }
                    if (e.button.button != SDL_BUTTON_LEFT) continue;
                    int sign = MenuImageLayout(winW,winH).signAt(e.button.x,e.button.y,false);
                    if (sign == 0)
                    {
                        gMainMenuOpen = false;
                        gSettingsMenuOpen = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                        smoothDX = smoothDY = 0.0f;
                    }
                    else if (sign == 1)
                    {
                        gSettingsMenuOpen = true;
                    }
                    else if (sign == 2)
                    {
                        running = false;
                    }
                    continue;
                }
                if (gSignEditOpen)
                    continue;
                if (gSplitterEditOpen)
                    continue;
                if (gButtonEditOpen)
                    continue;
                if (gButtonEditOpen)
                    continue;
                if (gClockEditOpen)
                    continue;
                if (pauseMenuOpen)
                {
                    if (gSettingsMenuOpen)
                    {
                        SettingsMenuLayout s = computeSettingsLayout(winW, winH);
                        bool hoverMinus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensMinusX, s.sensMinusY, s.sensMinusW, s.sensMinusH);
                        bool hoverPlus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensPlusX, s.sensPlusY, s.sensPlusW, s.sensPlusH);
                        bool hoverFps = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fpsBoxX, s.fpsBoxY, s.fpsBoxW, s.fpsBoxH);
                        bool hoverFullscreen = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fullscreenBoxX, s.fullscreenBoxY, s.fullscreenBoxW, s.fullscreenBoxH);
                        bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.backX, s.backY, s.backW, s.backH);
                        if (hoverMinus)
                        {
                            gConfig.mouseSensitivity = std::max(0.002f, gConfig.mouseSensitivity - 0.001f);
                            saveConfig(gConfig);
                        }
                        else if (hoverPlus)
                        {
                            gConfig.mouseSensitivity = std::min(0.05f, gConfig.mouseSensitivity + 0.001f);
                            saveConfig(gConfig);
                        }
                        else if (hoverFps)
                        {
                            gConfig.showFps = !gConfig.showFps;
                            saveConfig(gConfig);
                        }
                        else if (hoverFullscreen)
                        {
                            gConfig.fullscreenDefault = !gConfig.fullscreenDefault;
                            saveConfig(gConfig);
                            SDL_SetWindowFullscreen(window, gConfig.fullscreenDefault ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        }
                        else if (hoverBack)
                        {
                            gSettingsMenuOpen = false;
                        }
                        continue;
                    }
                    if (saveMenuOpen)
                    {
                        SaveMenuLayout sm = computeSaveMenuLayout(winW, winH);
                        bool hoverCreate =
                            pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.createX, sm.createY,
                                        sm.createW, sm.createH);
                        bool hoverOverwrite =
                            pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.overwriteX, sm.overwriteY,
                                        sm.overwriteW, sm.overwriteH);
                        bool hoverLoad = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.loadX,
                                                     sm.loadY, sm.loadW, sm.loadH);
                        bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.backX,
                                                     sm.backY, sm.backW, sm.backH);

                        if (pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.listX, sm.listY,
                                        sm.listW, sm.listH))
                        {
                            float relY = static_cast<float>(mouseY) - sm.listY;
                            int idx = saveOffset + static_cast<int>(relY / saveRowHeight);
                            if (idx >= 0 && idx < static_cast<int>(gSaveList.size()))
                                gSaveIndex = idx;
                            gSaveInputFocus = false;
                        }
                        // focus input on click inside field
                        if (pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.inputX, sm.inputY,
                                        sm.inputW, sm.inputH))
                        {
                            SDL_StartTextInput();
                            gSaveInputFocus = true;
                        }
                        else
                        {
                            gSaveInputFocus = false;
                        }

                        if (hoverCreate)
                        {
                            std::string path = buildSavePathFromInput(gSaveNameInput);
                            std::error_code lookupError;
                            if (std::filesystem::exists(path, lookupError))
                            {
                                saveStatus = "CE NOM EXISTE DEJA";
                                continue;
                            }
                            bool ok = saveWorldToFile(world, path, seed);
                            saveStatus = ok ? "CARTE ENREGISTREE" : "ECHEC DE LA SAUVEGARDE";
                            std::cout << (ok ? "Sauvegarde OK: " : "Sauvegarde KO: ") << path << "\n";
                            if (ok)
                            {
                                refreshSaveList();
                                int idx = findSaveIndexByStem(stemFromPath(path));
                                if (idx >= 0)
                                {
                                    gSaveIndex = idx;
                                    saveOffset = std::max(0, idx-saveRows+1);
                                }
                                gSaveNameInput = normalizeSaveInput(stemFromPath(path));
                                gSaveInputFocus = false;
                            }
                        }
                        else if (hoverOverwrite && gSaveIndex >= 0)
                        {
                            std::string path;
                            if (gSaveIndex >= 0 && gSaveIndex < static_cast<int>(gSaveList.size()))
                                path = gSaveList[gSaveIndex];
                            else
                                path = timestampSaveName();
                            bool ok = saveWorldToFile(world, path, seed);
                            saveStatus = ok ? "CARTE ENREGISTREE" : "ECHEC DE LA SAUVEGARDE";
                            std::cout << (ok ? "Sauvegarde OK: " : "Sauvegarde KO: ") << path << "\n";
                            if (ok)
                            {
                                refreshSaveList();
                                if (!gSaveList.empty())
                                    gSaveIndex = 0;
                            }
                        }
                        else if (hoverLoad)
                        {
                            if (gSaveIndex >= 0 && gSaveIndex < static_cast<int>(gSaveList.size()))
                            {
                                std::string path = gSaveList[gSaveIndex];
                                uint32_t newSeed = seed;
                                bool ok = loadWorldFromFile(world, path, newSeed);
                                if (ok)
                                {
                                    markAllChunksDirty();
                                    seed = newSeed;
                                    player.x = WIDTH * 0.5f;
                                    player.z = DEPTH * 0.5f;
                                    player.y =
                                        world.surfaceY(static_cast<int>(player.x), static_cast<int>(player.z)) + 0.2f;
                                }
                                saveStatus = ok ? "CARTE CHARGEE" : "ECHEC DU CHARGEMENT";
                                std::cout << (ok ? "Chargement OK: " : "Chargement KO: ") << path << "\n";
                            }
                        }
                        else if (hoverBack)
                        {
                            saveMenuOpen = false;
                            SDL_StopTextInput();
                        }
                    }
                    else
                    {
                        if (e.button.button != SDL_BUTTON_LEFT) continue;
                        int sign = MenuImageLayout(winW,winH).signAt(e.button.x,e.button.y,true);
                        if (sign == 3)
                        {
                            running = false;
                        }
                        else if (sign == 2)
                        {
                            gSettingsMenuOpen = true;
                            saveMenuOpen = false;
                        }
                        else if (sign == 1)
                        {
                            saveMenuOpen = true;
                            saveStatus.clear();
                            SDL_StartTextInput();
                            refreshSaveList();
                            if (gSaveNameInput.empty())
                                gSaveNameInput = normalizeSaveInput(stemFromPath(timestampSaveName()));
                            gSaveInputFocus = true;
                        }
                        else if (sign == 0)
                        {
                            pauseMenuOpen = false;
                            saveMenuOpen = false;
                            SDL_StopTextInput();
                            SDL_SetRelativeMouseMode(SDL_TRUE);
                            SDL_ShowCursor(SDL_FALSE);
                            smoothDX = smoothDY = 0.0f;
                        }
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
                            if (target == BlockType::Sign)
                            {
                                gSignEditOpen = true;
                                gSignEditX = hit.x;
                                gSignEditY = hit.y;
                                gSignEditZ = hit.z;
                                gSignEditBuffer = world.getSignText(hit.x, hit.y, hit.z);
                                SDL_StartTextInput();
                            }
                            else if (target == BlockType::Button)
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

        float simDt = (pauseMenuOpen || gSignEditOpen || gButtonEditOpen || gSplitterEditOpen || gWireInfoOpen || gClockEditOpen || gMainMenuOpen || gSettingsMenuOpen) ? 0.0f : dt;

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        // Movement uses a purely horizontal forward vector (independent of pitch)
        Vec3 fwd{std::sin(player.yaw), 0.0f, -std::cos(player.yaw)};
        Vec3 right{std::cos(player.yaw), 0.0f, std::sin(player.yaw)};
        float moveSpeed = SPEED * (sprinting ? (flying ? FLY_SPRINT_MULT : SPRINT_MULT) : 1.0f);
        bool forwardHeld = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_Z];

        if (!inventoryOpen && !pauseMenuOpen && !gSignEditOpen)
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
            if (keys[SDL_SCANCODE_A])
            {
                player.vx -= right.x * moveSpeed * simDt;
                player.vz -= right.z * moveSpeed * simDt;
            }
            if (keys[SDL_SCANCODE_D])
            {
                player.vx += right.x * moveSpeed * simDt;
                player.vz += right.z * moveSpeed * simDt;
            }

            if (flying)
            {
                // Vol : Espace monte, Shift descend, sans gravité
                player.vy = 0.0f;
                float flyVertSpeed = moveSpeed * FLY_VERTICAL_MULT;
                if (keys[SDL_SCANCODE_SPACE])
                {
                    player.vy = flyVertSpeed;
                }
                else if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
                {
                    player.vy = -flyVertSpeed;
                }
            }
            else
            {
                // Sol : saut unique tant qu'on est au sol (pas de montée infinie en maintenant Espace)
                bool onGround =
                    collidesAt(world, player.x, player.y - 0.05f, player.z, PLAYER_HEIGHT);
                if (keys[SDL_SCANCODE_SPACE] && onGround)
                {
                    player.vy = JUMP;
                }
                if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
                {
                    player.vy = -JUMP;
                }
            }
        }
        if (!forwardHeld || inventoryOpen || pauseMenuOpen || gSignEditOpen || gButtonEditOpen || gSplitterEditOpen || gWireInfoOpen || gClockEditOpen)
        {
            sprinting = false;
        }
        if (!flying)
            player.vy += GRAVITY * simDt;

        float nextY = player.y + player.vy * simDt;
        nextY = std::clamp(nextY, PLAYER_HEIGHT * 0.5f, HEIGHT - 2.0f);
        bool hitVertical = collidesAt(world, player.x, nextY, player.z, PLAYER_HEIGHT);
        if (hitVertical)
        {
            if (player.vy < 0.0f)
            {
                nextY = std::floor(player.y) + 0.001f;
            }
            else if (player.vy > 0.0f)
            {
                // clamp just below the ceiling block the head hit
                nextY = std::ceil(player.y + PLAYER_HEIGHT) - PLAYER_HEIGHT - 0.001f;
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

        // If we touched the ground while flying, disable fly mode
        bool onGroundNow = collidesAt(world, player.x, player.y - 0.05f, player.z, PLAYER_HEIGHT);
        if (flying && onGroundNow)
        {
            flying = false;
            player.vy = 0.0f;
        }

        // Frame-rate independent damping (was frame-based 0.85)
        float damp = std::pow(0.85f, simDt / (1.0f / 60.0f));
        player.vx *= damp;
        player.vy *= damp;
        player.vz *= damp;

        updateNpc(npc, world, simDt);
        updateNpc(npc2, world, simDt);
        updateNpc(npc3, world, simDt);
        if (gMainMenuOpen || pauseMenuOpen)
        {
            glClearColor(0.025f,0.025f,0.025f,1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            beginHud(winW,winH);
            MenuImageLayout layout(winW,winH);
            int hovered = gSettingsMenuOpen || saveMenuOpen ? -1 : layout.signAt(mouseX,mouseY,pauseMenuOpen);
            drawMenuImage(layout, pauseMenuOpen ? pauseImage : titleImage, hovered);
            if (gSettingsMenuOpen)
            {
                SettingsMenuLayout s = computeSettingsLayout(winW, winH);
                bool hoverMinus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensMinusX,
                                              s.sensMinusY, s.sensMinusW, s.sensMinusH);
                bool hoverPlus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensPlusX, s.sensPlusY,
                                             s.sensPlusW, s.sensPlusH);
                bool hoverFps = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fpsBoxX, s.fpsBoxY,
                                            s.fpsBoxW, s.fpsBoxH);
                bool hoverFullscreen =
                    pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fullscreenBoxX, s.fullscreenBoxY,
                                s.fullscreenBoxW, s.fullscreenBoxH);
                bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.backX, s.backY, s.backW,
                                             s.backH);
                drawSettingsMenu(winW, winH, s, gConfig, hoverBack, hoverMinus, hoverPlus, hoverFps, hoverFullscreen);
            }
            else if (saveMenuOpen)
            {
                SaveMenuLayout sm = computeSaveMenuLayout(winW, winH);
                bool hoverOverwrite = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.overwriteX,
                                                  sm.overwriteY, sm.overwriteW, sm.overwriteH);
                bool hoverLoad = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.loadX, sm.loadY,
                                             sm.loadW, sm.loadH);
                bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.backX, sm.backY,
                                             sm.backW, sm.backH);
                bool hoverCreate =
                    pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), sm.createX, sm.createY, sm.createW,
                                sm.createH);
                drawSaveMenu(winW, winH, sm, gSaveIndex, hoverCreate, hoverOverwrite, hoverLoad, hoverBack, gSaveNameInput);
            }
            endHud();
            SDL_GL_SwapWindow(window);
            updateTitle(window);
            continue;
        }

        updateLogic(world, markChunkFromBlock);
        glClearColor(0.55f, 0.75f, 0.95f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        Vec3 fwdView = forwardVec(player.yaw, player.pitch);
        gluLookAt(player.x, player.y+EYE_HEIGHT, player.z, player.x+fwdView.x,
                  player.y+EYE_HEIGHT+fwdView.y, player.z+fwdView.z, 0,1,0);

        drawSkybox(skyboxTex, 160.0f);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gAtlasTex);
        const float chunkView = 56.0f;
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
        // Glass pass
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (int cY = 0; cY < CHUNK_Y_COUNT; ++cY)
        {
            for (int cZ = 0; cZ < CHUNK_Z_COUNT; ++cZ)
            {
                for (int cX = 0; cX < CHUNK_X_COUNT; ++cX)
                {
                    int idx = chunkIndex(cX, cY, cZ);
                    if (idx < 0)
                        continue;
                    const ChunkMesh &cm = chunkMeshes[idx];
                    if (cm.glassVerts.empty())
                        continue;
                    glBindBuffer(GL_ARRAY_BUFFER, cm.glassVbo);
                    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(0));
                    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(sizeof(float) * 3));
                    glColorPointer(3, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(sizeof(float) * 5));
                    glDrawArrays(GL_QUADS, 0, static_cast<GLsizei>(cm.glassVerts.size()));
                }
            }
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

        // NPC blocky models
        drawNpcBlocky(npc);
        drawNpcBlocky(npc2);
        drawNpcBlocky(npc3);

        // bouton 0/1 affiché directement sur le bloc proche du joueur
        drawButtonStateLabels(world, player, 10.0f);

        Vec3 fwdCast = forwardVec(player.yaw, player.pitch);
        float eyeY = player.y + EYE_HEIGHT;
        HitInfo hit = raycast(world, player.x, eyeY, player.z, fwdCast.x, fwdCast.y, fwdCast.z, 8.0f);
        HoverLabel hoverLabel;
        if (hit.hit)
        {
            drawFaceHighlight(hit);
        }

        beginHud(winW, winH);
        if (gConfig.showFps)
        {
            char fpsBuf[32];
            std::snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.0f", fps);
            drawQuad(10.0f, 10.0f, 120.0f, 32.0f, 0.04f, 0.04f, 0.06f, 0.65f);
            drawOutline(10.0f, 10.0f, 120.0f, 32.0f, 1.0f, 1.0f, 1.0f, 0.12f, 2.0f);
            drawTextTiny(16.0f, 16.0f, 2.4f, fpsBuf, 1.0f, 0.97f, 0.9f, 1.0f);
        }
        if (!inventoryOpen && !pauseMenuOpen)
        {
            drawCrosshair(winW, winH);
            drawInventoryBar(winW, winH, hotbarSlots, selected);
        }
        if (inventoryOpen)
            drawInventoryPanel(winW, winH, inventorySlots, hotbarSlots, pendingSlot, pendingIsHotbar, mouseX, mouseY,
                               hoverLabel);
        if (hoverLabel.valid)
            drawTooltip(hoverLabel.x, hoverLabel.y, winW, winH, hoverLabel.text);
        if (gSignEditOpen)
            drawSignEditBox(winW, winH, gSignEditBuffer);
        if (gButtonEditOpen)
            drawButtonEditBox(winW, winH, gButtonEditBuffer, gButtonWidthBuffer, gButtonEditingWidth);
        if (gClockEditOpen)
            drawClockEditBox(winW, winH, gClockEditBuffer);
        if (gWireInfoOpen)
        {
            if (world.inside(gWireInfoX, gWireInfoY, gWireInfoZ))
            {
                gWireInfoWidth = world.getPowerWidth(gWireInfoX, gWireInfoY, gWireInfoZ);
                if (gWireInfoWidth == 0)
                    gWireInfoWidth = 8;
                uint8_t mask = gWireInfoWidth >= 8 ? 0xFFu : static_cast<uint8_t>((1u << gWireInfoWidth) - 1u);
                gWireInfoValue = static_cast<uint8_t>(world.getPower(gWireInfoX, gWireInfoY, gWireInfoZ) & mask);
            }
            drawWireInfoBox(winW, winH, gWireInfoWidth, gWireInfoValue);
        }
        if (gSplitterEditOpen)
            drawSplitterEditBox(winW, winH, gSplitterIsMerger, gSplitterWidthBuffer, gSplitterOrder);
        endHud();

        SDL_GL_SwapWindow(window);
        updateTitle(window);
    }

    glDeleteTextures(1,&titleImage);
    glDeleteTextures(1,&pauseImage);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
