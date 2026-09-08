#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

using BlockId = uint16_t;

enum : BlockId
{
    BLOCK_AIR = 0,
    BLOCK_GRASS = 1,
    BLOCK_DIRT = 2,
    BLOCK_STONE = 3,
    BLOCK_LOG = 4,
    BLOCK_LEAF = 5,
    BLOCK_SAND = 6,
    BLOCK_WATER = 7,
    BLOCK_GRASS_TALL = 8,
};

static constexpr size_t kBlockTypeCount = 9;

inline glm::vec3 colorFor(BlockId id)
{
    switch (id)
    {
        case BLOCK_GRASS:
            return glm::vec3(0.22f, 0.75f, 0.25f);
        case BLOCK_DIRT:
            return glm::vec3(0.45f, 0.3f, 0.16f);
        case BLOCK_STONE:
            return glm::vec3(0.55f, 0.55f, 0.6f);
        case BLOCK_LOG:
            return glm::vec3(0.55f, 0.4f, 0.22f);
        case BLOCK_LEAF:
            return glm::vec3(0.2f, 0.6f, 0.25f);
        case BLOCK_SAND:
            return glm::vec3(0.8f, 0.75f, 0.5f);
        case BLOCK_WATER:
            return glm::vec3(0.1f, 0.35f, 0.8f);
        case BLOCK_GRASS_TALL:
            return glm::vec3(0.18f, 0.55f, 0.2f);
        default:
            return glm::vec3(0.9f, 0.1f, 0.9f);
    }
}

inline glm::vec3 blockColor(BlockId id)
{
    return colorFor(id);
}
