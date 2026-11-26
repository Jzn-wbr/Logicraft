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

constexpr bool isDirectional(BlockType type)
{
    switch (type)
    {
    case BlockType::AndGate: case BlockType::OrGate: case BlockType::NotGate: case BlockType::XorGate:
    case BlockType::DFlipFlop: case BlockType::AddGate: case BlockType::Counter:
    case BlockType::Splitter: case BlockType::Merger: case BlockType::Decoder:
    case BlockType::Multiplexer: case BlockType::Comparator: case BlockType::Clock:
        return true;
    default:
        return false;
    }
}

constexpr std::array<int, 2> rotateHorizontal(int x, int z, uint8_t turns)
{
    switch (turns & 3)
    {
    case 1: return {-z, x};
    case 2: return {-x, -z};
    case 3: return {z, -x};
    default: return {x, z};
    }
}

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
