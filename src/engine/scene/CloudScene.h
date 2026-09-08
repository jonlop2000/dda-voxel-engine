#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/voxel/VoxelWorld.h"

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

// information about cloud volume for debug ui
struct CloudVolumeInfo
{
    std::string name;
    glm::ivec3 dimensions;
    glm::vec3 worldPosition;
    uint32_t flags;
};

// cloud type presets
enum class CloudType
{
    Cumulus,  // fluffy, puffy clouds (default)
    Stratus,  // flat, layered clouds
    Cirrus    // thin, wispy high-altitude clouds
};

// cloud generation parameters
struct CloudSettings
{
    uint32_t seed = 42;

    // volume dimensions (single cloud field)
    glm::ivec3 dims{256, 40, 256};

    // world position (high in sky)
    glm::vec3 position{-128.0f, 100.0f, -128.0f};

    // noise parameters
    float baseNoiseScale = 32.0f;     // large cloud shapes
    float detailNoiseScale = 8.0f;    // fluffy detail
    float densityThreshold = 0.35f;   // cloud fill threshold (0.0-1.0)

    // cloud type
    CloudType cloudType = CloudType::Cumulus;
};

struct CloudBuildResult
{
    engine::VolumeSpec spec{};
    std::vector<uint8_t> voxels{};
    size_t filledVoxelCount = 0;
};

class CloudScene
{
public:
    // build cloud voxel data on the cpu only. this is safe to call from worker threads.
    static CloudBuildResult buildVolumeData(const CloudSettings& settings);

    // initialize cloud volume in VoxelWorld.
    // returns the volume index of the created cloud, or -1 on failure.
    static int init(VulkanContext& ctx, engine::VoxelWorld& world,
                    engine::VoxelPalette& palette, const CloudSettings& settings);

    // get info about cloud volumes
    static const std::vector<CloudVolumeInfo>& getVolumeInfos();

    // volume indices
    static constexpr uint32_t VOLUME_CLOUD_FIELD = 0;
    static constexpr uint32_t VOLUME_COUNT = 1;

private:
    // volume builders - each returns voxel data ready for upload
    static std::vector<uint8_t> buildCloudVolume(const glm::ivec3& dims,
                                                  const CloudSettings& settings);

    // cloud type-specific builders
    static std::vector<uint8_t> buildCumulusCloud(const glm::ivec3& dims,
                                                   const CloudSettings& settings);
    static std::vector<uint8_t> buildStratusCloud(const glm::ivec3& dims,
                                                   const CloudSettings& settings);
    static std::vector<uint8_t> buildCirrusCloud(const glm::ivec3& dims,
                                                  const CloudSettings& settings);
};
