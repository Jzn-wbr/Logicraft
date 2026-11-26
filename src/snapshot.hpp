#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

class World;
bool writeSnapshot(std::ostream &out, const World &world, uint32_t seed);
bool readSnapshot(std::istream &in, World &world, uint32_t &seed);
bool saveWorldToFile(const World &world, const std::string &path, uint32_t seed);
bool loadWorldFromFile(World &world, const std::string &path, uint32_t &seed);
