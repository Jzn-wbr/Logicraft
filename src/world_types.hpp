#pragma once
#include <array>
#include <cstdint>
#include <string>

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
    Sign,
    DFlipFlop,
    AddGate,
    Counter,
    Splitter,
    Merger,
    Decoder,
    Multiplexer,
    Comparator,
    Clock
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

struct HitInfo
{
    int x, y, z;
    int nx, ny, nz;
    bool hit;
};
