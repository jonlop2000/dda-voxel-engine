#include "engine/scene/CloudScene.h"

#include <chrono>
#include <cmath>

#include "Core/Logger.h"
#include "Utils/CloudNoise.h"
#include "engine/voxel/VoxelBuilder.h"

CloudBuildResult CloudScene::buildVolumeData(const CloudSettings& settings)
{
    const auto buildStart = std::chrono::steady_clock::now();

    CloudBuildResult result{};
    result.spec.name = "CloudField";
    result.spec.dims = settings.dims;
    result.spec.position = settings.position;
    result.spec.flags = engine::VoxelVolume::FLAG_STATIC |
                        engine::VoxelVolume::FLAG_WRAP_XZ |
                        engine::VoxelVolume::FLAG_CLOUD;
    result.voxels = buildCloudVolume(settings.dims, settings);

    for (uint8_t voxel : result.voxels)
    {
        if (voxel != 0)
        {
            ++result.filledVoxelCount;
        }
    }

    const double buildMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
            .count();
    logInfo("CloudScene",
            makeLogMessage("Cloud CPU build finished in ", buildMs, " ms (",
                           result.filledVoxelCount, " / ", result.voxels.size(),
                           " voxels filled, type=",
                           settings.cloudType == CloudType::Cumulus
                               ? "Cumulus"
                               : settings.cloudType == CloudType::Stratus ? "Stratus"
                                                                           : "Cirrus",
                           ")."));

    return result;
}

std::vector<uint8_t> CloudScene::buildCloudVolume(const glm::ivec3& dims,
                                                  const CloudSettings& settings)
{
    switch (settings.cloudType)
    {
    case CloudType::Stratus:
        return buildStratusCloud(dims, settings);
    case CloudType::Cirrus:
        return buildCirrusCloud(dims, settings);
    case CloudType::Cumulus:
    default:
        return buildCumulusCloud(dims, settings);
    }
}

std::vector<uint8_t> CloudScene::buildCumulusCloud(const glm::ivec3& dims,
                                                   const CloudSettings& settings)
{
    engine::VoxelBuilder builder(dims);

    const float baseScale = settings.baseNoiseScale;
    const float detailScale = settings.detailNoiseScale;
    const float threshold = settings.densityThreshold;
    const float repeatX = static_cast<float>(dims.x);
    const float repeatZ = static_cast<float>(dims.z);
    const float cloudMinY = 0.0f;
    const float cloudHeight = static_cast<float>(dims.y);

    // first pass: determine density at each voxel
    std::vector<float> densities(static_cast<size_t>(dims.x) * dims.y * dims.z, 0.0f);

    auto densityIndex = [dims](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) + static_cast<size_t>(y) * dims.x +
               static_cast<size_t>(z) * dims.x * dims.y;
    };

    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                float density = CloudNoise::cloudDensity(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                    baseScale, detailScale, settings.seed,
                    cloudMinY, cloudHeight, threshold, repeatX, repeatZ);

                densities[densityIndex(x, y, z)] = density;
            }
        }
    }

    // second pass: assign materials based on depth inside cloud
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                float density = densities[densityIndex(x, y, z)];
                if (density <= 0.0f)
                {
                    continue;
                }

                // check if this voxel is on surface (has any empty neighbor)
                bool isSurface = false;
                for (int dz = -1; dz <= 1 && !isSurface; ++dz)
                {
                    for (int dy = -1; dy <= 1 && !isSurface; ++dy)
                    {
                        for (int dx = -1; dx <= 1 && !isSurface; ++dx)
                        {
                            if (dx == 0 && dy == 0 && dz == 0)
                            {
                                continue;
                            }

                            int nx = x + dx;
                            int ny = y + dy;
                            int nz = z + dz;

                            if (nx < 0 || nx >= dims.x || ny < 0 || ny >= dims.y || nz < 0 ||
                                nz >= dims.z)
                            {
                                isSurface = true;
                            }
                            else if (densities[densityIndex(nx, ny, nz)] <= 0.0f)
                            {
                                isSurface = true;
                            }
                        }
                    }
                }

                uint8_t material = 100;  // default cloud material
                if (isSurface)
                {
                    // normalized y position within cloud
                    float normY = static_cast<float>(y) / cloudHeight;

                    // use density and height to pick material variation
                    material = CloudNoise::cloudMaterialIndex(density, normY,
                                                              isSurface ? 1.0f : 3.0f,
                                                              x, y, z);
                }
                else
                {
                    // interior voxels use denser materials
                    material = 108 + static_cast<uint8_t>(
                                         std::min(7.0f, density * 8.0f));
                }

                builder.setVoxel(x, y, z, material);
            }
        }
    }

    return builder.data();
}

std::vector<uint8_t> CloudScene::buildStratusCloud(const glm::ivec3& dims,
                                                   const CloudSettings& settings)
{
    engine::VoxelBuilder builder(dims);

    const float baseScale = settings.baseNoiseScale * 1.5f;  // larger scale for flatter clouds
    const float threshold = settings.densityThreshold + 0.1f; // slightly sparser
    const float midY = dims.y * 0.5f;
    const float thickness = dims.y * 0.3f;

    // Stratus clouds are flatter, concentrated in middle y layers
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                // height falloff - concentrate in middle band
                float dy = std::abs(static_cast<float>(y) - midY) / thickness;
                float heightMask = std::max(0.0f, 1.0f - dy * dy);

                if (heightMask <= 0.0f)
                {
                    continue;
                }

                // use only base noise for smoother, flatter appearance
                float base = CloudNoise::fbm3D(
                    static_cast<float>(x) / baseScale,
                    static_cast<float>(y) / (baseScale * 0.5f),
                    static_cast<float>(z) / baseScale,
                    4, 2.0f, 0.5f, settings.seed);

                float density = base * heightMask;

                if (density > threshold)
                {
                    float normY = static_cast<float>(y) / static_cast<float>(dims.y);
                    uint8_t material = CloudNoise::cloudMaterialIndex(
                        density, normY, 1.0f, x, y, z);
                    builder.setVoxel(x, y, z, material);
                }
            }
        }
    }

    return builder.data();
}

std::vector<uint8_t> CloudScene::buildCirrusCloud(const glm::ivec3& dims,
                                                  const CloudSettings& settings)
{
    engine::VoxelBuilder builder(dims);

    const float baseScale = settings.baseNoiseScale * 0.7f;  // smaller scale for wispy detail
    const float threshold = settings.densityThreshold + 0.2f; // much sparser
    const float lowY = dims.y * 0.6f;

    // Cirrus clouds are thin, wispy, high in the volume
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                // only upper portion of volume
                if (y < lowY)
                {
                    continue;
                }

                // use directional streak noise
                float nx = static_cast<float>(x) / baseScale;
                float ny = static_cast<float>(y) / (baseScale * 2.0f);
                float nz = static_cast<float>(z) / baseScale;

                // stretch noise in x direction for wispy streaks
                float streak = CloudNoise::fbm3D(
                    nx * 2.5f, ny, nz,
                    3, 2.0f, 0.5f, settings.seed);

                // add some variation
                float detail = CloudNoise::fbm3D(
                    nx, ny * 3.0f, nz,
                    2, 2.0f, 0.5f, settings.seed + 123);

                float density = streak * 0.8f + detail * 0.2f;

                if (density > threshold)
                {
                    uint8_t material = 100 + static_cast<uint8_t>(
                                             std::min(7.0f, density * 8.0f));
                    builder.setVoxel(x, y, z, material);
                }
            }
        }
    }

    return builder.data();
}
