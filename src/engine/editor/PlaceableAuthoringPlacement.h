#pragma once

#include <cstddef>
#include <span>
#include <string>

#include <glm/vec2.hpp>

#include "engine/game/Placeables.h"

namespace engine::editor
{

struct PlaceableAuthoringSurface
{
    glm::vec2 minXZ{0.0f};
    glm::vec2 maxXZ{0.0f};
};

struct PlaceableAuthoringObstacle
{
    PlaceableInstance instance{};
    float baseRadius = 0.0f;
};

enum class PlaceableAuthoringPlacementError
{
    None,
    InvalidSurface,
    InvalidCandidate,
    InvalidCandidateRadius,
    InvalidObstacle,
    DuplicateIdentity,
    OutsideSurface,
    FootprintOccupied,
};

struct PlaceableAuthoringPlacementResult
{
    PlaceableAuthoringPlacementError error =
        PlaceableAuthoringPlacementError::None;
    size_t obstacleIndex = static_cast<size_t>(-1);
    std::string obstacleUuid{};

    [[nodiscard]] bool valid() const noexcept
    {
        return error == PlaceableAuthoringPlacementError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// validates the final snapped candidate footprint. radii are prototype-space
// values and are expanded by the largest authored X/Z scale component. exact
// touching is accepted; any true overlap is rejected.
[[nodiscard]] PlaceableAuthoringPlacementResult
validatePlaceableAuthoringPlacement(
    const PlaceableInstance& candidate, float candidateBaseRadius,
    const PlaceableAuthoringSurface& surface,
    std::span<const PlaceableAuthoringObstacle> obstacles);

[[nodiscard]] const char* placeableAuthoringPlacementErrorLabel(
    PlaceableAuthoringPlacementError error) noexcept;

} // namespace engine::editor
