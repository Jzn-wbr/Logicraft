#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/glew.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "inventory.hpp"
#include "world_types.hpp"

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
