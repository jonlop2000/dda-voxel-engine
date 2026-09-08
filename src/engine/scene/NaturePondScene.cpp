#include "engine/scene/NaturePondScene.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "Core/Logger.h"
#include "engine/render/VulkanContext.h"
#include "engine/scene/NaturePondGenerator.h"
#include "engine/scene/NaturePondSunroofProbe.h"
#include "engine/scene/SceneConfig.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelVolume.h"
#include "engine/voxel/VoxelWorld.h"

namespace
{
uint64_t volumeTexelCount(const glm::ivec3& dims)
{
    return static_cast<uint64_t>(dims.x) * static_cast<uint64_t>(dims.y) *
           static_cast<uint64_t>(dims.z);
}

int nextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value)
    {
        result *= 2;
    }
    return result;
}

uint64_t occupancyHierarchyTexelCount(const glm::ivec3& sourceDims)
{
    glm::ivec3 dims(nextPowerOfTwo(sourceDims.x),
                    nextPowerOfTwo(sourceDims.y),
                    nextPowerOfTwo(sourceDims.z));
    uint64_t result = 0;
    while (true)
    {
        result += volumeTexelCount(dims);
        if (std::max({dims.x, dims.y, dims.z}) <= 1)
        {
            return result;
        }
        dims = glm::max(dims / 2, glm::ivec3(1));
    }
}

engine::PaletteEntryCPU makeEntry(const glm::vec3& color, float roughness,
                                  engine::VoxelMaterialCategory category,
                                  float emissive = 0.0f)
{
    engine::PaletteEntryCPU entry{};
    entry.baseColor_alpha = glm::vec4(color, 1.0f);
    entry.pbr0 = glm::vec4(0.0f, roughness, emissive, 0.0f);
    entry.extra = glm::vec4(1.0f, 0.0f, 0.0f, static_cast<float>(category));
    return entry;
}

template <uint8_t ExpectedCount, size_t N>
void setBand(engine::VoxelPalette& palette, uint8_t base,
             const std::array<glm::vec3, N>& colors, float roughness,
             engine::VoxelMaterialCategory category, float emissive = 0.0f)
{
    static_assert(static_cast<size_t>(ExpectedCount) == N);
    for (size_t i = 0; i < colors.size(); ++i)
    {
        palette.setEntry(0, static_cast<uint32_t>(base) + static_cast<uint32_t>(i),
                         makeEntry(colors[i], roughness, category, emissive));
    }
}

void buildNaturePondPalette(engine::VoxelPalette& palette)
{
    // fish reuse the established aquarium palette bands; the nature bands occupy 200-235.
    palette.buildAquariumPalette();
    setBand<NaturePondMaterial::Soil::Count>(palette, NaturePondMaterial::Soil::Base,
            std::array{glm::vec3(0.22f, 0.15f, 0.09f), glm::vec3(0.30f, 0.21f, 0.12f),
                       glm::vec3(0.36f, 0.27f, 0.16f), glm::vec3(0.17f, 0.12f, 0.08f)},
            0.96f, engine::VoxelMaterialCategory::Generic);
    setBand<NaturePondMaterial::Meadow::Count>(
            palette, NaturePondMaterial::Meadow::Base,
            std::array{glm::vec3(0.31f, 0.55f, 0.18f), glm::vec3(0.40f, 0.66f, 0.22f),
                       glm::vec3(0.24f, 0.46f, 0.14f), glm::vec3(0.52f, 0.69f, 0.27f)},
            0.93f, engine::VoxelMaterialCategory::Plant);
    setBand<NaturePondMaterial::Stone::Count>(palette, NaturePondMaterial::Stone::Base,
            std::array{glm::vec3(0.33f, 0.35f, 0.31f), glm::vec3(0.42f, 0.43f, 0.38f),
                       glm::vec3(0.49f, 0.50f, 0.44f), glm::vec3(0.27f, 0.29f, 0.28f),
                       glm::vec3(0.39f, 0.41f, 0.36f), glm::vec3(0.55f, 0.54f, 0.47f)},
            0.91f, engine::VoxelMaterialCategory::Stone);
    setBand<NaturePondMaterial::Gravel::Count>(
            palette, NaturePondMaterial::Gravel::Base,
            std::array{glm::vec3(0.25f, 0.25f, 0.21f), glm::vec3(0.34f, 0.33f, 0.28f),
                       glm::vec3(0.43f, 0.41f, 0.34f), glm::vec3(0.20f, 0.22f, 0.20f)},
            0.96f, engine::VoxelMaterialCategory::Gravel);
    setBand<NaturePondMaterial::Wood::Count>(palette, NaturePondMaterial::Wood::Base,
            std::array{glm::vec3(0.20f, 0.12f, 0.07f), glm::vec3(0.29f, 0.18f, 0.10f),
                       glm::vec3(0.37f, 0.25f, 0.14f), glm::vec3(0.15f, 0.10f, 0.07f)},
            0.94f, engine::VoxelMaterialCategory::Wood);
    setBand<NaturePondMaterial::Foliage::Count>(
            palette, NaturePondMaterial::Foliage::Base,
            std::array{glm::vec3(0.10f, 0.32f, 0.08f), glm::vec3(0.15f, 0.43f, 0.09f),
                       glm::vec3(0.22f, 0.55f, 0.11f), glm::vec3(0.35f, 0.68f, 0.15f),
                       glm::vec3(0.08f, 0.28f, 0.14f), glm::vec3(0.14f, 0.40f, 0.18f),
                       glm::vec3(0.24f, 0.52f, 0.20f), glm::vec3(0.38f, 0.64f, 0.24f)},
            0.88f, engine::VoxelMaterialCategory::Plant);
    setBand<NaturePondMaterial::Flower::Count>(
            palette, NaturePondMaterial::Flower::Base,
            std::array{
                // four paired families keep colonies readable while each bloom
                // receives a deterministic light/dark accent variation.
                glm::vec3(1.00f, 0.20f, 0.28f), // coral red
                glm::vec3(1.00f, 0.37f, 0.66f), // vivid rose
                glm::vec3(1.00f, 0.76f, 0.06f), // sunflower gold
                glm::vec3(1.00f, 0.46f, 0.08f), // tangerine
                glm::vec3(0.69f, 0.27f, 0.98f), // saturated violet
                glm::vec3(0.88f, 0.48f, 1.00f), // orchid
                glm::vec3(0.22f, 0.58f, 1.00f), // cornflower blue
                glm::vec3(0.18f, 0.86f, 0.93f), // bright aqua
            },
            0.84f, engine::VoxelMaterialCategory::Plant);
    setBand<NaturePondMaterial::Ember::Count>(
        palette, NaturePondMaterial::Ember::Base,
        std::array{glm::vec3(0.95f, 0.08f, 0.015f),
                   glm::vec3(1.0f, 0.22f, 0.025f),
                   glm::vec3(1.0f, 0.52f, 0.06f),
                   glm::vec3(1.0f, 0.86f, 0.34f)},
        0.72f, engine::VoxelMaterialCategory::Generic, 1.25f);
    setBand<NaturePondMaterial::Architecture::Count>(
        palette, NaturePondMaterial::Architecture::Base,
        std::array{glm::vec3(0.91f, 0.88f, 0.78f),
                   glm::vec3(0.98f, 0.95f, 0.86f),
                   glm::vec3(0.67f, 0.65f, 0.59f),
                   glm::vec3(0.78f, 0.72f, 0.61f),
                   glm::vec3(0.48f, 0.58f, 0.42f),
                   glm::vec3(0.84f, 0.79f, 0.68f)},
        0.92f, engine::VoxelMaterialCategory::Stone);
}
} // namespace

bool isNaturePondScene(const SceneConfig& sceneConfig)
{
    return naturePondDetailTierForSceneName(sceneConfig.name).has_value();
}

bool NaturePondScene::init(VulkanContext& ctx, engine::VoxelWorld& world,
                           engine::VoxelPalette& palette, uint32_t seed,
                           const SceneConfig& sceneConfig,
                           NaturePondSceneRenderData* renderData)
{
    if (renderData != nullptr)
    {
        *renderData = {};
    }
    const NaturePondDetailTier detailTier = detailTierForScene(sceneConfig);
    const NaturePondDensityStep densityStep = densityStepForScene(sceneConfig);
    const NaturePondBuildOptions buildOptions{
        isNaturePondLightingParityProbeName(sceneConfig.name), densityStep};
    const NaturePondDetailTierInfo& tierInfo = naturePondDetailTierInfo(detailTier);
    const NaturePondDensityStepInfo& densityInfo =
        naturePondDensityStepInfo(densityStep);
    logInfo("NaturePondScene",
            makeLogMessage("Initializing deterministic nature pond scene at ",
                           tierInfo.label, "; density step ", densityInfo.label,
                           "."));
    buildNaturePondPalette(palette);
    if (!palette.upload(ctx))
    {
        logError("NaturePondScene", "Failed to upload nature pond palette.");
        return false;
    }

    NaturePondBuild build = buildNaturePond(seed, detailTier, buildOptions);
    if (isNaturePondSunroofProbeName(sceneConfig.name) &&
        !decorateNaturePondSunroofProbe(build))
    {
        logError("NaturePondScene",
                 "Failed to apply the central-island sunroof probe composition.");
        return false;
    }
    std::vector<engine::VolumeSpec> specs;
    specs.reserve(build.volumes.size());
    uint32_t legacyFoliageVolumeIndex = std::numeric_limits<uint32_t>::max();
    for (size_t volumeIndex = 0; volumeIndex < build.volumes.size(); ++volumeIndex)
    {
        const NaturePondVolumeData& volume = build.volumes[volumeIndex];
        engine::VolumeSpec spec{};
        spec.name = volume.name;
        spec.dims = volume.dims;
        spec.position = volume.position;
        spec.scale = volume.scale;
        spec.flags = engine::VoxelVolume::FLAG_STATIC;
        if (volume.role == NaturePondVolumeRole::TerrainOpaque ||
            volume.role == NaturePondVolumeRole::OpaqueDetails ||
            volume.role == NaturePondVolumeRole::HeroTrunkOpaque ||
            volume.role == NaturePondVolumeRole::FoliageTranslucentMeadow ||
            volume.role == NaturePondVolumeRole::HeroCanopyTranslucent ||
            volume.role == NaturePondVolumeRole::HeroCanopyOcclusionProxy ||
            volume.role == NaturePondVolumeRole::SunroofArchitectureOpaque)
        {
            spec.flags |= engine::VoxelVolume::FLAG_ALLOW_DENSE_SKIP;
        }
        switch (volume.role)
        {
        case NaturePondVolumeRole::FoliageTranslucentMeadow:
            spec.lightingOcclusionMode =
                engine::VoxelVolume::LightingOcclusionMode::TranslucentMeadow;
            legacyFoliageVolumeIndex = static_cast<uint32_t>(volumeIndex);
            break;
        case NaturePondVolumeRole::HeroCanopyTranslucent:
            spec.lightingOcclusionMode =
                engine::VoxelVolume::LightingOcclusionMode::TranslucentFoliage;
            break;
        case NaturePondVolumeRole::HeroCanopyOcclusionProxy:
            spec.lightingOcclusionMode =
                engine::VoxelVolume::LightingOcclusionMode::Proxy;
            break;
        default:
            spec.lightingOcclusionMode =
                engine::VoxelVolume::LightingOcclusionMode::BinaryOpaque;
            break;
        }
        specs.push_back(std::move(spec));
    }

    if (!world.initAquariumScene(
            ctx, palette, specs.size(),
            [&](size_t index) -> const engine::VolumeSpec& { return specs[index]; },
            [&](size_t index) -> const std::vector<uint8_t>& {
                return build.volumes[index].voxels;
            }))
    {
        logError("NaturePondScene", "Failed to initialize nature pond volumes.");
        return false;
    }

    if (renderData != nullptr)
    {
        renderData->foliageInstances = build.foliageInstances;
        renderData->foliageInstances.reserve(
            build.foliageInstances.size() +
            build.heroCanopyFoliageInstances.size());
        renderData->foliageInstances.insert(
            renderData->foliageInstances.end(),
            build.heroCanopyFoliageInstances.begin(),
            build.heroCanopyFoliageInstances.end());
        const glm::vec2 halfExtent = build.patchExtentMeters * 0.5f;
        const engine::scene::WindborneParticleDomain windborneDomain{
            glm::vec3(-halfExtent.x, 4.15f, -halfExtent.y),
            glm::vec3(halfExtent.x, 8.25f, halfExtent.y),
            NaturePondMaterial::Foliage::Base,
            NaturePondMaterial::Foliage::Count,
            NaturePondMaterial::Flower::Base,
            NaturePondMaterial::Flower::Count};
        renderData->windborneParticles =
            engine::scene::buildWindborneParticleField(windborneDomain, seed);
        renderData->legacyFoliageVolumeIndex = legacyFoliageVolumeIndex;
    }

    logInfo("NaturePondScene", makeLogMessage("Nature pond initialized with ", specs.size(),
                                               " layered volumes (seed ", seed, ")."));
    constexpr float kQaViewportHeight = 1080.0f;
    constexpr float kQaVerticalFovRadians = 1.0471975512f;
    const glm::vec3 qaReferencePoint(0.0f, naturePondLayout().pondSurfaceHeight, 0.0f);
    const auto& anchors = naturePondCameraAnchors();
    const float overviewPixels = naturePondProjectedPixelsPerVoxel(
        tierInfo.voxelPitch, anchors[0], qaReferencePoint, kQaViewportHeight,
        kQaVerticalFovRadians);
    const float bankPixels = naturePondProjectedPixelsPerVoxel(
        tierInfo.voxelPitch, anchors[1], qaReferencePoint, kQaViewportHeight,
        kQaVerticalFovRadians);
    const uint64_t packedVoxelCapacity =
        static_cast<uint64_t>(build.volumes[1].voxels.size()) +
        static_cast<uint64_t>(build.volumes[2].voxels.size());
    uint64_t nominalVoxelR8Bytes = 0;
    uint64_t nominalOccupancyR8Bytes = 0;
    for (const NaturePondVolumeData& volume : build.volumes)
    {
        nominalVoxelR8Bytes += volumeTexelCount(volume.dims);
        nominalOccupancyR8Bytes += occupancyHierarchyTexelCount(volume.dims);
    }
    logInfo("NaturePondScene",
            makeLogMessage("Detail tier: metricResolution=1920x1080 metricFovY=60deg pitch=",
                           tierInfo.voxelPitch, "m volumeCount=", build.volumes.size(),
                           " densityStep=", densityInfo.id,
                           " patchExtent=", build.patchExtentMeters.x, "x",
                           build.patchExtentMeters.y, "m scatterDensityMultiplier=",
                           build.scatterDensityMultiplier,
                           " terrainDims=", build.volumes[0].dims.x, "x",
                           build.volumes[0].dims.y, "x", build.volumes[0].dims.z,
                           " detailDims=", build.volumes[1].dims.x, "x",
                           build.volumes[1].dims.y, "x", build.volumes[1].dims.z,
                           " packedVoxelCapacity=", packedVoxelCapacity,
                           " nominalPackedR8Bytes=", packedVoxelCapacity,
                           " nominalVoxelR8Bytes=", nominalVoxelR8Bytes,
                           " nominalOccupancyR8Bytes=", nominalOccupancyR8Bytes,
                           " nominalTotalR8Bytes=",
                           nominalVoxelR8Bytes + nominalOccupancyR8Bytes,
                           " terrainOccupied=", build.terrainOccupiedVoxels,
                           " opaqueDetailOccupied=", build.opaqueDetailOccupiedVoxels,
                           " foliageOccupied=", build.foliageOccupiedVoxels,
                           " foliageInstances=",
                           build.foliageInstances.size() +
                               build.heroCanopyFoliageInstances.size(),
                           " meadowFoliageInstances=",
                           build.foliageInstances.size(),
                           " heroCanopyFoliageInstances=",
                           build.heroCanopyFoliageInstances.size(),
                           " heroTrunkOccupied=", build.heroTrunkOccupiedVoxels,
                           " heroCanopyOccupied=", build.heroCanopyOccupiedVoxels,
                           " heroCanopyProxyOccupied=",
                           build.heroCanopyProxyOccupiedVoxels,
                           " sunroofArchitectureOccupied=",
                           build.sunroofArchitectureOccupiedVoxels,
                           " lightingParityEmissiveOccupied=",
                           build.lightingParityEmissiveOccupiedVoxels,
                           " overviewPixelsPerVoxel=", overviewPixels,
                           " bankPixelsPerVoxel=", bankPixels, "."));
    logInfo(
        "NaturePondScene",
        "Foliage occlusion policy: trunk=BinaryOpaque "
        "meadow=TranslucentMeadow(shadow-only while instanced) "
        "canopy=TranslucentFoliage "
        "canopyProxy=Proxy(AO+local-only, color+reflection+sun hidden) "
        "sunroof=BinaryOpaque.");
    if (buildOptions.includeLightingParityProbe)
    {
        logInfo(
            "NaturePondScene",
            makeLogMessage(
                "Lighting parity content: emissiveAlcove=1 emissiveOccupied=",
                build.lightingParityEmissiveOccupiedVoxels,
                " localLightPreset=scene-owned."));
    }
    return true;
}

NaturePondDetailTier NaturePondScene::detailTierForScene(const SceneConfig& sceneConfig)
{
    return naturePondDetailTierForSceneName(sceneConfig.name)
        .value_or(NaturePondDetailTier::Reference10Cm);
}

NaturePondDensityStep NaturePondScene::densityStepForScene(
    const SceneConfig& sceneConfig)
{
    return naturePondDensityStepForSceneName(sceneConfig.name)
        .value_or(NaturePondDensityStep::Baseline);
}

const engine::game::FishHabitat& NaturePondScene::fishHabitat()
{
    return naturePondLayout().fishHabitat;
}

const std::array<NaturePondCameraAnchor, 3>& NaturePondScene::cameraAnchors()
{
    return naturePondCameraAnchors();
}

glm::vec3 NaturePondScene::pondBoundsMin()
{
    return naturePondLayout().pondBoundsMin;
}

glm::vec3 NaturePondScene::pondBoundsMax()
{
    return naturePondLayout().pondBoundsMax;
}

float NaturePondScene::pondSurfaceHeight()
{
    return naturePondLayout().pondSurfaceHeight;
}
