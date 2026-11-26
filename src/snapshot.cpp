#include "snapshot.hpp"
#include "world.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
template<class T>
void writeInteger(std::ostream &out, T value)
{
    for (size_t i = 0; i < sizeof(T); ++i)
    {
        out.put(static_cast<char>(value & 255));
        value >>= 8;
    }
}

template<class T>
void readInteger(std::istream &in, T &value)
{
    value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
    {
        int byte = in.get();
        if (byte < 0)
            return;
        value |= static_cast<T>(byte) << (8 * i);
    }
}

struct SaveHeader
{
    char magic[8] = {'B', 'U', 'L', 'L', 'D', 'O', 'G', '\0'};
    uint32_t version = 11;
    uint32_t w = 0, h = 0, d = 0;
    uint32_t seed = 0;
};

} // namespace

bool writeSnapshot(std::ostream &out, const World &world, uint32_t seed)
{
    SaveHeader hdr;
    hdr.w = static_cast<uint32_t>(world.getWidth());
    hdr.h = static_cast<uint32_t>(world.getHeight());
    hdr.d = static_cast<uint32_t>(world.getDepth());
    hdr.seed = seed;
    out.write(hdr.magic, 8);
    writeInteger(out, hdr.version);
    writeInteger(out, hdr.w);
    writeInteger(out, hdr.h);
    writeInteger(out, hdr.d);
    writeInteger(out, hdr.seed);
    writeInteger(out, world.tick());
    int total = world.totalSize();
    for (int i = 0; i < total; ++i)
    {
        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();
        uint8_t b = static_cast<uint8_t>(world.get(x, y, z));
        uint8_t p = world.getPower(x, y, z);
        uint8_t width = world.getPowerWidth(x, y, z);
        uint8_t btn = world.getButtonState(x, y, z);
        uint8_t btnVal = world.getButtonValue(x, y, z);
        uint8_t btnWidth = world.getButtonWidth(x, y, z);
        uint8_t splitWidth = world.getSplitterWidth(x, y, z);
        uint8_t splitOrder = world.getSplitterOrder(x, y, z);
        uint8_t clkFreq = world.getClockFreq(x, y, z);
        out.write(reinterpret_cast<const char *>(&b), 1);
        out.write(reinterpret_cast<const char *>(&p), 1);
        out.write(reinterpret_cast<const char *>(&btn), 1);
        out.write(reinterpret_cast<const char *>(&btnVal), 1);
        out.write(reinterpret_cast<const char *>(&btnWidth), 1);
        out.write(reinterpret_cast<const char *>(&splitWidth), 1);
        out.write(reinterpret_cast<const char *>(&splitOrder), 1);
        out.write(reinterpret_cast<const char *>(&clkFreq), 1);
        out.write(reinterpret_cast<const char *>(&width), 1);
    }

    // Save sign texts (only for version >= 2)
    std::vector<std::pair<uint32_t, std::string>> signs;
    signs.reserve(128);
    for (int i = 0; i < total; ++i)
    {
        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();
        if (world.get(x, y, z) != BlockType::Sign)
            continue;
        const std::string &txt = world.getSignText(x, y, z);
        if (txt.empty())
            continue;
        signs.emplace_back(static_cast<uint32_t>(i), txt);
    }
    uint32_t signCount = static_cast<uint32_t>(signs.size());
    writeInteger(out, signCount);
    for (const auto &s : signs)
    {
        uint32_t idx = s.first;
        const std::string &txt = s.second;
        uint16_t len = static_cast<uint16_t>(std::min<size_t>(txt.size(), 65535));
        writeInteger(out, idx);
        writeInteger(out, len);
        if (len > 0)
            out.write(txt.data(), len);
    }
    return static_cast<bool>(out);
}

bool readSnapshot(std::istream &in, World &destination, uint32_t &seedOut)
{
    if (!in)
        return false;
    SaveHeader hdr{};
    in.read(hdr.magic, 8);
    readInteger(in, hdr.version);
    readInteger(in, hdr.w);
    readInteger(in, hdr.h);
    readInteger(in, hdr.d);
    readInteger(in, hdr.seed);
    if (!in || std::string(hdr.magic, hdr.magic + 7) != "BULLDOG")
        return false;
    if (hdr.version < 1 || hdr.version > 11)
        return false;
    if (hdr.w != destination.getWidth() || hdr.h != destination.getHeight() || hdr.d != destination.getDepth())
        return false;
    uint64_t tick = 0;
    if (hdr.version >= 11)
        readInteger(in, tick);
    if (!in)
        return false;
    World world(hdr.w, hdr.h, hdr.d);
    world.setTick(tick);
    int total = world.totalSize();
    for (int i = 0; i < total; ++i)
    {
        uint8_t b = 0, p = 0, btn = 0, btnVal = 255;
        uint8_t btnWidth = 0;
        uint8_t splitWidth = 1;
        uint8_t splitOrder = 0;
        uint8_t clkFreq = 60;
        in.read(reinterpret_cast<char *>(&b), 1);
        in.read(reinterpret_cast<char *>(&p), 1);
        in.read(reinterpret_cast<char *>(&btn), 1);
        if (hdr.version >= 6)
            in.read(reinterpret_cast<char *>(&btnVal), 1);
        if (hdr.version >= 7)
            in.read(reinterpret_cast<char *>(&btnWidth), 1);
        if (hdr.version >= 9)
        {
            in.read(reinterpret_cast<char *>(&splitWidth), 1);
            in.read(reinterpret_cast<char *>(&splitOrder), 1);
        }
        if (hdr.version >= 10)
        {
            in.read(reinterpret_cast<char *>(&clkFreq), 1);
        }
        uint8_t width = 8;
        if (hdr.version >= 11)
            in.read(reinterpret_cast<char *>(&width), 1);
        if (width < 1 || width > 8 || b > static_cast<uint8_t>(BlockType::Clock))
            return false;
        if (!in)
            return false;

        // Backward compatibility: versions 1–2 were saved before XorGate was inserted
        // into the BlockType enum, so Led/Button/Wire/Sign indices moved.
        if (hdr.version < 3)
        {
            // Old mapping:
            // 13 = Led, 14 = Button, 15 = Wire, 16 = Sign
            // New mapping:
            // 14 = Led, 15 = Button, 16 = Wire, 17 = Sign
            if (b == 13)
                b = static_cast<uint8_t>(BlockType::Led);
            else if (b == 14)
                b = static_cast<uint8_t>(BlockType::Button);
            else if (b == 15)
                b = static_cast<uint8_t>(BlockType::Wire);
            else if (b == 16)
                b = static_cast<uint8_t>(BlockType::Sign);
        }

        int x = i % world.getWidth();
        int y = (i / world.getWidth()) / world.getDepth();
        int z = (i / world.getWidth()) % world.getDepth();
        world.set(x, y, z, static_cast<BlockType>(b));
        world.setPower(x, y, z, p);
        world.setPowerWidth(x, y, z, width);
        world.setButtonState(x, y, z, btn);
        if (b == static_cast<uint8_t>(BlockType::Button))
        {
            // For maps saved before version 8, force default 1-bit value = 1
            if (hdr.version < 8)
            {
                btnWidth = 1;
                btnVal = 1;
            }
            world.setButtonWidth(x, y, z, btnWidth == 0 ? 8 : btnWidth);
            world.setButtonValue(x, y, z, btnVal);
        }
        if (b == static_cast<uint8_t>(BlockType::Splitter) || b == static_cast<uint8_t>(BlockType::Merger))
        {
            if (hdr.version < 9)
            {
                splitWidth = 1;
                splitOrder = 0;
            }
            world.setSplitterWidth(x, y, z, splitWidth == 0 ? 1 : splitWidth);
            world.setSplitterOrder(x, y, z, splitOrder);
        }
        if (b == static_cast<uint8_t>(BlockType::Clock))
        {
            if (hdr.version < 10)
                clkFreq = 60;
            world.setClockFreq(x, y, z, clkFreq == 0 ? 1 : clkFreq);
        }
    }

    // Load sign texts for version >= 2
    if (hdr.version >= 2)
    {
        uint32_t signCount = 0;
        readInteger(in, signCount);
        if (!in || signCount > static_cast<uint32_t>(total))
            return false;
        for (uint32_t i = 0; i < signCount; ++i)
        {
            uint32_t idx = 0;
            uint16_t len = 0;
            readInteger(in, idx);
            readInteger(in, len);
            if (!in)
                return false;
            std::string txt;
            txt.resize(len);
            if (len > 0)
            {
                in.read(&txt[0], len);
                if (!in)
                    return false;
            }
            int x = static_cast<int>(idx % static_cast<uint32_t>(world.getWidth()));
            int y = static_cast<int>((idx / static_cast<uint32_t>(world.getWidth())) /
                                     static_cast<uint32_t>(world.getDepth()));
            int z = static_cast<int>((idx / static_cast<uint32_t>(world.getWidth())) %
                                     static_cast<uint32_t>(world.getDepth()));
            if (world.inside(x, y, z) && world.get(x, y, z) == BlockType::Sign)
            {
                world.setSignText(x, y, z, txt);
            }
        }
    }
    seedOut = hdr.seed;
    destination = std::move(world);
    return true;
}

bool loadWorldFromFile(World &world, const std::string &path, uint32_t &seed)
{
    std::ifstream in(path, std::ios::binary);
    return readSnapshot(in, world, seed);
}

bool saveWorldToFile(const World &world, const std::string &path, uint32_t seed)
{
    namespace fs = std::filesystem;
    fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path())
        fs::create_directories(target.parent_path(), ec);
    if (ec)
        return false;

    // Reserve a sibling directory so concurrent writers never share a staging file.
    fs::path staging = target;
    staging += ".saving";
    unsigned attempt = 0;
    while (!fs::create_directory(staging, ec))
    {
        if (ec || ++attempt == 128)
            return false;
        staging = target;
        staging += ".saving." + std::to_string(attempt);
    }
    fs::path candidate = staging / "snapshot";
    std::ofstream out(candidate, std::ios::binary);
    bool ok = out && writeSnapshot(out, world, seed);
    out.close();
    if (ok && out)
        fs::rename(candidate, target, ec);
    else
        ok = false;
    ok = ok && !ec;
    fs::remove(candidate, ec);
    fs::remove(staging, ec);
    return ok;
}
