#include "engine/game/PlaceableTransform.h"

#include <cmath>

namespace engine::game
{
namespace
{

constexpr float kMinimumQuaternionLengthSquared = 1e-12f;

bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finiteQuat(const glm::quat& value) noexcept
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

float positiveZero(float value) noexcept
{
    return value == 0.0f ? 0.0f : value;
}

bool shouldFlipCanonicalQuaternion(const glm::quat& value) noexcept
{
    if (value.w != 0.0f)
    {
        return value.w < 0.0f;
    }
    if (value.x != 0.0f)
    {
        return value.x < 0.0f;
    }
    if (value.y != 0.0f)
    {
        return value.y < 0.0f;
    }
    return value.z < 0.0f;
}

PlaceableTransformError canonicalizeQuaternion(const glm::quat& input,
                                                glm::quat& output) noexcept
{
    if (!finiteQuat(input))
    {
        return PlaceableTransformError::NonFiniteRotation;
    }

    const float lengthSquared = input.w * input.w + input.x * input.x +
                                input.y * input.y + input.z * input.z;
    if (!std::isfinite(lengthSquared))
    {
        return PlaceableTransformError::NonFiniteRotation;
    }
    if (lengthSquared <= kMinimumQuaternionLengthSquared)
    {
        return PlaceableTransformError::DegenerateRotation;
    }

    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    output = glm::quat(input.w * inverseLength, input.x * inverseLength,
                       input.y * inverseLength, input.z * inverseLength);
    if (shouldFlipCanonicalQuaternion(output))
    {
        output = glm::quat(-output.w, -output.x, -output.y, -output.z);
    }
    output.w = positiveZero(output.w);
    output.x = positiveZero(output.x);
    output.y = positiveZero(output.y);
    output.z = positiveZero(output.z);
    return PlaceableTransformError::None;
}

bool nearlyEqual(float lhs, float rhs, float epsilon) noexcept
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool nearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs,
                 float epsilon) noexcept
{
    return nearlyEqual(lhs.x, rhs.x, epsilon) &&
           nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

bool nearlyEqual(const glm::quat& lhs, const glm::quat& rhs,
                 float epsilon) noexcept
{
    return nearlyEqual(lhs.w, rhs.w, epsilon) &&
           nearlyEqual(lhs.x, rhs.x, epsilon) &&
           nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

} // namespace

PlaceableTransformValidation validatePlaceableTransform(
    const PlaceableInstance& instance) noexcept
{
    PlaceableTransformValidation result{};
    result.canonical = instance;

    if (!finiteVec3(instance.position))
    {
        result.error = PlaceableTransformError::NonFinitePosition;
        return result;
    }
    glm::quat rotation{};
    result.error = canonicalizeQuaternion(instance.rotation, rotation);
    if (!result.valid())
    {
        return result;
    }
    if (!finiteVec3(instance.scale))
    {
        result.error = PlaceableTransformError::NonFiniteScale;
        return result;
    }
    if (instance.scale.x <= 0.0f || instance.scale.y <= 0.0f ||
        instance.scale.z <= 0.0f)
    {
        result.error = PlaceableTransformError::NonPositiveScale;
        return result;
    }
    if (!std::isfinite(1.0f / instance.scale.x) ||
        !std::isfinite(1.0f / instance.scale.y) ||
        !std::isfinite(1.0f / instance.scale.z))
    {
        result.error = PlaceableTransformError::NonInvertibleScale;
        return result;
    }

    result.canonical.position = glm::vec3(positiveZero(instance.position.x),
                                          positiveZero(instance.position.y),
                                          positiveZero(instance.position.z));
    result.canonical.rotation = rotation;
    result.canonical.scale = glm::vec3(positiveZero(instance.scale.x),
                                       positiveZero(instance.scale.y),
                                       positiveZero(instance.scale.z));
    result.error = PlaceableTransformError::None;
    return result;
}

bool placeableTransformEquivalent(const PlaceableInstance& lhs,
                                  const PlaceableInstance& rhs,
                                  float epsilon) noexcept
{
    if (!std::isfinite(epsilon) || epsilon < 0.0f)
    {
        return false;
    }

    const PlaceableTransformValidation left = validatePlaceableTransform(lhs);
    const PlaceableTransformValidation right = validatePlaceableTransform(rhs);
    if (!left || !right)
    {
        return false;
    }

    return nearlyEqual(left.canonical.position, right.canonical.position, epsilon) &&
           nearlyEqual(left.canonical.rotation, right.canonical.rotation, epsilon) &&
           nearlyEqual(left.canonical.scale, right.canonical.scale, epsilon);
}

PlaceableRotationComposition composePlaceableRotation(
    const glm::quat& authored, const glm::quat& animationOffset,
    const glm::quat& seededVariation) noexcept
{
    PlaceableRotationComposition result{};
    glm::quat canonicalAuthored{};
    glm::quat canonicalAnimation{};
    glm::quat canonicalVariation{};

    result.error = canonicalizeQuaternion(authored, canonicalAuthored);
    if (!result.valid())
    {
        return result;
    }
    result.error = canonicalizeQuaternion(animationOffset, canonicalAnimation);
    if (!result.valid())
    {
        return result;
    }
    result.error = canonicalizeQuaternion(seededVariation, canonicalVariation);
    if (!result.valid())
    {
        return result;
    }

    const glm::quat composed =
        canonicalAnimation * canonicalAuthored * canonicalVariation;
    result.error = canonicalizeQuaternion(composed, result.canonical);
    return result;
}

const char* placeableTransformErrorLabel(PlaceableTransformError error) noexcept
{
    switch (error)
    {
    case PlaceableTransformError::None:
        return "none";
    case PlaceableTransformError::NonFinitePosition:
        return "non-finite position";
    case PlaceableTransformError::NonFiniteRotation:
        return "non-finite rotation";
    case PlaceableTransformError::DegenerateRotation:
        return "degenerate rotation";
    case PlaceableTransformError::NonFiniteScale:
        return "non-finite scale";
    case PlaceableTransformError::NonPositiveScale:
        return "non-positive scale";
    case PlaceableTransformError::NonInvertibleScale:
        return "non-invertible scale";
    }
    return "unknown transform error";
}

} // namespace engine::game
