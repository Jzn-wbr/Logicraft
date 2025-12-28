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
const int ATLAS_ROWS = 13; // increased to fit new gate tiles
const int ATLAS_TILE_SIZE = 32;
std::map<BlockType, int> gBlockTile;
int gAndTopTile = 0;
int gOrTopTile = 0;
int gNotTopTile = 0;
int gXorTopTile = 0;
int gDffTopTile = 0;
int gAddTopTile = 0;
int gAddBottomTile = 0;
int gAddBackTile = 0;
int gCounterTopTile = 0;
int gSplitterTopTile = 0;
int gMergerTopTile = 0;
int gDecoderTopTile = 0;
int gMuxTopTile = 0;
int gMuxInTile[4] = {0, 0, 0, 0};
int gComparatorTopTile = 0;
int gComparatorInLeftTile = 0;
int gComparatorInRightTile = 0;
int gComparatorGtTile = 0;
int gComparatorEqTile = 0;
int gComparatorLtTile = 0;
int gClockTopTile = 0;
int gGrassTopTile = 0;
const int MAX_STACK = 64;
const int INV_COLS = 7;
const int INV_ROWS = 4;

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

void ensureVbo(GLuint &vbo)
{
    if (vbo == 0)
    {
        glGenBuffers(1, &vbo);
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
            if (styleSeed == 88)
            {
                // Glass: uniform tint, very transparent
                uint8_t R = static_cast<uint8_t>(baseColor[0] * 255.0f);
                uint8_t G = static_cast<uint8_t>(baseColor[1] * 255.0f);
                uint8_t B = static_cast<uint8_t>(baseColor[2] * 255.0f);
                uint8_t A = 60;
                writePixel(pix, texW, x0 + x, y0 + y, R, G, B, A);
            }
            else
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
                else if (styleSeed == 17)
                    A = 220;
                else if (styleSeed == 19)
                    A = 255;
                writePixel(pix, texW, x0 + x, y0 + y, R, G, B, A);
            }
        }
    }
}

void fillRect(std::vector<uint8_t> &pix, int texW, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
              uint8_t a);

void fillWoodTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor)
{
    int tileX = tileIdx % ATLAS_COLS;
    int tileY = tileIdx / ATLAS_COLS;
    int x0 = tileX * ATLAS_TILE_SIZE;
    int y0 = tileY * ATLAS_TILE_SIZE;

    auto toByte = [](float v)
    { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };

    int plankW = ATLAS_TILE_SIZE / 3;
    int seam = 1;
    float plankTint[3] = {0.03f, 0.0f, 0.06f};

    for (int y = 0; y < ATLAS_TILE_SIZE; ++y)
    {
        for (int x = 0; x < ATLAS_TILE_SIZE; ++x)
        {
            int p = std::min(x / plankW, 2);
            float tone = plankTint[p];
            float n = hashNoise(x + p * 11, y * 2 + p * 19, 51);
            float grain = 0.08f * std::sin((y * 0.6f + n * 5.5f));
            float shade = 0.7f + grain + tone;

            float r = baseColor[0] * shade;
            float g = baseColor[1] * shade;
            float b = baseColor[2] * shade;
            writePixel(pix, texW, x0 + x, y0 + y, toByte(r), toByte(g), toByte(b), 255);
        }
    }

    uint8_t seamR = toByte(baseColor[0] * 0.45f);
    uint8_t seamG = toByte(baseColor[1] * 0.45f);
    uint8_t seamB = toByte(baseColor[2] * 0.45f);
    fillRect(pix, texW, x0 + plankW - seam, y0, seam, ATLAS_TILE_SIZE, seamR, seamG, seamB, 255);
    fillRect(pix, texW, x0 + 2 * plankW - seam, y0, seam, ATLAS_TILE_SIZE, seamR, seamG, seamB, 255);

    auto drawKnot = [&](int cx, int cy, int radius)
    {
        for (int y = -radius; y <= radius; ++y)
        {
            for (int x = -radius; x <= radius; ++x)
            {
                float d2 = static_cast<float>(x * x + y * y);
                if (d2 > radius * radius)
                    continue;
                float ring = std::clamp((radius * radius - d2) / (radius * radius), 0.0f, 1.0f);
                float rim = std::sin((1.0f - ring) * 3.1415926f);
                float dark = 0.45f + (1.0f - ring) * 0.18f;
                float light = 0.65f + rim * 0.2f;
                int px = cx + x;
                int py = cy + y;
                if (px < 0 || px >= ATLAS_TILE_SIZE || py < 0 || py >= ATLAS_TILE_SIZE)
                    continue;
                float blend = ring * 0.7f + 0.3f;
                uint8_t r = toByte(baseColor[0] * (dark * blend + light * (1.0f - blend)));
                uint8_t g = toByte(baseColor[1] * (dark * blend + light * (1.0f - blend)));
                uint8_t b = toByte(baseColor[2] * (dark * blend + light * (1.0f - blend)));
                writePixel(pix, texW, x0 + px, y0 + py, r, g, b, 255);
            }
        }
    };

    drawKnot(plankW / 2, ATLAS_TILE_SIZE / 3, 4);
    drawKnot(plankW + plankW / 2 + 2, ATLAS_TILE_SIZE * 2 / 3, 3);
}

void fillGrassSideTile(std::vector<uint8_t> &pix, int texW, int tileIdx, int dirtTileIdx,
                       const std::array<float, 3> &grassColor, const std::array<float, 3> &dirtColor)
{
    int tileX = tileIdx % ATLAS_COLS;
    int tileY = tileIdx / ATLAS_COLS;
    int x0 = tileX * ATLAS_TILE_SIZE;
    int y0 = tileY * ATLAS_TILE_SIZE;
    int bandH = std::max(2, static_cast<int>(ATLAS_TILE_SIZE * 0.2f));

    auto toByte = [](float v)
    { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };

    auto sample = [&](const std::array<float, 3> &baseColor, int styleSeed, int tileSeed, int x, int y)
    {
        float n = hashNoise(x, y + tileSeed * 17, styleSeed);
        float shade = 0.85f + n * 0.25f;
        if ((styleSeed % 3 == 0) && (y % 8 == 0))
            shade *= 0.92f;
        if ((styleSeed % 4 == 1) && (x % 6 == 0))
            shade *= 0.9f;
        float r = std::clamp(baseColor[0] * shade, 0.0f, 1.0f);
        float g = std::clamp(baseColor[1] * shade, 0.0f, 1.0f);
        float b = std::clamp(baseColor[2] * shade, 0.0f, 1.0f);
        return std::array<float, 3>{r, g, b};
    };

    for (int y = 0; y < ATLAS_TILE_SIZE; ++y)
    {
        bool isGrass = (y < bandH);
        float mix = (y == bandH || y == bandH - 1) ? 0.5f : (isGrass ? 1.0f : 0.0f);
        for (int x = 0; x < ATLAS_TILE_SIZE; ++x)
        {
            auto grass = sample(grassColor, 3, tileIdx, x, y);
            auto dirt = sample(dirtColor, 4, dirtTileIdx, x, y);
            float r = dirt[0] * (1.0f - mix) + grass[0] * mix;
            float g = dirt[1] * (1.0f - mix) + grass[1] * mix;
            float b = dirt[2] * (1.0f - mix) + grass[2] * mix;
            writePixel(pix, texW, x0 + x, y0 + y, toByte(r), toByte(g), toByte(b), 255);
        }
    }
}

void fillStoneBrickTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor)
{
    int tileX = tileIdx % ATLAS_COLS;
    int tileY = tileIdx / ATLAS_COLS;
    int x0 = tileX * ATLAS_TILE_SIZE;
    int y0 = tileY * ATLAS_TILE_SIZE;

    auto toByte = [](float v)
    { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };

    const int brickH = 6;
    const int mortar = 1;
    uint8_t mortarR = toByte(baseColor[0] * 0.8f);
    uint8_t mortarG = toByte(baseColor[1] * 0.8f);
    uint8_t mortarB = toByte(baseColor[2] * 0.8f);

    for (int y = 0; y < ATLAS_TILE_SIZE; ++y)
    {
        int row = y / brickH;
        int yIn = y % brickH;
        bool mortarRow = (yIn == brickH - 1);
        int rowOffset = (row % 2) ? (brickH / 2) : 0;
        for (int x = 0; x < ATLAS_TILE_SIZE; ++x)
        {
            int xShift = (x + rowOffset) % ATLAS_TILE_SIZE;
            int xIn = xShift % brickH;
            bool mortarCol = (xIn == brickH - 1);
            if (mortarRow || mortarCol)
            {
                writePixel(pix, texW, x0 + x, y0 + y, mortarR, mortarG, mortarB, 255);
                continue;
            }

            float n = hashNoise(x + row * 13, y + row * 7, 61);
            float shade = 1.0f + n * 0.05f;
            float r = baseColor[0] * shade;
            float g = baseColor[1] * shade;
            float b = baseColor[2] * shade;
            writePixel(pix, texW, x0 + x, y0 + y, toByte(r), toByte(g), toByte(b), 255);
        }
    }
}

void fillWireTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor)
{
    int tileX = tileIdx % ATLAS_COLS;
    int tileY = tileIdx / ATLAS_COLS;
    int x0 = tileX * ATLAS_TILE_SIZE;
    int y0 = tileY * ATLAS_TILE_SIZE;

    auto toByte = [](float v)
    { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };

    for (int y = 0; y < ATLAS_TILE_SIZE; ++y)
    {
        for (int x = 0; x < ATLAS_TILE_SIZE; ++x)
        {
            float n = hashNoise(x, y + tileIdx * 13, 43);
            float grain = (hashNoise(x * 3, y * 3 + 7, 91) - 0.5f) * 0.05f;
            float stripe = ((x + y) % 9 == 0) ? 0.025f : 0.0f;
            float xNorm = static_cast<float>(x) / static_cast<float>(ATLAS_TILE_SIZE - 1);
            float highlight = 0.06f * (0.5f + 0.5f * std::cos((xNorm - 0.35f) * 3.1415926f));
            float shade = 0.3f + n * 0.08f + grain + stripe + highlight;
            float r = baseColor[0] * shade;
            float g = baseColor[1] * shade;
            float b = baseColor[2] * shade;
            writePixel(pix, texW, x0 + x, y0 + y, toByte(r), toByte(g), toByte(b), 255);
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
                       const std::string &gateLabel, const char *left = "IN", const char *right = "IN",
                       const char *out = "OUT", bool outBottom = false, bool inputsTop = false,
                       bool centerDown = false)
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

    if (centerDown)
        fillRect(pix, texW, tileX + centerX - 2, tileY + midY - 1, 4, ATLAS_TILE_SIZE - (midY - 1), accentR, accentG,
                 accentB, 255);
    else
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
    int leftWidth = tinyTextWidthOnTile(left, labelScale);
    int rightWidth = tinyTextWidthOnTile(right, labelScale);
    int outWidth = tinyTextWidthOnTile(out, labelScale);
    int gateWidth = tinyTextWidthOnTile(gateLabel, labelScale);

    int inY = inputsTop ? 4 : (ATLAS_TILE_SIZE - labelHeight - 3);
    int outY = outBottom ? (inputsTop ? ATLAS_TILE_SIZE - labelHeight - 3 : inY) : 4;
    int gateY = midY - 2;

    fillRect(pix, texW, tileX + 1, tileY + inY - 1, leftWidth + 4, labelHeight + 2, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - rightWidth - 5, tileY + inY - 1, rightWidth + 4, labelHeight + 2,
             bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - outWidth) / 2 - 2, tileY + outY - 1, outWidth + 4,
             labelHeight + 2, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - gateWidth) / 2 - 2, tileY + gateY - 1, gateWidth + 4,
             labelHeight + 2, bgR, bgG, bgB, 255);

    blitTinyTextToTile(pix, texW, tileIdx, 2, inY, left, labelScale, textR, textG, textB, 255);
    int rightX = ATLAS_TILE_SIZE - rightWidth - 2;
    blitTinyTextToTile(pix, texW, tileIdx, rightX, inY, right, labelScale, textR, textG, textB, 255);
    int outX = (ATLAS_TILE_SIZE - outWidth) / 2;
    blitTinyTextToTile(pix, texW, tileIdx, outX, outY, out, labelScale, textR, textG, textB, 255);
    int gateX = (ATLAS_TILE_SIZE - gateWidth) / 2;
    blitTinyTextToTile(pix, texW, tileIdx, gateX, gateY, gateLabel, labelScale, textR, textG, textB, 255);
}

void fillGateTileWithLabels(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                            int styleSeed, const std::string &gateLabel, const char *left = "IN",
                            const char *right = "IN", const char *out = "OUT", bool outBottom = false,
                            bool inputsTop = false, bool centerDown = false)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    drawGateTopLabels(pix, texW, tileIdx, baseColor, gateLabel, left, right, out, outBottom, inputsTop, centerDown);
}

void fillNotGateTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                     int styleSeed)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    auto toByte = [](float v, float mul)
    {
        return static_cast<uint8_t>(std::clamp(v * mul, 0.0f, 1.0f) * 255.0f);
    };
    uint8_t textR = 245, textG = 245, textB = 240;
    uint8_t accentR = toByte(baseColor[0], 1.25f);
    uint8_t accentG = toByte(baseColor[1], 1.25f);
    uint8_t accentB = toByte(baseColor[2], 1.25f);
    uint8_t bgR = toByte(baseColor[0], 0.55f);
    uint8_t bgG = toByte(baseColor[1], 0.55f);
    uint8_t bgB = toByte(baseColor[2], 0.55f);

    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int centerY = ATLAS_TILE_SIZE / 2;

    // single input bar on left
    fillRect(pix, texW, tileX + 2, tileY + centerY - 2, 10, 4, accentR, accentG, accentB, 255);
    // output bar on right
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - 12, tileY + centerY - 2, 10, 4, accentR, accentG, accentB, 255);
    // small triangle body
    for (int i = 0; i < 8; ++i)
    {
        int y = centerY - 6 + i;
        int width = 8 + i * 2;
        int startX = (ATLAS_TILE_SIZE - width) / 2;
        fillRect(pix, texW, tileX + startX, tileY + y, width, 1, accentR, accentG, accentB, 255);
    }
    // bubble inversion
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - 18, tileY + centerY - 4, 8, 8, textR, textG, textB, 255);

    // labels
    int scale = 1;
    int inWidth = tinyTextWidthOnTile("IN", scale);
    int outWidth = tinyTextWidthOnTile("OUT", scale);
    fillRect(pix, texW, tileX + 2, tileY + ATLAS_TILE_SIZE - 9, inWidth + 4, 7, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - outWidth - 6, tileY + 2, outWidth + 4, 7, bgR, bgG, bgB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, 4, ATLAS_TILE_SIZE - 8, "IN", scale, textR, textG, textB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, ATLAS_TILE_SIZE - outWidth - 4, 4, "OUT", scale, textR, textG, textB, 255);
    int notW = tinyTextWidthOnTile("NOT", scale);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - notW) / 2 - 2, tileY + centerY - 6, notW + 4, 7, bgR, bgG, bgB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, (ATLAS_TILE_SIZE - notW) / 2, centerY - 5, "NOT", scale, textR, textG, textB,
                       255);
}

void fillDffTopTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                    int styleSeed)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    auto toByte = [](float v, float mul)
    {
        return static_cast<uint8_t>(std::clamp(v * mul, 0.0f, 1.0f) * 255.0f);
    };
    uint8_t textR = 245, textG = 245, textB = 240;
    uint8_t accentR = toByte(baseColor[0], 1.25f);
    uint8_t accentG = toByte(baseColor[1], 1.25f);
    uint8_t accentB = toByte(baseColor[2], 1.25f);
    uint8_t bgR = toByte(baseColor[0], 0.55f);
    uint8_t bgG = toByte(baseColor[1], 0.55f);
    uint8_t bgB = toByte(baseColor[2], 0.55f);

    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int centerX = ATLAS_TILE_SIZE / 2;
    int centerY = ATLAS_TILE_SIZE / 2;

    // D input rail (left) and CLK rail (right)
    fillRect(pix, texW, tileX + 2, tileY + centerY - 2, centerX - 6, 4, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + centerX + 4, tileY + centerY - 2, ATLAS_TILE_SIZE - centerX - 6, 4, accentR, accentG,
             accentB, 255);
    // Clock teeth on the right rail
    for (int i = 0; i < 3; ++i)
    {
        int toothY = centerY - 6 + i * 4;
        fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - 8, tileY + toothY, 3, 3, textR, textG, textB, 255);
    }

    // Vertical pillar to the Q output
    fillRect(pix, texW, tileX + centerX - 1, tileY + 4, 3, centerY - 6, accentR, accentG, accentB, 255);
    fillRect(pix, texW, tileX + centerX - 5, tileY + 6, 10, 3, accentR, accentG, accentB, 255);

    int scale = 1;
    int dWidth = tinyTextWidthOnTile("D", scale);
    int clkWidth = tinyTextWidthOnTile("CLK", scale);
    int qWidth = tinyTextWidthOnTile("Q", scale);
    int gateWidth = tinyTextWidthOnTile("DFF", scale);

    int labelBgH = 7;
    int baseLabelY = ATLAS_TILE_SIZE - labelBgH - 2;
    fillRect(pix, texW, tileX + 2, tileY + baseLabelY - 1, dWidth + 4, labelBgH, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - clkWidth - 6, tileY + baseLabelY - 1, clkWidth + 4, labelBgH, bgR,
             bgG, bgB, 255);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - qWidth) / 2 - 2, tileY + 2, qWidth + 4, labelBgH, bgR, bgG, bgB, 255);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - gateWidth) / 2 - 2, tileY + centerY - 7, gateWidth + 4, labelBgH,
             bgR, bgG, bgB, 255);

    blitTinyTextToTile(pix, texW, tileIdx, 4, baseLabelY, "D", scale, textR, textG, textB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, ATLAS_TILE_SIZE - clkWidth - 4, baseLabelY, "CLK", scale, textR, textG,
                       textB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, (ATLAS_TILE_SIZE - qWidth) / 2, 3, "Q", scale, textR, textG, textB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, (ATLAS_TILE_SIZE - gateWidth) / 2, centerY - 6, "DFF", scale, textR, textG,
                       textB, 255);
}

void fillAddTopTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                    int styleSeed)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    drawGateTopLabels(pix, texW, tileIdx, baseColor, "ADD");
}

void fillAddBottomTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                       int styleSeed)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    int scale = 1;
    int coutWidth = tinyTextWidthOnTile("C OUT", scale);
    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int x = tileX + (ATLAS_TILE_SIZE - coutWidth) / 2;
    int y = tileY + (ATLAS_TILE_SIZE - 5 * scale) / 2;
    blitTinyTextToTile(pix, texW, tileIdx, x - tileX, y - tileY, "C OUT", scale, 245, 245, 245, 255);
}

void fillAddBackTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                     int styleSeed)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    int scale = 1;
    int cinWidth = tinyTextWidthOnTile("C IN", scale);
    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int x = tileX + (ATLAS_TILE_SIZE - cinWidth) / 2;
    int y = tileY + (ATLAS_TILE_SIZE - 5 * scale) / 2;
    blitTinyTextToTile(pix, texW, tileIdx, x - tileX, y - tileY, "C IN", scale, 245, 245, 245, 255);
}

void fillCounterTopTile(std::vector<uint8_t> &pix, int texW, int tileIdx, const std::array<float, 3> &baseColor,
                        int styleSeed)
{
    fillTile(pix, texW, tileIdx, baseColor, styleSeed);
    auto toByte = [](float v, float mul)
    {
        return static_cast<uint8_t>(std::clamp(v * mul, 0.0f, 1.0f) * 255.0f);
    };
    uint8_t bgR = toByte(baseColor[0], 0.55f);
    uint8_t bgG = toByte(baseColor[1], 0.55f);
    uint8_t bgB = toByte(baseColor[2], 0.55f);
    uint8_t textR = 245, textG = 245, textB = 240;
    int tileX = (tileIdx % ATLAS_COLS) * ATLAS_TILE_SIZE;
    int tileY = (tileIdx / ATLAS_COLS) * ATLAS_TILE_SIZE;
    int scale = 2;
    int qWidth = tinyTextWidthOnTile("CTR", scale);
    fillRect(pix, texW, tileX + (ATLAS_TILE_SIZE - qWidth) / 2 - 2, tileY + ATLAS_TILE_SIZE / 2 - 13, qWidth + 4, 13,
             bgR, bgG, bgB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, (ATLAS_TILE_SIZE - qWidth) / 2, ATLAS_TILE_SIZE / 2 - 11, "CTR", scale, textR,
                       textG, textB, 255);
    // Input hint on the +X side
    int inScale = 1;
    int inW = tinyTextWidthOnTile("IN", inScale);
    fillRect(pix, texW, tileX + ATLAS_TILE_SIZE - inW - 23, tileY + ATLAS_TILE_SIZE - 12, inW + 4, 7, bgR, bgG, bgB, 255);
    blitTinyTextToTile(pix, texW, tileIdx, ATLAS_TILE_SIZE - inW - 21, ATLAS_TILE_SIZE - 11, "IN", inScale, textR, textG,
                       textB, 255);
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

GLuint loadCubemapFromBMP(const std::array<std::string, 6> &paths)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    int baseW = -1;
    int baseH = -1;
    bool success = true;
    for (int i = 0; i < 6; ++i)
    {
        SDL_Surface *surf = SDL_LoadBMP(paths[i].c_str());
        if (!surf)
        {
            std::cerr << "Failed to load cubemap face \"" << paths[i] << "\": " << SDL_GetError() << "\n";
            success = false;
            continue;
        }
        SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(surf);
        if (!rgba)
        {
            std::cerr << "Failed to convert cubemap face \"" << paths[i] << "\" to RGBA: " << SDL_GetError() << "\n";
            success = false;
            continue;
        }
        if (baseW < 0)
        {
            baseW = rgba->w;
            baseH = rgba->h;
        }
        else if (rgba->w != baseW || rgba->h != baseH)
        {
            std::cerr << "Cubemap face \"" << paths[i] << "\" has different size (" << rgba->w << "x" << rgba->h
                      << "), expected " << baseW << "x" << baseH << ".\n";
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba->pixels);
        SDL_FreeSurface(rgba);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    if (!success)
    {
        glDeleteTextures(1, &tex);
        return 0;
    }
    return tex;
}

void drawSkybox(GLuint cubemap, float size)
{
    if (cubemap == 0)
        return;

    float half = size * 0.5f;

    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean tex2DEnabled = glIsEnabled(GL_TEXTURE_2D);
    GLint prevEnvMode = GL_MODULATE;
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &prevEnvMode);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    if (tex2DEnabled)
        glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);

    GLfloat viewMat[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, viewMat);
    // remove translation so the skybox stays centered on camera
    viewMat[12] = viewMat[13] = viewMat[14] = 0.0f;

    glPushMatrix();
    glLoadMatrixf(viewMat);

    glBegin(GL_QUADS);
    // +X (Right) rotated 90° CW (roll)
    glTexCoord3f(half, half, -half);
    glVertex3f(half, -half, -half);
    glTexCoord3f(half, -half, -half);
    glVertex3f(half, -half, half);
    glTexCoord3f(half, -half, half);
    glVertex3f(half, half, half);
    glTexCoord3f(half, half, half);
    glVertex3f(half, half, -half);

    // -X (Left) rotated -90°
    glTexCoord3f(-half, half, -half);
    glVertex3f(-half, -half, half);
    glTexCoord3f(-half, half, half);
    glVertex3f(-half, -half, -half);
    glTexCoord3f(-half, -half, half);
    glVertex3f(-half, half, -half);
    glTexCoord3f(-half, -half, -half);
    glVertex3f(-half, half, half);

    // +Y (Top)
    glTexCoord3f(-half, half, -half);
    glVertex3f(-half, half, -half);
    glTexCoord3f(half, half, -half);
    glVertex3f(half, half, -half);
    glTexCoord3f(half, half, half);
    glVertex3f(half, half, half);
    glTexCoord3f(-half, half, half);
    glVertex3f(-half, half, half);

    // -Y (Bottom) rotated 180°
    glTexCoord3f(half, -half, -half);
    glVertex3f(-half, -half, half);
    glTexCoord3f(-half, -half, -half);
    glVertex3f(half, -half, half);
    glTexCoord3f(-half, -half, half);
    glVertex3f(half, -half, -half);
    glTexCoord3f(half, -half, half);
    glVertex3f(-half, -half, -half);

    // +Z (Front)
    glTexCoord3f(-half, -half, half);
    glVertex3f(-half, -half, half);
    glTexCoord3f(-half, half, half);
    glVertex3f(-half, half, half);
    glTexCoord3f(half, half, half);
    glVertex3f(half, half, half);
    glTexCoord3f(half, -half, half);
    glVertex3f(half, -half, half);

    // -Z (Back) rotated 180°
    glTexCoord3f(-half, half, -half);
    glVertex3f(half, -half, -half);
    glTexCoord3f(-half, -half, -half);
    glVertex3f(half, half, -half);
    glTexCoord3f(half, -half, -half);
    glVertex3f(-half, half, -half);
    glTexCoord3f(half, half, -half);
    glVertex3f(-half, -half, -half);
    glEnd();

    glPopMatrix();
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glDisable(GL_TEXTURE_CUBE_MAP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, prevEnvMode);
    if (tex2DEnabled)
        glEnable(GL_TEXTURE_2D);
    if (cullEnabled)
        glEnable(GL_CULL_FACE);
    if (depthEnabled)
        glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}
void createAtlasTexture()
{
    gBlockTile = {{BlockType::Grass, 0},
                  {BlockType::Dirt, 1},
                  {BlockType::Stone, 2},
                  {BlockType::Wood, 3},
                  {BlockType::Leaves, 4},
                  {BlockType::Water, 5},
                  {BlockType::Plank, 6},
                  {BlockType::Sand, 7},
                  {BlockType::Air, 8},
                  {BlockType::Glass, 9},
                  {BlockType::AndGate, 10},
                  {BlockType::OrGate, 11},
                  {BlockType::NotGate, 12},
                  {BlockType::XorGate, 13},
                  {BlockType::Led, 14},
                  {BlockType::Button, 15},
                  {BlockType::Wire, 16},
                  {BlockType::Sign, 17},
                  {BlockType::DFlipFlop, 18},
                  {BlockType::AddGate, 19},
                  {BlockType::Counter, 20},
                  {BlockType::Splitter, 21},
                  {BlockType::Merger, 22},
                  {BlockType::Decoder, 23},
                  {BlockType::Multiplexer, 24},
                  {BlockType::Comparator, 25},
                  {BlockType::Clock, 26}};

    int nextTile = static_cast<int>(gBlockTile.size());
    gGrassTopTile = nextTile++;
    gAndTopTile = nextTile++;
    gOrTopTile = nextTile++;
    gNotTopTile = nextTile++;
    gXorTopTile = nextTile++;
    gDffTopTile = nextTile++;
    gAddTopTile = nextTile++;
    gAddBottomTile = nextTile++;
    gAddBackTile = nextTile++;
    gCounterTopTile = nextTile++;
    gSplitterTopTile = nextTile++;
    gMergerTopTile = nextTile++;
    gDecoderTopTile = nextTile++;
    gMuxTopTile = nextTile++;
    gMuxInTile[0] = nextTile++;
    gMuxInTile[1] = nextTile++;
    gMuxInTile[2] = nextTile++;
    gMuxInTile[3] = nextTile++;
    gComparatorTopTile = nextTile++;
    gComparatorInLeftTile = nextTile++;
    gComparatorInRightTile = nextTile++;
    gComparatorGtTile = nextTile++;
    gComparatorEqTile = nextTile++;
    gComparatorLtTile = nextTile++;
    gClockTopTile = nextTile++;

    int texW = ATLAS_COLS * ATLAS_TILE_SIZE;
    int texH = ATLAS_ROWS * ATLAS_TILE_SIZE;
    int atlasCapacity = ATLAS_COLS * ATLAS_ROWS;
    int maxTileIdx = 0;
    for (const auto &kv : gBlockTile)
        maxTileIdx = std::max(maxTileIdx, kv.second);
    maxTileIdx = std::max(maxTileIdx, std::max(std::max(gGrassTopTile, gAndTopTile), std::max(gOrTopTile, gXorTopTile)));
    maxTileIdx = std::max(maxTileIdx,
                          std::max(std::max(gDffTopTile, gAddTopTile), std::max(gAddBottomTile, gAddBackTile)));
    maxTileIdx = std::max(maxTileIdx, gCounterTopTile);
    maxTileIdx = std::max(maxTileIdx,
                          std::max(std::max(gSplitterTopTile, gMergerTopTile),
                                   std::max(gDecoderTopTile,
                                            std::max(gMuxTopTile,
                                                     std::max(gMuxInTile[0],
                                                              std::max(gMuxInTile[1],
                                                                       std::max(gMuxInTile[2], gMuxInTile[3])))))));
    maxTileIdx = std::max(maxTileIdx,
                          std::max(std::max(gComparatorTopTile, gComparatorInLeftTile),
                                   std::max(gComparatorInRightTile,
                                            std::max(gComparatorGtTile,
                                                     std::max(gComparatorEqTile, gComparatorLtTile)))));
    maxTileIdx = std::max(maxTileIdx, gClockTopTile);
    if (maxTileIdx >= atlasCapacity)
    {
        std::cerr << "Atlas capacity too small for gate labels.\n";
        return;
    }
    std::vector<uint8_t> pixels(texW * texH * 4, 0);

    auto base = [&](BlockType b)
    { return BLOCKS.at(b).color; };

    fillGrassSideTile(pixels, texW, gBlockTile[BlockType::Grass], gBlockTile[BlockType::Dirt], {0.16f, 0.42f, 0.18f},
                      base(BlockType::Dirt));
    fillTile(pixels, texW, gGrassTopTile, {0.16f, 0.42f, 0.18f}, 3);
    fillTile(pixels, texW, gBlockTile[BlockType::Dirt], base(BlockType::Dirt), 4);
    fillStoneBrickTile(pixels, texW, gBlockTile[BlockType::Stone], base(BlockType::Stone));
    fillWoodTile(pixels, texW, gBlockTile[BlockType::Wood], base(BlockType::Wood));
    fillTile(pixels, texW, gBlockTile[BlockType::Leaves], base(BlockType::Leaves), 5);
    fillTile(pixels, texW, gBlockTile[BlockType::Water], base(BlockType::Water), 99);
    fillTile(pixels, texW, gBlockTile[BlockType::Plank], base(BlockType::Plank), 0);
    fillTile(pixels, texW, gBlockTile[BlockType::Sand], base(BlockType::Sand), 2);
    fillTile(pixels, texW, gBlockTile[BlockType::Air], {0.7f, 0.85f, 1.0f}, 6);
    fillTile(pixels, texW, gBlockTile[BlockType::Glass], {0.85f, 0.9f, 0.95f}, 88);
    fillTile(pixels, texW, gBlockTile[BlockType::AndGate], base(BlockType::AndGate), 15);
    fillTile(pixels, texW, gBlockTile[BlockType::OrGate], base(BlockType::OrGate), 16);
    fillTile(pixels, texW, gBlockTile[BlockType::XorGate], base(BlockType::XorGate), 21);
    fillTile(pixels, texW, gBlockTile[BlockType::DFlipFlop], base(BlockType::DFlipFlop), 23);
    fillTile(pixels, texW, gBlockTile[BlockType::AddGate], base(BlockType::AddGate), 25);
    fillTile(pixels, texW, gBlockTile[BlockType::Counter], base(BlockType::Counter), 12);
    fillTile(pixels, texW, gBlockTile[BlockType::Led], base(BlockType::Led), 17);
    fillTile(pixels, texW, gBlockTile[BlockType::Button], base(BlockType::Button), 18);
    fillWireTile(pixels, texW, gBlockTile[BlockType::Wire], base(BlockType::Wire));
    fillTile(pixels, texW, gBlockTile[BlockType::NotGate], base(BlockType::NotGate), 20);
    fillTile(pixels, texW, gBlockTile[BlockType::Sign], base(BlockType::Sign), 1);
    fillTile(pixels, texW, gBlockTile[BlockType::Splitter], base(BlockType::Splitter), 22);
    fillTile(pixels, texW, gBlockTile[BlockType::Merger], base(BlockType::Merger), 23);
    fillTile(pixels, texW, gBlockTile[BlockType::Decoder], base(BlockType::Decoder), 24);
    fillTile(pixels, texW, gBlockTile[BlockType::Multiplexer], base(BlockType::Multiplexer), 26);
    fillTile(pixels, texW, gBlockTile[BlockType::Clock], base(BlockType::Clock), 34);
    // Comparator inventory tile styled like other gates with clear label
    fillGateTileWithLabels(pixels, texW, gBlockTile[BlockType::Comparator], base(BlockType::Comparator), 31, "CMP", "A",
                           "B", "OUT");
    fillTile(pixels, texW, gMuxInTile[0], base(BlockType::Multiplexer), 27);
    fillTile(pixels, texW, gMuxInTile[1], base(BlockType::Multiplexer), 28);
    fillTile(pixels, texW, gMuxInTile[2], base(BlockType::Multiplexer), 29);
    fillTile(pixels, texW, gMuxInTile[3], base(BlockType::Multiplexer), 30);
    blitTinyTextToTile(pixels, texW, gMuxInTile[0], 12, 12, "0", 3, 245, 245, 240, 255);
    blitTinyTextToTile(pixels, texW, gMuxInTile[1], 12, 12, "1", 3, 245, 245, 240, 255);
    blitTinyTextToTile(pixels, texW, gMuxInTile[2], 12, 12, "2", 3, 245, 245, 240, 255);
    blitTinyTextToTile(pixels, texW, gMuxInTile[3], 12, 12, "3", 3, 245, 245, 240, 255);
    fillGateTileWithLabels(pixels, texW, gAndTopTile, base(BlockType::AndGate), 15, "AND");
    fillGateTileWithLabels(pixels, texW, gOrTopTile, base(BlockType::OrGate), 16, "OR");
    fillGateTileWithLabels(pixels, texW, gXorTopTile, base(BlockType::XorGate), 17, "XOR");
    fillNotGateTile(pixels, texW, gNotTopTile, base(BlockType::NotGate), 15);
    fillDffTopTile(pixels, texW, gDffTopTile, base(BlockType::DFlipFlop), 23);
    fillAddTopTile(pixels, texW, gAddTopTile, base(BlockType::AddGate), 25);
    fillAddBottomTile(pixels, texW, gAddBottomTile, base(BlockType::AddGate), 25);
    fillAddBackTile(pixels, texW, gAddBackTile, base(BlockType::AddGate), 25);
    fillCounterTopTile(pixels, texW, gCounterTopTile, base(BlockType::Counter), 12);
    fillGateTileWithLabels(pixels, texW, gSplitterTopTile, base(BlockType::Splitter), 22, "SPLIT", "B2", "B1", "BUS",
                           true, true, true);
    fillGateTileWithLabels(pixels, texW, gMergerTopTile, base(BlockType::Merger), 23, "MERGE", "B2", "B1", "BUS");
    fillGateTileWithLabels(pixels, texW, gDecoderTopTile, base(BlockType::Decoder), 24, "DEC", "EN", "SEL", "OUT");
    fillGateTileWithLabels(pixels, texW, gMuxTopTile, base(BlockType::Multiplexer), 26, "MUX", "OUT", "SEL", "");

    auto fillComparatorLabel = [&](int tileIdx, const std::string &text)
    {
        fillTile(pixels, texW, tileIdx, base(BlockType::Comparator), 32);
        blitTinyTextToTile(pixels, texW, tileIdx, 8, 12, text, 4, 245, 245, 240, 255);
    };
    // Top face shows inputs like other gate tops
    fillGateTileWithLabels(pixels, texW, gComparatorTopTile, base(BlockType::Comparator), 32, "COMP", "A", "B", "OUT");
    // Side faces keep a neutral texture; only outputs show symbols with direction dots
    fillTile(pixels, texW, gComparatorInLeftTile, base(BlockType::Comparator), 32);
    fillTile(pixels, texW, gComparatorInRightTile, base(BlockType::Comparator), 32);
    fillComparatorLabel(gComparatorGtTile, ">.");
    fillComparatorLabel(gComparatorEqTile, "=");
    fillComparatorLabel(gComparatorLtTile, "<.");
    fillGateTileWithLabels(pixels, texW, gClockTopTile, base(BlockType::Clock), 35, "CLK", "", "", "OUT");

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
    mesh.glassVerts.clear();
    int x0 = cx * CHUNK_SIZE;
    int y0 = cy * CHUNK_SIZE;
    int z0 = cz * CHUNK_SIZE;
    int x1 = std::min(world.getWidth(), x0 + CHUNK_SIZE);
    int y1 = std::min(world.getHeight(), y0 + CHUNK_SIZE);
    int z1 = std::min(world.getDepth(), z0 + CHUNK_SIZE);

    const float lightX = -0.45f;
    const float lightY = 0.85f;
    const float lightZ = -0.35f;
    const float lightLen = std::sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);
    const float lx = lightX / lightLen;
    const float ly = lightY / lightLen;
    const float lz = lightZ / lightLen;

    auto occludesAt = [&](int ox, int oy, int oz)
    {
        if (!world.inside(ox, oy, oz))
            return false;
        return occludesFaces(world.get(ox, oy, oz));
    };

    auto aoFactor = [&](bool side1, bool side2, bool corner)
    {
        int occ = (side1 && side2) ? 3 : (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
        float ao = 1.0f - static_cast<float>(occ) * 0.18f;
        return std::clamp(ao, 0.5f, 1.0f);
    };

    auto faceLight = [&](int nx, int ny, int nz, float emissive)
    {
        float dot = nx * lx + ny * ly + nz * lz;
        float wrap = std::clamp(dot * 0.6f + 0.4f, 0.0f, 1.0f);
        float light = 0.35f + 0.65f * wrap;
        if (ny == 1)
            light *= 1.05f;
        else if (ny == -1)
            light *= 0.92f;
        light += emissive;
        return std::clamp(light, 0.2f, 1.2f);
    };

    auto addFace = [&](int x, int y, int z, const int nx, const int ny, const int nz, const std::array<float, 3> &col,
                       int tile, float emissive, bool toGlass)
    {
        auto &vec = toGlass ? mesh.glassVerts : mesh.verts;
        float bx = static_cast<float>(x);
        float by = static_cast<float>(y);
        float bz = static_cast<float>(z);
        float br = col[0];
        float bg = col[1];
        float bb = col[2];
        float baseLight = faceLight(nx, ny, nz, emissive);

        float du = 1.0f / ATLAS_COLS;
        float dv = 1.0f / ATLAS_ROWS;
        int tx = tile % ATLAS_COLS;
        int ty = tile / ATLAS_COLS;
        const float pad = 0.0015f;
        float u0 = tx * du + pad;
        float v0 = ty * dv + pad;
        float u1 = (tx + 1) * du - pad;
        float v1 = (ty + 1) * dv - pad;

        auto applyAo = [&](float ao)
        {
            if (toGlass)
                return 0.75f + 0.25f * ao;
            return ao;
        };

        auto push = [&](float px, float py, float pz, float u, float v, float shade)
        {
            float r = std::clamp(br * shade, 0.0f, 1.0f);
            float g = std::clamp(bg * shade, 0.0f, 1.0f);
            float b = std::clamp(bb * shade, 0.0f, 1.0f);
            vec.push_back(Vertex{px, py, pz, u, v, r, g, b});
        };

        auto sideDelta = [](int offset)
        { return offset == 1 ? 0 : -1; };

        auto aoForX = [&](int vy, int vz, int planeX)
        {
            int ySide = sideDelta(vy);
            int zSide = sideDelta(vz);
            bool side1 = occludesAt(planeX, y + vy + ySide, z + vz);
            bool side2 = occludesAt(planeX, y + vy, z + vz + zSide);
            bool corner = occludesAt(planeX, y + vy + ySide, z + vz + zSide);
            return aoFactor(side1, side2, corner);
        };

        auto aoForY = [&](int vx, int vz, int planeY)
        {
            int xSide = sideDelta(vx);
            int zSide = sideDelta(vz);
            bool side1 = occludesAt(x + vx + xSide, planeY, z + vz);
            bool side2 = occludesAt(x + vx, planeY, z + vz + zSide);
            bool corner = occludesAt(x + vx + xSide, planeY, z + vz + zSide);
            return aoFactor(side1, side2, corner);
        };

        auto aoForZ = [&](int vx, int vy, int planeZ)
        {
            int xSide = sideDelta(vx);
            int ySide = sideDelta(vy);
            bool side1 = occludesAt(x + vx + xSide, y + vy, planeZ);
            bool side2 = occludesAt(x + vx, y + vy + ySide, planeZ);
            bool corner = occludesAt(x + vx + xSide, y + vy + ySide, planeZ);
            return aoFactor(side1, side2, corner);
        };

        if (nx == 1)
        {
            int planeX = x + 1;
            float ao00 = applyAo(aoForX(0, 0, planeX));
            float ao10 = applyAo(aoForX(1, 0, planeX));
            float ao11 = applyAo(aoForX(1, 1, planeX));
            float ao01 = applyAo(aoForX(0, 1, planeX));
            push(bx + 1, by, bz, u1, v1, baseLight * ao00);
            push(bx + 1, by + 1, bz, u1, v0, baseLight * ao10);
            push(bx + 1, by + 1, bz + 1, u0, v0, baseLight * ao11);
            push(bx + 1, by, bz + 1, u0, v1, baseLight * ao01);
        }
        else if (nx == -1)
        {
            int planeX = x - 1;
            float ao00 = applyAo(aoForX(0, 0, planeX));
            float ao01 = applyAo(aoForX(0, 1, planeX));
            float ao11 = applyAo(aoForX(1, 1, planeX));
            float ao10 = applyAo(aoForX(1, 0, planeX));
            push(bx, by, bz, u1, v1, baseLight * ao00);
            push(bx, by, bz + 1, u0, v1, baseLight * ao01);
            push(bx, by + 1, bz + 1, u0, v0, baseLight * ao11);
            push(bx, by + 1, bz, u1, v0, baseLight * ao10);
        }
        else if (ny == 1)
        {
            int planeY = y + 1;
            float ao00 = applyAo(aoForY(0, 0, planeY));
            float ao10 = applyAo(aoForY(1, 0, planeY));
            float ao11 = applyAo(aoForY(1, 1, planeY));
            float ao01 = applyAo(aoForY(0, 1, planeY));
            push(bx, by + 1, bz, u1, v1, baseLight * ao00);
            push(bx + 1, by + 1, bz, u0, v1, baseLight * ao10);
            push(bx + 1, by + 1, bz + 1, u0, v0, baseLight * ao11);
            push(bx, by + 1, bz + 1, u1, v0, baseLight * ao01);
        }
        else if (ny == -1)
        {
            int planeY = y - 1;
            float ao00 = applyAo(aoForY(0, 0, planeY));
            float ao10 = applyAo(aoForY(1, 0, planeY));
            float ao11 = applyAo(aoForY(1, 1, planeY));
            float ao01 = applyAo(aoForY(0, 1, planeY));
            push(bx, by, bz, u1, v1, baseLight * ao00);
            push(bx + 1, by, bz, u0, v1, baseLight * ao10);
            push(bx + 1, by, bz + 1, u0, v0, baseLight * ao11);
            push(bx, by, bz + 1, u1, v0, baseLight * ao01);
        }
        else if (nz == 1)
        {
            int planeZ = z + 1;
            float ao00 = applyAo(aoForZ(0, 0, planeZ));
            float ao10 = applyAo(aoForZ(1, 0, planeZ));
            float ao11 = applyAo(aoForZ(1, 1, planeZ));
            float ao01 = applyAo(aoForZ(0, 1, planeZ));
            push(bx, by, bz + 1, u1, v1, baseLight * ao00);
            push(bx + 1, by, bz + 1, u0, v1, baseLight * ao10);
            push(bx + 1, by + 1, bz + 1, u0, v0, baseLight * ao11);
            push(bx, by + 1, bz + 1, u1, v0, baseLight * ao01);
        }
        else if (nz == -1)
        {
            int planeZ = z - 1;
            float ao00 = applyAo(aoForZ(0, 0, planeZ));
            float ao01 = applyAo(aoForZ(0, 1, planeZ));
            float ao11 = applyAo(aoForZ(1, 1, planeZ));
            float ao10 = applyAo(aoForZ(1, 0, planeZ));
            push(bx, by, bz, u1, v1, baseLight * ao00);
            push(bx, by + 1, bz, u1, v0, baseLight * ao01);
            push(bx + 1, by + 1, bz, u0, v0, baseLight * ao11);
            push(bx + 1, by, bz, u0, v1, baseLight * ao10);
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
    auto addWireBox = [&](float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                          const std::array<float, 3> &col, int tile, float emissive)
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

        auto push = [&](float px, float py, float pz, float u, float v, float shade)
        {
            float r = std::clamp(br * shade, 0.0f, 1.0f);
            float g = std::clamp(bg * shade, 0.0f, 1.0f);
            float b = std::clamp(bb * shade, 0.0f, 1.0f);
            mesh.verts.push_back(Vertex{px, py, pz, u, v, r, g, b});
        };

        float sPX = faceLight(1, 0, 0, emissive);
        float sNX = faceLight(-1, 0, 0, emissive);
        float sPY = faceLight(0, 1, 0, emissive);
        float sNY = faceLight(0, -1, 0, emissive);
        float sPZ = faceLight(0, 0, 1, emissive);
        float sNZ = faceLight(0, 0, -1, emissive);

        push(maxX, minY, minZ, u1, v1, sPX);
        push(maxX, maxY, minZ, u1, v0, sPX);
        push(maxX, maxY, maxZ, u0, v0, sPX);
        push(maxX, minY, maxZ, u0, v1, sPX);

        push(minX, minY, minZ, u1, v1, sNX);
        push(minX, minY, maxZ, u0, v1, sNX);
        push(minX, maxY, maxZ, u0, v0, sNX);
        push(minX, maxY, minZ, u1, v0, sNX);

        push(minX, maxY, minZ, u1, v1, sPY);
        push(maxX, maxY, minZ, u0, v1, sPY);
        push(maxX, maxY, maxZ, u0, v0, sPY);
        push(minX, maxY, maxZ, u1, v0, sPY);

        push(minX, minY, minZ, u1, v1, sNY);
        push(maxX, minY, minZ, u0, v1, sNY);
        push(maxX, minY, maxZ, u0, v0, sNY);
        push(minX, minY, maxZ, u1, v0, sNY);

        push(minX, minY, maxZ, u1, v1, sPZ);
        push(maxX, minY, maxZ, u0, v1, sPZ);
        push(maxX, maxY, maxZ, u0, v0, sPZ);
        push(minX, maxY, maxZ, u1, v0, sPZ);

        push(minX, minY, minZ, u1, v1, sNZ);
        push(minX, maxY, minZ, u1, v0, sNZ);
        push(maxX, maxY, minZ, u0, v0, sNZ);
        push(maxX, minY, minZ, u0, v1, sNZ);
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
                if (b == BlockType::Grass)
                {
                    color[0] = brightness;
                    color[1] = brightness;
                    color[2] = brightness;
                }
                else if (b == BlockType::Dirt)
                {
                    color[0] = brightness;
                    color[1] = brightness;
                    color[2] = brightness;
                }
                float emissive = 0.0f;
                if (b == BlockType::Led && world.getPower(x, y, z))
                {
                    color[0] = std::min(color[0] * 1.6f, 1.0f);
                    color[1] = std::min(color[1] * 1.4f, 1.0f);
                    color[2] = std::min(color[2] * 1.1f, 1.0f);
                    emissive = 0.25f;
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
                    color[0] = std::min(color[0] * 0.7f + 0.35f, 1.0f);
                    color[1] = std::min(color[1] * 0.7f + 0.55f, 1.0f);
                    color[2] = std::min(color[2] * 0.7f + 0.75f, 1.0f);
                    emissive = 0.6f;
                }
                else if (b == BlockType::DFlipFlop && world.getPower(x, y, z))
                {
                    color[0] = std::min(color[0] + 0.16f, 1.0f);
                    color[1] = std::min(color[1] + 0.08f, 1.0f);
                    color[2] = std::min(color[2] + 0.08f, 1.0f);
                    emissive = 0.08f;
                }
                else if (b == BlockType::AddGate)
                {
                    // no persistent power, but give a mild accent when adjacent wire powers it
                    if (world.getPower(x - 1, y, z) || world.getPower(x + 1, y, z) || world.getPower(x, y, z - 1))
                    {
                        color[0] = std::min(color[0] + 0.08f, 1.0f);
                        color[1] = std::min(color[1] + 0.08f, 1.0f);
                        color[2] = std::min(color[2] + 0.05f, 1.0f);
                    }
                }
                int tIdx = tileIndexFor(b);
                auto faceTile = [&](int nx, int ny, int nz)
                {
                    (void)nx;
                    (void)nz;
                    if (ny == 1 && b == BlockType::Grass)
                        return gGrassTopTile;
                    if (ny == -1 && b == BlockType::Grass)
                        return gBlockTile[BlockType::Dirt];
                    if (ny == 1)
                    {
                        if (b == BlockType::AndGate)
                            return gAndTopTile;
                        if (b == BlockType::OrGate)
                            return gOrTopTile;
                        if (b == BlockType::NotGate)
                            return gNotTopTile;
                        if (b == BlockType::XorGate)
                            return gXorTopTile;
                        if (b == BlockType::DFlipFlop)
                            return gDffTopTile;
                        if (b == BlockType::AddGate)
                            return gAddTopTile;
                        if (b == BlockType::Counter)
                            return gCounterTopTile;
                        if (b == BlockType::Splitter)
                            return gSplitterTopTile;
                        if (b == BlockType::Merger)
                            return gMergerTopTile;
                        if (b == BlockType::Decoder)
                            return gDecoderTopTile;
                        if (b == BlockType::Comparator)
                            return gComparatorTopTile;
                        if (b == BlockType::Clock)
                            return gClockTopTile;
                        if (b == BlockType::Multiplexer)
                            return gMuxTopTile;
                    }
                    if (ny == -1 && b == BlockType::AddGate)
                        return gAddBottomTile;
                    if (nz == -1 && b == BlockType::AddGate)
                        return gAddBackTile;
                    if (b == BlockType::Comparator)
                    {
                        if (nx == -1)
                            return gComparatorInLeftTile;
                        if (nx == 1)
                            return gComparatorInRightTile;
                        if (nz == -1)
                            return gComparatorGtTile;
                        if (nz == 1)
                            return gComparatorEqTile;
                        if (ny == -1)
                            return gComparatorLtTile;
                    }
                    if (b == BlockType::Multiplexer)
                    {
                        if (nz == -1)
                            return gMuxInTile[0];
                        if (nz == 1)
                            return gMuxInTile[1];
                        if (ny == -1)
                            return gMuxInTile[2];
                        if (ny == 1)
                            return gMuxInTile[3];
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
                        if (nb == BlockType::NotGate)
                        {
                            // Only connect on right (input) or left (output) sides
                            if (dx == 1)
                                return true; // input side
                            if (dx == -1)
                                return true; // output side
                            return false;
                        }
                        if (nb == BlockType::Counter)
                        {
                            // Counter input is on its +X face, so from the wire perspective the counter is at dx = -1
                            return dx == -1;
                        }
                        if (nb == BlockType::AndGate || nb == BlockType::OrGate || nb == BlockType::XorGate ||
                            nb == BlockType::DFlipFlop || nb == BlockType::AddGate)
                        {
                            // Inputs on left/right, Cin on -Z, Sum on +Z, Cout on -Y (top face is non-connectable)
                            if (dx == 1 || dx == -1)
                                return true; // P/Q
                            if (dz == -1)
                                return true; // Cin
                            if (dz == 1)
                                return true; // Sum
                            if (dy == 1)
                                return true; // Cout (wire is below, gate above)
                            if (dy == -1)
                                return false; // top blocked
                            if (nb == BlockType::Counter)
                            {
                                // only +X input
                                return dx == 1;
                            }
                            return false;
                        }
                        if (nb == BlockType::Splitter)
                        {
                            // BUS on -Z (in), B1 on -X (out), B2 on +X (out)
                            if (dx == -1 || dx == 1)
                                return true;
                            if (dz == 1)
                                return true; // BUS side (wire is at z-1, so from wire view dz=+1)
                            return false;
                        }
                        if (nb == BlockType::Merger)
                        {
                            // B1 on -X (in), B2 on +X (in), BUS on +Z (out)
                            if (dx == -1 || dx == 1)
                                return true; // inputs
                            if (dz == -1)
                                return true; // BUS side (+Z face of merger)
                            return false;
                        }
                        if (nb == BlockType::Decoder)
                        {
                            // SEL on -X (in), EN on +X (in), OUT on +Z
                            if (dx == -1 || dx == 1)
                                return true;
                            if (dz == -1)
                                return true; // OUT (+Z)
                            return false;
                        }
                        if (nb == BlockType::Multiplexer)
                        {
                            // SEL on -X, OUT on +X, D0 on -Z, D1 on +Z, D2 on -Y, D3 on +Y
                            if (dx == -1 || dx == 1)
                                return true;
                            if (dz == -1 || dz == 1)
                                return true;
                            if (dy == -1 || dy == 1)
                                return true;
                            return false;
                        }
                        if (nb == BlockType::Clock)
                        {
                            // Output on +Z only
                            return dz == -1;
                        }
                        if (nb == BlockType::Comparator)
                        {
                            // Inputs on -X/+X, outputs: > on -Z, = on +Z, < on -Y. Top (+Y) not connectable.
                            if (dy == -1)
                                return false; // top blocked
                            if (dx == -1 || dx == 1)
                                return true; // inputs
                            if (dz == -1 || dz == 1)
                                return true; // front/back outputs
                            if (dy == 1)
                                return true; // downward output
                            return false;
                        }
                        return nb == BlockType::Wire || nb == BlockType::Button || nb == BlockType::Led ||
                               nb == BlockType::AndGate || nb == BlockType::OrGate || nb == BlockType::DFlipFlop ||
                               nb == BlockType::Counter;
                    };

                    float cx = static_cast<float>(x) + 0.5f;
                    float cy = static_cast<float>(y) + 0.5f;
                    float cz = static_cast<float>(z) + 0.5f;
                    const float half = 0.12f;
                    const float margin = 0.04f;
                    const float join = 0.002f;

                    addWireBox(cx - half, cy - half, cz - half, cx + half, cy + half, cz + half, color, tIdx, emissive);
                    if (connects(1, 0, 0))
                        addWireBox(cx + join, cy - half, cz - half, static_cast<float>(x + 1) - margin, cy + half, cz + half,
                                   color, tIdx, emissive);
                    if (connects(-1, 0, 0))
                        addWireBox(static_cast<float>(x) + margin, cy - half, cz - half, cx - join, cy + half, cz + half, color,
                                   tIdx, emissive);
                    if (connects(0, 1, 0))
                        addWireBox(cx - half, cy + join, cz - half, cx + half, static_cast<float>(y + 1) - margin, cz + half,
                                   color, tIdx, emissive);
                    if (connects(0, -1, 0))
                        addWireBox(cx - half, static_cast<float>(y) + margin, cz - half, cx + half, cy - join, cz + half, color,
                                   tIdx, emissive);
                    if (connects(0, 0, 1))
                        addWireBox(cx - half, cy - half, cz + join, cx + half, cy + half, static_cast<float>(z + 1) - margin,
                                   color, tIdx, emissive);
                    if (connects(0, 0, -1))
                        addWireBox(cx - half, cy - half, static_cast<float>(z) + margin, cx + half, cy + half, cz - join, color,
                                   tIdx, emissive);
                    continue;
                }

                if (b == BlockType::Sign)
                {
                    float cx = static_cast<float>(x) + 0.5f;
                    float cz = static_cast<float>(z) + 0.5f;
                    float baseY = static_cast<float>(y);
                    float boardW = 0.9f;
                    float boardH = 0.6f;
                    float boardT = 0.08f;
                    float poleW = 0.12f;
                    float poleH = 0.6f;
                    float halfBoardW = boardW * 0.5f;
                    float halfBoardH = boardH * 0.5f;
                    float halfBoardT = boardT * 0.5f;
                    float halfPoleW = poleW * 0.5f;

                    float boardMinX = cx - halfBoardW;
                    float boardMaxX = cx + halfBoardW;
                    float boardMinY = baseY + 0.8f - halfBoardH;
                    float boardMaxY = baseY + 0.8f + halfBoardH;
                    float boardMinZ = cz - halfBoardT;
                    float boardMaxZ = cz + halfBoardT;

                    float poleMinX = cx - halfPoleW;
                    float poleMaxX = cx + halfPoleW;
                    float poleMinY = baseY + 0.1f;
                    float poleMaxY = baseY + 0.1f + poleH;
                    float poleMinZ = cz - halfPoleW;
                    float poleMaxZ = cz + halfPoleW;

                    int tile = tileIndexFor(b);
                    auto addBoxWithTile = [&](float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
                    {
                        addBox(minX, minY, minZ, maxX, maxY, maxZ, color, tile);
                    };

                    // Board + pole
                    addBoxWithTile(boardMinX, boardMinY, boardMinZ, boardMaxX, boardMaxY, boardMaxZ);
                    addBoxWithTile(poleMinX, poleMinY, poleMinZ, poleMaxX, poleMaxY, poleMaxZ);

                    // Text drawn slightly in front of the board (both faces), up to 4 lines of 16 chars
                    const std::string &txt = world.getSignText(x, y, z);
                    if (!txt.empty())
                    {
                        std::string text = txt;
                        const int glyphCols = 4;
                        const int glyphRows = 5;
                        const float spacingCols = 1.0f; // one empty column between chars
                        const int maxCharsPerLine = 16;
                        const int maxLines = 4;
                        const int maxTotalChars = maxCharsPerLine * maxLines;

                        if (static_cast<int>(text.size()) > maxTotalChars)
                            text.resize(maxTotalChars);
                        if (text.empty())
                            goto after_sign_text;

                        float marginX = boardW * 0.08f;
                        float marginY = boardH * 0.15f;
                        float usableW = boardW - marginX * 2.0f;
                        float usableH = boardH - marginY * 2.0f;
                        if (usableW <= 0.0f || usableH <= 0.0f)
                            goto after_sign_text;

                        // Split text into multiple lines (up to 4x16)
                        std::vector<std::string> lines;
                        for (size_t i = 0; i < text.size(); i += maxCharsPerLine)
                        {
                            if (static_cast<int>(lines.size()) >= maxLines)
                                break;
                            size_t len = std::min<size_t>(maxCharsPerLine, text.size() - i);
                            lines.emplace_back(text.substr(i, len));
                        }
                        if (lines.empty())
                            goto after_sign_text;

                        auto lineUnitsX = [&](int chars)
                        {
                            if (chars <= 0)
                                return 0.0f;
                            return static_cast<float>(chars * glyphCols) +
                                   static_cast<float>(std::max(0, chars - 1)) * spacingCols;
                        };

                        float longestUnitsX = 0.0f;
                        for (const auto &ln : lines)
                            longestUnitsX = std::max(longestUnitsX, lineUnitsX(static_cast<int>(ln.size())));
                        if (longestUnitsX <= 0.0f)
                            goto after_sign_text;

                        const int lineGapRows = 2;
                        int lineCount = static_cast<int>(lines.size());
                        float totalUnitsY = static_cast<float>(lineCount * glyphRows +
                                                               std::max(0, lineCount - 1) * lineGapRows);

                        float cellFromW = usableW / longestUnitsX;
                        float cellFromH = usableH / totalUnitsY;
                        float cell = std::min(cellFromW, cellFromH);
                        if (cell <= 0.0f)
                            goto after_sign_text;

                        float boardCenterY = 0.5f * (boardMinY + boardMaxY);
                        float totalTextHeight = totalUnitsY * cell;
                        // Y+ is up, so the top of the text block is at center + half height
                        float textMaxYAll = boardCenterY + totalTextHeight * 0.5f;

                        std::array<float, 3> textColor = {0.15f, 0.07f, 0.02f};

                        auto drawTextSide = [&](float zFront, float zBack, bool mirrorX)
                        {
                            for (int li = 0; li < static_cast<int>(lines.size()); ++li)
                            {
                                const std::string &line = lines[li];
                                if (line.empty())
                                    continue;

                                float lineUnitsYBefore = static_cast<float>(li * (glyphRows + lineGapRows));
                                float lineMaxY = textMaxYAll - lineUnitsYBefore * cell;

                                float unitsX = lineUnitsX(static_cast<int>(line.size()));
                                float lineWidth = unitsX * cell;
                                float lineMinX = cx - lineWidth * 0.5f;

                                for (size_t i = 0; i < line.size(); ++i)
                                {
                                    char c = static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
                                    auto it = FONT5x4.find(c);
                                    if (it == FONT5x4.end())
                                        continue;

                                    float charOffsetUnits =
                                        static_cast<float>(i) * (static_cast<float>(glyphCols) + spacingCols);
                                    float charMinX = lineMinX + charOffsetUnits * cell;

                                    const auto &rows = it->second;
                                    for (int row = 0; row < glyphRows; ++row)
                                    {
                                        uint8_t mask = rows[row];
                                        for (int col = 0; col < glyphCols; ++col)
                                        {
                                            if (!(mask & (1u << (glyphCols - 1 - col))))
                                                continue;
                                            float px0 = charMinX + static_cast<float>(col) * cell;
                                            float px1 = px0 + cell;
                                            if (mirrorX)
                                            {
                                                float d0 = px0 - cx;
                                                float d1 = px1 - cx;
                                                px0 = cx - d1;
                                                px1 = cx - d0;
                                            }
                                            float py1 = lineMaxY - static_cast<float>(row) * cell;
                                            float py0 = py1 - cell;
                                            addBox(px0, py0, zFront, px1, py1, zBack, textColor, tile);
                                        }
                                    }
                                }
                            }
                        };

                        // Front (+Z) and back (-Z, mirrored)
                        drawTextSide(boardMaxZ + 0.002f, boardMaxZ + 0.004f, false);
                        drawTextSide(boardMinZ - 0.004f, boardMinZ - 0.002f, true);
                    }

                after_sign_text:
                    continue;
                }

                bool isGlass = (b == BlockType::Glass);
                auto neighborIsGlass = [&](int nx, int ny, int nz)
                {
                    if (!world.inside(nx, ny, nz))
                        return false;
                    return world.get(nx, ny, nz) == BlockType::Glass;
                };

                if (x == 0 || (!occludesFaces(world.get(x - 1, y, z)) && !(isGlass && neighborIsGlass(x - 1, y, z))))
                    addFace(x, y, z, -1, 0, 0, color, faceTile(-1, 0, 0), emissive, isGlass);
                if (x == world.getWidth() - 1 ||
                    (!occludesFaces(world.get(x + 1, y, z)) && !(isGlass && neighborIsGlass(x + 1, y, z))))
                    addFace(x, y, z, 1, 0, 0, color, faceTile(1, 0, 0), emissive, isGlass);
                if (y == 0 || (!occludesFaces(world.get(x, y - 1, z)) && !(isGlass && neighborIsGlass(x, y - 1, z))))
                    addFace(x, y, z, 0, -1, 0, color, faceTile(0, -1, 0), emissive, isGlass);
                if (y == world.getHeight() - 1 ||
                    (!occludesFaces(world.get(x, y + 1, z)) && !(isGlass && neighborIsGlass(x, y + 1, z))))
                    addFace(x, y, z, 0, 1, 0, color, faceTile(0, 1, 0), emissive, isGlass);
                if (z == 0 || (!occludesFaces(world.get(x, y, z - 1)) && !(isGlass && neighborIsGlass(x, y, z - 1))))
                    addFace(x, y, z, 0, 0, -1, color, faceTile(0, 0, -1), emissive, isGlass);
                if (z == world.getDepth() - 1 ||
                    (!occludesFaces(world.get(x, y, z + 1)) && !(isGlass && neighborIsGlass(x, y, z + 1))))
                    addFace(x, y, z, 0, 0, 1, color, faceTile(0, 0, 1), emissive, isGlass);
            }
        }
    }

    ensureVbo(mesh.vbo);
    ensureVbo(mesh.glassVbo);
    if (!mesh.verts.empty())
    {
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.verts.size() * sizeof(Vertex), mesh.verts.data(), GL_STATIC_DRAW);
    }
    if (!mesh.glassVerts.empty())
    {
        glBindBuffer(GL_ARRAY_BUFFER, mesh.glassVbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.glassVerts.size() * sizeof(Vertex), mesh.glassVerts.data(), GL_STATIC_DRAW);
    }
    mesh.dirty = false;
}

void drawNpcBlocky(const NPC &npc)
{
    const float s = 0.25f;
    float baseY = npc.y;

    auto drawBox = [](float cx, float cy, float cz, float sx, float sy, float sz, float r, float g, float b)
    {
        float hx = sx * 0.5f;
        float hy = sy * 0.5f;
        float hz = sz * 0.5f;
        float x0 = cx - hx, x1 = cx + hx;
        float y0 = cy - hy, y1 = cy + hy;
        float z0 = cz - hz, z1 = cz + hz;
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

    auto drawColoredCube = [&](float cx, float cy, float cz, float size, float r, float g, float b)
    {
        drawBox(cx, cy, cz, size, size, size, r, g, b);
    };

    auto drawHead = [&](float cx, float cy, float cz)
    {
        float hx = s * 0.5f;
        float x0 = cx - hx, x1 = cx + hx;
        float y0 = cy - hx, y1 = cy + hx;
        float z0 = cz - hx, z1 = cz + hx;

        if (npc.texture == 0)
        {
            drawColoredCube(cx, cy, cz, s, 0.0f, 0.0f, 0.0f);
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
        glColor3f(0.0f, 0.0f, 0.0f);
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

    // Palette
    const std::array<float, 3> shirt{22.0f / 255.0f, 46.0f / 255.0f, 148.0f / 255.0f}; // blue torso/arms (RGB 22,46,148)
    const std::array<float, 3> pants{0.1f, 0.12f, 0.2f};
    const std::array<float, 3> shoes{0.05f, 0.05f, 0.05f};
    const std::array<float, 3> skin{0.78f, 0.63f, 0.48f};

    // Leg stack
    float legH = s * 1.3f;
    float legW = s * 0.55f;
    float legD = s * 0.7f;
    float shoeH = legH * 0.25f;
    float legUpperH = legH - shoeH;

    float legYOffset = baseY + legUpperH * 0.5f;
    float legZ = npc.z;
    float legXOff = legW * 0.7f;
    // Upper legs
    drawBox(npc.x - legXOff, legYOffset, legZ, legW, legUpperH, legD, pants[0], pants[1], pants[2]);
    drawBox(npc.x + legXOff, legYOffset, legZ, legW, legUpperH, legD, pants[0], pants[1], pants[2]);
    // Shoes
    float shoeY = baseY + legUpperH + shoeH * 0.5f;
    drawBox(npc.x - legXOff, shoeY, legZ, legW, shoeH, legD + s * 0.1f, shoes[0], shoes[1], shoes[2]);
    drawBox(npc.x + legXOff, shoeY, legZ, legW, shoeH, legD + s * 0.1f, shoes[0], shoes[1], shoes[2]);

    // Torso
    float torsoH = s * 2.3f;
    float torsoW = s * 1.4f;
    float torsoD = s * 1.0f;
    float torsoBaseY = baseY + legH;
    float torsoCenterY = torsoBaseY + torsoH * 0.5f;
    drawBox(npc.x, torsoCenterY, npc.z, torsoW, torsoH, torsoD, shirt[0], shirt[1], shirt[2]);
    // Arms
    float armH = torsoH * 0.9f;
    float armW = s * 0.40f;
    float armD = s * 0.55f;
    float armY = torsoBaseY + armH * 0.5f;
    float armXOff = torsoW * 0.5f + armW * 0.45f + s * 0.06f; // closer to torso while still avoiding z-fighting
    drawBox(npc.x - armXOff, armY, npc.z, armW, armH, armD, shirt[0], shirt[1], shirt[2]);
    drawBox(npc.x + armXOff, armY, npc.z, armW, armH, armD, shirt[0], shirt[1], shirt[2]);
    // Hands
    float handH = armH * 0.22f;
    float handW = armW * 0.85f;
    float handD = armD * 0.8f;
    float handY = torsoBaseY - handH * 0.5f - s * 0.03f; // place hands just below arm to avoid coplanar faces
    float handZOff = s * 0.08f;                          // slight Z offset to remove z-fighting with arm
    drawBox(npc.x - armXOff, handY, npc.z + handZOff, handW, handH, handD, skin[0], skin[1], skin[2]);
    drawBox(npc.x + armXOff, handY, npc.z + handZOff, handW, handH, handD, skin[0], skin[1], skin[2]);

    // Head
    float headY = torsoBaseY + torsoH + s * 0.6f;
    drawHead(npc.x, headY, npc.z);
}
