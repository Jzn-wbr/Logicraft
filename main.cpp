#define SDL_MAIN_HANDLED
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

#include "render.hpp"
#include "types.hpp"
#include "world.hpp"

void drawTextTiny(float x, float y, float size, const std::string &text, float r, float g, float b, float a);
std::string stemFromPath(const std::string &path);
extern bool gSaveInputFocus;
extern std::vector<std::string> gSaveList;
extern int gSaveIndex;

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
    {'I', {0b1110, 0b0100, 0b0100, 0b0100, 0b1110}},
    {'L', {0b1000, 0b1000, 0b1000, 0b1000, 0b1111}},
    {'M', {0b1001, 0b1101, 0b1011, 0b1001, 0b1001}},
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
        drawSlotIcon(hotbar[i], x, y, slotSize);

        // stack count
        int count = hotbar[i].count;
        drawNumber(x + slotSize - 4.0f, y + slotSize - 16.0f, count, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f);

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
    l.panelW = 520.0f;
    l.panelH = 330.0f;
    l.panelX = (winW - l.panelW) * 0.5f;
    l.panelY = (winH - l.panelH) * 0.5f;

    l.resumeW = l.panelW * 0.82f;
    l.resumeH = 56.0f;
    l.resumeX = l.panelX + (l.panelW - l.resumeW) * 0.5f;
    l.resumeY = l.panelY + 28.0f;

    l.manageW = l.resumeW;
    l.manageH = 56.0f;
    l.manageX = l.resumeX;
    l.manageY = l.resumeY + l.resumeH + 24.0f;

    l.quitW = l.resumeW;
    l.quitH = 56.0f;
    l.quitX = l.resumeX;
    l.quitY = l.manageY + l.manageH + 24.0f;
    return l;
}

SaveMenuLayout computeSaveMenuLayout(int winW, int winH)
{
    SaveMenuLayout s;
    s.panelW = 560.0f;
    s.panelH = 400.0f;
    s.panelX = (winW - s.panelW) * 0.5f;
    s.panelY = (winH - s.panelH) * 0.5f;

    s.listW = s.panelW * 0.86f;
    s.listH = s.panelH * 0.52f;
    s.listX = s.panelX + (s.panelW - s.listW) * 0.5f;
    s.listY = s.panelY + 30.0f;

    s.inputW = s.panelW * 0.86f;
    s.inputH = 36.0f;
    s.inputX = s.panelX + (s.panelW - s.inputW) * 0.5f;
    s.inputY = s.listY + s.listH + 12.0f;

    s.overwriteW = s.panelW * 0.26f;
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

void drawPauseMenu(int winW, int winH, const PauseMenuLayout &l, bool hoverResume, bool hoverManage, bool hoverQuit)
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
    drawButton(l.manageX, l.manageY, l.manageW, l.manageH, 0.2f, 0.4f, 0.8f, hoverManage);
    drawButton(l.quitX, l.quitY, l.quitW, l.quitH, 0.65f, 0.18f, 0.12f, hoverQuit);

    auto centerTinyText = [](float x, float y, float w, float size, const std::string &txt, float r, float g, float b,
                             float a)
    {
        float width = static_cast<float>(txt.size()) * (4.0f * size + size * 0.8f) - size * 0.8f;
        float tx = x + (w - width) * 0.5f;
        drawTextTiny(tx, y, size, txt, r, g, b, a);
    };

    float labelSize = 1.7f;
    centerTinyText(l.resumeX, l.resumeY + l.resumeH * 0.33f, l.resumeW, labelSize, "RESUME", 1.0f, 1.0f, 1.0f,
                   hoverResume ? 1.0f : 0.9f);
    centerTinyText(l.manageX, l.manageY + l.manageH * 0.33f, l.manageW, labelSize, "SAVE / LOAD", 1.0f, 1.0f, 1.0f,
                   hoverManage ? 1.0f : 0.9f);
    centerTinyText(l.quitX, l.quitY + l.quitH * 0.33f, l.quitW, labelSize, "QUIT", 1.0f, 1.0f, 1.0f,
                   hoverQuit ? 1.0f : 0.9f);
}

void drawSaveMenu(int winW, int winH, const SaveMenuLayout &s, int highlightedIndex, bool hoverCreate,
                  bool hoverOverwrite, bool hoverLoad, bool hoverBack, const std::string &inputName)
{
    drawQuad(0.0f, 0.0f, static_cast<float>(winW), static_cast<float>(winH), 0.0f, 0.0f, 0.0f, 0.55f);
    drawQuad(s.panelX, s.panelY, s.panelW, s.panelH, 0.05f, 0.05f, 0.08f, 0.92f);
    drawOutline(s.panelX, s.panelY, s.panelW, s.panelH, 1.0f, 1.0f, 1.0f, 0.08f, 3.0f);

    drawQuad(s.listX, s.listY, s.listW, s.listH, 0.12f, 0.12f, 0.15f, 0.9f);
    drawOutline(s.listX, s.listY, s.listW, s.listH, 1.0f, 1.0f, 1.0f, 0.15f, 2.0f);

    float lineH = 20.0f;
    float textSize = 1.4f;
    for (int i = 0; i < static_cast<int>(gSaveList.size()); ++i)
    {
        float y = s.listY + 8.0f + i * lineH;
        if (y + lineH > s.listY + s.listH - 8.0f)
            break;
        bool sel = (i == highlightedIndex);
        if (sel)
            drawQuad(s.listX + 4.0f, y - 2.0f, s.listW - 8.0f, lineH + 4.0f, 0.2f, 0.3f, 0.5f, 0.7f);
        std::string name = stemFromPath(gSaveList[i]);
        drawTextTiny(s.listX + 10.0f, y + 4.0f, textSize, name, 1.0f, 1.0f, 1.0f, 0.95f);
    }

    // input field
    drawQuad(s.inputX, s.inputY, s.inputW, s.inputH, 0.12f, 0.12f, 0.15f, 0.9f);
    float outlineA = gSaveInputFocus ? 0.55f : 0.2f;
    drawOutline(s.inputX, s.inputY, s.inputW, s.inputH, 1.0f, 1.0f, 1.0f, outlineA, 2.0f);
    std::string displayName = inputName.empty() ? "<nom>" : inputName;
    float textSizeInput = 1.5f;
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

    auto drawButton = [](float x, float y, float w, float h, float r, float g, float b, bool hover)
    {
        float a = hover ? 0.95f : 0.8f;
        drawQuad(x, y, w, h, r, g, b, a);
        drawOutline(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.45f, 3.0f);
    };

    drawButton(s.createX, s.createY, s.createW, s.createH, 0.2f, 0.5f, 0.8f, hoverCreate);
    drawButton(s.overwriteX, s.overwriteY, s.overwriteW, s.overwriteH, 0.18f, 0.55f, 0.25f, hoverOverwrite);
    drawButton(s.loadX, s.loadY, s.loadW, s.loadH, 0.45f, 0.18f, 0.75f, hoverLoad);
    drawButton(s.backX, s.backY, s.backW, s.backH, 0.65f, 0.18f, 0.12f, hoverBack);

    auto centerTinyText = [](float x, float y, float w, float size, const std::string &txt, float r, float g, float b,
                             float a)
    {
        float width = static_cast<float>(txt.size()) * (4.0f * size + size * 0.8f) - size * 0.8f;
        float tx = x + (w - width) * 0.5f;
        drawTextTiny(tx, y, size, txt, r, g, b, a);
    };

    float labelSize = 1.6f;
    centerTinyText(s.createX, s.createY + s.createH * 0.33f, s.createW, labelSize, "CREATE", 1.0f, 1.0f, 1.0f,
                   hoverCreate ? 1.0f : 0.9f);
    centerTinyText(s.overwriteX, s.overwriteY + s.overwriteH * 0.33f, s.overwriteW, labelSize, "OVERWRITE", 1.0f, 1.0f,
                   1.0f, hoverOverwrite ? 1.0f : 0.9f);
    centerTinyText(s.loadX, s.loadY + s.loadH * 0.33f, s.loadW, labelSize, "LOAD", 1.0f, 1.0f, 1.0f,
                   hoverLoad ? 1.0f : 0.9f);
    centerTinyText(s.backX, s.backY + s.backH * 0.33f, s.backW, labelSize, "RETURN", 1.0f, 1.0f, 1.0f,
                   hoverBack ? 1.0f : 0.9f);
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

// ---------- Save / Load ----------
static const char *MAPS_DIR = "maps";

struct SaveHeader
{
    char magic[8] = {'B', 'U', 'L', 'L', 'D', 'O', 'G', '\0'};
    uint32_t version = 1;
    uint32_t w = 0, h = 0, d = 0;
    uint32_t seed = 0;
};

bool saveWorldToFile(const World &world, const std::string &path, uint32_t seed)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    SaveHeader hdr;
    hdr.w = static_cast<uint32_t>(world.getWidth());
    hdr.h = static_cast<uint32_t>(world.getHeight());
    hdr.d = static_cast<uint32_t>(world.getDepth());
    hdr.seed = seed;
    out.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
    int total = world.totalSize();
    for (int i = 0; i < total; ++i)
    {
        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();
        uint8_t b = static_cast<uint8_t>(world.get(x, y, z));
        uint8_t p = world.getPower(x, y, z);
        uint8_t btn = world.getButtonState(x, y, z);
        out.write(reinterpret_cast<const char *>(&b), 1);
        out.write(reinterpret_cast<const char *>(&p), 1);
        out.write(reinterpret_cast<const char *>(&btn), 1);
    }
    return static_cast<bool>(out);
}

bool loadWorldFromFile(World &world, const std::string &path, uint32_t &seedOut)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    SaveHeader hdr{};
    in.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    if (!in || std::string(hdr.magic, hdr.magic + 7) != "BULLDOG")
        return false;
    if (hdr.version != 1)
        return false;
    if (hdr.w != static_cast<uint32_t>(world.getWidth()) || hdr.h != static_cast<uint32_t>(world.getHeight()) ||
        hdr.d != static_cast<uint32_t>(world.getDepth()))
        return false;

    int total = world.totalSize();
    for (int i = 0; i < total; ++i)
    {
        uint8_t b = 0, p = 0, btn = 0;
        in.read(reinterpret_cast<char *>(&b), 1);
        in.read(reinterpret_cast<char *>(&p), 1);
        in.read(reinterpret_cast<char *>(&btn), 1);
        if (!in)
            return false;
        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();
        world.set(x, y, z, static_cast<BlockType>(b));
        world.setPower(x, y, z, p);
        world.setButtonState(x, y, z, btn);
    }
    seedOut = hdr.seed;
    markAllChunksDirty();
    return true;
}

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
    std::snprintf(buf, sizeof(buf), "maps/%04d%02d%02d_%02d%02d%02d.bulldog", tmStruct.tm_year + 1900,
                  tmStruct.tm_mon + 1, tmStruct.tm_mday, tmStruct.tm_hour, tmStruct.tm_min, tmStruct.tm_sec);
    return std::string(buf);
}

std::string latestSaveInMaps()
{
    namespace fs = std::filesystem;
    fs::path dir(MAPS_DIR);
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

std::string stemFromPath(const std::string &path);

void refreshSaveList()
{
    gSaveList.clear();
    gSaveIndex = -1;
    namespace fs = std::filesystem;
    fs::path dir(MAPS_DIR);
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
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
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
    return std::string("maps/") + name;
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
    {
        glColor4f(1.0f, 1.0f, 1.0f, 0.92f);
        glLineWidth(2.0f);
        float pad = slotSize * 0.18f;
        drawOutline(x + pad, y + pad, slotSize - pad * 2, slotSize - pad * 2, 1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
        const char *label = (slot.type == BlockType::AndGate) ? "AND" : "OR";
        float txtSize = 1.6f;
        float textWidth = static_cast<float>(std::strlen(label)) * (4.0f * txtSize + txtSize * 0.8f) - txtSize * 0.8f;
        float textX = x + (slotSize - textWidth) * 0.5f;
        float textY = y + slotSize * 0.36f;
        drawTextTiny(textX, textY, txtSize, label, 1.0f, 1.0f, 1.0f, 1.0f);
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
    case BlockType::Air:
    case BlockType::Grass:
    case BlockType::Dirt:
    case BlockType::Stone:
    case BlockType::Wood:
    case BlockType::Leaves:
    case BlockType::Water:
    case BlockType::Plank:
    case BlockType::Sand:
    case BlockType::Glass:
        break;
    default:
        break;
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
    GLuint npcTextureAlt = loadTextureFromBMP("images/npc_head_alt.bmp");
    if (npcTextureAlt == 0)
    {
        std::cerr << "Could not load NPC texture (images/npc_head_alt.bmp). Using flat color.\n";
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
                        saveMenuOpen = false;
                        SDL_StopTextInput();
                        gSaveInputFocus = false;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        SDL_ShowCursor(SDL_FALSE);
                    }
                    else
                    {
                        pauseMenuOpen = true;
                        saveMenuOpen = false;
                        refreshSaveList();
                        inventoryOpen = false;
                        pendingSlot = -1;
                        SDL_StopTextInput();
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
                    float spawnX = WIDTH * 0.5f;
                    float spawnZ = DEPTH * 0.5f;
                    int sx = static_cast<int>(std::floor(spawnX));
                    int sz = static_cast<int>(std::floor(spawnZ));
                    int sy = world.surfaceY(sx, sz);
                    if (sy < 0)
                        sy = 0;
                    // Find the first free spot above surface if blocked
                    int checkY = sy + 1;
                    auto blockedAt = [&](int y) {
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
                else if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_6)
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
                else if (saveMenuOpen && e.key.keysym.sym == SDLK_TAB)
                {
                    std::string suggested = stemFromPath(timestampSaveName());
                    gSaveNameInput = normalizeSaveInput(suggested);
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
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                if (pauseMenuOpen)
                {
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
                            float relY = static_cast<float>(mouseY) - sm.listY - 8.0f;
                            int idx = static_cast<int>(relY / 20.0f);
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
                            bool ok = saveWorldToFile(world, path, seed);
                            std::cout << (ok ? "Sauvegarde OK: " : "Sauvegarde KO: ") << path << "\n";
                            if (ok)
                            {
                                refreshSaveList();
                                int idx = findSaveIndexByStem(stemFromPath(path));
                                if (idx >= 0)
                                    gSaveIndex = idx;
                                gSaveNameInput = normalizeSaveInput(stemFromPath(path));
                                gSaveInputFocus = false;
                            }
                        }
                        else if (hoverOverwrite)
                        {
                            std::string path;
                            if (gSaveIndex >= 0 && gSaveIndex < static_cast<int>(gSaveList.size()))
                                path = gSaveList[gSaveIndex];
                            else
                                path = timestampSaveName();
                            bool ok = saveWorldToFile(world, path, seed);
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
                                    seed = newSeed;
                                    player.x = WIDTH * 0.5f;
                                    player.z = DEPTH * 0.5f;
                                    player.y =
                                        world.surfaceY(static_cast<int>(player.x), static_cast<int>(player.z)) + 0.2f;
                                }
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
                        PauseMenuLayout l = computePauseLayout(winW, winH);
                        bool hoverResume = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.resumeX,
                                                       l.resumeY, l.resumeW, l.resumeH);
                        bool hoverManage = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.manageX,
                                                       l.manageY, l.manageW, l.manageH);
                        bool hoverQuit = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.quitX,
                                                     l.quitY, l.quitW, l.quitH);
                        if (hoverQuit)
                        {
                            running = false;
                        }
                        else if (hoverManage)
                        {
                            saveMenuOpen = true;
                            SDL_StartTextInput();
                            refreshSaveList();
                            if (gSaveNameInput.empty())
                                gSaveNameInput = normalizeSaveInput(stemFromPath(timestampSaveName()));
                            gSaveInputFocus = true;
                        }
                        else if (hoverResume)
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

        player.vx *= 0.85f;
        player.vy *= 0.85f;
        player.vz *= 0.85f;

        updateNpc(npc, world, simDt);
        updateNpc(npc2, world, simDt);
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

        // NPC blocky models
        drawNpcBlocky(npc);
        drawNpcBlocky(npc2);

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
            if (saveMenuOpen)
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
            else
            {
                PauseMenuLayout l = computePauseLayout(winW, winH);
                bool hoverResume = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.resumeX, l.resumeY,
                                               l.resumeW, l.resumeH);
                bool hoverManage = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.manageX, l.manageY,
                                               l.manageW, l.manageH);
                bool hoverQuit = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), l.quitX, l.quitY, l.quitW,
                                             l.quitH);
                drawPauseMenu(winW, winH, l, hoverResume, hoverManage, hoverQuit);
            }
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
