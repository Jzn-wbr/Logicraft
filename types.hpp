#pragma once

#include <GL/glew.h>
#include <array>
#include <cstdint>
#include <map>
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

struct BlockInfo
{
    std::string name;
    bool solid;
    std::array<float, 3> color;
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

struct Vec3
{
    float x, y, z;
};

struct HitInfo
{
    int x, y, z;
    int nx, ny, nz;
    bool hit;
};
