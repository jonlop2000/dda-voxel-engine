#pragma once

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

#include "engine/scene/EnvironmentWind.h"

namespace engine::game
{

enum class FoliageForm : uint32_t
{
    MeadowBlade = 0,
    Reed = 1,
};

// coarse ecological identity authored by placement. this remains renderer
// independent so density changes cannot cause a render-side reclassification.
enum class FoliagePatchKind : uint32_t
{
    GrassTuft = 0,
    Shrub = 1,
    FlowerCluster = 2,
    ReedCluster = 3,
    WaterLilyCluster = 4,
    TreeCanopyCluster = 5,
    Count = 6,
};

// fine visual identity expanded by the one-batch renderer. flower patches choose
// one stable species while the other ecological categories map one-to-one.
enum class FoliageMorphology : uint32_t
{
    GrassTuft = 0,
    BranchingShrub = 1,
    DaisyFlower = 2,
    SpikeFlower = 3,
    Reed = 4,
    WaterLily = 5,
    TreeCanopySprig = 6,
    Count = 7,
};

enum FoliageBladeFlags : uint32_t
{
    FoliageBladeNone = 0,
    FoliageBladeReed = 1u << 0u,
    FoliageBladeFlower = 1u << 1u,
    // this semantic anchor contributes only the low meadow carpet. patch kind
    // and morphology still carry the shared material/wind identity, but the
    // species accent expander must skip it.
    FoliageBladeGroundCoverOnly = 1u << 2u,
    // this plant is rooted to a water surface rather than terrain. the flag is
    // semantic (placement/tests/editor inspection); it does not create another
    // rendering path or foliage batch.
    FoliageBladeAquatic = 1u << 3u,
    // this sprig is rooted to the authored hero canopy rather than the terrain
    // ecology lattice. it remains in the same one-batch foliage renderer.
    FoliageBladeTreeCanopy = 1u << 4u,
};

struct FoliageSwayProfile
{
    // maximum animated tip displacement. rest lean is accounted for separately.
    float maxTipDisplacementMeters = 0.05f;
    float angularSpeedRadiansPerSecond = 1.0f;
    float bendExponent = 1.8f;
    // fraction of blade height that remains rigid before animated bending begins.
    float rootRigidity = 0.25f;
};

struct FoliageArchetype
{
    FoliageForm form = FoliageForm::MeadowBlade;
    float halfWidthMeters = 0.035f;
    FoliageSwayProfile sway{};
};

struct FoliageMorphologyArchetype
{
    FoliageMorphology morphology = FoliageMorphology::GrassTuft;
    FoliageSwayProfile sway{};
    // hard cpu expansion bounds. these protect the one-batch renderer from an
    // accidental content-dependent topology explosion.
    uint32_t minimumPrimitivesPerPatch = 1u;
    uint32_t maximumPrimitivesPerPatch = 1u;
};

// renderer-independent authored placement. material ids are resolved by the shared
// voxel palette, so the legacy voxel and instanced paths retain one color authority.
struct FoliageBladeInstance
{
    uint64_t stableId = 0;
    uint64_t stablePatchId = 0;
    glm::vec3 rootWorld{0.0f};
    glm::vec3 patchRootWorld{0.0f};
    float heightMeters = 0.0f;
    float cellSizeMeters = 0.1f;
    float patchRadiusMeters = 0.1f;
    float restLeanMeters = 0.0f;
    float yawRadians = 0.0f;
    float phaseRadians = 0.0f;
    uint32_t randomSeed = 0;
    uint32_t patchSeed = 0;
    uint32_t stemMaterialId = 0;
    uint32_t secondaryMaterialId = 0;
    uint32_t tipMaterialId = 0;
    uint32_t flags = FoliageBladeNone;
    FoliagePatchKind patchKind = FoliagePatchKind::GrassTuft;
    FoliageMorphology morphology = FoliageMorphology::GrassTuft;
};

[[nodiscard]] const FoliageArchetype& foliageArchetype(FoliageForm form);
[[nodiscard]] const FoliageMorphologyArchetype& foliageMorphologyArchetype(
    FoliageMorphology morphology);
[[nodiscard]] std::string_view foliageMorphologyName(
    FoliageMorphology morphology);
// a stable flower-patch seed selects one of two visual species so topology is
// deterministic and input-order independent of the scene distribution mix.
[[nodiscard]] FoliageMorphology foliageFlowerMorphology(uint32_t stableSeed);
[[nodiscard]] FoliageMorphology foliageMorphologyForPatch(
    FoliagePatchKind kind, uint32_t stableSeed);
[[nodiscard]] float foliageMorphologyResolvedMaxTipDisplacementMeters(
    FoliageMorphology morphology, uint32_t stableSeed);
[[nodiscard]] FoliageForm foliageForm(const FoliageBladeInstance& instance);
[[nodiscard]] uint64_t foliageStableId(uint32_t worldSeed, int candidateX,
                                       int candidateZ);
// separate namespace for carpet anchors on the same canonical candidate cell.
// supported coordinates are the ecology sampler's bounded [-512, 512] range.
[[nodiscard]] uint64_t foliageCarpetStableId(uint32_t worldSeed,
                                             int candidateX,
                                             int candidateZ);
// separate namespace for floating aquatic anchors on the canonical candidate
// lattice. this stays disjoint from both land accents and the meadow carpet.
[[nodiscard]] uint64_t foliageAquaticStableId(uint32_t worldSeed,
                                              int candidateX,
                                              int candidateZ);
// separate namespaces for deterministic hero-canopy sprigs and their coherent
// lobe patches. both remain disjoint from land, carpet, and aquatic identities.
[[nodiscard]] uint64_t foliageTreeCanopyStableId(uint32_t worldSeed,
                                                 uint32_t lobeIndex,
                                                 uint32_t sprigIndex);
[[nodiscard]] uint64_t foliageTreeCanopyPatchStableId(uint32_t worldSeed,
                                                      uint32_t lobeIndex);
[[nodiscard]] float foliageResolvedMaxTipDisplacementMeters(
    const FoliageBladeInstance& instance);

// cpu reference mirrored by foliage.vert. this returns animated displacement only;
// authored rest lean is baked into geometry. normalizedHeight=0 is an exact anchor.
[[nodiscard]] glm::vec3 foliageDisplacement(
    const FoliageBladeInstance& instance, float normalizedHeight, float timeSeconds,
    const engine::scene::EnvironmentWindSettings& wind =
        engine::scene::defaultEnvironmentWindSettings(),
    float swayStrength = 1.0f);
[[nodiscard]] float foliageConservativeDisplacement(
    const FoliageBladeInstance& instance,
    const engine::scene::EnvironmentWindSettings& wind =
        engine::scene::defaultEnvironmentWindSettings(),
    float swayStrength = 1.0f);

} // namespace engine::game
