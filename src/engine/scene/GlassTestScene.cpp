#include "engine/scene/GlassTestScene.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "Core/Logger.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"
#include "engine/render/VulkanContext.h"

namespace
{

// material ids - using existing palette entries
constexpr uint8_t kMatGlass = 3;      // glass material from palette
constexpr uint8_t kMatFloor = 6;      // floor material
constexpr uint8_t kMatAccent = 12;    // accent color for visibility

engine::PaletteEntryCPU makeGlassEntry(const glm::vec3& tint, float roughness, float ior)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(tint, 1.0f);
    // pbr0: x=metallic, y=roughness, z=ao, w=shadingModel (1.0 = SHADING_GLASS)
    entry.pbr0 = glm::vec4(0.0f, roughness, 0.0f, 1.0f);
    // extra: x=ior (index of refraction, ~1.5 for glass)
    entry.extra = glm::vec4(ior, 0.0f, 0.0f, 0.0f);
    return entry;
}

engine::PaletteEntryCPU makeOpaqueEntry(const glm::vec3& color, float roughness)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(0.0f, roughness, 0.0f, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return entry;
}

// glass panel - flat for testing refraction
std::vector<uint8_t> buildGlassPanel(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    // fill entire volume with glass
    builder.fillBox({0, 0, 0}, dims - glm::ivec3(1), kMatGlass);
    return std::move(builder.data());
}

// glass cube - solid for thickness testing
std::vector<uint8_t> buildGlassCube(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    builder.fillBox({0, 0, 0}, dims - glm::ivec3(1), kMatGlass);
    return std::move(builder.data());
}

// glass sphere - for curved surface testing
std::vector<uint8_t> buildGlassSphere(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    const glm::vec3 center = glm::vec3(dims) * 0.5f;
    const float radius = std::min({center.x, center.y, center.z}) - 1.0f;
    builder.fillSphere(center, radius, kMatGlass);
    return std::move(builder.data());
}

// glass shell - hollow box for viewing through multiple layers
std::vector<uint8_t> buildGlassShell(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    // create hollow shell with 2-voxel thick walls
    builder.fillShellBox({0, 0, 0}, dims - glm::ivec3(1), 2, kMatGlass);
    return std::move(builder.data());
}

// floor platform with accent marker
std::vector<uint8_t> buildFloorPlatform(const glm::ivec3& dims)
{
    engine::VoxelBuilder builder(dims);
    // main floor
    builder.fillBox({0, 0, 0}, {dims.x - 1, 1, dims.z - 1}, kMatFloor);
    // accent cross pattern for refraction reference
    const int midX = dims.x / 2;
    const int midZ = dims.z / 2;
    builder.fillBox({midX - 1, 0, 0}, {midX, 1, dims.z - 1}, kMatAccent);
    builder.fillBox({0, 0, midZ - 1}, {dims.x - 1, 1, midZ}, kMatAccent);
    return std::move(builder.data());
}

} // namespace

bool GlassTestScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                           engine::VoxelPalette& palette)
{
    logInfo("GlassTestScene", "Initializing Glass Test Scene.");

    // setup palette with glass materials
    palette.buildDefaultPalette0();

    // glass material - cyan tint for visibility, ior=1.5 for standard glass
    palette.setEntry(0, kMatGlass, makeGlassEntry(glm::vec3(0.85f, 0.95f, 0.98f), 0.05f, 1.5f));

    // floor - dark gray
    palette.setEntry(0, kMatFloor, makeOpaqueEntry(glm::vec3(0.3f, 0.3f, 0.35f), 0.8f));

    // accent - bright orange for refraction reference
    palette.setEntry(0, kMatAccent, makeOpaqueEntry(glm::vec3(1.0f, 0.5f, 0.1f), 0.6f));

    if (!palette.upload(ctx))
    {
        logError("GlassTestScene", "Failed to upload glass test palette.");
        return false;
    }

    // define test volumes - various glass structures at different positions
    const std::vector<engine::VolumeSpec> specs = {
        // floor platform with reference pattern
        {"Floor", {40, 4, 40}, {-20.0f, -2.0f, -20.0f}, engine::VoxelVolume::FLAG_STATIC},

        // thin glass panel - front facing (tests basic refraction)
        {"GlassPanel", {16, 12, 3}, {-8.0f, 2.0f, 8.0f},
         engine::VoxelVolume::FLAG_STATIC | engine::VoxelVolume::FLAG_GLASS},

        // thick glass cube - tests absorption with thickness
        {"GlassCube", {8, 8, 8}, {8.0f, 2.0f, 0.0f},
         engine::VoxelVolume::FLAG_STATIC | engine::VoxelVolume::FLAG_GLASS},

        // glass sphere - tests curved normals and varying thickness
        {"GlassSphere", {14, 14, 14}, {-6.0f, 4.0f, -8.0f},
         engine::VoxelVolume::FLAG_STATIC | engine::VoxelVolume::FLAG_GLASS},

        // hollow glass shell - tests multiple glass layers
        {"GlassShell", {18, 14, 18}, {6.0f, 2.0f, -14.0f},
         engine::VoxelVolume::FLAG_STATIC | engine::VoxelVolume::FLAG_GLASS},
    };

    // build volume data
    std::vector<std::vector<uint8_t>> data;
    data.reserve(specs.size());
    data.push_back(buildFloorPlatform(specs[0].dims));
    data.push_back(buildGlassPanel(specs[1].dims));
    data.push_back(buildGlassCube(specs[2].dims));
    data.push_back(buildGlassSphere(specs[3].dims));
    data.push_back(buildGlassShell(specs[4].dims));

    if (!world.initAquariumScene(ctx, palette, specs.size(),
                                 [&](size_t index) -> const engine::VolumeSpec& {
                                     return specs[index];
                                 },
                                 [&](size_t index) -> const std::vector<uint8_t>& {
                                     return data[index];
                                 }))
    {
        logError("GlassTestScene", "Failed to initialize glass test volumes.");
        return false;
    }

    logInfo("GlassTestScene",
            makeLogMessage("Glass Test Scene initialized with ", specs.size(), " volumes"));
    return true;
}
