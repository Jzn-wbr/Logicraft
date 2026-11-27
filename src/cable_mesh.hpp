#pragma once

#include "types.hpp"

#include <algorithm>
#include <cmath>

namespace cable
{
constexpr float pi = 3.14159265359f;
constexpr int strandSides = 6;

inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 scale(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 faceReference(Vec3 axis)
{
    // Both directions have the same cross section on a shared block face.
    return std::abs(axis.y) > 0.5f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
}

struct End
{
    Vec3 axis{};
    bool cap = false;
};

template<class MakeVertex>
void append(std::vector<Vertex> &vertices, Vec3 center, const std::array<End, 6> &ends, int count,
            int width, MakeVertex makeVertex)
{
    constexpr float turn = 2 * pi;
    const float strandRadius = width == 1 ? 0.031f : 0.025f;
    const float braidRadius = width == 1 ? 0.0f : 1.04f * strandRadius / std::sin(pi / width);

    auto sweep = [&](End entry, End exit, float startDistance, float endDistance)
    {
        const Vec3 a = entry.axis, b = exit.axis;
        constexpr float radius = 0.29f;
        constexpr float straight = 0.5f - radius;
        const bool bent = dot(a, b) == 0;
        const float length = bent ? 2 * straight + radius * pi / 2 : startDistance + endDistance;
        const Vec3 base = bent ? cross(a, b) : faceReference(a);
        auto rollAt = [&](Vec3 tangent)
        {
            Vec3 reference = faceReference(tangent);
            return std::atan2(dot(reference, cross(tangent, base)), dot(reference, base));
        };
        const float initialRoll = rollAt(scale(a, -1));
        const float rollChange = std::remainder(rollAt(b) - initialRoll, turn);
        const int steps = bent ? 12 : width > 1 ? (length > 0.75f ? 10 : 6) : 1;
        using Ring = std::array<Vertex, strandSides + 1>;
        std::array<Ring, 8> previous{};
        for (int step = 0; step <= steps; ++step)
        {
            const float t = static_cast<float>(step) / steps;
            const float distance = t * length;
            Vec3 tangent = scale(a, -1);
            Vec3 position = add(center, scale(a, startDistance - distance));
            if (bent && distance >= straight)
            {
                const float arcDistance = distance - straight;
                if (arcDistance < radius * pi / 2)
                {
                    const float theta = arcDistance / radius;
                    const float sine = std::sin(theta), cosine = std::cos(theta);
                    position = add(center, add(scale(a, radius * (1 - sine)), scale(b, radius * (1 - cosine))));
                    tangent = add(scale(a, -cosine), scale(b, sine));
                }
                else
                {
                    position = add(center, scale(b, endDistance - (length - distance)));
                    tangent = b;
                }
            }

            const Vec3 side = cross(tangent, base);
            const float roll = initialRoll + t * (rollChange + (width > 1 ? turn : 0.0f));
            std::array<Vec3, strandSides + 1> normals{};
            for (int index = 0; index < strandSides; ++index)
            {
                const float angle = roll + turn * index / strandSides;
                normals[index] = add(scale(base, std::cos(angle)), scale(side, std::sin(angle)));
            }
            normals[strandSides] = normals[0];
            for (int strand = 0; strand < width; ++strand)
            {
                const float phase = roll + turn * strand / width;
                const Vec3 offset = add(scale(base, std::cos(phase)), scale(side, std::sin(phase)));
                const Vec3 strandCenter = add(position, scale(offset, braidRadius));
                Ring ring;
                for (int index = 0; index <= strandSides; ++index)
                    ring[index] = makeVertex(add(strandCenter, scale(normals[index], strandRadius)), normals[index],
                                             static_cast<float>(index) / strandSides, t);
                const bool cap = (step == 0 && entry.cap) || (step == steps && exit.cap);
                for (int index = 0; index < strandSides; ++index)
                {
                    if (step)
                        vertices.insert(vertices.end(), {previous[strand][index], previous[strand][index + 1],
                                                         ring[index + 1], ring[index]});
                    if (cap)
                    {
                        const Vertex tip = makeVertex(strandCenter, scale(tangent, step == 0 ? -1.0f : 1.0f), 0.5f, t);
                        if (step == 0)
                            vertices.insert(vertices.end(), {ring[index + 1], ring[index], tip, tip});
                        else
                            vertices.insert(vertices.end(), {ring[index], ring[index + 1], tip, tip});
                    }
                }
                previous[strand] = ring;
            }
        }
    };

    if (count == 0)
        sweep({{-1, 0, 0}, true}, {{1, 0, 0}, true}, 0.20f, 0.20f);
    else if (count == 1)
        sweep(ends[0], {scale(ends[0].axis, -1), true}, 0.5f, 0.20f);
    else if (count == 2)
        sweep(ends[0], ends[1], 0.5f, 0.5f);
    else
    {
        // A rounded knot joins the bundles at branches.
        const float hubRadius = braidRadius + strandRadius + 0.016f;
        constexpr int rings = 5, slices = 10;
        auto knot = [&](int row, int column)
        {
            const float u = static_cast<float>(column) / slices;
            const float v = static_cast<float>(row) / rings;
            const float azimuth = u * turn, latitude = (v - 0.5f) * pi;
            const Vec3 normal{std::cos(latitude) * std::cos(azimuth), std::sin(latitude),
                              std::cos(latitude) * std::sin(azimuth)};
            return makeVertex(add(center, scale(normal, hubRadius)), normal, u, v);
        };
        for (int row = 0; row < rings; ++row)
            for (int column = 0; column < slices; ++column)
                vertices.insert(vertices.end(), {knot(row, column), knot(row + 1, column),
                                                 knot(row + 1, column + 1), knot(row, column + 1)});
        for (int i = 0; i < count; ++i)
            sweep(ends[i], {scale(ends[i].axis, -1), false}, 0.5f, 0.0f);
    }
}
} // namespace cable
