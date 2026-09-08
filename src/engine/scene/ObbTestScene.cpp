#include "engine/scene/ObbTestScene.h"

#include <algorithm>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Logger.h"
#include "engine/voxel/Noise.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"
#include "engine/render/VulkanContext.h"

namespace
{
constexpr uint8_t kMatShell = 11;
constexpr uint8_t kMatSphere = 12;
constexpr uint8_t kMatDetail = 13;
constexpr uint8_t kMatSlab = 14;

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness, float metallic)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

float rand01(int x, int y, int z, uint32_t seed)
{
    const uint32_t hx = static_cast<uint32_t>(x) * 73856093u;
    const uint32_t hy = static_cast<uint32_t>(y) * 19349663u;
    const uint32_t hz = static_cast<uint32_t>(z) * 83492791u;
    const uint32_t h = hash32(hx ^ hy ^ hz ^ seed);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

std::vector<uint8_t> buildShellVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), 2, kMatShell);
    return std::move(builder.data());
}

std::vector<uint8_t> buildSphereVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    const glm::vec3 center = glm::vec3(dims) * 0.5f;
    const float radius = std::min({center.x, center.y, center.z}) - 2.0f;
    builder.fillSphere(center, radius, kMatSphere);
    return std::move(builder.data());
}

std::vector<uint8_t> buildDetailVolume(const glm::ivec3& dims, uint32_t seed)
{
    engine::VoxelBuilder builder(dims);
    auto& data = builder.data();

    auto idx3 = [&](int x, int y, int z) -> size_t
    {
        return static_cast<size_t>(x) + static_cast<size_t>(y) * dims.x +
               static_cast<size_t>(z) * dims.x * dims.y;
    };

    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                const float n = rand01(x, y, z, seed);
                if (n > 0.52f)
                {
                    data[idx3(x, y, z)] = kMatDetail;
                }
            }
        }
    }
    return std::move(data);
}

std::vector<uint8_t> buildSlabVolume(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    builder.fillBox({0, 0, 0}, {dims.x - 1, 4, dims.z - 1}, kMatSlab);
    builder.fillBox({2, 6, 2}, {dims.x - 3, 8, dims.z - 3}, kMatSlab);
    return std::move(builder.data());
}

} // namespace

bool ObbTestScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                        engine::VoxelPalette& palette)
{
    logInfo("ObbTestScene", "Initializing OBB DDA torture test scene.");

    palette.buildDefaultPalette0();
    palette.setEntry(0, kMatShell, makeEntry(glm::vec3(0.85f, 0.72f, 0.25f), 0.45f, 0.0f));
    palette.setEntry(0, kMatSphere, makeEntry(glm::vec3(0.25f, 0.65f, 0.9f), 0.2f, 0.1f));
    palette.setEntry(0, kMatDetail, makeEntry(glm::vec3(0.85f, 0.35f, 0.4f), 0.75f, 0.0f));
    palette.setEntry(0, kMatSlab, makeEntry(glm::vec3(0.45f, 0.85f, 0.55f), 0.6f, 0.0f));

    if (!palette.upload(ctx))
    {
        logError("ObbTestScene", "Failed to upload OBB test palette.");
        return false;
    }

    const std::vector<engine::VolumeSpec> specs = {
        {"Shell", {40, 32, 40}, {-20.0f, 0.0f, -20.0f}, engine::VoxelVolume::FLAG_STATIC},
        {"Sphere", {28, 28, 28}, {-4.0f, 6.0f, -4.0f}, engine::VoxelVolume::FLAG_STATIC},
        {"DetailChunk", {24, 20, 24}, {10.0f, 4.0f, -12.0f}, engine::VoxelVolume::FLAG_STATIC},
        {"Slab", {22, 18, 22}, {-18.0f, 8.0f, 6.0f}, engine::VoxelVolume::FLAG_STATIC},
    };

    std::vector<std::vector<uint8_t>> data;
    data.reserve(specs.size());
    data.push_back(buildShellVolume(specs[0].dims));
    data.push_back(buildSphereVolume(specs[1].dims));
    data.push_back(buildDetailVolume(specs[2].dims, 1234u));
    data.push_back(buildSlabVolume(specs[3].dims));

    if (!world.initAquariumScene(ctx, palette, specs.size(),
                                 [&](size_t index) -> const engine::VolumeSpec& {
                                     return specs[index];
                                 },
                                 [&](size_t index) -> const std::vector<uint8_t>& {
                                     return data[index];
                                 }))
    {
        logError("ObbTestScene", "Failed to initialize OBB torture test volumes.");
        return false;
    }

    logInfo("ObbTestScene",
            makeLogMessage("OBB torture test scene initialized with ", specs.size(),
                           " volumes"));
    return true;
}
