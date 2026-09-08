#include "engine/scene/DenseSkipProbeScene.h"

#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Logger.h"
#include "engine/render/VulkanContext.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{
constexpr uint8_t kMatShell = 15;
constexpr uint8_t kMatAccent = 16;
constexpr uint8_t kMatFloor = 17;
constexpr uint32_t kDenseSkipProbeFlags = engine::VoxelVolume::FLAG_STATIC |
                                          engine::VoxelVolume::FLAG_ALLOW_DENSE_SKIP;

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness, float metallic)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

void clearBox(engine::VoxelBuilder& builder, const glm::ivec3& min, const glm::ivec3& max)
{
    for (int z = min.z; z <= max.z; ++z)
    {
        for (int y = min.y; y <= max.y; ++y)
        {
            for (int x = min.x; x <= max.x; ++x)
            {
                builder.setVoxel(x, y, z, 0);
            }
        }
    }
}

std::vector<uint8_t> buildFortressVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), 2, kMatShell);
    builder.fillBox({4, 0, 4}, {dims.x - 5, 1, dims.z - 5}, kMatFloor);
    builder.fillBox({6, 2, 6}, {dims.x - 7, 6, 9}, kMatAccent);
    builder.fillBox({10, 2, 18}, {dims.x - 11, 4, 22}, kMatAccent);

    // a large front opening makes the probe camera traverse empty interior space
    // before hitting the back wall, which exercises dense non-wrapped mip-1 skip.
    clearBox(builder, {20, 2, dims.z - 4}, {43, 23, dims.z - 1});
    clearBox(builder, {12, 10, dims.z - 4}, {18, 18, dims.z - 1});
    clearBox(builder, {45, 10, dims.z - 4}, {51, 18, dims.z - 1});

    return std::move(builder.data());
}

std::vector<uint8_t> buildTowerVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), 2, kMatShell);
    builder.fillBox({4, 0, 4}, {dims.x - 5, 1, dims.z - 5}, kMatFloor);
    builder.fillBox({10, 12, 6}, {dims.x - 11, 20, 9}, kMatAccent);
    builder.fillBox({10, 28, dims.z - 10}, {dims.x - 11, 34, dims.z - 7}, kMatAccent);

    clearBox(builder, {10, 10, dims.z - 4}, {21, 22, dims.z - 1});
    clearBox(builder, {10, 26, 0}, {21, 36, 3});

    return std::move(builder.data());
}

std::vector<uint8_t> buildBridgeVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), 2, kMatShell);
    builder.fillBox({4, 0, 4}, {dims.x - 5, 1, dims.z - 5}, kMatFloor);
    builder.fillBox({6, 2, 8}, {dims.x - 7, 5, 11}, kMatAccent);

    clearBox(builder, {8, 3, dims.z - 4}, {dims.x - 9, 10, dims.z - 1});
    clearBox(builder, {8, 3, 0}, {dims.x - 9, 10, 3});

    return std::move(builder.data());
}

bool initDenseSkipProbeScene(VulkanContext& ctx, engine::VoxelWorld& world,
                             engine::VoxelPalette& palette,
                             const char* subsystem,
                             const char* introMessage,
                             const std::vector<engine::VolumeSpec>& specs,
                             std::vector<std::vector<uint8_t>>&& data)
{
    logInfo(subsystem, introMessage);

    palette.buildDefaultPalette0();
    palette.setEntry(0, kMatShell, makeEntry(glm::vec3(0.72f, 0.78f, 0.92f), 0.65f, 0.0f));
    palette.setEntry(0, kMatAccent, makeEntry(glm::vec3(0.72f, 0.92f, 0.55f), 0.45f, 0.0f));
    palette.setEntry(0, kMatFloor, makeEntry(glm::vec3(0.28f, 0.34f, 0.42f), 0.82f, 0.0f));

    if (!palette.upload(ctx))
    {
        logError(subsystem, "Failed to upload dense skip probe palette.");
        return false;
    }

    if (!world.initAquariumScene(ctx, palette, specs.size(),
                                 [&](size_t index) -> const engine::VolumeSpec& {
                                     return specs[index];
                                 },
                                 [&](size_t index) -> const std::vector<uint8_t>& {
                                     return data[index];
                                 }))
    {
        logError(subsystem, "Failed to initialize dense skip probe volumes.");
        return false;
    }

    logInfo(subsystem,
            makeLogMessage("Dense skip probe scene initialized with ", specs.size(),
                           " volumes"));
    return true;
}

} // namespace

bool DenseSkipProbeScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                               engine::VoxelPalette& palette)
{
    const std::vector<engine::VolumeSpec> specs = {
        {"Fortress", {64, 32, 64}, {-32.0f, 0.0f, -32.0f}, kDenseSkipProbeFlags},
        {"Tower", {32, 64, 32}, {40.0f, 0.0f, -16.0f}, kDenseSkipProbeFlags},
        {"Bridge", {64, 16, 32}, {-32.0f, 28.0f, 20.0f}, kDenseSkipProbeFlags},
    };

    std::vector<std::vector<uint8_t>> data;
    data.reserve(specs.size());
    data.push_back(buildFortressVolume(specs[0].dims));
    data.push_back(buildTowerVolume(specs[1].dims));
    data.push_back(buildBridgeVolume(specs[2].dims));

    return initDenseSkipProbeScene(ctx, world, palette, "DenseSkipProbeScene",
                                   "Initializing dense non-cubic power-of-two skip probe scene.",
                                   specs, std::move(data));
}

bool DenseSkipProbeScene::initNpot(VulkanContext& ctx, engine::VoxelWorld& world,
                                   engine::VoxelPalette& palette)
{
    const std::vector<engine::VolumeSpec> specs = {
        {"NpotFortress", {80, 48, 96}, {-40.0f, 0.0f, -48.0f}, kDenseSkipProbeFlags},
        {"NpotTower", {48, 80, 40}, {46.0f, 0.0f, -20.0f}, kDenseSkipProbeFlags},
        {"NpotBridge", {96, 24, 56}, {-48.0f, 34.0f, 28.0f}, kDenseSkipProbeFlags},
    };

    std::vector<std::vector<uint8_t>> data;
    data.reserve(specs.size());
    data.push_back(buildFortressVolume(specs[0].dims));
    data.push_back(buildTowerVolume(specs[1].dims));
    data.push_back(buildBridgeVolume(specs[2].dims));

    return initDenseSkipProbeScene(ctx, world, palette, "DenseSkipProbeScene",
                                   "Initializing dense non-cubic NPOT skip probe scene.", specs,
                                   std::move(data));
}

bool DenseSkipProbeScene::initIrregular(VulkanContext& ctx, engine::VoxelWorld& world,
                                        engine::VoxelPalette& palette)
{
    const std::vector<engine::VolumeSpec> specs = {
        {"IrregularFortress", {48, 30, 40}, {-24.0f, 0.0f, -20.0f}, kDenseSkipProbeFlags},
        {"IrregularTower", {30, 44, 26}, {34.0f, 0.0f, -14.0f}, kDenseSkipProbeFlags},
        {"IrregularBridge", {34, 18, 30}, {-20.0f, 24.0f, 18.0f}, kDenseSkipProbeFlags},
    };

    std::vector<std::vector<uint8_t>> data;
    data.reserve(specs.size());
    data.push_back(buildFortressVolume(specs[0].dims));
    data.push_back(buildTowerVolume(specs[1].dims));
    data.push_back(buildBridgeVolume(specs[2].dims));

    return initDenseSkipProbeScene(ctx, world, palette, "DenseSkipProbeScene",
                                   "Initializing dense smaller irregular skip probe scene.",
                                   specs, std::move(data));
}
