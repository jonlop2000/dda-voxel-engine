#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

#include "engine/game/FoliageArchetype.h"
#include "engine/game/FoliagePatch.h"

namespace engine::render
{

inline constexpr const char* kFoliageVoxelTopologyName =
    "foliage-meadow-volume-v10";
inline constexpr uint32_t kFoliageVoxelVerticesPerPrimitive = 36u;
inline constexpr uint32_t kFoliageVoxelMaximumCellsPerAxis = 32u;
// the high seed nibble is renderer metadata. oriented meadow slabs set the high
// bit and store an octagonal yaw in bits 28-30; all other primitives keep zeroes
// there. the remaining 28 bits retain deterministic material/topology variation.
inline constexpr uint32_t kFoliageVoxelOrientedSlabFlag = 0x80000000u;
inline constexpr uint32_t kFoliageVoxelYawOctantShift = 28u;
inline constexpr uint32_t kFoliageVoxelYawOctantMask = 0x70000000u;
inline constexpr uint32_t kFoliageVoxelRandomSeedMask = 0x0fffffffu;

enum class FoliageVoxelPrimitiveRole : uint32_t
{
    Stem = 0,
    Leaf = 1,
    Branch = Leaf, // source-compatible name for the archived tuft topology/tests.
    Flower = 2,
    GroundCover = 3,
};

// std430-compatible packing for one procedurally expanded closed cube.
struct alignas(16) FoliageGpuPrimitive
{
    glm::vec4 centerMotionT{0.0f};       // xyz=rest center, w=normalized motion height
    glm::vec4 halfExtentPhase{0.0f};     // xyz=positive half extent, w=shared plant phase
    glm::uvec4 materialSeedFlags{0u};    // material, morphology, seed+orientation, role
    glm::vec4 swayProfile{0.0f};         // resolved amplitude, speed, bend exponent, root rigidity
};

static_assert(std::is_standard_layout_v<FoliageGpuPrimitive>);
static_assert(sizeof(FoliageGpuPrimitive) == 64);
static_assert(alignof(FoliageGpuPrimitive) == 16);
static_assert(offsetof(FoliageGpuPrimitive, centerMotionT) == 0);
static_assert(offsetof(FoliageGpuPrimitive, halfExtentPhase) == 16);
static_assert(offsetof(FoliageGpuPrimitive, materialSeedFlags) == 32);
static_assert(offsetof(FoliageGpuPrimitive, swayProfile) == 48);

struct FoliageUnitCubeVertex
{
    // each component is exactly -1 or +1 for a valid topology vertex.
    glm::vec3 positionSign{0.0f};
    glm::vec3 normal{0.0f};
};

// decodes the non-indexed closed-cube topology used by foliage.vert. indices outside
// [0, kFoliageVoxelVerticesPerPrimitive) return the zero-initialized invalid value.
[[nodiscard]] FoliageUnitCubeVertex decodeFoliageUnitCubeVertex(
    uint32_t vertexIndex) noexcept;

struct FoliageVoxelGeometry
{
    size_t semanticInstanceCount = 0;
    size_t patchCount = 0;
    std::array<size_t,
               static_cast<size_t>(engine::game::FoliagePatchKind::Count)>
        patchKindCounts{};
    std::array<size_t,
               static_cast<size_t>(engine::game::FoliageMorphology::Count)>
        morphologyPatchCounts{};
    std::vector<FoliageGpuPrimitive> primitives;
};

// pure deterministic geometry expansion. Invalid/non-finite semantic input fails
// closed by returning no primitives while preserving semanticInstanceCount.
[[nodiscard]] FoliageVoxelGeometry buildFoliageVoxelGeometry(
    std::span<const engine::game::FoliageBladeInstance> plants);

} // namespace engine::render
