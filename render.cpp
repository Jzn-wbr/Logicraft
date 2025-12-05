#include "render.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

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

            if ((styleSeed % 3 == 0) && (y % 8 == 0))
                shade *= 0.92f;
            if ((styleSeed % 4 == 1) && (x % 6 == 0))
                shade *= 0.9f;
            r = std::clamp(baseColor[0] * shade, 0.0f, 1.0f);
            g = std::clamp(baseColor[1] * shade, 0.0f, 1.0f);
            b = std::clamp(baseColor[2] * shade, 0.0f, 1.0f);

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
            else if (styleSeed == 88)
                A = 120;
            else if (styleSeed == 17)
                A = 220;
            else if (styleSeed == 19)
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

extern const std::map<char, std::array<uint8_t, 5>> FONT5x4; // defined in main

void blitTinyCharToTile(std::vector<uint8_t> &pix, int texW, int tileIdx, int x, int y, char c, int scale, uint8_t r,
                        uint8_t g, uint8_t b, uint8_t a)
{
    auto it = FONT5x4.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
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

    fillRect(pix, texW, tileX + centerX - 2, tileY, 4, midY - 1, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + 8, tileY + midY - 1, ATLAS_TILE_SIZE - 16, 2, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + 2, tileY + midY - 2, 5, 4, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - 7, tileY + midY - 2, 5, 4, accentR, accentG, accentB, 255);

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
        const float pad = 0.0015f;
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

        push(maxX, minY, minZ, u1, v1);
        push(maxX, maxY, minZ, u1, v0);
        push(maxX, maxY, maxZ, u0, v0);
        push(maxX, minY, maxZ, u0, v1);

        push(minX, minY, minZ, u1, v1);
        push(minX, minY, maxZ, u0, v1);
        push(minX, maxY, maxZ, u0, v0);
        push(minX, maxY, minZ, u1, v0);

        push(minX, maxY, minZ, u1, v1);
        push(maxX, maxY, minZ, u0, v1);
        push(maxX, maxY, maxZ, u0, v0);
        push(minX, maxY, maxZ, u1, v0);

        push(minX, minY, minZ, u1, v1);
        push(maxX, minY, minZ, u0, v1);
        push(maxX, minY, maxZ, u0, v0);
        push(minX, minY, maxZ, u1, v0);

        push(minX, minY, maxZ, u1, v1);
        push(maxX, minY, maxZ, u0, v1);
        push(maxX, maxY, maxZ, u0, v0);
        push(minX, maxY, maxZ, u1, v0);

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

void drawNpcBlocky(const NPC &npc)
{
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
        glVertex3f(x0, y0, z1);
        glVertex3f(x1, y0, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x0, y1, z1);
        glVertex3f(x1, y0, z0);
        glVertex3f(x0, y0, z0);
        glVertex3f(x0, y1, z0);
        glVertex3f(x1, y1, z0);
        glVertex3f(x0, y0, z0);
        glVertex3f(x0, y0, z1);
        glVertex3f(x0, y1, z1);
        glVertex3f(x0, y1, z0);
        glVertex3f(x1, y0, z1);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y1, z0);
        glVertex3f(x1, y1, z1);
        glVertex3f(x0, y1, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x1, y1, z0);
        glVertex3f(x0, y1, z0);
        glVertex3f(x0, y0, z0);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y0, z1);
        glVertex3f(x0, y0, z1);
        glEnd();
    };

    auto drawHead = [&](float cx, float cy, float cz)
    {
        float hx = s * 0.5f;
        float x0 = cx - hx, x1 = cx + hx;
        float y0 = cy - hx, y1 = cy + hx;
        float z0 = cz - hx, z1 = cz + hx;

        if (npc.texture == 0)
        {
            drawColoredCube(cx, cy, cz, s, 0.9f, 0.9f, 0.9f);
            return;
        }

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, npc.texture);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        // Front (+Z)
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(x0, y0, z1);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(x1, y0, z1);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(x1, y1, z1);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(x0, y1, z1);
        // Back (-Z)
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(x1, y0, z0);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(x0, y0, z0);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(x0, y1, z0);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(x1, y1, z0);
        // Left (-X)
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(x0, y0, z0);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(x0, y0, z1);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(x0, y1, z1);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(x0, y1, z0);
        // Right (+X)
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(x1, y0, z1);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(x1, y0, z0);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(x1, y1, z0);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(x1, y1, z1);
        // Top (+Y) left plain white (no texture)
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);

        glDisable(GL_TEXTURE_2D);
        glColor3f(0.95f, 0.95f, 0.95f);
        glBegin(GL_QUADS);
        glVertex3f(x0, y1, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x1, y1, z0);
        glVertex3f(x0, y1, z0);
        // Bottom (-Y) plain white too
        glVertex3f(x0, y0, z0);
        glVertex3f(x1, y0, z0);
        glVertex3f(x1, y0, z1);
        glVertex3f(x0, y0, z1);
        glEnd();
    };

    drawHead(npc.x, baseY + s * 2.2f, npc.z);
    drawColoredCube(npc.x, baseY + s * 0.8f, npc.z, s * 1.6f, 0.2f, 0.5f, 0.9f);
    drawColoredCube(npc.x, baseY + s * 0.2f, npc.z, s * 0.6f, 0.2f, 0.5f, 0.9f);
}
