#pragma once

#include <cstdint>

#include "engine/voxel/ChunkGrid.h"

struct WorldGenStats
{
    uint32_t treesPlaced = 0;
    uint32_t foliagePlaced = 0;
    int minHeight = 0;
    int maxHeight = 0;
    float avgHeight = 0.0f;
};

WorldGenStats generateWorld(ChunkGrid& grid, uint32_t seed);
