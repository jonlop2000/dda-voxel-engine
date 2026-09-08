#include "engine/editor/TransformSnapping.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace engine::editor
{
namespace
{

constexpr float kTerrainTranslationStep = 1.0f;
constexpr float kPlaceableTranslationStep = 0.25f;
constexpr float kVoxelYawStepDegrees = 90.0f;
constexpr float kPlaceableYawStepDegrees = 15.0f;
constexpr float kOptionalScaleStep = 0.1f;

bool finitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0f;
}

bool validTranslationAxis(bool enabled, float step) noexcept
{
    return !enabled || finitePositive(step);
}

bool validProfile(const TransformSnapProfile& profile) noexcept
{
    if (!validTranslationAxis(profile.snapTranslationX,
                              profile.translationStep.x) ||
        !validTranslationAxis(profile.snapTranslationY,
                              profile.translationStep.y) ||
        !validTranslationAxis(profile.snapTranslationZ,
                              profile.translationStep.z) ||
        (profile.surfaceY && !std::isfinite(*profile.surfaceY)) ||
        (!profile.forceIdentityRotation && profile.yawStepDegrees != 0.0f &&
         !finitePositive(profile.yawStepDegrees)) ||
        (!profile.forceIdentityScale && profile.scaleStep != 0.0f &&
         !finitePositive(profile.scaleStep)))
    {
        return false;
    }

    switch (profile.target)
    {
    case TransformSnapTarget::TerrainCell:
        return profile.snapTranslationX && profile.snapTranslationY &&
               profile.snapTranslationZ &&
               profile.translationStep == glm::vec3(kTerrainTranslationStep) &&
               !profile.surfaceY && profile.yawStepDegrees == 0.0f &&
               profile.forceIdentityRotation && profile.scaleStep == 0.0f &&
               profile.forceIdentityScale;
    case TransformSnapTarget::VoxelVolume:
        return profile.snapTranslationX && profile.snapTranslationY &&
               profile.snapTranslationZ && !profile.surfaceY &&
               profile.yawStepDegrees == kVoxelYawStepDegrees &&
               !profile.forceIdentityRotation &&
               (profile.scaleStep == 0.0f ||
                profile.scaleStep == kOptionalScaleStep) &&
               !profile.forceIdentityScale;
    case TransformSnapTarget::Placeable:
        return profile.snapTranslationX && !profile.snapTranslationY &&
               profile.snapTranslationZ &&
               profile.translationStep.x == kPlaceableTranslationStep &&
               profile.translationStep.z == kPlaceableTranslationStep &&
               profile.yawStepDegrees == kPlaceableYawStepDegrees &&
               !profile.forceIdentityRotation &&
               (profile.scaleStep == 0.0f ||
                profile.scaleStep == kOptionalScaleStep) &&
               !profile.forceIdentityScale;
    case TransformSnapTarget::Freeform:
        return !profile.snapTranslationX && !profile.snapTranslationY &&
               !profile.snapTranslationZ && !profile.surfaceY &&
               profile.yawStepDegrees == 0.0f &&
               !profile.forceIdentityRotation && profile.scaleStep == 0.0f &&
               !profile.forceIdentityScale;
    }
    return false;
}

float positiveZero(float value) noexcept
{
    return value == 0.0f ? 0.0f : value;
}

float snapScalar(float value, float step) noexcept
{
    return positiveZero(std::round(value / step) * step);
}

float snappedYawRadians(const glm::quat& rotation,
                        float stepDegrees) noexcept
{
    const float numerator =
        2.0f * (rotation.w * rotation.y + rotation.x * rotation.z);
    const float denominator =
        1.0f - 2.0f * (rotation.x * rotation.x +
                       rotation.y * rotation.y);
    const float yaw = std::atan2(numerator, denominator);
    const float step = glm::radians(stepDegrees);
    return snapScalar(yaw, step);
}

TransformState transformStateFromPlaceable(
    const PlaceableInstance& instance) noexcept
{
    return {instance.position, instance.rotation, instance.scale};
}

PlaceableInstance placeableFromTransformState(const TransformState& state) noexcept
{
    PlaceableInstance instance{};
    instance.position = state.translation;
    instance.rotation = state.rotation;
    instance.scale = state.scale;
    return instance;
}

} // namespace

TransformSnapProfile terrainCellSnapProfile() noexcept
{
    TransformSnapProfile profile{};
    profile.target = TransformSnapTarget::TerrainCell;
    profile.translationStep = glm::vec3(kTerrainTranslationStep);
    profile.snapTranslationX = true;
    profile.snapTranslationY = true;
    profile.snapTranslationZ = true;
    profile.forceIdentityRotation = true;
    profile.forceIdentityScale = true;
    return profile;
}

TransformSnapProfile voxelVolumeSnapProfile(const glm::vec3& cellScale,
                                            bool snapScale) noexcept
{
    TransformSnapProfile profile{};
    profile.target = TransformSnapTarget::VoxelVolume;
    profile.translationStep = cellScale;
    profile.snapTranslationX = true;
    profile.snapTranslationY = true;
    profile.snapTranslationZ = true;
    profile.yawStepDegrees = kVoxelYawStepDegrees;
    profile.scaleStep = snapScale ? kOptionalScaleStep : 0.0f;
    return profile;
}

TransformSnapProfile placeableSnapProfile(std::optional<float> surfaceY,
                                          bool snapScale) noexcept
{
    TransformSnapProfile profile{};
    profile.target = TransformSnapTarget::Placeable;
    profile.translationStep = glm::vec3(kPlaceableTranslationStep, 0.0f,
                                        kPlaceableTranslationStep);
    profile.snapTranslationX = true;
    profile.snapTranslationZ = true;
    profile.surfaceY = surfaceY;
    profile.yawStepDegrees = kPlaceableYawStepDegrees;
    profile.scaleStep = snapScale ? kOptionalScaleStep : 0.0f;
    return profile;
}

TransformSnapProfile freeformSnapProfile() noexcept
{
    TransformSnapProfile profile{};
    profile.target = TransformSnapTarget::Freeform;
    return profile;
}

TransformSnapResult snapTransform(const TransformState& input,
                                  const TransformSnapProfile& profile,
                                  TransformChannel channels) noexcept
{
    TransformSnapResult result{};
    result.value = input;
    if (!validProfile(profile))
    {
        result.error = TransformSnapError::InvalidProfile;
        return result;
    }

    const engine::game::PlaceableTransformValidation inputValidation =
        engine::game::validatePlaceableTransform(
            placeableFromTransformState(input));
    if (!inputValidation)
    {
        result.error = TransformSnapError::InvalidTransform;
        result.transformError = inputValidation.error;
        return result;
    }

    result.value = transformStateFromPlaceable(inputValidation.canonical);
    if (includesTransformChannel(channels, TransformChannel::Translation))
    {
        if (profile.snapTranslationX)
        {
            result.value.translation.x =
                snapScalar(result.value.translation.x,
                           profile.translationStep.x);
        }
        if (profile.snapTranslationY)
        {
            result.value.translation.y =
                snapScalar(result.value.translation.y,
                           profile.translationStep.y);
        }
        else if (profile.surfaceY)
        {
            result.value.translation.y = positiveZero(*profile.surfaceY);
        }
        if (profile.snapTranslationZ)
        {
            result.value.translation.z =
                snapScalar(result.value.translation.z,
                           profile.translationStep.z);
        }
    }

    if (includesTransformChannel(channels, TransformChannel::Rotation))
    {
        if (profile.forceIdentityRotation)
        {
            result.value.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        else if (profile.yawStepDegrees > 0.0f)
        {
            result.value.rotation = glm::angleAxis(
                snappedYawRadians(result.value.rotation,
                                   profile.yawStepDegrees),
                glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    if (includesTransformChannel(channels, TransformChannel::Scale))
    {
        if (profile.forceIdentityScale)
        {
            result.value.scale = glm::vec3(1.0f);
        }
        else if (profile.scaleStep > 0.0f)
        {
            const float representativeScale = static_cast<float>(
                (static_cast<double>(result.value.scale.x) +
                 static_cast<double>(result.value.scale.y) +
                 static_cast<double>(result.value.scale.z)) /
                3.0);
            const float uniformScale =
                std::max(profile.scaleStep,
                         snapScalar(representativeScale, profile.scaleStep));
            result.value.scale = glm::vec3(uniformScale);
        }
    }

    const engine::game::PlaceableTransformValidation outputValidation =
        engine::game::validatePlaceableTransform(
            placeableFromTransformState(result.value));
    if (!outputValidation)
    {
        result.error = TransformSnapError::InvalidTransform;
        result.transformError = outputValidation.error;
        return result;
    }
    result.value = transformStateFromPlaceable(outputValidation.canonical);
    return result;
}

PlaceableSnapResult snapPlaceableInstance(
    const PlaceableInstance& input, const TransformSnapProfile& profile,
    TransformChannel channels)
{
    PlaceableSnapResult result{};
    result.canonical = input;

    const TransformSnapResult snapped =
        snapTransform(transformStateFromPlaceable(input), profile, channels);
    if (!snapped)
    {
        result.error = snapped.error;
        result.transformError = snapped.transformError;
        return result;
    }

    result.canonical.position = snapped.value.translation;
    result.canonical.rotation = snapped.value.rotation;
    result.canonical.scale = snapped.value.scale;
    const engine::game::PlaceableTransformValidation validation =
        engine::game::validatePlaceableTransform(result.canonical);
    if (!validation)
    {
        result.error = TransformSnapError::InvalidTransform;
        result.transformError = validation.error;
        return result;
    }
    result.canonical = validation.canonical;
    return result;
}

const char* transformSnapErrorLabel(TransformSnapError error) noexcept
{
    switch (error)
    {
    case TransformSnapError::None:
        return "none";
    case TransformSnapError::InvalidProfile:
        return "invalid snap profile";
    case TransformSnapError::InvalidTransform:
        return "invalid transform";
    }
    return "unknown snap error";
}

} // namespace engine::editor
