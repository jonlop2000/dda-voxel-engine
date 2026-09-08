#pragma once

#include "engine/game/Placeables.h"

namespace engine::game
{

enum class PlaceableTransformError
{
    None,
    NonFinitePosition,
    NonFiniteRotation,
    DegenerateRotation,
    NonFiniteScale,
    NonPositiveScale,
    NonInvertibleScale,
};

struct PlaceableTransformValidation
{
    PlaceableInstance canonical{};
    PlaceableTransformError error = PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == PlaceableTransformError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

struct PlaceableRotationComposition
{
    glm::quat canonical{1.0f, 0.0f, 0.0f, 0.0f};
    PlaceableTransformError error = PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == PlaceableTransformError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// validates the authored transform without changing identity/prototype data.
// valid positive nonuniform scale is preserved. the returned quaternion is unit
// length and has a deterministic sign, so q and -q share one canonical form.
[[nodiscard]] PlaceableTransformValidation validatePlaceableTransform(
    const PlaceableInstance& instance) noexcept;

// compares only position, rotation, and scale. quaternion sign is ignored through
// canonicalization; invalid transforms are never equivalent.
[[nodiscard]] bool placeableTransformEquivalent(
    const PlaceableInstance& lhs, const PlaceableInstance& rhs,
    float epsilon = 1e-5f) noexcept;

// locks the authored/runtime composition contract to this order. each input and
// the product are normalized and sign-canonicalized; invalid input fails closed.
[[nodiscard]] PlaceableRotationComposition composePlaceableRotation(
    const glm::quat& authored, const glm::quat& animationOffset,
    const glm::quat& seededVariation) noexcept;

[[nodiscard]] const char* placeableTransformErrorLabel(
    PlaceableTransformError error) noexcept;

} // namespace engine::game
