#include "engine/scene/PhysicsSandboxScene.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Logger.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"
#include "engine/render/VulkanContext.h"

namespace
{
constexpr uint8_t kMatSphere = 12;
constexpr uint8_t kMatFloor = 14;
constexpr uint8_t kMatWall = 16;

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness, float metallic)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

std::vector<uint8_t> buildSphereVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    const glm::vec3 center = glm::vec3(dims) * 0.5f;
    const float radius = std::min({center.x, center.y, center.z}) - 2.0f;
    builder.fillSphere(center, radius, kMatSphere);
    return std::move(builder.data());
}


std::vector<uint8_t> buildBoxVolume(const glm::ivec3& dims, uint8_t materialId)
{
    engine::VoxelBuilder builder(dims);
    // The DDA renderer skips the outer voxel layer, so leave it empty.
    builder.fillBox({1, 1, 1}, {dims.x - 2, dims.y - 2, dims.z - 2}, materialId);
    return std::move(builder.data());
}

} // namespace

bool PhysicsSandboxScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                        engine::VoxelPalette& palette)
{
    logInfo("PhysicsSandboxScene", "Initializing PhysicsSandbox scene.");

    palette.buildDefaultPalette0();
    palette.setEntry(0, kMatSphere, makeEntry(glm::vec3(0.25f, 0.65f, 0.9f), 0.2f, 0.1f));
    palette.setEntry(0, kMatFloor, makeEntry(glm::vec3(0.45f, 0.85f, 0.55f), 0.6f, 0.0f));
    palette.setEntry(0, kMatWall, makeEntry(glm::vec3(0.9f, 0.55f, 0.2f), 0.6f, 0.0f));

    if (!palette.upload(ctx))
    {
        logError("PhysicsSandboxScene", "Failed to upload PhysicsSandbox palette.");
        return false;
    }

    const std::vector<engine::VolumeSpec> specs = {
        {"Sphere", {24, 24, 24}, {-0.12f, 1.88f, -0.12f},
         engine::VoxelVolume::FLAG_DYNAMIC, {0.01f, 0.01f, 0.01f}},
        // One solid layer plus an empty border. At scale 0.1, the solid
        // floor spans x/z = [-5, 5] and y = [-0.1, 0].
        {"Floor", {102, 3, 102}, {-5.1f, -0.2f, -5.1f},
         engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}},
        // the empty border puts the solid inner face at x = 5.0.
        // the solid wall is 0.1 m thick, 3 m high, and 10 m long.
        {"RightWall", {3, 32, 102}, {4.9f, -0.1f, -5.1f},
         engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}},
        // the solid layer spans x = -5.1 to -5.0, so its inner face is at -5.0.
        {"LeftWall", {3, 32, 102}, {-5.2f, -0.1f, -5.1f},
         engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}},
    };

    std::vector<std::vector<uint8_t>> data;
    data.reserve(specs.size());
    // voxel data follows the same order as the volume specs.
    data.push_back(buildSphereVolume(specs[0].dims));
    data.push_back(buildBoxVolume(specs[1].dims, kMatFloor));
    data.push_back(buildBoxVolume(specs[2].dims, kMatWall));
    data.push_back(buildBoxVolume(specs[3].dims, kMatWall));

    if (!world.initAquariumScene(ctx, palette, specs.size(),
                                 [&](size_t index) -> const engine::VolumeSpec& {
                                     return specs[index];
                                 },
                                 [&](size_t index) -> const std::vector<uint8_t>& {
                                     return data[index];
                                 }))
    {
        logError("PhysicsSandboxScene", "Failed to initialize PhysicsSandbox volumes.");
        return false;
    }

    logInfo("PhysicsSandboxScene",
            makeLogMessage("PhysicsSandbox scene initialized with ", specs.size(),
                           " volumes"));
    return true;
}
