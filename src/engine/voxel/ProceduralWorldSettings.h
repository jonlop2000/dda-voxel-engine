#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

namespace engine
{

// procedural worldgen parameters (grid/chunk dims, terrain noise, caves,
// trees, rocks) edited by the debug ui and consumed by the procedural world
// rebuild. plain data; field names keep their original app spellings.
struct ProceduralWorldSettings
{
    uint32_t proceduralSeed_ = 1337;
    float proceduralBaseHeight_ = 10.0f;
    float proceduralNoiseScale_ = 32.0f;
    float proceduralHeightAmplitude_ = 16.0f;
    int proceduralDirtDepth_ = 4;
    bool proceduralCavesEnabled_ = true;
    float proceduralCaveNoiseScale_ = 24.0f;
    float proceduralCaveThreshold_ = 0.65f;
    int proceduralCaveMinY_ = 0;
    int proceduralCaveMaxY_ = 1024;
    bool proceduralTreesEnabled_ = true;
    int proceduralTreesPerChunk_ = 2;
    int proceduralTrunkMinH_ = 4;
    int proceduralTrunkMaxH_ = 7;
    int proceduralLeafRadius_ = 3;
    bool proceduralRocksEnabled_ = true;
    int proceduralRocksPerChunk_ = 1;
    int proceduralRockMinR_ = 2;
    int proceduralRockMaxR_ = 4;
    glm::ivec3 proceduralGridDims_{4, 4, 1};
    glm::ivec3 proceduralChunkDims_{32, 32, 32};
    bool proceduralDirty_ = false;
};

} // namespace engine
