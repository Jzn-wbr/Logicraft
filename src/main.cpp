#define SDL_MAIN_HANDLED
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif
#include <GL/glew.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#endif
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
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "circuit_ports.hpp"
#include "edit_history.hpp"
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
bool pointInRect(float mx, float my, float x, float y, float w, float h);
std::string stemFromPath(const std::string &path);
std::string saveStatus;
std::vector<std::string> saveDates;
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
    const int slotCount = static_cast<int>(hotbarSize);
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
                MenuAction action = MenuAction::Normal, float maxTextSize = 3.0f)
{
    bool enabled = action != MenuAction::Disabled;
    signBoard(x,y,w,h,enabled ? 1.04f : 0.65f);
    if (hover && enabled)
        drawOutline(x+1,y+1,w-2,h-2,0.99f,0.85f,0.33f,1,2);
    const float labelWidth = std::max(1.0f, std::strlen(label) * 4.8f - 0.8f);
    const float size = std::min({maxTextSize, w < 145 ? 2.0f : 3.0f, (w - 12.0f) / labelWidth});
    const float tw = labelWidth * size;
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
    s.panelH = 500.0f - (saveRows-visibleRows)*saveRowHeight;
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

std::pair<float, float> saveActionPosition(const SaveMenuLayout &layout, int index)
{
    return {layout.createX + (index % 4) * (layout.createW + 12.0f), layout.createY + 60.0f + (index / 4) * 42.0f};
}

void drawSaveMenu(int winW, int winH, const SaveMenuLayout &s, int highlightedIndex, bool hoverCreate,
                  bool hoverOverwrite, bool hoverLoad, bool hoverBack, const std::string &inputName,
                  int mouseX, int mouseY)
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
        signText(s.listX+15,y+5,2,stemFromPath(gSaveList[i]).substr(0,34));
        signText(s.listX+s.listW-92,y+5,2,saveDates[i]);
    }
    signText(s.inputX,s.inputY-19,2,"NOM POUR ENREGISTRER SOUS");
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

    menuButton(s.createX, s.createY, s.createW, s.createH, "ENREG. SOUS", hoverCreate, MenuAction::Normal);
    menuButton(s.overwriteX, s.overwriteY, s.overwriteW, s.overwriteH, "ECRASER", hoverOverwrite, highlightedIndex < 0 ? MenuAction::Disabled : MenuAction::Caution);
    menuButton(s.loadX, s.loadY, s.loadW, s.loadH, "CHARGER", hoverLoad, highlightedIndex < 0 ? MenuAction::Disabled : MenuAction::Primary);
    menuButton(s.backX, s.backY, s.backW, s.backH, "RETOUR", hoverBack, MenuAction::Normal);
    static constexpr const char *labels[] = {
        "ENREGISTRER", "NOUVELLE", "RENOMMER", "DUPLIQUER", "REPRENDRE", "IMPORTER", "EXPORTER", "TUTORIEL"};
    for (int i = 0; i < 8; ++i)
    {
        auto [x, y] = saveActionPosition(s, i);
        menuButton(x, y, s.createW, 36.0f, labels[i],
                   pointInRect(mouseX, mouseY, x, y, s.createW, 36.0f));
    }
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
    bool invertY = false;
    bool smoothCamera = true;
    bool toggleSprint = false;
    bool highContrast = false;
    int fov = 70;
    int uiSize = 100;
    int keyboardProfile = 0;
    std::array<SDL_Keycode, 4> movement{};
    std::array<SDL_Keycode, 4> actions{SDLK_e, SDLK_UNKNOWN, SDLK_r, SDLK_t};
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
    s.panelH = 510.0f;
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

const char *movementLabel(int i)
{
    static constexpr const char *names[] = {"AVANCER", "RECULER", "GAUCHE", "DROITE",
                                            "INVENTAIRE", "BLOC", "RETOUR DEPART", "TOURNER"};
    return names[i];
}

std::pair<SDL_Keycode, SDL_Keycode> movementKeys(const Config &cfg, int index)
{
    if (index >= 4)
    {
        SDL_Keycode key = cfg.actions[index - 4];
        if (index == 5 && key == SDLK_UNKNOWN)
            key = cfg.keyboardProfile == 2 || cfg.movement[2] == SDLK_q ? SDLK_f : SDLK_q;
        return {key, SDLK_UNKNOWN};
    }
    if (cfg.movement[index])
        return {cfg.movement[index], SDLK_UNKNOWN};
    if (index == 0)
        return {cfg.keyboardProfile == 2 ? SDLK_z : SDLK_w, cfg.keyboardProfile == 0 ? SDLK_z : SDLK_UNKNOWN};
    if (index == 2)
        return {cfg.keyboardProfile == 2 ? SDLK_q : SDLK_a, SDLK_UNKNOWN};
    return {index == 1 ? SDLK_s : SDLK_d, SDLK_UNKNOWN};
}

std::pair<float, float> settingsOptionPosition(const SettingsMenuLayout &layout, int index)
{
    return {layout.panelX + 14.0f + (index % 3) * 165.0f, layout.panelY + 239.0f + (index / 3) * 33.0f};
}

void drawSettingsMenu(int winW, int winH, const SettingsMenuLayout &s, const Config &cfg, bool hoverBack,
                      bool hoverMinus, bool hoverPlus, bool hoverFps, bool hoverFullscreen,
                      int mouseX, int mouseY, int binding, const std::string &bindingStatus)
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
    static constexpr const char *profiles[] = {"AUTO", "QWERTY", "AZERTY"};
    const std::string labels[] = {
        "VISION " + std::to_string(cfg.fov), cfg.invertY ? "Y INVERSE" : "Y NORMAL",
        cfg.smoothCamera ? "CAMERA LISSEE" : "CAMERA DIRECTE", "INTERFACE " + std::to_string(cfg.uiSize) + "%",
        cfg.highContrast ? "CONTRASTE FORT" : "CONTRASTE STANDARD",
        cfg.toggleSprint ? "ALT BASCULE" : "ALT MAINTIEN", std::string("CLAVIER ") + profiles[cfg.keyboardProfile],
        "REINITIALISER TOUCHES"
    };
    for (int i = 0; i < 16; ++i)
    {
        auto [x, y] = settingsOptionPosition(s, i);
        std::string label = i < 8 ? labels[i] : std::string(movementLabel(i - 8)) + " " +
                                                 SDL_GetKeyName(movementKeys(cfg, i - 8).first);
        if (i >= 8 && binding == i - 8)
            label = "APPUYEZ UNE TOUCHE";
        menuButton(x, y, 158, 32, label.c_str(), pointInRect(mouseX, mouseY, x, y, 158, 32));
    }
    if (!bindingStatus.empty())
        signText(s.panelX + 20, s.panelY + 436, 1.6f, bindingStatus);
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

    signText(textX, textY + 100.0f, 1.5f, "ESC FERMER");
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
static std::filesystem::path configFile = "config.cfg";

void initializePlayerStorage()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(gResourceRoot / "CMakeLists.txt", ec))
        return;
    char *preferred = SDL_GetPrefPath("Logicraft", "Logicraft");
    if (!preferred)
        return;
    fs::path root = fs::u8path(preferred);
    SDL_free(preferred);

    fs::path maps = root / "maps";
    if (fs::create_directories(maps, ec))
    {
        for (fs::directory_iterator it(gResourceRoot / "maps", ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code copyError;
            if (it->path().extension() == ".bulldog" && it->is_regular_file(copyError))
                fs::copy_file(it->path(), maps / it->path().filename(), copyError);
        }
    }
    ec.clear();
    if (fs::is_directory(maps, ec))
        gMapsDir = maps;
    configFile = root / "config.cfg";
    ec.clear();
    if (!fs::exists(configFile, ec) && !ec)
        fs::copy_file(gResourceRoot / "config.cfg", configFile, ec);
}

void loadConfig(Config &cfg)
{
    std::ifstream in(configFile);
    if (!in)
        return;
    bool currentBindings = false;
    std::string line;
    while (std::getline(in, line))
    {
        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if (key == "bindings_version")
            currentBindings = val == "2";
        else if (key == "mouse_sensitivity")
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
        else if (key == "invert_y" || key == "smooth_camera" || key == "toggle_sprint" || key == "high_contrast")
        {
            bool enabled = val == "1" || val == "true";
            if (key == "invert_y") cfg.invertY = enabled;
            if (key == "smooth_camera") cfg.smoothCamera = enabled;
            if (key == "toggle_sprint") cfg.toggleSprint = enabled;
            if (key == "high_contrast") cfg.highContrast = enabled;
        }
        else if (key == "fov" || key == "ui_size" || key == "keyboard_profile" ||
                 (key.size() == 5 && (key.compare(0, 4, "move") == 0 || key.compare(0, 4, "bind") == 0) &&
                  key[4] >= '0' && key[4] <= '3'))
        {
            char *end = nullptr;
            long number = std::strtol(val.c_str(), &end, 10);
            if (end != val.c_str() && !*end)
            {
                if (key == "fov") cfg.fov = static_cast<int>(std::clamp(number, 55L, 100L));
                else if (key == "ui_size") cfg.uiSize = static_cast<int>(std::clamp(number, 75L, 125L));
                else if (key == "keyboard_profile") cfg.keyboardProfile = static_cast<int>(std::clamp(number, 0L, 2L));
                else if (number >= 'a' && number <= 'z')
                {
                    auto &bindings = key[0] == 'm' ? cfg.movement : cfg.actions;
                    bindings[key[4] - '0'] = static_cast<SDL_Keycode>(number);
                }
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
    if (!currentBindings && cfg.actions[1] == SDLK_f)
    {
        const auto reserved = [&](SDL_Keycode key)
        {
            return std::find(cfg.movement.begin(), cfg.movement.end(), key) != cfg.movement.end() ||
                   cfg.actions[0] == key || cfg.actions[2] == key || cfg.actions[3] == key;
        };
        if (!reserved(SDLK_q))
            cfg.actions[1] = SDLK_UNKNOWN;
    }
}

void saveConfig(const Config &cfg)
{
    std::ofstream out(configFile);
    if (!out)
        return;
    out << "mouse_sensitivity=" << cfg.mouseSensitivity << "\n";
    out << "fullscreen=" << (cfg.fullscreenDefault ? 1 : 0) << "\n";
    out << "show_fps=" << (cfg.showFps ? 1 : 0) << "\n";
    out << "vsync=" << (cfg.vsync ? 1 : 0) << "\n";
    out << "invert_y=" << cfg.invertY << '\n';
    out << "smooth_camera=" << cfg.smoothCamera << '\n';
    out << "toggle_sprint=" << cfg.toggleSprint << '\n';
    out << "high_contrast=" << cfg.highContrast << '\n';
    out << "fov=" << cfg.fov << '\n';
    out << "ui_size=" << cfg.uiSize << '\n';
    out << "bindings_version=2\n";
    out << "keyboard_profile=" << cfg.keyboardProfile << '\n';
    for (int i = 0; i < 4; ++i)
    {
        out << "move" << i << '=' << cfg.movement[i] << '\n';
        out << "bind" << i << '=' << cfg.actions[i] << '\n';
    }
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
    saveDates.clear();
    gSaveIndex = -1;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, std::string>> entries;
    for (fs::directory_iterator it(gMapsDir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entryError;
        if (it->path().extension() != ".bulldog" || !it->is_regular_file(entryError))
            continue;
        auto modified = it->last_write_time(entryError);
        if (!entryError)
            entries.emplace_back(modified, it->path().string());
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b)
              { return a.first == b.first ? a.second < b.second : a.first > b.first; });
    const auto fileNow = fs::file_time_type::clock::now();
    const auto systemNow = std::chrono::system_clock::now();
    for (const auto &[modified, path] : entries)
    {
        gSaveList.push_back(path);
        auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(modified - fileNow + systemNow);
        auto time = std::chrono::system_clock::to_time_t(systemTime);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &time);
#else
        localtime_r(&time, &local);
#endif
        char date[16];
        std::strftime(date, sizeof(date), "%d/%m/%y", &local);
        saveDates.emplace_back(date);
    }
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

enum class ConfirmationChoice { Cancel, Proceed, Save };

struct MapConfirmation
{
    std::string title, detail, error;
    bool offerSave;
    std::function<void()> proceed;
    bool relativeMouse, cursorVisible, textInput;
    int selected = 0;
};

struct ConfirmationLayout
{
    float x, y, width;
    static constexpr float height = 180.0f;
    static constexpr float margin = 18.0f;
    static constexpr float gap = 10.0f;
    static constexpr float buttonHeight = 36.0f;
    int count;

    ConfirmationLayout(int winW, int winH, bool offerSave)
        : x(std::max(16.0f, (winW - 580.0f) * 0.5f)), y((winH - height) * 0.5f),
          width(winW - 2 * x), count(offerSave ? 3 : 2) {}

    float buttonWidth() const { return (width - 2 * margin - (count - 1) * gap) / count; }
    float buttonX(int i) const { return x + margin + i * (buttonWidth() + gap); }
    float buttonY() const { return y + height - margin - buttonHeight; }

    int hit(int mouseX, int mouseY) const
    {
        for (int i = 0; i < count; ++i)
            if (pointInRect(mouseX, mouseY, buttonX(i), buttonY(), buttonWidth(), buttonHeight))
                return i;
        return -1;
    }
};

void drawMapConfirmation(int winW, int winH, const MapConfirmation &confirmation, int mouseX, int mouseY)
{
    ConfirmationLayout panel(winW, winH, confirmation.offerSave);
    drawQuad(0, 0, winW, winH, 0, 0, 0, 0.72f);
    menuPanel(panel.x, panel.y, panel.width, panel.height, confirmation.title.c_str());
    signText(panel.x + panel.margin, panel.y + 59, 2, confirmation.detail);
    if (!confirmation.error.empty())
        signText(panel.x + panel.margin, panel.y + 90, 2, confirmation.error);
    int hover = panel.hit(mouseX, mouseY);
    for (int i = 0; i < panel.count; ++i)
    {
        const char *label = i == 0 ? "ANNULER" : i == 2 ? "ENREGISTRER" :
                            confirmation.offerSave ? "SANS ENREGISTRER" : "ECRASER";
        menuButton(panel.buttonX(i), panel.buttonY(), panel.buttonWidth(), panel.buttonHeight, label,
                   i == hover || i == confirmation.selected,
                   i == 2 ? MenuAction::Primary : i == 1 ? MenuAction::Caution : MenuAction::Normal, 2.0f);
    }
}

#ifdef _WIN32
std::filesystem::path chooseMapFile(SDL_Window *window, bool exportFile)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info))
        return {};
    wchar_t path[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = info.info.win.window;
    dialog.lpstrFilter = L"Cartes Logicraft (*.bulldog)\0*.bulldog\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"bulldog";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   (exportFile ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (exportFile ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
        return path;
    return {};
}
#endif

bool saveHotbar(const std::string &path, const std::vector<ItemStack> &slots, int selected,
                const std::set<BlockType> &favorites)
{
    namespace fs = std::filesystem;
    const fs::path target = path + ".hotbar";
    fs::path temporary = path + ".hotbar.saving";
    std::error_code ec;
    if (!fs::create_directory(temporary, ec))
        return false;
    const fs::path candidate = temporary / "hotbar";
    std::ofstream output(candidate);
    output << "LOGICRAFT-HOTBAR-1 " << selected << '\n';
    for (const ItemStack &slot : slots)
        output << static_cast<int>(slot.type) << ' ' << slot.count << '\n';
    for (BlockType type : favorites)
        output << static_cast<int>(type) << ' ';
    output << '\n';
    output.close();
    if (output)
        fs::rename(candidate, target, ec);
    bool ok = output && !ec;
    fs::remove(candidate, ec);
    fs::remove(temporary, ec);
    return ok;
}

void loadHotbar(const std::string &path, std::vector<ItemStack> &slots, int &selected,
                std::set<BlockType> &favorites)
{
    std::ifstream input(path + ".hotbar");
    std::string version;
    int index = 0;
    std::vector<ItemStack> loaded(slots.size());
    bool valid = (input >> version >> index) && version == "LOGICRAFT-HOTBAR-1" &&
                 index >= 0 && index < static_cast<int>(slots.size());
    for (ItemStack &slot : loaded)
    {
        int type, count;
        if (!(input >> type >> count) || !BLOCKS.count(static_cast<BlockType>(type)) ||
            count < 0 || count > maxStackSize || ((count == 0) != (type == static_cast<int>(BlockType::Air))))
        {
            valid = false;
            break;
        }
        slot = {static_cast<BlockType>(type), count};
    }
    favorites.clear();
    if (valid)
    {
        int id;
        while (input >> id)
        {
            BlockType type = static_cast<BlockType>(id);
            if (type != BlockType::Air && BLOCKS.count(type))
                favorites.insert(type);
        }
        std::copy(loaded.begin(), loaded.end(), slots.begin());
        selected = index;
    }
    else
    {
        std::fill(slots.begin(), slots.end(), ItemStack{});
        selected = 0;
    }
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
        glLineWidth(2.0f);
        for (float phase : {0.0f, 3.14159265f})
        {
            glColor4f(0.87f, 0.33f, 0.48f, 1.0f);
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i <= 16; ++i)
            {
                float t = i / 16.0f;
                glVertex2f(x + slotSize * (0.15f + 0.70f * t),
                           y + slotSize * (0.5f + 0.12f * std::sin(t * 12.56637f + phase)));
            }
            glEnd();
        }
        glLineWidth(1.0f);
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
    drawOutline(x, y, slotSize, slotSize, 0.0f, 0.0f, 0.0f, gConfig.highContrast ? 1.0f : 0.5f, 2.0f);
    drawSlotIcon(slot, x, y, slotSize);
    if (slot.count > 0)
        drawNumber(x + slotSize - 4.0f, y + slotSize - 16.0f, slot.count, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    if (selected)
        drawOutline(x - 6.0f, y - 6.0f, slotSize + 12.0f, slotSize + 12.0f, 1.0f, 0.9f, 0.2f, 0.9f, 3.5f);
    else if (hovered)
        drawOutline(x - 6.0f, y - 6.0f, slotSize + 12.0f, slotSize + 12.0f, 1.0f, 1.0f, 1.0f, 0.35f, 2.0f);
}

void hudText(float x, float y, float size, const std::string &text)
{
    float width = static_cast<float>(text.size()) * size * 4.8f;
    drawQuad(x - 4, y - 4, width + 8, size * 5 + 8, 0.02f, 0.02f, 0.03f, gConfig.highContrast ? 0.96f : 0.73f);
    drawTextTiny(x, y, size, text, 1.0f, 0.97f, 0.88f, 1.0f);
}

struct SlotHit
{
    bool valid = false;
    bool isHotbar = false;
    int idx = -1;
};

struct InventoryLayout
{
    float size, gap, padding, invWidth, invHeight, invX, invY, hbWidth, hbX, hbY;

    InventoryLayout(int width, int height)
    {
        const float scale = std::min({1.0f, width / 650.0f, height / 420.0f});
        size = 54 * scale;
        gap = 10 * scale;
        padding = 16 * scale;
        invWidth = 2 * padding + INV_COLS * size + (INV_COLS + 1) * gap;
        invHeight = 2 * padding + INV_ROWS * size + (INV_ROWS + 1) * gap;
        hbWidth = 2 * padding + hotbarSize * size + (hotbarSize + 1) * gap;
        invX = (width - invWidth) * 0.5f;
        hbX = (width - hbWidth) * 0.5f;
        invY = (height - invHeight - size - 2 * padding - 24 * scale) * 0.5f + 10 * scale;
        hbY = invY + invHeight + 24 * scale;
    }

    std::pair<float, float> position(bool hotbar, int idx) const
    {
        int col = hotbar ? idx : idx % INV_COLS;
        int row = hotbar ? 0 : idx / INV_COLS;
        float x = (hotbar ? hbX : invX) + padding + gap + col * (size + gap);
        float y = (hotbar ? hbY : invY + gap) + padding + row * (size + gap);
        return {x, y};
    }
};

SlotHit hitTestInventoryUI(int mx, int my, int winW, int winH)
{
    InventoryLayout layout(winW, winH);
    for (bool hotbar : {false, true})
    {
        for (int idx = 0; idx < (hotbar ? hotbarSize : INV_COLS * INV_ROWS); ++idx)
        {
            auto [x, y] = layout.position(hotbar, idx);
            if (pointInRect(mx, my, x, y, layout.size, layout.size))
                return {true, hotbar, idx};
        }
    }
    return {};
}

void drawInventoryPanel(int winW, int winH, const std::vector<ItemStack> &inventory, const std::vector<ItemStack> &hotbar,
                        const ItemStack &held, const std::vector<ItemStack *> &dragSlots, bool dragging, bool rightDrag,
                        int mouseX, int mouseY, HoverLabel &hoverLabel, const std::string &title,
                        const std::string &instructions, const std::set<BlockType> *favorites)
{
    InventoryLayout layout(winW, winH);
    float margin = 16 * layout.size / 54;
    float panelW = std::max(layout.invWidth, layout.hbWidth) + 2 * margin;
    float panelY = layout.invY - 30 * layout.size / 54;
    drawQuad(0, 0, winW, winH, 0,0,0,0.18f);
    menuPanel((winW-panelW)*0.5f, panelY, panelW,
              layout.hbY+layout.size+2*layout.padding-panelY, title.c_str());
    signText(layout.hbX + layout.padding + layout.gap, layout.hbY-9, 1.6f, instructions);

    ItemStack remaining = held;
    for (bool isHotbar : {false, true})
    {
        const auto &slots = isHotbar ? hotbar : inventory;
        for (int idx = 0; idx < static_cast<int>(slots.size()); ++idx)
        {
            auto [x, y] = layout.position(isHotbar, idx);
            bool hovered = pointInRect(mouseX, mouseY, x, y, layout.size, layout.size);
            bool preview = dragging &&
                           std::find(dragSlots.begin(), dragSlots.end(), &slots[idx]) != dragSlots.end();
            ItemStack shown = slots[idx];
            if (preview)
            {
                int each = rightDrag ? 1 : held.count / static_cast<int>(dragSlots.size());
                if (rightDrag)
                    each = std::find(dragSlots.begin(), dragSlots.end(), &slots[idx]) - dragSlots.begin() < held.count;
                moveItems(remaining, shown, each);
            }
            drawSlotBox(x, y, layout.size, shown, preview, hovered);
            if (!isHotbar && shown.count && favorites && favorites->count(shown.type))
                signText(x + 3, y + 3, 1.5f, "*");
            if (hovered && shown.count && !held.count)
                hoverLabel = {true, BLOCKS.at(shown.type).name, static_cast<float>(mouseX), static_cast<float>(mouseY)};
        }
    }
    if (remaining.count)
        drawSlotBox(mouseX + 8.0f, mouseY + 8.0f, layout.size * 0.75f, remaining, false, false);
}

void setup3D(SDL_Window *window, int w, int h)
{
    int pixelWidth, pixelHeight;
    SDL_GL_GetDrawableSize(window, &pixelWidth, &pixelHeight);
    glViewport(0, 0, pixelWidth, pixelHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(gConfig.fov, static_cast<double>(w) / std::max(1, h), 0.1, 500.0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
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
    const float JUMP = 12.8f;
    const float RISING_GRAVITY = -48.0f;
    const float FALLING_GRAVITY = -54.0f;
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

#if defined(_WIN32) && defined(SDL_HINT_WINDOWS_DPI_SCALING)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL init error: " << SDL_GetError() << "\n";
        return 1;
    }

    initializePlayerStorage();
    loadConfig(gConfig);

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
    SDL_SetWindowMinimumSize(window, 640, 520);
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
    std::vector<ItemStack> hotbarSlots(hotbarSize);
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
    std::vector<ItemStack> paletteSlots(inventorySlots.size());
    std::set<BlockType> favorites;
    bool paletteOpen = false;
    int paletteCategory = 0;
    std::string paletteSearch;
    auto refreshPalette = [&]
    {
        std::fill(paletteSlots.begin(), paletteSlots.end(), ItemStack{});
        int index = 0;
        for (BlockType type : INVENTORY_ALLOWED)
        {
            bool material = type <= BlockType::Glass;
            if ((paletteCategory == 1 && !material) || (paletteCategory == 2 && material) ||
                (paletteCategory == 3 && !favorites.count(type)))
                continue;
            std::string name = BLOCKS.at(type).name;
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name.find(paletteSearch) != std::string::npos && index < static_cast<int>(paletteSlots.size()))
                paletteSlots[index++] = {type, maxStackSize};
        }
    };
    refreshPalette();
    bool inventoryOpen = false;
    bool pauseMenuOpen = false;
    bool saveMenuOpen = false;
    ItemStack held;
    bool mapEdited = false;
    std::vector<ItemStack *> dragSlots;
    SlotHit pressSlot;
    Uint8 inventoryButton = 0;
    bool dragging = false;
    bool inventoryFull = false;
    ItemStack *lastShiftSlot = nullptr;
    BlockType lastShiftType = BlockType::Air;
    int mouseX = 0, mouseY = 0;

    auto slotAt = [&](SlotHit hit) -> ItemStack *
    {
        return hit.valid && (hit.isHotbar || !paletteOpen) ?
               &(hit.isHotbar ? hotbarSlots : inventorySlots)[hit.idx] : nullptr;
    };
    auto visitDragSlot = [&](SlotHit hit)
    {
        ItemStack *slot = slotAt(hit);
        if (slot && held.count && slot->count < maxStackSize &&
            (!slot->count || slot->type == held.type) &&
            std::find(dragSlots.begin(), dragSlots.end(), slot) == dragSlots.end())
        {
            dragSlots.push_back(slot);
            if (slot != slotAt(pressSlot))
                dragging = true;
        }
    };
    auto cancelInventoryGesture = [&]
    {
        inventoryButton = 0;
        dragging = false;
        dragSlots.clear();
    };
    auto closeInventory = [&]
    {
        cancelInventoryGesture();
        storeItems(held, inventorySlots);
        const int left = held.count;
        storeItems(held, hotbarSlots);
        mapEdited |= left != held.count;
        inventoryFull = held.count != 0;
        if (inventoryFull)
            return;
        inventoryOpen = paletteOpen = false;
        SDL_StopTextInput();
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_ShowCursor(SDL_FALSE);
    };

    bool running = true;
    Uint64 prev = SDL_GetPerformanceCounter();
    float fps = 60.0f;
    float smoothDX = 0.0f;
    float smoothDY = 0.0f;
    float elapsedTime = 0.0f;
    float lastForwardTap = -1.0f;
    float lastSpaceTap = -1.0f;
    bool sprinting = false;
    bool sprintToggled = false;
    int binding = -1;
    std::string bindingStatus;
    bool flying = false;
    float jumpUntil = -1.0f;
    float groundedUntil = -1.0f;
    constexpr float tickInterval = 1.0f / 20.0f;
    float tickBudget = 0.0f;

    std::optional<MapConfirmation> confirmation;
    auto canSimulate = [&]
    {
        return (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
               !confirmation && !inventoryOpen && !pauseMenuOpen && !gMainMenuOpen && !gSettingsMenuOpen &&
               !gSignEditOpen;
    };
    auto canPlay = [&]
    {
        return canSimulate() && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen;
    };
    auto resetPosition = [&]
    {
        player.x = WIDTH * 0.5f + 0.5f;
        player.z = DEPTH * 0.5f + 0.5f;
        player.y = world.surfaceY(static_cast<int>(player.x), static_cast<int>(player.z)) + 0.001f;
        player.vx = player.vy = player.vz = 0.0f;
        flying = false;
        jumpUntil = groundedUntil = -1.0f;
    };
    EditHistory history;
    std::optional<BlockState> copiedSettings;
    std::optional<BlockPosition> cornerA, cornerB;
    std::vector<std::pair<BlockPosition, BlockState>> clipboard;
    bool pastePreview = false;
    uint8_t placementTurns = 0;
    std::optional<BlockPosition> probe;
    std::optional<BlockPosition> brokenBlock;
    float breakEffectUntil = -1.0f;
    int tutorial = 0;
    std::optional<BlockPosition> poweredTutorialLed;
    std::deque<std::pair<uint8_t, uint8_t>> samples;
    std::string activeMap;
    float saveNoticeUntil = -1.0f;
    float nextAutosave = 120.0f;
    int autosaveSlot = 0;
    auto saveMap = [&](const std::string &path)
    {
        if (!saveWorldToFile(world, path, seed))
        {
            saveStatus = "ECHEC DE LA SAUVEGARDE";
            saveNoticeUntil = elapsedTime + 4.0f;
            return false;
        }
        activeMap = path;
        bool savedBar = saveHotbar(path, hotbarSlots, selected, favorites);
        mapEdited = !savedBar;
        auto timestamp = stemFromPath(timestampSaveName());
        std::string hour = timestamp.substr(9, 2) + ":" + timestamp.substr(11, 2);
        if (tutorial == 6)
            tutorial = 7;
        saveStatus = savedBar ? "CARTE ENREGISTREE A " + hour : "CARTE SAUVEE - ECHEC BARRE RAPIDE";
        saveNoticeUntil = elapsedTime + 4.0f;
        refreshSaveList();
        auto found = std::find(gSaveList.begin(), gSaveList.end(), path);
        if (found != gSaveList.end())
        {
            gSaveIndex = static_cast<int>(found - gSaveList.begin());
            saveOffset = std::max(0, gSaveIndex - saveRows + 1);
        }
        return savedBar;
    };
    auto saveAs = [&]
    {
        std::string path = buildSavePathFromInput(gSaveNameInput);
        std::error_code ec;
        if (std::filesystem::exists(path, ec) || ec)
        {
            saveStatus = ec ? "NOM INACCESSIBLE" : "CE NOM EXISTE DEJA";
            saveNoticeUntil = elapsedTime + 4.0f;
            return false;
        }
        return saveMap(path);
    };
    auto saveCurrent = [&]
    {
        return activeMap.empty() ? saveMap(timestampSaveName()) : saveMap(activeMap);
    };
    auto requestConfirmation = [&](std::string title, std::string detail, bool offerSave, std::function<void()> proceed)
    {
        if (offerSave && !mapEdited)
        {
            proceed();
            return;
        }
        confirmation = MapConfirmation{std::move(title), std::move(detail), {}, offerSave, std::move(proceed),
                                       SDL_GetRelativeMouseMode() == SDL_TRUE, SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE,
                                       SDL_IsTextInputActive() == SDL_TRUE};
        SDL_StopTextInput();
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_TRUE);
    };
    auto resolveConfirmation = [&](ConfirmationChoice choice)
    {
        if (choice == ConfirmationChoice::Save && !saveCurrent())
        {
            confirmation->error = saveStatus;
            return;
        }
        MapConfirmation closed = std::move(*confirmation);
        confirmation.reset();
        SDL_SetRelativeMouseMode(closed.relativeMouse ? SDL_TRUE : SDL_FALSE);
        SDL_ShowCursor(closed.cursorVisible ? SDL_TRUE : SDL_FALSE);
        if (closed.textInput)
            SDL_StartTextInput();
        if (choice != ConfirmationChoice::Cancel)
            closed.proceed();
    };
    auto requestQuit = [&]
    {
        requestConfirmation("QUITTER LOGICRAFT ?", "LA CARTE CONTIENT DES MODIFICATIONS.", true,
                            [&] { running = false; });
    };
    auto openMap = [&](const std::string &path)
    {
        uint32_t newSeed = seed;
        if (!loadWorldFromFile(world, path, newSeed))
        {
            saveStatus = "ECHEC DU CHARGEMENT";
            return false;
        }
        seed = newSeed;
        activeMap = stemFromPath(path).rfind("RECUPERATION-", 0) == 0 ? std::string{} : path;
        mapEdited = activeMap.empty();
        loadHotbar(path, hotbarSlots, selected, favorites);
        refreshPalette();
        resetPosition();
        tickBudget = 0;
        nextAutosave = elapsedTime + 120.0f;
        markAllChunksDirty();
        history = {};
        tutorial = 0;
        brokenBlock.reset();
        copiedSettings.reset();
        cornerA.reset();
        cornerB.reset();
        pastePreview = false;
        probe.reset();
        samples.clear();
        saveStatus = "CARTE CHARGEE";
        return true;
    };
    auto newMap = [&]
    {
        seed = static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count());
        world.generate(seed);
        world.setTick(0);
        activeMap.clear();
        mapEdited = false;
        std::fill(hotbarSlots.begin(), hotbarSlots.end(), ItemStack{});
        selected = 0;
        favorites.clear();
        refreshPalette();
        resetPosition();
        markAllChunksDirty();
        history = {};
        tutorial = 0;
        brokenBlock.reset();
        copiedSettings.reset();
        cornerA.reset();
        cornerB.reset();
        probe.reset();
        samples.clear();
        pastePreview = false;
        tickBudget = 0;
        nextAutosave = elapsedTime + 120.0f;
        saveStatus = "NOUVELLE CARTE";
    };
    auto duplicateOrRename = [&](bool rename)
    {
        if (gSaveIndex < 0 || gSaveIndex >= static_cast<int>(gSaveList.size()) || gSaveNameInput.empty())
        {
            saveStatus = "CHOISISSEZ UNE CARTE ET UN NOUVEAU NOM";
            return;
        }
        const std::string source = gSaveList[gSaveIndex];
        const std::string destination = buildSavePathFromInput(gSaveNameInput);
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists(destination, ec) || ec)
        {
            saveStatus = "NOM DEJA UTILISE OU INACCESSIBLE";
            return;
        }
        if (rename)
            fs::rename(source, destination, ec);
        else
            fs::copy_file(source, destination, ec);
        if (ec)
        {
            saveStatus = "OPERATION IMPOSSIBLE";
            return;
        }
        if (activeMap == source && rename)
            activeMap = destination;
        if (fs::exists(source + ".hotbar", ec) && !ec)
        {
            if (rename)
                fs::rename(source + ".hotbar", destination + ".hotbar", ec);
            else
                fs::copy_file(source + ".hotbar", destination + ".hotbar", ec);
        }
        saveStatus = ec ? "CARTE TRAITEE - ECHEC BARRE RAPIDE" : rename ? "CARTE RENOMMEE" : "CARTE DUPLIQUEE";
        refreshSaveList();
    };
    auto resumePlay = [&]
    {
        gMainMenuOpen = gSettingsMenuOpen = pauseMenuOpen = saveMenuOpen = false;
        gSaveInputFocus = false;
        SDL_StopTextInput();
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_ShowCursor(SDL_FALSE);
        smoothDX = smoothDY = 0.0f;
    };
    auto requestOpenMap = [&](std::string path)
    {
        const std::string detail = "CARTE A OUVRIR : " + stemFromPath(path).substr(0, 36);
        requestConfirmation("ENREGISTRER AVANT DE CHARGER ?", detail, true, [&, path = std::move(path)]
        {
            if (openMap(path))
                resumePlay();
        });
    };
    auto requestNewMap = [&](bool startTutorial)
    {
        requestConfirmation(startTutorial ? "OUVRIR LE TUTORIEL ?" : "CREER UNE NOUVELLE CARTE ?",
                            "LA CARTE CONTIENT DES MODIFICATIONS.", true, [&, startTutorial]
        {
            newMap();
            if (startTutorial)
            {
                tutorial = 1;
                hotbarSlots[0] = {BlockType::Button, maxStackSize};
                hotbarSlots[1] = {BlockType::Wire, maxStackSize};
                hotbarSlots[2] = {BlockType::Led, maxStackSize};
                hotbarSlots[3] = {BlockType::NotGate, maxStackSize};
            }
            resumePlay();
        });
    };
    auto targetBlock = [&]
    {
        Vec3 fwd = forwardVec(player.yaw, player.pitch);
        return raycast(world, player.x, player.y + EYE_HEIGHT, player.z, fwd.x, fwd.y, fwd.z, 8.0f);
    };
    auto canPlace = [&](const HitInfo &hit)
    {
        int x = hit.x + hit.nx, y = hit.y + hit.ny, z = hit.z + hit.nz;
        return hit.hit && hotbarSlots[selected].count && world.inside(x, y, z) &&
               (world.get(x, y, z) == BlockType::Air || world.get(x, y, z) == BlockType::Water) &&
               !blockIntersectsPlayer(player, x, y, z, PLAYER_HEIGHT);
    };
    auto editCell = [&](BlockPosition position, auto change)
    {
        const auto [x, y, z] = position;
        BlockState before = BlockState::read(world, position);
        change();
        history.record({{position, std::move(before), BlockState::read(world, position)}});
        mapEdited = true;
        markNeighborsDirty(x, y, z);
    };
    auto copySelection = [&]
    {
        if (!cornerA || !cornerB)
            return;
        BlockPosition min{}, max{};
        int volume = 1;
        for (int axis = 0; axis < 3; ++axis)
        {
            min[axis] = std::min((*cornerA)[axis], (*cornerB)[axis]);
            max[axis] = std::max((*cornerA)[axis], (*cornerB)[axis]);
            volume *= max[axis] - min[axis] + 1;
        }
        if (volume > 4096)
            return;
        clipboard.clear();
        for (int y = min[1]; y <= max[1]; ++y)
            for (int z = min[2]; z <= max[2]; ++z)
                for (int x = min[0]; x <= max[0]; ++x)
                {
                    auto state = BlockState::read(world, {x, y, z});
                    if (state.type != BlockType::Air)
                        clipboard.emplace_back(BlockPosition{x - min[0], y - min[1], z - min[2]}, std::move(state));
                }
        cornerA.reset();
        cornerB.reset();
    };
    auto rotateClipboard = [&]
    {
        int minX = 0;
        for (auto &[offset, state] : clipboard)
        {
            auto [x, z] = rotateHorizontal(offset[0], offset[2], 1);
            offset[0] = x;
            offset[2] = z;
            minX = std::min(minX, x);
            if (isDirectional(state.type))
                state.orientation = (state.orientation + 1) & 3;
        }
        for (auto &entry : clipboard)
            entry.first[0] -= minX;
    };
    auto validPaste = [&](const HitInfo &hit)
    {
        return hit.hit && std::all_of(clipboard.begin(), clipboard.end(), [&](const auto &entry)
        {
            const auto [dx, dy, dz] = entry.first;
            int x = hit.x + hit.nx + dx, y = hit.y + hit.ny + dy, z = hit.z + hit.nz + dz;
            return world.inside(x, y, z) && world.get(x, y, z) == BlockType::Air &&
                   !blockIntersectsPlayer(player, x, y, z, PLAYER_HEIGHT);
        });
    };
    auto pasteBlocks = [&]
    {
        HitInfo hit = targetBlock();
        if (!validPaste(hit))
            return;
        std::vector<BlockEdit> changes;
        changes.reserve(clipboard.size());
        for (const auto &[offset, state] : clipboard)
        {
            BlockPosition position = {hit.x + hit.nx + offset[0], hit.y + hit.ny + offset[1], hit.z + hit.nz + offset[2]};
            BlockState before = BlockState::read(world, position);
            state.write(world, position, false);
            changes.push_back({position, std::move(before), BlockState::read(world, position)});
            markNeighborsDirty(position[0], position[1], position[2]);
        }
        history.record(std::move(changes));
        mapEdited = true;
        pastePreview = false;
    };
    auto exchangeTemplate = [&](bool exporting)
    {
        std::filesystem::path file = gMapsDir.parent_path() / "circuits" / "modele.bulldog";
#ifdef _WIN32
        file = chooseMapFile(window, exporting);
        if (file.empty())
            return;
#endif
        World model(WIDTH, HEIGHT, DEPTH);
        if (exporting)
        {
            if (clipboard.empty())
                return;
            for (const auto &[offset, state] : clipboard)
                state.write(model, offset, false);
            saveStatus = saveWorldToFile(model, file, 0) ? "CIRCUIT ENREGISTRE" : "ECHEC ECRITURE CIRCUIT";
        }
        else
        {
            uint32_t modelSeed;
            if (!loadWorldFromFile(model, file, modelSeed))
                saveStatus = "ECHEC LECTURE CIRCUIT";
            else
            {
                std::vector<std::pair<BlockPosition, BlockState>> loaded;
                for (int y = 0; y < HEIGHT && loaded.size() <= 4096; ++y)
                    for (int z = 0; z < DEPTH && loaded.size() <= 4096; ++z)
                        for (int x = 0; x < WIDTH && loaded.size() <= 4096; ++x)
                            if (model.get(x, y, z) != BlockType::Air)
                                loaded.emplace_back(BlockPosition{x, y, z}, BlockState::read(model, {x, y, z}));
                if (loaded.empty() || loaded.size() > 4096)
                    saveStatus = "MODELE VIDE OU LIMITE DE 4096 BLOCS";
                else
                {
                    clipboard = std::move(loaded);
                    pastePreview = true;
                    saveStatus = "CIRCUIT CHARGE";
                }
            }
        }
        saveNoticeUntil = elapsedTime + 4.0f;
    };
    auto editBlock = [&](Uint8 button, bool firstPress, bool forcePlace)
    {
        HitInfo hit = targetBlock();
        if (!hit.hit)
            return true;
        BlockType target = world.get(hit.x, hit.y, hit.z);
        if (button == SDL_BUTTON_LEFT)
        {
            if (target != BlockType::Air && target != BlockType::Water)
            {
                editCell({hit.x, hit.y, hit.z}, [&] { world.set(hit.x, hit.y, hit.z, BlockType::Air); });
                brokenBlock = BlockPosition{hit.x, hit.y, hit.z};
                breakEffectUntil = elapsedTime + 0.13f;
            }
        }
        else if (!forcePlace && (target == BlockType::Button || target == BlockType::Sign))
        {
            if (firstPress && target == BlockType::Button)
            {
                editCell({hit.x, hit.y, hit.z}, [&] { world.toggleButton(hit.x, hit.y, hit.z); });
            }
            if (firstPress && target == BlockType::Sign)
            {
                gSignEditOpen = true;
                gSignEditX = hit.x;
                gSignEditY = hit.y;
                gSignEditZ = hit.z;
                gSignEditBuffer = world.getSignText(hit.x, hit.y, hit.z);
                SDL_StartTextInput();
            }
            return !firstPress;
        }
        else if (canPlace(hit))
        {
            int x = hit.x + hit.nx, y = hit.y + hit.ny, z = hit.z + hit.nz;
            editCell({x, y, z}, [&]
            {
                if (copiedSettings && copiedSettings->type == hotbarSlots[selected].type)
                    copiedSettings->write(world, {x, y, z}, false);
                else
                {
                    world.set(x, y, z, hotbarSlots[selected].type);
                    world.setOrientation(x, y, z, placementTurns);
                }
            });
            if (tutorial == 1 && hotbarSlots[selected].type == BlockType::Button)
                tutorial = 2;
            else if (tutorial == 2 && hotbarSlots[selected].type == BlockType::Wire)
                tutorial = 3;
            else if (tutorial == 3 && hotbarSlots[selected].type == BlockType::Led)
            {
                tutorial = 4;
                poweredTutorialLed = BlockPosition{x, y, z};
            }
        }
        return true;
    };
    Uint8 repeatingButton = 0;
    float repeatAt = 0.0f;
    SDL_SetRelativeMouseMode(gMainMenuOpen ? SDL_FALSE : SDL_TRUE);
    SDL_ShowCursor(gMainMenuOpen ? SDL_TRUE : SDL_FALSE);

    std::cout << "Commandes: WASD/ZQSD deplacement, souris pour la camera, clic gauche miner, clic droit placer, "
                 "1-9 changer de bloc, Space saut, Shift descendre, Double jump fly, R teleport to spawn, Esc menu pause/save/load.\n";

    int winW = 1280, winH = 720;
    int displayW = 1280, displayH = 720;
    float uiScale = 1.0f;
    auto uiCoordinate = [&](int position) { return static_cast<int>(position / uiScale); };
    auto resize = [&]
    {
        SDL_GetWindowSize(window, &displayW, &displayH);
        uiScale = std::min({gConfig.uiSize / 100.0f, std::max(1, displayW) / 620.0f,
                            std::max(1, displayH) / 520.0f});
        winW = std::max(1, uiCoordinate(displayW));
        winH = std::max(1, uiCoordinate(displayH));
        int screenX, screenY;
        SDL_GetMouseState(&screenX, &screenY);
        mouseX = uiCoordinate(screenX);
        mouseY = uiCoordinate(screenY);
        setup3D(window, displayW, displayH);
    };
    resize();

    auto settingsClick = [&](int x, int y)
    {
        int option = -1;
        auto layout = computeSettingsLayout(winW, winH);
        for (int i = 0; i < 16; ++i)
        {
            auto [left, top] = settingsOptionPosition(layout, i);
            if (pointInRect(x, y, left, top, 158, 32))
                option = i;
        }
        if (option < 0)
            return false;
        bindingStatus.clear();
        binding = -1;
        switch (option)
        {
        case 0: gConfig.fov = gConfig.fov >= 100 ? 55 : std::min(100, gConfig.fov + 5); resize(); break;
        case 1: gConfig.invertY = !gConfig.invertY; break;
        case 2: gConfig.smoothCamera = !gConfig.smoothCamera; break;
        case 3: gConfig.uiSize = gConfig.uiSize >= 125 ? 75 : std::min(125, gConfig.uiSize + 25); resize(); break;
        case 4: gConfig.highContrast = !gConfig.highContrast; break;
        case 5: gConfig.toggleSprint = !gConfig.toggleSprint; break;
        case 6:
            gConfig.keyboardProfile = (gConfig.keyboardProfile + 1) % 3;
            gConfig.movement = {};
            gConfig.actions = Config{}.actions;
            break;
        case 7: gConfig.movement = {}; gConfig.actions = Config{}.actions; break;
        default: binding = option - 8; return true;
        }
        saveConfig(gConfig);
        return true;
    };

    int menuFocus = -1;
    constexpr Uint32 keyboardPointer = SDL_TOUCH_MOUSEID - 1;
    auto menuView = [&]
    {
        if (gSettingsMenuOpen) return 3;
        if (saveMenuOpen) return 2;
        if (pauseMenuOpen) return 1;
        return gMainMenuOpen ? 0 : -1;
    };
    int focusedView = -1;
    auto navigationPoints = [&]
    {
        std::vector<MenuPoint> points;
        auto center = [&](float x, float y, float w, float h)
        {
            points.push_back({x + w / 2, y + h / 2});
        };
        if (gSettingsMenuOpen)
        {
            auto pane = computeSettingsLayout(winW, winH);
            center(pane.sensMinusX, pane.sensMinusY, pane.sensMinusW, pane.sensMinusH);
            center(pane.sensPlusX, pane.sensPlusY, pane.sensPlusW, pane.sensPlusH);
            center(pane.fpsBoxX, pane.fpsBoxY, pane.fpsBoxW, pane.fpsBoxH);
            center(pane.fullscreenBoxX, pane.fullscreenBoxY, pane.fullscreenBoxW, pane.fullscreenBoxH);
            for (int i = 0; i < 16; ++i)
            {
                auto [x, y] = settingsOptionPosition(pane, i);
                center(x, y, 158, 32);
            }
            center(pane.backX, pane.backY, pane.backW, pane.backH);
        }
        else if (saveMenuOpen)
        {
            auto pane = computeSaveMenuLayout(winW, winH);
            center(pane.inputX, pane.inputY, pane.inputW, pane.inputH);
            center(pane.createX, pane.createY, pane.createW, pane.createH);
            center(pane.overwriteX, pane.overwriteY, pane.overwriteW, pane.overwriteH);
            center(pane.loadX, pane.loadY, pane.loadW, pane.loadH);
            for (int i = 0; i < 8; ++i)
            {
                auto [x, y] = saveActionPosition(pane, i);
                center(x, y, pane.createW, 36);
            }
            center(pane.backX, pane.backY, pane.backW, pane.backH);
        }
        else if (gMainMenuOpen || pauseMenuOpen)
        {
            MenuImageLayout layout(winW, winH);
            for (int i = 0; i < (pauseMenuOpen ? 4 : 3); ++i)
            {
                MenuPoint middle{};
                for (const auto &corner : menuSigns[i])
                {
                    middle.x += corner.x / 4;
                    middle.y += corner.y / 4;
                }
                points.push_back({layout.x + middle.x * layout.scale, layout.y + middle.y * layout.scale});
            }
            center(20, pauseMenuOpen ? 40 : 20, 152, 32);
        }
        return points;
    };

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
        if (!confirmation && !inventoryOpen && !pauseMenuOpen && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen && !gMainMenuOpen && !gSettingsMenuOpen)
        {
            const float sensitivity = gConfig.mouseSensitivity;
            player.yaw += smoothDX * sensitivity;
            player.pitch += smoothDY * sensitivity * (gConfig.invertY ? 1.0f : -1.0f);
            smoothDX *= gConfig.smoothCamera ? 0.5f : 0.0f;
            smoothDY *= gConfig.smoothCamera ? 0.5f : 0.0f;
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
            if (confirmation)
            {
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    resize();
                else if (e.type == SDL_MOUSEMOTION)
                {
                    mouseX = uiCoordinate(e.motion.x);
                    mouseY = uiCoordinate(e.motion.y);
                }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                {
                    mouseX = uiCoordinate(e.button.x);
                    mouseY = uiCoordinate(e.button.y);
                    int hit = ConfirmationLayout(winW, winH, confirmation->offerSave).hit(mouseX, mouseY);
                    if (hit >= 0)
                        resolveConfirmation(static_cast<ConfirmationChoice>(hit));
                }
                else if (e.type == SDL_KEYDOWN && !e.key.repeat)
                {
                    int count = confirmation->offerSave ? 3 : 2;
                    auto key = e.key.keysym.sym;
                    if (key == SDLK_ESCAPE)
                        resolveConfirmation(ConfirmationChoice::Cancel);
                    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
                        resolveConfirmation(static_cast<ConfirmationChoice>(confirmation->selected));
                    else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_TAB)
                    {
                        bool reverse = key == SDLK_LEFT || (key == SDLK_TAB && (e.key.keysym.mod & KMOD_SHIFT));
                        confirmation->selected = (confirmation->selected + count + (reverse ? -1 : 1)) % count;
                    }
                }
                if (!running)
                    break;
                continue;
            }
            if (focusedView != menuView())
            {
                menuFocus = -1;
                focusedView = menuView();
            }
            if (e.type == SDL_QUIT)
            {
                requestQuit();
            }
            else if (e.type == SDL_KEYDOWN)
            {
                if (gSettingsMenuOpen && binding >= 0 && !e.key.repeat)
                {
                    SDL_Keycode key = e.key.keysym.sym;
                    bool reserved = key < 'a' || key > 'z';
                    for (int i = 0; i < 8 && !reserved; ++i)
                    {
                        if (i == binding)
                            continue;
                        auto [primary, alternate] = movementKeys(gConfig, i);
                        reserved = key == primary || key == alternate;
                    }
                    if (key == SDLK_ESCAPE)
                    {
                        binding = -1;
                        bindingStatus.clear();
                    }
                    else if (reserved)
                        bindingStatus = "TOUCHE RESERVEE OU DEJA UTILISEE";
                    else
                    {
                        if (binding < 4)
                            gConfig.movement[binding] = key;
                        else
                            gConfig.actions[binding - 4] = key;
                        saveConfig(gConfig);
                        binding = -1;
                        bindingStatus.clear();
                    }
                    continue;
                }
                if (!e.key.repeat && (gMainMenuOpen || pauseMenuOpen) &&
                    (e.key.keysym.sym == SDLK_TAB || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                {
                    const auto positions = navigationPoints();
                    if (e.key.keysym.sym == SDLK_TAB && !positions.empty())
                    {
                        const int size = static_cast<int>(positions.size());
                        if (e.key.keysym.mod & KMOD_SHIFT)
                            menuFocus = (menuFocus < 0 ? size : menuFocus) - 1;
                        else
                            menuFocus = (menuFocus + 1) % size;
                        if (menuFocus < 0)
                            menuFocus = size - 1;
                        mouseX = static_cast<int>(positions[menuFocus].x);
                        mouseY = static_cast<int>(positions[menuFocus].y);
                        gSaveInputFocus = false;
                        continue;
                    }
                    if (menuFocus >= 0 && menuFocus < static_cast<int>(positions.size()))
                    {
                        const auto position = positions[menuFocus];
                        SDL_Event click{};
                        click.type = SDL_MOUSEBUTTONDOWN;
                        click.button.which = keyboardPointer;
                        click.button.button = SDL_BUTTON_LEFT;
                        click.button.x = static_cast<int>(std::ceil(position.x * uiScale));
                        click.button.y = static_cast<int>(std::ceil(position.y * uiScale));
                        SDL_PushEvent(&click);
                        continue;
                    }
                }
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN)
                    menuFocus = -1;
                if (canPlay() && !e.key.repeat && gConfig.toggleSprint &&
                    (e.key.keysym.sym == SDLK_LALT || e.key.keysym.sym == SDLK_RALT))
                    sprintToggled = !sprintToggled;
                if (!e.key.repeat && e.key.keysym.sym == SDLK_o && (e.key.keysym.mod & KMOD_CTRL) &&
                    (gMainMenuOpen || canPlay()) && !gSettingsMenuOpen)
                {
                    gMainMenuOpen = false;
                    pauseMenuOpen = saveMenuOpen = true;
                    saveStatus.clear();
                    gSaveInputFocus = false;
                    refreshSaveList();
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    SDL_ShowCursor(SDL_TRUE);
                    continue;
                }
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
                else if (!e.key.repeat && e.key.keysym.sym == SDLK_s && (e.key.keysym.mod & KMOD_CTRL) &&
                         (canPlay() || (pauseMenuOpen && !gSettingsMenuOpen)))
                {
                    if (e.key.keysym.mod & KMOD_SHIFT)
                    {
                        pauseMenuOpen = saveMenuOpen = true;
                        saveStatus.clear();
                        gSaveNameInput.clear();
                        gSaveInputFocus = true;
                        refreshSaveList();
                        SDL_StartTextInput();
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                    }
                    else
                    {
                        saveCurrent();
                    }
                }
                else if (saveMenuOpen && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) && !e.key.repeat)
                {
                    if (gSaveInputFocus)
                        saveAs();
                    else if (gSaveIndex >= 0 && gSaveIndex < static_cast<int>(gSaveList.size()))
                        requestOpenMap(gSaveList[gSaveIndex]);
                }
                else if (saveMenuOpen && !gSaveList.empty() &&
                         (e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN))
                {
                    int direction = e.key.keysym.sym == SDLK_UP ? -1 : 1;
                    gSaveIndex = std::clamp(gSaveIndex + direction, 0, static_cast<int>(gSaveList.size()) - 1);
                    saveOffset = std::clamp(saveOffset, std::max(0, gSaveIndex - saveRows + 1), gSaveIndex);
                    gSaveInputFocus = false;
                }
                else if (e.key.keysym.sym == SDLK_ESCAPE)
                {
                    if (pastePreview && canPlay())
                        pastePreview = false;
                    else if (gSignEditOpen)
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
                    else if (inventoryOpen)
                    {
                        closeInventory();
                    }
                    else if (saveMenuOpen)
                    {
                        saveMenuOpen = false;
                        gSaveInputFocus = false;
                        SDL_StopTextInput();
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
                        SDL_StopTextInput();
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        SDL_ShowCursor(SDL_TRUE);
                        smoothDX = smoothDY = 0.0f;
                    }
                }
                else if (inventoryOpen && e.key.keysym.sym == SDLK_TAB && !e.key.repeat)
                {
                    cancelInventoryGesture();
                    paletteOpen = !paletteOpen;
                    if (paletteOpen)
                    {
                        refreshPalette();
                        SDL_StartTextInput();
                    }
                    else
                        SDL_StopTextInput();
                }
                else if (inventoryOpen && paletteOpen && e.key.keysym.sym == SDLK_BACKSPACE)
                {
                    if (!paletteSearch.empty())
                        paletteSearch.pop_back();
                    refreshPalette();
                }
                else if (inventoryOpen && paletteOpen && e.key.keysym.sym == SDLK_RETURN && !e.key.repeat)
                {
                    cancelInventoryGesture();
                    paletteCategory = (paletteCategory + 1) % 4;
                    refreshPalette();
                }
                else if (e.key.keysym.sym == gConfig.actions[0] && (!inventoryOpen || !paletteOpen) && !pauseMenuOpen && !gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen)
                {
                    if (e.key.repeat)
                        continue;
                    if (inventoryOpen)
                        closeInventory();
                    else
                        inventoryOpen = true;
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
                else if (canPlay() && !e.key.repeat && (e.key.keysym.mod & KMOD_CTRL) &&
                         (e.key.keysym.sym == SDLK_z || e.key.keysym.sym == SDLK_y))
                {
                    if (history.apply(world, e.key.keysym.sym == SDLK_y, [&](BlockPosition position, BlockType type)
                    {
                        return !isSolid(type) || !blockIntersectsPlayer(player, position[0], position[1], position[2], PLAYER_HEIGHT);
                    }, markNeighborsDirty))
                        mapEdited = true;
                }
                else if (canPlay() && !e.key.repeat && (e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_b)
                {
                    HitInfo hit = targetBlock();
                    if (hit.hit)
                    {
                        BlockPosition position{hit.x, hit.y, hit.z};
                        if (!cornerA || cornerB)
                        {
                            cornerA = position;
                            cornerB.reset();
                        }
                        else
                            cornerB = position;
                    }
                }
                else if (canPlay() && !e.key.repeat && (e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_c)
                    copySelection();
                else if (canPlay() && !e.key.repeat && (e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_v)
                    pastePreview = !pastePreview && !clipboard.empty();
                else if (canPlay() && pastePreview && !e.key.repeat && e.key.keysym.sym == SDLK_RETURN)
                    pasteBlocks();
                else if (canPlay() && !e.key.repeat && e.key.keysym.sym == gConfig.actions[2])
                    resetPosition();
                else if (canPlay() && !e.key.repeat && e.key.keysym.sym == gConfig.actions[3])
                {
                    if (pastePreview)
                        rotateClipboard();
                    else if (e.key.keysym.mod & KMOD_SHIFT)
                    {
                        HitInfo hit = targetBlock();
                        if (hit.hit && isDirectional(world.get(hit.x, hit.y, hit.z)))
                            editCell({hit.x, hit.y, hit.z}, [&]
                            {
                                world.setOrientation(hit.x, hit.y, hit.z, world.getOrientation(hit.x, hit.y, hit.z) + 1);
                            });
                    }
                    else if (copiedSettings)
                        copiedSettings->orientation = (copiedSettings->orientation + 1) & 3;
                    else
                        placementTurns = (placementTurns + 1) & 3;
                    if (tutorial == 5 && hotbarSlots[selected].type == BlockType::NotGate)
                        tutorial = 6;
                }
                else if (canPlay() && !e.key.repeat && e.key.keysym.sym == SDLK_F9)
                {
                    HitInfo hit = targetBlock();
                    BlockPosition position{hit.x, hit.y, hit.z};
                    if (hit.hit && probe != position)
                        probe = position;
                    else
                        probe.reset();
                    samples.clear();
                }
                else if (canPlay() && !e.key.repeat && (e.key.keysym.sym == SDLK_F3 || e.key.keysym.sym == SDLK_F4))
                    exchangeTemplate(e.key.keysym.sym == SDLK_F4);
                else if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9 && !paletteOpen && !pauseMenuOpen && !gSignEditOpen &&
                         !gButtonEditOpen && !gWireInfoOpen && !gSplitterEditOpen && !gClockEditOpen)
                {
                    selected = static_cast<int>(e.key.keysym.sym - SDLK_1);
                    if (inventoryOpen && !inventoryButton && !e.key.repeat)
                    {
                        if (ItemStack *slot = slotAt(hitTestInventoryUI(mouseX, mouseY, winW, winH)))
                        {
                            std::swap(*slot, hotbarSlots[selected]);
                            mapEdited = true;
                        }
                    }
                }
                else if (saveMenuOpen && gSaveInputFocus && e.key.keysym.sym == SDLK_BACKSPACE)
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
                else if (gSignEditOpen && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER))
                {
                    editCell({gSignEditX, gSignEditY, gSignEditZ}, [&]
                    {
                        world.setSignText(gSignEditX, gSignEditY, gSignEditZ, gSignEditBuffer);
                    });
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
                    editCell({gButtonEditX, gButtonEditY, gButtonEditZ}, [&]
                    {
                        world.setButtonWidth(gButtonEditX, gButtonEditY, gButtonEditZ, static_cast<uint8_t>(width));
                        world.setButtonValue(gButtonEditX, gButtonEditY, gButtonEditZ, static_cast<uint8_t>(v));
                    });
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
                    editCell({gSplitterX, gSplitterY, gSplitterZ}, [&]
                    {
                        world.setSplitterWidth(gSplitterX, gSplitterY, gSplitterZ, static_cast<uint8_t>(width));
                        world.setSplitterOrder(gSplitterX, gSplitterY, gSplitterZ, gSplitterOrder ? 1 : 0);
                    });
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
                    editCell({gClockEditX, gClockEditY, gClockEditZ}, [&]
                    {
                        world.setClockFreq(gClockEditX, gClockEditY, gClockEditZ, static_cast<uint8_t>(freq));
                    });
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
                    jumpUntil = elapsedTime + 0.12f;
                    if (lastSpaceTap >= 0.0f && (elapsedTime - lastSpaceTap) <= SPRINT_DOUBLE_TAP)
                    {
                        flying = !flying;
                        if (flying)
                            player.vy = 0.0f;
                    }
                    lastSpaceTap = elapsedTime;
                }
                else if (!gSignEditOpen && !gButtonEditOpen && !gSplitterEditOpen && !gWireInfoOpen && !gClockEditOpen &&
                         (e.key.keysym.sym == movementKeys(gConfig, 0).first ||
                          e.key.keysym.sym == movementKeys(gConfig, 0).second) && e.key.repeat == 0)
                {
                    if (lastForwardTap >= 0.0f && (elapsedTime - lastForwardTap) <= SPRINT_DOUBLE_TAP)
                    {
                        sprinting = true;
                    }
                    lastForwardTap = elapsedTime;
                }
                else if (!(e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == movementKeys(gConfig, 5).first && !inventoryOpen && !pauseMenuOpen && !gSignEditOpen &&
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
                mouseX = uiCoordinate(e.motion.x);
                mouseY = uiCoordinate(e.motion.y);
                menuFocus = -1;
                if (inventoryOpen && inventoryButton)
                    visitDragSlot(hitTestInventoryUI(mouseX, mouseY, winW, winH));
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
                resize();
            }
            else if (e.type == SDL_TEXTINPUT)
            {
                if (inventoryOpen && paletteOpen)
                {
                    for (unsigned char c : std::string(e.text.text))
                        if ((std::isalnum(c) || c == ' ' || c == '-') && paletteSearch.size() < 28)
                            paletteSearch.push_back(static_cast<char>(std::tolower(c)));
                    refreshPalette();
                }
                else if (saveMenuOpen)
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
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                cancelInventoryGesture();
                repeatingButton = 0;
                sprinting = sprintToggled = false;
                lastForwardTap = lastSpaceTap = jumpUntil = groundedUntil = -1.0f;
                smoothDX = smoothDY = 0.0f;
            }
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == repeatingButton)
            {
                repeatingButton = 0;
            }
            else if (e.type == SDL_MOUSEBUTTONUP && inventoryOpen && e.button.button == inventoryButton)
            {
                SlotHit release = hitTestInventoryUI(uiCoordinate(e.button.x), uiCoordinate(e.button.y), winW, winH);
                visitDragSlot(release);
                bool right = inventoryButton == SDL_BUTTON_RIGHT;
                if (dragging)
                    distributeItems(held, dragSlots, right);
                else if (ItemStack *slot = slotAt(release); slot && slot == slotAt(pressSlot))
                    clickStack(held, *slot, right);
                if (pressSlot.isHotbar || release.isHotbar ||
                    std::any_of(dragSlots.begin(), dragSlots.end(), [&](ItemStack *slot)
                    {
                        return std::any_of(hotbarSlots.begin(), hotbarSlots.end(),
                                           [slot](ItemStack &entry) { return &entry == slot; });
                    }))
                    mapEdited = true;
                cancelInventoryGesture();
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN)
            {
                mouseX = uiCoordinate(e.button.x);
                mouseY = uiCoordinate(e.button.y);
                if (e.button.which != keyboardPointer)
                    menuFocus = -1;
                if (gMainMenuOpen)
                {
                    if (e.button.button != SDL_BUTTON_LEFT)
                        continue;
                    if (!gSettingsMenuOpen && pointInRect(mouseX, mouseY, 20, 20, 152, 32))
                    {
                        gMainMenuOpen = false;
                        pauseMenuOpen = saveMenuOpen = true;
                        gSaveInputFocus = false;
                        refreshSaveList();
                        continue;
                    }
                    if (gSettingsMenuOpen)
                    {
                        SettingsMenuLayout s = computeSettingsLayout(winW, winH);
                        bool hoverMinus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensMinusX, s.sensMinusY, s.sensMinusW, s.sensMinusH);
                        bool hoverPlus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensPlusX, s.sensPlusY, s.sensPlusW, s.sensPlusH);
                        bool hoverFps = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fpsBoxX, s.fpsBoxY, s.fpsBoxW, s.fpsBoxH);
                        bool hoverFullscreen = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fullscreenBoxX, s.fullscreenBoxY, s.fullscreenBoxW, s.fullscreenBoxH);
                        bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.backX, s.backY, s.backW, s.backH);
                        if (settingsClick(mouseX, mouseY))
                            continue;
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
                            binding = -1;
                            bindingStatus.clear();
                        }
                        continue;
                    }
                    if (e.button.button != SDL_BUTTON_LEFT) continue;
                    int sign = MenuImageLayout(winW,winH).signAt(mouseX,mouseY,false);
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
                    if (e.button.button != SDL_BUTTON_LEFT)
                        continue;
                    if (gSettingsMenuOpen)
                    {
                        SettingsMenuLayout s = computeSettingsLayout(winW, winH);
                        bool hoverMinus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensMinusX, s.sensMinusY, s.sensMinusW, s.sensMinusH);
                        bool hoverPlus = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.sensPlusX, s.sensPlusY, s.sensPlusW, s.sensPlusH);
                        bool hoverFps = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fpsBoxX, s.fpsBoxY, s.fpsBoxW, s.fpsBoxH);
                        bool hoverFullscreen = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.fullscreenBoxX, s.fullscreenBoxY, s.fullscreenBoxW, s.fullscreenBoxH);
                        bool hoverBack = pointInRect(static_cast<float>(mouseX), static_cast<float>(mouseY), s.backX, s.backY, s.backW, s.backH);
                        if (settingsClick(mouseX, mouseY))
                            continue;
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
                            binding = -1;
                            bindingStatus.clear();
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

                        int action = -1;
                        for (int i = 0; i < 8; ++i)
                        {
                            auto [x, y] = saveActionPosition(sm, i);
                            if (pointInRect(mouseX, mouseY, x, y, sm.createW, 36.0f))
                                action = i;
                        }
                        if (action == 0)
                            saveCurrent();
                        else if (action == 1)
                            requestNewMap(false);
                        else if (action == 2 || action == 3)
                            duplicateOrRename(action == 2);
                        else if (action == 4 && !gSaveList.empty())
                            requestOpenMap(gSaveList.front());
                        else if (action == 5 || action == 6)
                        {
#ifdef _WIN32
                            namespace fs = std::filesystem;
                            bool exporting = action == 6;
                            if (exporting && (gSaveIndex < 0 || gSaveIndex >= static_cast<int>(gSaveList.size())))
                                continue;
                            fs::path external = chooseMapFile(window, exporting);
                            if (external.empty())
                                continue;
                            fs::path internal = exporting ? fs::path(gSaveList[gSaveIndex]) : gMapsDir / (normalizeSaveInput(external.stem().u8string()) + ".bulldog");
                            std::error_code ec;
                            if (!exporting)
                            {
                                World candidate(WIDTH, HEIGHT, DEPTH);
                                uint32_t importedSeed;
                                if (!loadWorldFromFile(candidate, external, importedSeed))
                                {
                                    saveStatus = "FORMAT DE CARTE INVALIDE";
                                    continue;
                                }
                            }
                            const fs::path &from = exporting ? internal : external;
                            const fs::path &to = exporting ? external : internal;
                            fs::copy_file(from, to, exporting ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
                            if (ec)
                                saveStatus = "COPIE IMPOSSIBLE OU NOM DEJA UTILISE";
                            else
                            {
                                saveStatus = exporting ? "CARTE EXPORTEE" : "CARTE IMPORTEE";
                                if (fs::exists(from.string() + ".hotbar", ec) && !ec)
                                    fs::copy_file(from.u8string() + ".hotbar", to.u8string() + ".hotbar",
                                                  exporting ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
                                if (ec)
                                    saveStatus += " - BARRE RAPIDE NON COPIEE";
                            }
                            refreshSaveList();
#else
                            saveStatus = "COPIEZ LES FICHIERS BULLDOG VIA LE DOSSIER MAPS";
#endif
                        }
                        else if (action == 7)
                            requestNewMap(true);

                        if (hoverCreate)
                        {
                            saveAs();
                        }
                        else if (gSaveIndex >= 0 && gSaveIndex < static_cast<int>(gSaveList.size()))
                        {
                            const std::string path = gSaveList[gSaveIndex];
                            if (hoverOverwrite)
                                requestConfirmation("REMPLACER LA SAUVEGARDE ?", stemFromPath(path).substr(0, 52), false,
                                                    [&, path] { saveMap(path); });
                            else if (hoverLoad)
                                requestOpenMap(path);
                        }
                        if (hoverBack)
                        {
                            saveMenuOpen = false;
                            gSaveInputFocus = false;
                            SDL_StopTextInput();
                        }
                    }
                    else
                    {
                        if (e.button.button != SDL_BUTTON_LEFT) continue;
                        if (pointInRect(mouseX, mouseY, 20, 40, 152, 32))
                        {
                            saveCurrent();
                            continue;
                        }
                        int sign = MenuImageLayout(winW,winH).signAt(mouseX,mouseY,true);
                        if (sign == 3)
                        {
                            requestQuit();
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
                    inventoryFull = false;
                    const Uint8 button = e.button.button;
                    if (inventoryButton)
                        continue;
                    SlotHit hit = hitTestInventoryUI(uiCoordinate(e.button.x), uiCoordinate(e.button.y), winW, winH);
                    if (paletteOpen && hit.valid && !hit.isHotbar && paletteSlots[hit.idx].count)
                    {
                        BlockType type = paletteSlots[hit.idx].type;
                        if (button == SDL_BUTTON_MIDDLE)
                        {
                            if (!favorites.erase(type))
                                favorites.insert(type);
                            refreshPalette();
                            mapEdited = true;
                        }
                        else if (!held.count || held.type == type)
                        {
                            ItemStack stock{type, maxStackSize};
                            moveItems(stock, held, button == SDL_BUTTON_RIGHT ? 1 : maxStackSize);
                        }
                        continue;
                    }
                    if (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT)
                        continue;
                    ItemStack *slot = slotAt(hit);
                    if (button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_SHIFT))
                    {
                        bool bulk = e.button.clicks == 2 && slot == lastShiftSlot;
                        if (slot && (slot->count || bulk))
                        {
                            auto &source = hit.isHotbar ? hotbarSlots : inventorySlots;
                            auto &destination = hit.isHotbar ? inventorySlots : hotbarSlots;
                            const BlockType type = bulk ? lastShiftType : slot->type;
                            lastShiftSlot = slot;
                            lastShiftType = type;
                            mapEdited = true;
                            if (slot->type == type)
                                storeItems(*slot, destination);
                            if (bulk)
                                for (ItemStack &other : source)
                                    if (other.type == type)
                                        storeItems(other, destination);
                        }
                    }
                    else if (button == SDL_BUTTON_LEFT && e.button.clicks == 2 && slot)
                    {
                        mapEdited = true;
                        if (!held.count)
                            clickStack(held, *slot, false);
                        for (auto *zone : {&inventorySlots, &hotbarSlots})
                            for (ItemStack &other : *zone)
                                if (held.count && held.type == other.type)
                                    moveItems(other, held, maxStackSize);
                    }
                    else
                    {
                        inventoryButton = button;
                        pressSlot = hit;
                        visitDragSlot(hit);
                    }
                }
                else if (canPlay())
                {
                    if (e.button.button == SDL_BUTTON_MIDDLE)
                    {
                        HitInfo hit = targetBlock();
                        if (hit.hit)
                        {
                            BlockType type = world.get(hit.x, hit.y, hit.z);
                            if (type == BlockType::Air)
                                continue;
                            mapEdited = true;
                            copiedSettings.reset();
                            if (SDL_GetModState() & KMOD_CTRL)
                                copiedSettings = BlockState::read(world, {hit.x, hit.y, hit.z});
                            auto found = std::find_if(hotbarSlots.begin(), hotbarSlots.end(),
                                                      [type](const ItemStack &slot) { return slot.type == type; });
                            if (found != hotbarSlots.end())
                                selected = static_cast<int>(found - hotbarSlots.begin());
                            else
                            {
                                storeItems(hotbarSlots[selected], inventorySlots);
                                if (!hotbarSlots[selected].count)
                                    hotbarSlots[selected] = {type, maxStackSize};
                            }
                        }
                    }
                    else if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
                    {
                        bool forcePlace = (SDL_GetModState() & KMOD_SHIFT) != 0;
                        repeatingButton = editBlock(e.button.button, true, forcePlace) ? e.button.button : 0;
                        repeatAt = elapsedTime + 0.20f;
                    }
                }
            }
            if (!canPlay())
            {
                repeatingButton = 0;
                sprinting = sprintToggled = false;
                lastForwardTap = lastSpaceTap = jumpUntil = groundedUntil = -1.0f;
            }
        }

        if (!running)
            break;
        const bool play = canPlay();
        if (!play)
        {
            repeatingButton = 0;
            sprinting = sprintToggled = false;
            lastForwardTap = lastSpaceTap = jumpUntil = groundedUntil = -1.0f;
        }
        if (play && mapEdited && elapsedTime >= nextAutosave)
        {
            std::string base = activeMap.empty() ? std::to_string(seed) : stemFromPath(activeMap);
            std::string name = "recuperation-" + base + "-" + std::to_string(autosaveSlot + 1) + ".bulldog";
            std::string path = (gMapsDir / name).string();
            if (saveWorldToFile(world, path, seed))
            {
                saveHotbar(path, hotbarSlots, selected, favorites);
                autosaveSlot = (autosaveSlot + 1) % 3;
            }
            nextAutosave = elapsedTime + 120.0f;
        }
        const float simDt = play ? std::min(dt, 0.04f) : 0.0f;
        if (play && repeatingButton && elapsedTime >= repeatAt)
        {
            editBlock(repeatingButton, false, (SDL_GetModState() & KMOD_SHIFT) != 0);
            repeatAt = elapsedTime + 0.12f;
        }

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const SDL_Keymod mods = SDL_GetModState();
        auto movementHeld = [&](int index)
        {
            auto [primary, alternate] = movementKeys(gConfig, index);
            auto heldKey = [&](SDL_Keycode key)
            {
                return key != SDLK_UNKNOWN && !(key == SDLK_z && (mods & KMOD_CTRL)) &&
                       keys[SDL_GetScancodeFromKey(key)];
            };
            return heldKey(primary) || heldKey(alternate);
        };
        bool forwardHeld = movementHeld(0);
        bool leftHeld = movementHeld(2);
        bool onGround = collidesAt(world, player.x, player.y - 0.05f, player.z, PLAYER_HEIGHT);
        bool crouching = play && !flying && (mods & KMOD_SHIFT);
        if (!forwardHeld || !play || crouching)
            sprinting = false;

        if (play)
        {
            Vec3 fwd{std::sin(player.yaw), 0.0f, -std::cos(player.yaw)};
            Vec3 right{std::cos(player.yaw), 0.0f, std::sin(player.yaw)};
            bool fast = forwardHeld && !crouching && (sprinting || (gConfig.toggleSprint ? sprintToggled : (mods & KMOD_ALT) != 0));
            float moveSpeed = SPEED * (fast ? (flying ? FLY_SPRINT_MULT : SPRINT_MULT) : 1.0f);
            if (crouching)
                moveSpeed *= 0.35f;
            float forward = static_cast<int>(forwardHeld) - static_cast<int>(movementHeld(1));
            float sideways = static_cast<int>(movementHeld(3)) - static_cast<int>(leftHeld);
            float length = std::max(1.0f, std::hypot(forward, sideways));
            player.vx += (fwd.x * forward + right.x * sideways) * moveSpeed * simDt / length;
            player.vz += (fwd.z * forward + right.z * sideways) * moveSpeed * simDt / length;

            if (flying)
            {
                player.vy = (static_cast<int>(keys[SDL_SCANCODE_SPACE]) - static_cast<int>((mods & KMOD_SHIFT) != 0)) *
                            moveSpeed * FLY_VERTICAL_MULT;
            }
            else
            {
                if (onGround && player.vy <= 0.0f)
                    groundedUntil = elapsedTime + 0.08f;
                if (elapsedTime <= jumpUntil && elapsedTime <= groundedUntil)
                {
                    player.vy = JUMP;
                    jumpUntil = groundedUntil = -1.0f;
                }
            }
        }
        const int motionSteps = std::max(1, static_cast<int>(std::ceil(simDt * 120.0f)));
        const float step = simDt / motionSteps;
        for (int iteration = 0; step > 0.0f && iteration < motionSteps; ++iteration)
        {
            if (!flying)
                player.vy += (player.vy > 0.0f ? RISING_GRAVITY : FALLING_GRAVITY) * step;

            float nextY = player.y + player.vy * step;
            nextY = std::min(nextY, HEIGHT - PLAYER_HEIGHT - 0.01f);
            bool hitVertical = collidesAt(world, player.x, nextY, player.z, PLAYER_HEIGHT);
            if (hitVertical)
            {
                nextY = player.y;
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

            bool stayOnEdge = crouching && player.vy <= 0.0f &&
                              collidesAt(world, player.x, nextY - 0.06f, player.z, PLAYER_HEIGHT);
            float nextX = player.x + player.vx * step;
            nextX = std::clamp(nextX, 1.0f, WIDTH - 2.0f);
            if (collidesAt(world, nextX, nextY, player.z, PLAYER_HEIGHT) ||
                (stayOnEdge && !collidesAt(world, nextX, nextY - 0.06f, player.z, PLAYER_HEIGHT)))
            {
                nextX = player.x;
                player.vx = 0.0f;
            }

            float nextZ = player.z + player.vz * step;
            nextZ = std::clamp(nextZ, 1.0f, DEPTH - 2.0f);
            if (collidesAt(world, nextX, nextY, nextZ, PLAYER_HEIGHT) ||
                (stayOnEdge && !collidesAt(world, nextX, nextY - 0.06f, nextZ, PLAYER_HEIGHT)))
            {
                nextZ = player.z;
                player.vz = 0.0f;
            }

            player.x = nextX;
            player.y = nextY;
            player.z = nextZ;
            if (player.y < -4.0f)
                resetPosition();

            // If we touched the ground while flying, disable fly mode
            bool onGroundNow = collidesAt(world, player.x, player.y - 0.05f, player.z, PLAYER_HEIGHT);
            if (flying && onGroundNow && player.vy < 0.0f)
            {
                flying = false;
                player.vy = 0.0f;
            }
        }

        // Frame-rate independent damping (was frame-based 0.85)
        float damp = std::pow(0.85f, simDt / (1.0f / 60.0f));
        player.vx *= damp;
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
                drawSettingsMenu(winW, winH, s, gConfig, hoverBack, hoverMinus, hoverPlus, hoverFps, hoverFullscreen,
                                 mouseX, mouseY, binding, bindingStatus);
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
                drawSaveMenu(winW, winH, sm, gSaveIndex, hoverCreate, hoverOverwrite, hoverLoad, hoverBack, gSaveNameInput, mouseX, mouseY);
            }
            if (gMainMenuOpen && !gSettingsMenuOpen)
                menuButton(20, 20, 152, 32, "CARTES",
                           pointInRect(mouseX, mouseY, 20, 20, 152, 32));
            if (pauseMenuOpen && !saveMenuOpen && !gSettingsMenuOpen)
            {
                std::string label = activeMap.empty() ? "CARTE SANS NOM" : stemFromPath(activeMap).substr(0, 40);
                hudText(20, 20, 2, label + (mapEdited ? " *" : ""));
                menuButton(20, 40, 152, 32, "ENREGISTRER",
                           pointInRect(mouseX, mouseY, 20, 40, 152, 32));
                if (elapsedTime < saveNoticeUntil)
                    hudText(20, 86, 2, saveStatus);
            }
            if (menuFocus >= 0 && focusedView == menuView())
                drawOutline(mouseX - 5.0f, mouseY - 5.0f, 10, 10, 1.0f, 1.0f, 1.0f, 0.9f, 2);
            if (confirmation)
                drawMapConfirmation(winW, winH, *confirmation, mouseX, mouseY);
            endHud();
            SDL_GL_SwapWindow(window);
            updateTitle(window);
            continue;
        }

        auto advanceTick = [&]
        {
            updateLogic(world,
                [&](int x, int y, int z)
                {
                    if (world.get(x, y, z) != BlockType::Wire)
                        markChunkFromBlock(x, y, z);
                },
                [&](int x, int y, int z)
                {
                    if (world.get(x, y, z) == BlockType::Wire)
                        markNeighborsDirty(x, y, z);
                });
            if (tutorial == 4 && poweredTutorialLed)
            {
                const auto [x, y, z] = *poweredTutorialLed;
                if (world.get(x, y, z) == BlockType::Led && world.getPower(x, y, z))
                    tutorial = 5;
            }
            if (probe)
            {
                const auto [x, y, z] = *probe;
                if (samples.size() == 48)
                    samples.pop_front();
                samples.emplace_back(world.getPower(x, y, z), world.getPowerWidth(x, y, z));
            }
        };
        if (canSimulate())
        {
            tickBudget = std::min(tickBudget + dt, 2.0f * tickInterval);
            while (tickBudget >= tickInterval)
            {
                advanceTick();
                tickBudget -= tickInterval;
            }
        }
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
        auto drawPorts = [&](BlockType type, BlockPosition cell, uint8_t turns, bool preview)
        {
            bool blocked = false;
            const auto [x, y, z] = cell;
            for (const auto &port : circuitPorts(type))
            {
                auto [dx, dz] = rotateHorizontal(port.x, port.z, turns);
                int px = x + dx, py = y + port.y, pz = z + dz;
                bool inside = world.inside(px, py, pz);
                BlockType neighbor = inside ? world.get(px, py, pz) : BlockType::Air;
                bool incompatible = !inside || (isSolid(neighbor) && neighbor < BlockType::AndGate);
                if (isDirectional(neighbor))
                {
                    const auto otherTurns = world.getOrientation(px, py, pz);
                    const auto &otherPorts = circuitPorts(neighbor);
                    incompatible = std::none_of(otherPorts.begin(), otherPorts.end(), [&](const CircuitPort &other)
                    {
                        auto [ox, oz] = rotateHorizontal(other.x, other.z, otherTurns);
                        return ox == -dx && oz == -dz && other.y == -port.y && other.output != port.output;
                    });
                }
                blocked |= incompatible;
                float reach = preview ? 0.51f : 0.54f;
                Vec3 center{x + 0.5f + dx * reach, y + 0.5f + port.y * reach, z + 0.5f + dz * reach};
                Vec3 axis{static_cast<float>(dx), static_cast<float>(port.y), static_cast<float>(dz)};
                Vec3 wing = dx || dz ? Vec3{0, 0.10f, 0} : Vec3{0.10f, 0, 0};
                float direction = port.output ? 1.0f : -1.0f;
                Vec3 tip{center.x + axis.x * direction * 0.19f, center.y + axis.y * direction * 0.19f,
                         center.z + axis.z * direction * 0.19f};
                if (incompatible)
                    glColor3f(1.0f, 0.29f, 0.18f);
                else if (port.output)
                    glColor3f(1.0f, 0.83f, 0.17f);
                else
                    glColor3f(0.30f, 0.83f, 1.0f);
                glBegin(GL_TRIANGLES);
                glVertex3f(tip.x, tip.y, tip.z);
                glVertex3f(center.x + wing.x, center.y + wing.y, center.z + wing.z);
                glVertex3f(center.x - wing.x, center.y - wing.y, center.z - wing.z);
                glEnd();
            }
            return blocked;
        };
        bool incompatiblePorts = false;
        HoverLabel hoverLabel;
        if (play && hit.hit)
        {
            drawFaceHighlight(hit);
            drawBlockOutlineExact(hit.x, hit.y, hit.z);
            BlockType aimed = world.get(hit.x, hit.y, hit.z);
            if (isDirectional(aimed))
                incompatiblePorts = drawPorts(aimed, {hit.x, hit.y, hit.z}, world.getOrientation(hit.x, hit.y, hit.z), false);
            if (canPlace(hit) && !pastePreview)
            {
                drawOutlinedCube(hit.x + hit.nx + 0.5f, hit.y + hit.ny + 0.5f, hit.z + hit.nz + 0.5f, 1.004f);
                const BlockType selectedType = hotbarSlots[selected].type;
                const uint8_t orientation = copiedSettings && copiedSettings->type == selectedType ?
                                            copiedSettings->orientation : placementTurns;
                drawPorts(selectedType, {hit.x + hit.nx, hit.y + hit.ny, hit.z + hit.nz}, orientation, true);
            }
            if (pastePreview && validPaste(hit))
                for (const auto &entry : clipboard)
                {
                    const auto [dx, dy, dz] = entry.first;
                    drawBlockOutlineExact(hit.x + hit.nx + dx, hit.y + hit.ny + dy, hit.z + hit.nz + dz);
                }
        }
        if (play && brokenBlock && elapsedTime < breakEffectUntil)
            drawOutlinedCube((*brokenBlock)[0] + 0.5f, (*brokenBlock)[1] + 0.5f, (*brokenBlock)[2] + 0.5f,
                             0.25f + 4.0f * (breakEffectUntil - elapsedTime));
        if (play && probe)
            drawBlockOutlineExact((*probe)[0], (*probe)[1], (*probe)[2]);
        if (play && cornerA)
            drawBlockOutlineExact((*cornerA)[0], (*cornerA)[1], (*cornerA)[2]);
        if (play && cornerB)
            drawBlockOutlineExact((*cornerB)[0], (*cornerB)[1], (*cornerB)[2]);

        beginHud(winW, winH);
        if (gConfig.showFps)
        {
            char fpsBuf[32];
            std::snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.0f", fps);
            drawQuad(10.0f, 10.0f, 120.0f, 32.0f, 0.04f, 0.04f, 0.06f, 0.65f);
            drawOutline(10.0f, 10.0f, 120.0f, 32.0f, 1.0f, 1.0f, 1.0f, 0.12f, 2.0f);
            drawTextTiny(16.0f, 16.0f, 2.4f, fpsBuf, 1.0f, 0.97f, 0.9f, 1.0f);
        }
        if (elapsedTime < saveNoticeUntil)
            hudText(20.0f, 56.0f, 2, saveStatus);
        if (!inventoryOpen && !pauseMenuOpen)
        {
            drawCrosshair(winW, winH);
            drawInventoryBar(winW, winH, hotbarSlots, selected);
            if (play)
            {
                if (tutorial > 0)
                {
                    static constexpr const char *steps[] = {"", "1  POSEZ UN BOUTON AVEC LE CLIC DROIT",
                        "2  POSEZ UN FIL A COTE DU BOUTON", "3  POSEZ UNE LED AU BOUT DU FIL",
                        "4  ACTIVEZ LE BOUTON ET OBSERVEZ LA LED", "", "6  CTRL+S POUR ENREGISTRER", "CIRCUIT ENREGISTRE"};
                    std::string label = tutorial == 5 ? std::string("5  CHOISISSEZ LA PORTE (4), TOURNEZ AVEC ") +
                                                       SDL_GetKeyName(gConfig.actions[3]) : steps[tutorial];
                    hudText(20, winH - 130.0f, 2, label);
                }
                if (pastePreview)
                    hudText(20.0f, 106.0f, 2, validPaste(hit) ?
                            std::string("ENTREE COLLER   ") + SDL_GetKeyName(gConfig.actions[3]) + " TOURNER" : "COLLAGE BLOQUE");
                if (probe)
                {
                    drawQuad(20, 134, 258, 86, 0.04f, 0.04f, 0.06f, 0.88f);
                    std::string label = "SONDE";
                    if (!samples.empty())
                        label += " " + std::to_string(samples.back().first) + " / " + std::to_string(samples.back().second) + " BITS";
                    hudText(28, 142, 2, label);
                    float x = 29.0f;
                    for (const auto &[value, width] : samples)
                    {
                        int maxValue = (1 << std::clamp<int>(width, 1, 8)) - 1;
                        float bar = 1.0f + 52.0f * std::min<int>(value, maxValue) / maxValue;
                        drawQuad(x, 211.0f - bar, 4.0f, bar, 1.0f, 0.84f, 0.35f, 1.0f);
                        x += 5.0f;
                    }
                }
                if (hotbarSlots[selected].count)
                {
                    const BlockType selectedType = hotbarSlots[selected].type;
                    std::string label = std::to_string(selected + 1) + "  " + BLOCKS.at(selectedType).name;
                    if (isDirectional(selectedType))
                    {
                        const int turns = copiedSettings && copiedSettings->type == selectedType ?
                                          copiedSettings->orientation : placementTurns;
                        label += std::string("  ") + SDL_GetKeyName(gConfig.actions[3]) + " " + std::to_string(turns * 90);
                    }
                    hudText(20.0f, winH - 30.0f, 2, label);
                }
                if (hit.hit)
                {
                    const BlockType type = world.get(hit.x, hit.y, hit.z);
                    if (type == BlockType::Wire)
                    {
                        std::string signal = std::to_string(world.getPower(hit.x, hit.y, hit.z)) + " / " +
                                             std::to_string(world.getPowerWidth(hit.x, hit.y, hit.z)) + " BITS";
                        hudText(winW * 0.5f + 18, winH * 0.5f + 12, 2, signal);
                    }
                    else if (type == BlockType::Button)
                    {
                        hudText(winW * 0.5f + 18, winH * 0.5f + 12, 2,
                                std::string(SDL_GetKeyName(movementKeys(gConfig, 5).first)) + " REGLAGES");
                        hudText(winW * 0.5f + 18, winH * 0.5f + 32, 2, "CLIC DROIT ACTIONNER");
                    }
                    else if (type == BlockType::Sign)
                        hudText(winW * 0.5f + 18, winH * 0.5f + 12, 2, "CLIC DROIT INTERAGIR");
                    else if (isDirectional(type))
                    {
                        bool configurable = type == BlockType::Clock || type == BlockType::Splitter || type == BlockType::Merger;
                        std::string hint = configurable ? std::string(SDL_GetKeyName(movementKeys(gConfig, 5).first)) + " REGLAGES   " : "";
                        hint += std::string("MAJ+") + SDL_GetKeyName(gConfig.actions[3]) + " TOURNER";
                        hudText(winW * 0.5f + 18, winH * 0.5f + 12, 2, hint);
                        if (incompatiblePorts)
                            hudText(winW * 0.5f + 18, winH * 0.5f + 27, 2, "PORT BLOQUE OU INCOMPATIBLE");
                    }
                    else if (hotbarSlots[selected].count && !canPlace(hit))
                        hudText(winW * 0.5f + 18, winH * 0.5f + 12, 2, "POSE BLOQUEE");
                }
            }
        }
        if (inventoryOpen)
        {
            static constexpr const char *categories[] = {"TOUT", "MATERIAUX", "LOGIQUE", "FAVORIS"};
            const std::string title = paletteOpen ? std::string(categories[paletteCategory]) + "  " + paletteSearch : "INVENTAIRE";
            drawInventoryPanel(winW, winH, paletteOpen ? paletteSlots : inventorySlots, hotbarSlots, held, dragSlots, dragging,
                               inventoryButton == SDL_BUTTON_RIGHT, mouseX, mouseY, hoverLabel, title,
                               paletteOpen ? "TAB INVENTAIRE   ENTREE CATEGORIE   MOLETTE FAVORI" : "TAB PALETTE   1-9 RACCOURCIS",
                               paletteOpen ? &favorites : nullptr);
            if (inventoryFull)
                hudText(20, 24, 2, "INVENTAIRE PLEIN - RANGEZ LA PILE");
        }
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
        if (confirmation)
            drawMapConfirmation(winW, winH, *confirmation, mouseX, mouseY);
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
