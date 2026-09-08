#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "engine/game/FishHabitat.h"
#include "engine/scene/NaturePondGenerator.h"
#include "engine/scene/WindborneParticleField.h"

struct SceneConfig;
struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

bool isNaturePondScene(const SceneConfig& sceneConfig);

struct NaturePondSceneRenderData
{
    std::vector<engine::game::FoliageBladeInstance> foliageInstances;
    std::vector<engine::scene::WindborneParticleCandidate>
        windborneParticles;
    uint32_t legacyFoliageVolumeIndex = std::numeric_limits<uint32_t>::max();
};

class NaturePondScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world,
                     engine::VoxelPalette& palette, uint32_t seed,
                     const SceneConfig& sceneConfig,
                     NaturePondSceneRenderData* renderData = nullptr);
    static NaturePondDetailTier detailTierForScene(const SceneConfig& sceneConfig);
    static NaturePondDensityStep densityStepForScene(const SceneConfig& sceneConfig);
    static const engine::game::FishHabitat& fishHabitat();
    static const std::array<NaturePondCameraAnchor, 3>& cameraAnchors();
    static glm::vec3 pondBoundsMin();
    static glm::vec3 pondBoundsMax();
    static float pondSurfaceHeight();
};
