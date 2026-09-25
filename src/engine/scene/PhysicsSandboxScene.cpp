#include "engine/scene/PhysicsSandboxScene.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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
constexpr uint8_t kMatSphereA = 12;
constexpr uint8_t kMatSphereB = 13;
constexpr uint8_t kMatFloor = 14;
constexpr uint8_t kMatSphereC = 15;
constexpr uint8_t kMatWall = 16;
constexpr std::array<uint8_t, 3> kSphereMaterials = {kMatSphereA, kMatSphereB, kMatSphereC};

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness, float metallic)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

std::vector<uint8_t> buildSphereVolume(const glm::ivec3& dims, uint8_t materialId)
{
    engine::VoxelBuilder builder(dims);
    const glm::vec3 center = glm::vec3(dims) * 0.5f;
    const float radius = std::min({center.x, center.y, center.z}) - 2.0f;
    builder.fillSphere(center, radius, materialId);
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
                              engine::VoxelPalette& palette,
                              const std::vector<engine::physics::Ball>& balls)
{
    logInfo("PhysicsSandboxScene", "Initializing PhysicsSandbox scene.");

    palette.buildDefaultPalette0();
    palette.setEntry(0, kMatSphereA, makeEntry(glm::vec3(0.25f, 0.65f, 0.9f), 0.2f, 0.1f));
    palette.setEntry(0, kMatSphereB, makeEntry(glm::vec3(0.9f, 0.25f, 0.45f), 0.2f, 0.1f));
    palette.setEntry(0, kMatSphereC, makeEntry(glm::vec3(0.95f, 0.8f, 0.15f), 0.2f, 0.1f));
    palette.setEntry(0, kMatFloor, makeEntry(glm::vec3(0.45f, 0.85f, 0.55f), 0.6f, 0.0f));
    palette.setEntry(0, kMatWall, makeEntry(glm::vec3(0.9f, 0.55f, 0.2f), 0.6f, 0.0f));

    if (!palette.upload(ctx))
    {
        logError("PhysicsSandboxScene", "Failed to upload PhysicsSandbox palette.");
        return false;
    }

    std::vector<engine::VolumeSpec> specs;
    std::vector<std::vector<uint8_t>> data;
    specs.reserve(balls.size() + 5); // one sphere per ball, then the floor and four walls
    data.reserve(balls.size() + 5);

    // Sphere instances come first: ball i must match instance i in the frame update.
    for (std::size_t ballIndex = 0; ballIndex < balls.size(); ++ballIndex)
    {
        const auto& ball = balls[ballIndex];
        // The current 0.1 m balls use 24-voxel volumes at 0.01 m per voxel.
        // Subtract the 0.12 m center offset to obtain each volume's origin.
        const glm::vec3 volumePosition = {
            static_cast<float>(ball.position.x - 0.12),
            static_cast<float>(ball.position.y - 0.12),
            static_cast<float>(ball.position.z - 0.12)
        };
        specs.push_back({"Sphere" + std::to_string(ballIndex), {24, 24, 24}, volumePosition,
                         engine::VoxelVolume::FLAG_DYNAMIC, {0.01f, 0.01f, 0.01f}});
        // A is blue, B is pink, and C is yellow; reuse this sequence for additional balls.
        const uint8_t material = kSphereMaterials[ballIndex % kSphereMaterials.size()];
        data.push_back(buildSphereVolume(specs.back().dims, material));
    }

    // Append each box and its voxel data together, after all the spheres.
    const auto addBox = [&](const engine::VolumeSpec& spec, uint8_t material) {
        specs.push_back(spec);
        data.push_back(buildBoxVolume(spec.dims, material));
    };
    // One solid layer plus an empty border. At scale 0.1, the solid
    // floor spans x/z = [-5, 5] and y = [-0.1, 0].
    addBox({"Floor", {102, 3, 102}, {-5.1f, -0.2f, -5.1f},
            engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}}, kMatFloor);
    // The empty border puts the solid inner face at x = 5.0.
    // The solid wall is 0.1 m thick, 3 m high, and 10 m long.
    addBox({"RightWall", {3, 32, 102}, {4.9f, -0.1f, -5.1f},
            engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}}, kMatWall);
    // The solid layer spans x = -5.1 to -5.0, so its inner face is at -5.0.
    addBox({"LeftWall", {3, 32, 102}, {-5.2f, -0.1f, -5.1f},
            engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}}, kMatWall);
    // The solid inner face is at z = 5.0, with thickness extending outward.
    addBox({"FrontWall", {102, 32, 3}, {-5.1f, -0.1f, 4.9f},
            engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}}, kMatWall);
    // The solid layer spans z = -5.1 to -5.0, so its inner face is at -5.0.
    addBox({"BackWall", {102, 32, 3}, {-5.1f, -0.1f, -5.2f},
            engine::VoxelVolume::FLAG_STATIC, {0.1f, 0.1f, 0.1f}}, kMatWall);

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
