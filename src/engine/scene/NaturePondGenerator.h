#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "engine/game/FishHabitat.h"
#include "engine/game/FoliageArchetype.h"

enum class NaturePondVolumeRole
{
    TerrainOpaque,
    OpaqueDetails,
    FoliageTranslucentMeadow,
    HeroTrunkOpaque,
    HeroCanopyTranslucent,
    HeroCanopyOcclusionProxy,
    SunroofArchitectureOpaque,
};

enum class NaturePondDetailTier
{
    Reference10Cm,
    Balanced20Cm,
    Coarse25Cm,
};

enum class NaturePondDensityStep
{
    Baseline,
    ExpandedCoverage,
    ExpandedCoverageDenseScatter,
};

// shared contract between deterministic voxel generation and palette construction.
namespace NaturePondMaterial
{
namespace Soil
{
inline constexpr uint8_t Base = 200;
inline constexpr uint8_t Count = 4;
} // namespace Soil
namespace Meadow
{
inline constexpr uint8_t Base = 204;
inline constexpr uint8_t Count = 4;
} // namespace Meadow
namespace Stone
{
inline constexpr uint8_t Base = 208;
inline constexpr uint8_t Count = 6;
} // namespace Stone
namespace Gravel
{
inline constexpr uint8_t Base = 214;
inline constexpr uint8_t Count = 4;
} // namespace Gravel
namespace Wood
{
inline constexpr uint8_t Base = 218;
inline constexpr uint8_t Count = 4;
} // namespace Wood
namespace Foliage
{
inline constexpr uint8_t Base = 222;
inline constexpr uint8_t Count = 8;
} // namespace Foliage
namespace Flower
{
inline constexpr uint8_t Base = 230;
inline constexpr uint8_t Count = 8;
} // namespace Flower
namespace Ember
{
inline constexpr uint8_t Base = 238;
inline constexpr uint8_t Count = 4;
} // namespace Ember
namespace Architecture
{
inline constexpr uint8_t Base = 242;
inline constexpr uint8_t Count = 6;
} // namespace Architecture

static_assert(Soil::Base + Soil::Count == Meadow::Base);
static_assert(Meadow::Base + Meadow::Count == Stone::Base);
static_assert(Stone::Base + Stone::Count == Gravel::Base);
static_assert(Gravel::Base + Gravel::Count == Wood::Base);
static_assert(Wood::Base + Wood::Count == Foliage::Base);
static_assert(Foliage::Base + Foliage::Count == Flower::Base);
static_assert(Flower::Base + Flower::Count == Ember::Base);
static_assert(Ember::Base + Ember::Count == Architecture::Base);
static_assert(Architecture::Base + Architecture::Count <= 256);
} // namespace NaturePondMaterial

namespace NaturePondLightingParity
{
inline constexpr std::string_view LitSceneName =
    "nature_pond_lighting_probe";
inline constexpr std::string_view UnlitSceneName =
    "nature_pond_lighting_unlit_probe";
inline constexpr glm::vec3 FireLightPosition{-2.8f, 4.85f, 6.0f};
} // namespace NaturePondLightingParity

namespace NaturePondAtmosphere
{
inline constexpr std::string_view StarSceneName = "nature_pond_star_probe";
} // namespace NaturePondAtmosphere

namespace NaturePondSunroof
{
inline constexpr std::string_view SceneName = "nature_pond_sunroof_probe";
inline constexpr glm::vec2 IslandCenterXZ{0.0f, 0.0f};
} // namespace NaturePondSunroof

struct NaturePondBuildOptions
{
    bool includeLightingParityProbe = false;
    NaturePondDensityStep densityStep = NaturePondDensityStep::Baseline;
};

struct NaturePondDetailTierInfo
{
    NaturePondDetailTier tier = NaturePondDetailTier::Reference10Cm;
    std::string_view sceneName;
    std::string_view label;
    float voxelPitch = 0.1f;
};

struct NaturePondDensityStepInfo
{
    NaturePondDensityStep step = NaturePondDensityStep::Baseline;
    std::string_view id = "baseline";
    std::string_view overviewSceneName;
    std::string_view bankSceneName;
    std::string_view label;
    glm::vec2 patchExtentMeters{24.0f, 20.0f};
    float scatterDensityMultiplier = 1.0f;
};

struct NaturePondVolumeData
{
    std::string name;
    glm::ivec3 dims{0};
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    NaturePondVolumeRole role = NaturePondVolumeRole::TerrainOpaque;
    std::vector<uint8_t> voxels;
};

struct NaturePondLayout
{
    glm::vec3 pondBoundsMin{0.0f};
    glm::vec3 pondBoundsMax{0.0f};
    float pondSurfaceHeight = 0.0f;
    glm::vec2 heroTreeCenterXZ{0.0f};
    engine::game::FishHabitat fishHabitat{};
};

struct NaturePondBuild
{
    uint32_t seed = 0;
    NaturePondDetailTier detailTier = NaturePondDetailTier::Reference10Cm;
    NaturePondDensityStep densityStep = NaturePondDensityStep::Baseline;
    float detailVoxelPitch = 0.1f;
    glm::vec2 patchExtentMeters{24.0f, 20.0f};
    float scatterDensityMultiplier = 1.0f;
    NaturePondLayout layout{};
    std::vector<NaturePondVolumeData> volumes;
    std::vector<engine::game::FoliageBladeInstance> foliageInstances;
    std::vector<engine::game::FoliageBladeInstance>
        heroCanopyFoliageInstances;
    uint64_t terrainOccupiedVoxels = 0;
    uint64_t opaqueDetailOccupiedVoxels = 0;
    uint64_t foliageOccupiedVoxels = 0;
    uint64_t heroTrunkOccupiedVoxels = 0;
    uint64_t heroCanopyOccupiedVoxels = 0;
    uint64_t heroCanopyProxyOccupiedVoxels = 0;
    uint64_t sunroofArchitectureOccupiedVoxels = 0;
    uint64_t lightingParityEmissiveOccupiedVoxels = 0;
};

struct NaturePondCameraAnchor
{
    std::string_view name;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

const NaturePondLayout& naturePondLayout();
const std::array<NaturePondCameraAnchor, 3>& naturePondCameraAnchors();
const std::array<NaturePondDetailTierInfo, 3>& naturePondDetailTiers();
const NaturePondDetailTierInfo& naturePondDetailTierInfo(NaturePondDetailTier tier);
const std::array<NaturePondDensityStepInfo, 3>& naturePondDensitySteps();
const NaturePondDensityStepInfo& naturePondDensityStepInfo(NaturePondDensityStep step);
std::optional<NaturePondDensityStep> naturePondDensityStepForSceneName(
    std::string_view sceneName);
bool isNaturePondDensityProbeName(std::string_view sceneName);
std::optional<NaturePondDetailTier> naturePondDetailTierForSceneName(
    std::string_view sceneName);
bool isNaturePondLightingParityProbeName(std::string_view sceneName);
bool isNaturePondSunroofProbeName(std::string_view sceneName);
float naturePondProjectedPixelsPerVoxel(float voxelPitch,
                                        const NaturePondCameraAnchor& camera,
                                        const glm::vec3& referencePoint,
                                        float viewportHeight, float verticalFovRadians);
NaturePondBuild buildNaturePond(
    uint32_t seed,
    NaturePondDetailTier detailTier = NaturePondDetailTier::Reference10Cm,
    NaturePondBuildOptions options = {});
