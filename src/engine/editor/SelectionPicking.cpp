#include "engine/editor/SelectionPicking.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace engine::editor
{
namespace
{

using Matrix3 = std::array<std::array<double, 3>, 3>;

struct PreparedCandidate
{
    const SelectionCandidate* source = nullptr;
    Matrix3 localFromWorld{};
    std::array<double, 3> worldTranslation{};
};

struct TargetLess
{
    bool operator()(const SelectionTarget& lhs,
                    const SelectionTarget& rhs) const noexcept
    {
        return selectionTargetLess(lhs, rhs);
    }
};

bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finiteVec2(const glm::vec2& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finiteMatrix(const glm::mat4& value) noexcept
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(value[column][row]))
            {
                return false;
            }
        }
    }
    return true;
}

bool nonsingularMatrix(const glm::mat4& value) noexcept
{
    std::array<std::array<double, 4>, 4> rows{};
    for (size_t row = 0; row < 4; ++row)
    {
        for (size_t column = 0; column < 4; ++column)
        {
            rows[row][column] = value[column][row];
        }
    }

    for (size_t column = 0; column < 4; ++column)
    {
        size_t pivot = column;
        for (size_t row = column + 1; row < 4; ++row)
        {
            if (std::abs(rows[row][column]) >
                std::abs(rows[pivot][column]))
            {
                pivot = row;
            }
        }
        if (rows[pivot][column] == 0.0)
        {
            return false;
        }
        if (pivot != column)
        {
            std::swap(rows[pivot], rows[column]);
        }

        for (size_t row = column + 1; row < 4; ++row)
        {
            const double factor = rows[row][column] / rows[column][column];
            if (!std::isfinite(factor))
            {
                return false;
            }
            for (size_t remaining = column; remaining < 4; ++remaining)
            {
                rows[row][remaining] -= factor * rows[column][remaining];
                if (!std::isfinite(rows[row][remaining]))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool unproject(const glm::mat4& inverseViewProjection,
               const std::array<double, 4>& clip,
               std::array<double, 3>& world) noexcept
{
    std::array<double, 4> homogeneous{};
    for (size_t row = 0; row < 4; ++row)
    {
        for (size_t column = 0; column < 4; ++column)
        {
            homogeneous[row] +=
                static_cast<double>(inverseViewProjection[column][row]) *
                clip[column];
        }
        if (!std::isfinite(homogeneous[row]))
        {
            return false;
        }
    }
    if (homogeneous[3] == 0.0)
    {
        return false;
    }

    constexpr double maxFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    for (size_t axis = 0; axis < 3; ++axis)
    {
        world[axis] = homogeneous[axis] / homogeneous[3];
        if (!std::isfinite(world[axis]) || std::abs(world[axis]) > maxFloat)
        {
            return false;
        }
    }
    return true;
}

SelectionPickResult errorResult(SelectionPickError error,
                                size_t candidateIndex) noexcept
{
    SelectionPickResult result{};
    result.error = error;
    result.invalidCandidateIndex = candidateIndex;
    return result;
}

double determinant(const Matrix3& value) noexcept
{
    return value[0][0] *
               (value[1][1] * value[2][2] -
                value[1][2] * value[2][1]) -
           value[0][1] *
               (value[1][0] * value[2][2] -
                value[1][2] * value[2][0]) +
           value[0][2] *
               (value[1][0] * value[2][1] -
                value[1][1] * value[2][0]);
}

bool inverse(const Matrix3& value, Matrix3& result) noexcept
{
    const double det = determinant(value);
    if (!std::isfinite(det) || det == 0.0)
    {
        return false;
    }

    const double reciprocal = 1.0 / det;
    if (!std::isfinite(reciprocal))
    {
        return false;
    }

    result[0][0] =
        (value[1][1] * value[2][2] - value[1][2] * value[2][1]) *
        reciprocal;
    result[0][1] =
        (value[0][2] * value[2][1] - value[0][1] * value[2][2]) *
        reciprocal;
    result[0][2] =
        (value[0][1] * value[1][2] - value[0][2] * value[1][1]) *
        reciprocal;
    result[1][0] =
        (value[1][2] * value[2][0] - value[1][0] * value[2][2]) *
        reciprocal;
    result[1][1] =
        (value[0][0] * value[2][2] - value[0][2] * value[2][0]) *
        reciprocal;
    result[1][2] =
        (value[0][2] * value[1][0] - value[0][0] * value[1][2]) *
        reciprocal;
    result[2][0] =
        (value[1][0] * value[2][1] - value[1][1] * value[2][0]) *
        reciprocal;
    result[2][1] =
        (value[0][1] * value[2][0] - value[0][0] * value[2][1]) *
        reciprocal;
    result[2][2] =
        (value[0][0] * value[1][1] - value[0][1] * value[1][0]) *
        reciprocal;

    constexpr double maxFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    for (const auto& row : result)
    {
        for (const double component : row)
        {
            if (!std::isfinite(component) || std::abs(component) > maxFloat)
            {
                return false;
            }
        }
    }
    return true;
}

std::array<double, 3> multiply(const Matrix3& matrix,
                               const std::array<double, 3>& vector) noexcept
{
    return {
        matrix[0][0] * vector[0] + matrix[0][1] * vector[1] +
            matrix[0][2] * vector[2],
        matrix[1][0] * vector[0] + matrix[1][1] * vector[1] +
            matrix[1][2] * vector[2],
        matrix[2][0] * vector[0] + matrix[2][1] * vector[1] +
            matrix[2][2] * vector[2],
    };
}

std::array<double, 3> multiplyTranspose(
    const Matrix3& matrix, const std::array<double, 3>& vector) noexcept
{
    return {
        matrix[0][0] * vector[0] + matrix[1][0] * vector[1] +
            matrix[2][0] * vector[2],
        matrix[0][1] * vector[0] + matrix[1][1] * vector[1] +
            matrix[2][1] * vector[2],
        matrix[0][2] * vector[0] + matrix[1][2] * vector[1] +
            matrix[2][2] * vector[2],
    };
}

SelectionPickError prepareCandidate(const SelectionCandidate& candidate,
                                    PreparedCandidate& prepared) noexcept
{
    if (!candidate.target.valid())
    {
        return SelectionPickError::InvalidTarget;
    }
    if (!finiteVec3(candidate.localBounds.localMin) ||
        !finiteVec3(candidate.localBounds.localMax) ||
        candidate.localBounds.localMin.x > candidate.localBounds.localMax.x ||
        candidate.localBounds.localMin.y > candidate.localBounds.localMax.y ||
        candidate.localBounds.localMin.z > candidate.localBounds.localMax.z)
    {
        return SelectionPickError::InvalidBounds;
    }
    if (!finiteMatrix(candidate.worldFromLocal))
    {
        return SelectionPickError::NonFiniteTransform;
    }

    // only affine transforms are accepted. exact comparison is intentional:
    // silently approximating a perspective matrix as affine would produce a
    // plausible but incorrect editor selection.
    if (candidate.worldFromLocal[0][3] != 0.0f ||
        candidate.worldFromLocal[1][3] != 0.0f ||
        candidate.worldFromLocal[2][3] != 0.0f ||
        candidate.worldFromLocal[3][3] != 1.0f)
    {
        return SelectionPickError::NonAffineTransform;
    }

    Matrix3 worldFromLocal{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            worldFromLocal[row][column] = static_cast<double>(
                candidate.worldFromLocal[column][row]);
        }
        prepared.worldTranslation[static_cast<size_t>(row)] =
            static_cast<double>(candidate.worldFromLocal[3][row]);
    }
    if (!inverse(worldFromLocal, prepared.localFromWorld))
    {
        return SelectionPickError::NonInvertibleTransform;
    }

    constexpr double maxFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const std::array<double, 3> local = {
            static_cast<double>((corner & 1u) != 0u
                                    ? candidate.localBounds.localMax.x
                                    : candidate.localBounds.localMin.x),
            static_cast<double>((corner & 2u) != 0u
                                    ? candidate.localBounds.localMax.y
                                    : candidate.localBounds.localMin.y),
            static_cast<double>((corner & 4u) != 0u
                                    ? candidate.localBounds.localMax.z
                                    : candidate.localBounds.localMin.z),
        };
        std::array<double, 3> world = multiply(worldFromLocal, local);
        for (size_t axis = 0; axis < 3; ++axis)
        {
            world[axis] += prepared.worldTranslation[axis];
            if (!std::isfinite(world[axis]) ||
                std::abs(world[axis]) > maxFloat)
            {
                return SelectionPickError::NonFiniteWorldBounds;
            }
        }
    }

    prepared.source = &candidate;
    return SelectionPickError::None;
}

struct BoundsHit
{
    bool hit = false;
    double distance = 0.0;
    std::array<double, 3> localNormal{};
};

BoundsHit intersectBounds(const SelectionRay& ray,
                          const std::array<double, 3>& worldDirection,
                          const PreparedCandidate& prepared) noexcept
{
    const SelectionCandidate& candidate = *prepared.source;
    const std::array<double, 3> worldOffset = {
        static_cast<double>(ray.origin.x) - prepared.worldTranslation[0],
        static_cast<double>(ray.origin.y) - prepared.worldTranslation[1],
        static_cast<double>(ray.origin.z) - prepared.worldTranslation[2],
    };
    const std::array<double, 3> localOrigin =
        multiply(prepared.localFromWorld, worldOffset);
    const std::array<double, 3> localDirection =
        multiply(prepared.localFromWorld, worldDirection);
    const std::array<double, 3> localMin = {
        static_cast<double>(candidate.localBounds.localMin.x),
        static_cast<double>(candidate.localBounds.localMin.y),
        static_cast<double>(candidate.localBounds.localMin.z),
    };
    const std::array<double, 3> localMax = {
        static_cast<double>(candidate.localBounds.localMax.x),
        static_cast<double>(candidate.localBounds.localMax.y),
        static_cast<double>(candidate.localBounds.localMax.z),
    };

    double entry = -std::numeric_limits<double>::infinity();
    double exit = std::numeric_limits<double>::infinity();
    int entryAxis = -1;
    double entryNormalSign = 0.0;

    for (int axis = 0; axis < 3; ++axis)
    {
        const size_t index = static_cast<size_t>(axis);
        if (localDirection[index] == 0.0)
        {
            if (localOrigin[index] < localMin[index] ||
                localOrigin[index] > localMax[index])
            {
                return {};
            }
            continue;
        }

        double nearDistance =
            (localMin[index] - localOrigin[index]) / localDirection[index];
        double farDistance =
            (localMax[index] - localOrigin[index]) / localDirection[index];
        double nearNormalSign = -1.0;
        if (nearDistance > farDistance)
        {
            std::swap(nearDistance, farDistance);
            nearNormalSign = 1.0;
        }

        // equal entry distances retain the lowest axis (x, then y, then z),
        // making edge/corner normals deterministic.
        if (nearDistance > entry)
        {
            entry = nearDistance;
            entryAxis = axis;
            entryNormalSign = nearNormalSign;
        }
        exit = std::min(exit, farDistance);
        if (entry > exit)
        {
            return {};
        }
    }

    if (exit < 0.0)
    {
        return {};
    }

    BoundsHit hit{};
    hit.distance = std::max(entry, 0.0);
    if (hit.distance > static_cast<double>(ray.maxDistance))
    {
        return {};
    }
    hit.hit = true;
    if (entry >= 0.0 && entryAxis >= 0)
    {
        hit.localNormal[static_cast<size_t>(entryAxis)] = entryNormalSign;
    }
    return hit;
}

bool hitPrecedes(double distance, const SelectionCandidate& candidate,
                 double currentDistance,
                 const SelectionHit& current) noexcept
{
    if (distance != currentDistance)
    {
        return distance < currentDistance;
    }
    if (candidate.priority != current.priority)
    {
        return candidate.priority > current.priority;
    }
    return selectionTargetLess(candidate.target, current.target);
}

} // namespace

SelectionTarget SelectionTarget::terrainCell(std::string terrainKey,
                                             const glm::ivec3& cell)
{
    return SelectionTarget{
        TerrainCellSelection{std::move(terrainKey), cell}};
}

SelectionTarget SelectionTarget::voxelVolume(uint64_t structureRevision,
                                             uint32_t index)
{
    return SelectionTarget{VoxelVolumeSelection{structureRevision, index}};
}

SelectionTarget SelectionTarget::placeable(std::string uuid)
{
    return SelectionTarget{PlaceableSelection{std::move(uuid)}};
}

SelectionTargetKind SelectionTarget::kind() const noexcept
{
    switch (value.index())
    {
    case 1:
        return SelectionTargetKind::TerrainCell;
    case 2:
        return SelectionTargetKind::VoxelVolume;
    case 3:
        return SelectionTargetKind::Placeable;
    default:
        return SelectionTargetKind::None;
    }
}

bool SelectionTarget::valid() const noexcept
{
    if (const auto* terrain = std::get_if<TerrainCellSelection>(&value))
    {
        return !terrain->terrainKey.empty();
    }
    if (const auto* volume = std::get_if<VoxelVolumeSelection>(&value))
    {
        return volume->structureRevision != 0u;
    }
    if (const auto* placeable = std::get_if<PlaceableSelection>(&value))
    {
        return !placeable->uuid.empty();
    }
    return false;
}

bool selectionTargetEqual(const SelectionTarget& lhs,
                          const SelectionTarget& rhs) noexcept
{
    if (lhs.kind() != rhs.kind())
    {
        return false;
    }
    if (const auto* left = std::get_if<TerrainCellSelection>(&lhs.value))
    {
        const auto& right = std::get<TerrainCellSelection>(rhs.value);
        return left->terrainKey == right.terrainKey &&
               left->cell.x == right.cell.x && left->cell.y == right.cell.y &&
               left->cell.z == right.cell.z;
    }
    if (const auto* left = std::get_if<VoxelVolumeSelection>(&lhs.value))
    {
        const auto& right = std::get<VoxelVolumeSelection>(rhs.value);
        return left->structureRevision == right.structureRevision &&
               left->index == right.index;
    }
    if (const auto* left = std::get_if<PlaceableSelection>(&lhs.value))
    {
        return left->uuid == std::get<PlaceableSelection>(rhs.value).uuid;
    }
    return true;
}

bool selectionTargetLess(const SelectionTarget& lhs,
                         const SelectionTarget& rhs) noexcept
{
    if (lhs.kind() != rhs.kind())
    {
        return static_cast<int>(lhs.kind()) < static_cast<int>(rhs.kind());
    }
    if (const auto* left = std::get_if<TerrainCellSelection>(&lhs.value))
    {
        const auto& right = std::get<TerrainCellSelection>(rhs.value);
        if (left->terrainKey != right.terrainKey)
        {
            return left->terrainKey < right.terrainKey;
        }
        if (left->cell.x != right.cell.x)
        {
            return left->cell.x < right.cell.x;
        }
        if (left->cell.y != right.cell.y)
        {
            return left->cell.y < right.cell.y;
        }
        return left->cell.z < right.cell.z;
    }
    if (const auto* left = std::get_if<VoxelVolumeSelection>(&lhs.value))
    {
        const auto& right = std::get<VoxelVolumeSelection>(rhs.value);
        if (left->structureRevision != right.structureRevision)
        {
            return left->structureRevision < right.structureRevision;
        }
        return left->index < right.index;
    }
    if (const auto* left = std::get_if<PlaceableSelection>(&lhs.value))
    {
        return left->uuid < std::get<PlaceableSelection>(rhs.value).uuid;
    }
    return false;
}

SelectionRayBuildResult selectionRayFromViewport(
    const glm::vec2& cursorPosition, const glm::uvec2& viewportExtent,
    const glm::mat4& inverseViewProjection, float maxDistance) noexcept
{
    SelectionRayBuildResult result{};
    const auto fail = [&](SelectionRayBuildError error) {
        result.error = error;
        return result;
    };

    if (!finiteVec2(cursorPosition))
    {
        return fail(SelectionRayBuildError::NonFiniteCursor);
    }
    if (viewportExtent.x == 0u || viewportExtent.y == 0u)
    {
        return fail(SelectionRayBuildError::InvalidViewportExtent);
    }
    if (cursorPosition.x < 0.0f || cursorPosition.y < 0.0f ||
        static_cast<double>(cursorPosition.x) >
            static_cast<double>(viewportExtent.x) ||
        static_cast<double>(cursorPosition.y) >
            static_cast<double>(viewportExtent.y))
    {
        return fail(SelectionRayBuildError::CursorOutsideViewport);
    }
    if (!std::isfinite(maxDistance) || maxDistance < 0.0f)
    {
        return fail(SelectionRayBuildError::InvalidMaxDistance);
    }
    if (!finiteMatrix(inverseViewProjection))
    {
        return fail(
            SelectionRayBuildError::NonFiniteInverseViewProjection);
    }
    if (!nonsingularMatrix(inverseViewProjection))
    {
        return fail(SelectionRayBuildError::InvalidInverseViewProjection);
    }

    const double ndcX =
        2.0 * static_cast<double>(cursorPosition.x) /
            static_cast<double>(viewportExtent.x) -
        1.0;
    // camera's vulkan projection has already flipped y. engine screen mapping
    // is (ndcY * 0.5 + 0.5) * height, so top-left cursor y maps -1 -> +1.
    const double ndcY =
        2.0 * static_cast<double>(cursorPosition.y) /
            static_cast<double>(viewportExtent.y) -
        1.0;
    const std::array<double, 4> nearClip = {ndcX, ndcY, 0.0, 1.0};
    const std::array<double, 4> farClip = {ndcX, ndcY, 1.0, 1.0};
    std::array<double, 3> nearWorld{};
    std::array<double, 3> farWorld{};
    if (!unproject(inverseViewProjection, nearClip, nearWorld) ||
        !unproject(inverseViewProjection, farClip, farWorld))
    {
        return fail(SelectionRayBuildError::InvalidUnprojection);
    }

    std::array<double, 3> direction = {
        farWorld[0] - nearWorld[0], farWorld[1] - nearWorld[1],
        farWorld[2] - nearWorld[2]};
    const double lengthSquared = direction[0] * direction[0] +
                                 direction[1] * direction[1] +
                                 direction[2] * direction[2];
    if (!std::isfinite(lengthSquared) || lengthSquared == 0.0)
    {
        return fail(SelectionRayBuildError::DegenerateRay);
    }
    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    for (double& component : direction)
    {
        component *= inverseLength;
    }

    result.ray.origin =
        glm::vec3(static_cast<float>(nearWorld[0]),
                  static_cast<float>(nearWorld[1]),
                  static_cast<float>(nearWorld[2]));
    result.ray.direction =
        glm::vec3(static_cast<float>(direction[0]),
                  static_cast<float>(direction[1]),
                  static_cast<float>(direction[2]));
    result.ray.maxDistance = maxDistance;
    return result;
}

const char* selectionRayBuildErrorLabel(
    SelectionRayBuildError error) noexcept
{
    switch (error)
    {
    case SelectionRayBuildError::None:
        return "none";
    case SelectionRayBuildError::NonFiniteCursor:
        return "non_finite_cursor";
    case SelectionRayBuildError::CursorOutsideViewport:
        return "cursor_outside_viewport";
    case SelectionRayBuildError::InvalidViewportExtent:
        return "invalid_viewport_extent";
    case SelectionRayBuildError::NonFiniteInverseViewProjection:
        return "non_finite_inverse_view_projection";
    case SelectionRayBuildError::InvalidInverseViewProjection:
        return "invalid_inverse_view_projection";
    case SelectionRayBuildError::InvalidUnprojection:
        return "invalid_unprojection";
    case SelectionRayBuildError::DegenerateRay:
        return "degenerate_ray";
    case SelectionRayBuildError::InvalidMaxDistance:
        return "invalid_max_distance";
    }
    return "unknown";
}

SelectionPickResult pickSelection(
    const SelectionRay& ray,
    std::span<const SelectionCandidate> candidates)
{
    if (!finiteVec3(ray.origin) || !finiteVec3(ray.direction))
    {
        return errorResult(SelectionPickError::NonFiniteRay,
                           SelectionPickResult::kNoCandidate);
    }
    if (!std::isfinite(ray.maxDistance) || ray.maxDistance < 0.0f)
    {
        return errorResult(SelectionPickError::InvalidMaxDistance,
                           SelectionPickResult::kNoCandidate);
    }

    const double directionLengthSquared =
        static_cast<double>(ray.direction.x) * ray.direction.x +
        static_cast<double>(ray.direction.y) * ray.direction.y +
        static_cast<double>(ray.direction.z) * ray.direction.z;
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared == 0.0)
    {
        return errorResult(SelectionPickError::DegenerateRayDirection,
                           SelectionPickResult::kNoCandidate);
    }
    const double inverseDirectionLength =
        1.0 / std::sqrt(directionLengthSquared);
    const std::array<double, 3> worldDirection = {
        static_cast<double>(ray.direction.x) * inverseDirectionLength,
        static_cast<double>(ray.direction.y) * inverseDirectionLength,
        static_cast<double>(ray.direction.z) * inverseDirectionLength,
    };

    std::vector<PreparedCandidate> prepared(candidates.size());
    std::set<SelectionTarget, TargetLess> identities{};
    for (size_t index = 0; index < candidates.size(); ++index)
    {
        const SelectionPickError error =
            prepareCandidate(candidates[index], prepared[index]);
        if (error != SelectionPickError::None)
        {
            return errorResult(error, index);
        }
        if (!identities.insert(candidates[index].target).second)
        {
            return errorResult(SelectionPickError::DuplicateTarget, index);
        }
    }

    SelectionPickResult result{};
    std::optional<double> bestDistance{};
    for (const PreparedCandidate& candidate : prepared)
    {
        const BoundsHit boundsHit =
            intersectBounds(ray, worldDirection, candidate);
        if (!boundsHit.hit ||
            (result.hit.has_value() &&
             !hitPrecedes(boundsHit.distance, *candidate.source,
                          *bestDistance, *result.hit)))
        {
            continue;
        }

        SelectionHit hit{};
        hit.target = candidate.source->target;
        hit.distance = static_cast<float>(boundsHit.distance);
        hit.worldPosition = glm::vec3(
            static_cast<float>(static_cast<double>(ray.origin.x) +
                               worldDirection[0] * boundsHit.distance),
            static_cast<float>(static_cast<double>(ray.origin.y) +
                               worldDirection[1] * boundsHit.distance),
            static_cast<float>(static_cast<double>(ray.origin.z) +
                               worldDirection[2] * boundsHit.distance));
        hit.priority = candidate.source->priority;

        if (boundsHit.localNormal[0] != 0.0 ||
            boundsHit.localNormal[1] != 0.0 ||
            boundsHit.localNormal[2] != 0.0)
        {
            std::array<double, 3> normal = multiplyTranspose(
                candidate.localFromWorld, boundsHit.localNormal);
            const double length = std::sqrt(normal[0] * normal[0] +
                                            normal[1] * normal[1] +
                                            normal[2] * normal[2]);
            if (std::isfinite(length) && length > 0.0)
            {
                normal[0] /= length;
                normal[1] /= length;
                normal[2] /= length;
                hit.worldNormal = glm::vec3(static_cast<float>(normal[0]),
                                            static_cast<float>(normal[1]),
                                            static_cast<float>(normal[2]));
            }
        }
        result.hit = std::move(hit);
        bestDistance = boundsHit.distance;
    }
    return result;
}

const char* selectionPickErrorLabel(SelectionPickError error) noexcept
{
    switch (error)
    {
    case SelectionPickError::None:
        return "none";
    case SelectionPickError::NonFiniteRay:
        return "non_finite_ray";
    case SelectionPickError::DegenerateRayDirection:
        return "degenerate_ray_direction";
    case SelectionPickError::InvalidMaxDistance:
        return "invalid_max_distance";
    case SelectionPickError::InvalidTarget:
        return "invalid_target";
    case SelectionPickError::DuplicateTarget:
        return "duplicate_target";
    case SelectionPickError::InvalidBounds:
        return "invalid_bounds";
    case SelectionPickError::NonFiniteTransform:
        return "non_finite_transform";
    case SelectionPickError::NonAffineTransform:
        return "non_affine_transform";
    case SelectionPickError::NonInvertibleTransform:
        return "non_invertible_transform";
    case SelectionPickError::NonFiniteWorldBounds:
        return "non_finite_world_bounds";
    }
    return "unknown";
}

} // namespace engine::editor
