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
    NotGate,
    XorGate,
    Led,
    Button,
    Wire,
    Sign
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
    float manageX = 0.0f, manageY = 0.0f, manageW = 0.0f, manageH = 0.0f;
    float quitX = 0.0f, quitY = 0.0f, quitW = 0.0f, quitH = 0.0f;
};

struct SaveMenuLayout
{
    float panelX = 0.0f, panelY = 0.0f, panelW = 0.0f, panelH = 0.0f;
    float listX = 0.0f, listY = 0.0f, listW = 0.0f, listH = 0.0f;
    float inputX = 0.0f, inputY = 0.0f, inputW = 0.0f, inputH = 0.0f;
    float createX = 0.0f, createY = 0.0f, createW = 0.0f, createH = 0.0f;
    float overwriteX = 0.0f, overwriteY = 0.0f, overwriteW = 0.0f, overwriteH = 0.0f;
    float loadX = 0.0f, loadY = 0.0f, loadW = 0.0f, loadH = 0.0f;
    float backX = 0.0f, backY = 0.0f, backW = 0.0f, backH = 0.0f;
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
    std::vector<Vertex> glassVerts;
    GLuint vbo = 0;
    GLuint glassVbo = 0;
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
