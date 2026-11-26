#pragma once

#include "world_types.hpp"

#include <vector>

struct CircuitPort
{
    int x, y, z;
    const char *label;
    bool output;
};

inline const std::vector<CircuitPort> &circuitPorts(BlockType type)
{
    static const std::vector<CircuitPort> empty;
    static const std::vector<CircuitPort> binary{
        {-1, 0, 0, "A", false}, {1, 0, 0, "B", false}, {0, 0, 1, "OUT", true}};
    static const std::vector<CircuitPort> inverter{{1, 0, 0, "IN", false}, {-1, 0, 0, "OUT", true}};
    static const std::vector<CircuitPort> flipFlop{
        {1, 0, 0, "D", false}, {-1, 0, 0, "CLK", false}, {0, 0, 1, "Q", true}};
    static const std::vector<CircuitPort> adder{
        {-1, 0, 0, "P", false}, {1, 0, 0, "Q", false}, {0, 0, -1, "CIN", false},
        {0, 0, 1, "S", true}, {0, -1, 0, "COUT", true}};
    static const std::vector<CircuitPort> counter{{1, 0, 0, "IN", false}};
    static const std::vector<CircuitPort> splitter{
        {0, 0, -1, "BUS", false}, {-1, 0, 0, "B1", true}, {1, 0, 0, "B2", true}};
    static const std::vector<CircuitPort> merger{
        {-1, 0, 0, "B1", false}, {1, 0, 0, "B2", false}, {0, 0, 1, "BUS", true}};
    static const std::vector<CircuitPort> decoder{
        {-1, 0, 0, "SEL", false}, {1, 0, 0, "EN", false}, {0, 0, 1, "OUT", true}};
    static const std::vector<CircuitPort> multiplexer{
        {-1, 0, 0, "SEL", false}, {0, 0, -1, "I0", false}, {0, 0, 1, "I1", false},
        {0, -1, 0, "I2", false}, {0, 1, 0, "I3", false}, {1, 0, 0, "OUT", true}};
    static const std::vector<CircuitPort> comparator{
        {-1, 0, 0, "B", false}, {1, 0, 0, "A", false}, {0, 0, -1, "GT", true},
        {0, 0, 1, "EQ", true}, {0, -1, 0, "LT", true}};
    static const std::vector<CircuitPort> clock{{0, 0, 1, "OUT", true}};
    switch (type)
    {
    case BlockType::AndGate: case BlockType::OrGate: case BlockType::XorGate: return binary;
    case BlockType::NotGate: return inverter;
    case BlockType::DFlipFlop: return flipFlop;
    case BlockType::AddGate: return adder;
    case BlockType::Counter: return counter;
    case BlockType::Splitter: return splitter;
    case BlockType::Merger: return merger;
    case BlockType::Decoder: return decoder;
    case BlockType::Multiplexer: return multiplexer;
    case BlockType::Comparator: return comparator;
    case BlockType::Clock: return clock;
    default: return empty;
    }
}
