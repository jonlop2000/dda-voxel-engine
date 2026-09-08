#include "engine/editor/PlaceableAuthoringPlacement.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "engine/game/PlaceableTransform.h"

namespace engine::editor
{
namespace
{

bool finite(const glm::vec2& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float effectiveRadius(const PlaceableInstance& instance,
                      float baseRadius) noexcept
{
    return baseRadius * std::max(instance.scale.x, instance.scale.z);
}

PlaceableAuthoringPlacementResult reject(
    PlaceableAuthoringPlacementError error,
    size_t obstacleIndex = static_cast<size_t>(-1),
    std::string obstacleUuid = {})
{
    PlaceableAuthoringPlacementResult result{};
    result.error = error;
    result.obstacleIndex = obstacleIndex;
    result.obstacleUuid = std::move(obstacleUuid);
    return result;
}

} // namespace

PlaceableAuthoringPlacementResult validatePlaceableAuthoringPlacement(
    const PlaceableInstance& candidate, float candidateBaseRadius,
    const PlaceableAuthoringSurface& surface,
    std::span<const PlaceableAuthoringObstacle> obstacles)
{
    if (!finite(surface.minXZ) || !finite(surface.maxXZ) ||
        surface.minXZ.x > surface.maxXZ.x ||
        surface.minXZ.y > surface.maxXZ.y)
    {
        return reject(PlaceableAuthoringPlacementError::InvalidSurface);
    }
    if (candidate.uuid.empty() || candidate.prototypeSlug.empty() ||
        candidate.prototypeVersion == 0 ||
        !engine::game::validatePlaceableTransform(candidate))
    {
        return reject(PlaceableAuthoringPlacementError::InvalidCandidate);
    }
    if (!std::isfinite(candidateBaseRadius) || candidateBaseRadius < 0.0f)
    {
        return reject(
            PlaceableAuthoringPlacementError::InvalidCandidateRadius);
    }

    const float candidateRadius =
        effectiveRadius(candidate, candidateBaseRadius);
    if (!std::isfinite(candidateRadius))
    {
        return reject(
            PlaceableAuthoringPlacementError::InvalidCandidateRadius);
    }
    const glm::vec2 center{candidate.position.x, candidate.position.z};
    if (center.x < surface.minXZ.x + candidateRadius ||
        center.x > surface.maxXZ.x - candidateRadius ||
        center.y < surface.minXZ.y + candidateRadius ||
        center.y > surface.maxXZ.y - candidateRadius)
    {
        return reject(PlaceableAuthoringPlacementError::OutsideSurface);
    }

    for (size_t index = 0; index < obstacles.size(); ++index)
    {
        const PlaceableAuthoringObstacle& obstacle = obstacles[index];
        if (obstacle.instance.uuid.empty() ||
            obstacle.instance.prototypeSlug.empty() ||
            obstacle.instance.prototypeVersion == 0 ||
            !engine::game::validatePlaceableTransform(obstacle.instance) ||
            !std::isfinite(obstacle.baseRadius) || obstacle.baseRadius < 0.0f)
        {
            return reject(PlaceableAuthoringPlacementError::InvalidObstacle,
                          index, obstacle.instance.uuid);
        }
        if (obstacle.instance.uuid == candidate.uuid)
        {
            return reject(PlaceableAuthoringPlacementError::DuplicateIdentity,
                          index, obstacle.instance.uuid);
        }

        const float obstacleRadius =
            effectiveRadius(obstacle.instance, obstacle.baseRadius);
        if (!std::isfinite(obstacleRadius))
        {
            return reject(PlaceableAuthoringPlacementError::InvalidObstacle,
                          index, obstacle.instance.uuid);
        }
        const float minDistance = candidateRadius + obstacleRadius;
        const float dx = center.x - obstacle.instance.position.x;
        const float dz = center.y - obstacle.instance.position.z;
        if (dx * dx + dz * dz < minDistance * minDistance)
        {
            return reject(PlaceableAuthoringPlacementError::FootprintOccupied,
                          index, obstacle.instance.uuid);
        }
    }
    return {};
}

const char* placeableAuthoringPlacementErrorLabel(
    PlaceableAuthoringPlacementError error) noexcept
{
    switch (error)
    {
    case PlaceableAuthoringPlacementError::None:
        return "none";
    case PlaceableAuthoringPlacementError::InvalidSurface:
        return "invalid authoring surface";
    case PlaceableAuthoringPlacementError::InvalidCandidate:
        return "invalid placeable candidate";
    case PlaceableAuthoringPlacementError::InvalidCandidateRadius:
        return "invalid candidate footprint";
    case PlaceableAuthoringPlacementError::InvalidObstacle:
        return "invalid existing footprint";
    case PlaceableAuthoringPlacementError::DuplicateIdentity:
        return "candidate identity already exists";
    case PlaceableAuthoringPlacementError::OutsideSurface:
        return "footprint outside authoring surface";
    case PlaceableAuthoringPlacementError::FootprintOccupied:
        return "footprint overlaps another placeable";
    }
    return "unknown placeable authoring placement error";
}

} // namespace engine::editor
