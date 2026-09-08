#include "engine/scene/StarScene.h"

#include "Core/Logger.h"
#include "engine/render/VulkanContext.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{

// simple hash for star placement
uint32_t starHash(int x, int y, int z, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h ^= static_cast<uint32_t>(z) * 2147483647u;
    h ^= static_cast<uint32_t>(seed) * 1013904223u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float starHashFloat(int x, int y, int z, int seed)
{
    return static_cast<float>(starHash(x, y, z, seed)) / 4294967295.0f;
}

// star material ids (using cloud palette range but bright white/yellow)
// we'll use palette id 100 (CoreWhite) for stars since they're bright points
constexpr uint8_t kStarMaterialBright = 100;  // bright white
constexpr uint8_t kStarMaterialDim = 101;     // slightly dimmer
constexpr uint8_t kStarMaterialYellow = 102;  // warm yellow star

} // anonymous namespace

int StarScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                    engine::VoxelPalette& palette, const StarSettings& settings)
{
    logInfo("StarScene", "Initializing Star Scene.");
    logInfo("StarScene",
            makeLogMessage("Star volume dimensions: ", settings.dims.x, "x", settings.dims.y,
                           "x", settings.dims.z));
    logInfo("StarScene", makeLogMessage("Star density: ", settings.density));

    // use cloud palette (stars are bright white voxels)
    palette.buildCloudPalette();
    if (!palette.upload(ctx))
    {
        logError("StarScene", "Failed to upload star palette.");
        return -1;
    }

    // create volume spec
    engine::VolumeSpec starSpec;
    starSpec.name = "StarField";
    starSpec.dims = settings.dims;
    starSpec.position = settings.position;
    starSpec.flags = engine::VoxelVolume::FLAG_STATIC;

    // build star voxel data
    std::vector<uint8_t> starData = buildStarVolume(settings.dims, settings);

    // count stars for debugging
    size_t starCount = 0;
    for (uint8_t v : starData)
    {
        if (v != 0) ++starCount;
    }
    logInfo("StarScene",
            makeLogMessage("Star field voxels: ", starCount, " / ", starData.size(), " (",
                           (100.0f * starCount / starData.size()), "%)"));

    // add volume to world
    int volumeIndex = world.addVolume(ctx, palette, starSpec, starData);
    if (volumeIndex < 0)
    {
        logError("StarScene", "Failed to add star volume to VoxelWorld.");
        return -1;
    }

    logInfo("StarScene",
            makeLogMessage("Star Scene initialized successfully (volume index: ", volumeIndex,
                           ")"));
    return volumeIndex;
}

std::vector<uint8_t> StarScene::buildStarVolume(const glm::ivec3& dims,
                                                 const StarSettings& settings)
{
    engine::VoxelBuilder builder(dims);

    const int seed = static_cast<int>(settings.seed);
    const float density = settings.density;

    // stars are placed randomly based on density threshold
    // very sparse - only place a star if hash is below density threshold
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                float r = starHashFloat(x, y, z, seed);

                if (r < density)
                {
                    // determine star brightness/color based on secondary hash
                    float colorHash = starHashFloat(x, y, z, seed + 1000);
                    uint8_t material;

                    if (colorHash < 0.6f)
                    {
                        material = kStarMaterialBright;  // most stars are bright white
                    }
                    else if (colorHash < 0.85f)
                    {
                        material = kStarMaterialDim;     // some are slightly dimmer
                    }
                    else
                    {
                        material = kStarMaterialYellow;  // few are warm/yellow
                    }

                    builder.setVoxel(x, y, z, material);
                }
            }
        }
    }

    return std::move(builder.data());
}
