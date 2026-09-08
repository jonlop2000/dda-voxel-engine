#pragma once

#include <glm/glm.hpp>

struct ChunkCoord
{
    int x = 0;
    int y = 0;
    int z = 0;

    ChunkCoord() = default;
    ChunkCoord(int xIn, int yIn, int zIn) : x(xIn), y(yIn), z(zIn) {}
};

inline bool operator==(const ChunkCoord& a, const ChunkCoord& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline glm::ivec3 toIvec3(const ChunkCoord& c)
{
    return glm::ivec3(c.x, c.y, c.z);
}
