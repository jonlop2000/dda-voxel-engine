#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>

#include <glm/glm.hpp>

namespace engine::editor
{

struct TerrainCellSelection
{
    // names the authored terrain lattice, so the same cell coordinate in two
    // independently editable grids cannot alias.
    std::string terrainKey{};
    glm::ivec3 cell{0};
};

struct VoxelVolumeSelection
{
    // runtime voxel volumes do not currently have authored persistent ids.
    // this read-only identity is intentionally ephemeral: any structural
    // rebuild changes structureRevision and makes a retained selection stale.
    // it must never be used as an authorable journal identity.
    uint64_t structureRevision = 0;
    uint32_t index = 0;
};

struct PlaceableSelection
{
    std::string uuid{};
};

enum class SelectionTargetKind
{
    None,
    TerrainCell,
    VoxelVolume,
    Placeable,
};

using SelectionTargetValue =
    std::variant<std::monostate, TerrainCellSelection, VoxelVolumeSelection,
                 PlaceableSelection>;

// typed, renderer-independent selection identity. Terrain/placeable identity
// is authored; voxel-volume identity is revision-scoped and read-only.
struct SelectionTarget
{
    SelectionTargetValue value{};

    [[nodiscard]] static SelectionTarget terrainCell(
        std::string terrainKey, const glm::ivec3& cell);
    [[nodiscard]] static SelectionTarget voxelVolume(
        uint64_t structureRevision, uint32_t index);
    [[nodiscard]] static SelectionTarget placeable(std::string uuid);

    [[nodiscard]] SelectionTargetKind kind() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] bool selectionTargetEqual(const SelectionTarget& lhs,
                                        const SelectionTarget& rhs) noexcept;

// strict total ordering by kind and typed identity. this is the final
// picking tie-break and is intentionally unrelated to candidate array order.
[[nodiscard]] bool selectionTargetLess(const SelectionTarget& lhs,
                                       const SelectionTarget& rhs) noexcept;

struct SelectionRay
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float maxDistance = 1000.0f;
};

enum class SelectionRayBuildError
{
    None,
    NonFiniteCursor,
    CursorOutsideViewport,
    InvalidViewportExtent,
    NonFiniteInverseViewProjection,
    InvalidInverseViewProjection,
    InvalidUnprojection,
    DegenerateRay,
    InvalidMaxDistance,
};

struct SelectionRayBuildResult
{
    SelectionRay ray{};
    SelectionRayBuildError error = SelectionRayBuildError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == SelectionRayBuildError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// converts a top-left-origin viewport position into a world-space ray using
// vulkan clip depth: near z = 0 and far z = 1. the supplied matrix must be the
// inverse of the engine's exact view-projection, including camera's vulkan y
// flip. consequently viewport y maps directly from top=-1 to bottom=+1;
// callers must not unflip the projection a second time. perspective and
// orthographic matrices are both supported through homogeneous division.
[[nodiscard]] SelectionRayBuildResult selectionRayFromViewport(
    const glm::vec2& cursorPosition, const glm::uvec2& viewportExtent,
    const glm::mat4& inverseViewProjection, float maxDistance) noexcept;

[[nodiscard]] const char* selectionRayBuildErrorLabel(
    SelectionRayBuildError error) noexcept;

struct SelectionBounds
{
    glm::vec3 localMin{-0.5f};
    glm::vec3 localMax{0.5f};
};

struct SelectionCandidate
{
    SelectionTarget target{};
    SelectionBounds localBounds{};

    // must be a finite, affine, safely invertible transform. intersection is
    // performed in local space, so rotated and nonuniformly scaled bounds stay
    // exact instead of being widened to a world aabb.
    glm::mat4 worldFromLocal{1.0f};

    // higher priority wins only when two entry distances are exactly equal.
    // tool modes can therefore prefer cells or placeables without hiding a
    // geometrically nearer target.
    uint32_t priority = 0;
};

struct SelectionHit
{
    SelectionTarget target{};
    float distance = 0.0f;
    glm::vec3 worldPosition{0.0f};
    glm::vec3 worldNormal{0.0f};
    uint32_t priority = 0;
};

enum class SelectionPickError
{
    None,
    NonFiniteRay,
    DegenerateRayDirection,
    InvalidMaxDistance,
    InvalidTarget,
    DuplicateTarget,
    InvalidBounds,
    NonFiniteTransform,
    NonAffineTransform,
    NonInvertibleTransform,
    NonFiniteWorldBounds,
};

struct SelectionPickResult
{
    static constexpr size_t kNoCandidate = static_cast<size_t>(-1);

    std::optional<SelectionHit> hit{};
    SelectionPickError error = SelectionPickError::None;
    // identifies the malformed candidate when validation fails. a valid hit is
    // identified by its stable SelectionTarget rather than array position.
    size_t invalidCandidateIndex = kNoCandidate;

    // a valid miss has no hit and error == None.
    [[nodiscard]] bool valid() const noexcept
    {
        return error == SelectionPickError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// validates the complete candidate set before intersecting anything. therefore
// malformed or duplicated editor data can never leak a partial selection.
[[nodiscard]] SelectionPickResult pickSelection(
    const SelectionRay& ray,
    std::span<const SelectionCandidate> candidates);

[[nodiscard]] const char* selectionPickErrorLabel(
    SelectionPickError error) noexcept;

} // namespace engine::editor
