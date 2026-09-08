#pragma once

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/game/PlaceableTransform.h"

namespace engine::editor
{

struct TransformState
{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

enum class TransformSnapTarget
{
    TerrainCell,
    VoxelVolume,
    Placeable,
    Freeform,
};

enum class TransformChannel : uint8_t
{
    None = 0,
    Translation = 1u << 0u,
    Rotation = 1u << 1u,
    Scale = 1u << 2u,
    All = (1u << 0u) | (1u << 1u) | (1u << 2u),
};

constexpr TransformChannel operator|(TransformChannel lhs,
                                     TransformChannel rhs) noexcept
{
    return static_cast<TransformChannel>(static_cast<uint8_t>(lhs) |
                                         static_cast<uint8_t>(rhs));
}

constexpr TransformChannel operator&(TransformChannel lhs,
                                     TransformChannel rhs) noexcept
{
    return static_cast<TransformChannel>(static_cast<uint8_t>(lhs) &
                                         static_cast<uint8_t>(rhs));
}

constexpr bool includesTransformChannel(TransformChannel channels,
                                        TransformChannel channel) noexcept
{
    return (channels & channel) != TransformChannel::None;
}

// a resolved, typed snapping policy. call the factory functions below instead
// of assembling profiles manually so editor tools share one authoring contract.
struct TransformSnapProfile
{
    TransformSnapTarget target = TransformSnapTarget::Freeform;
    glm::vec3 translationStep{0.0f};
    bool snapTranslationX = false;
    bool snapTranslationY = false;
    bool snapTranslationZ = false;
    std::optional<float> surfaceY{};
    float yawStepDegrees = 0.0f;
    bool forceIdentityRotation = false;
    float scaleStep = 0.0f;
    bool forceIdentityScale = false;
};

[[nodiscard]] TransformSnapProfile terrainCellSnapProfile() noexcept;
[[nodiscard]] TransformSnapProfile voxelVolumeSnapProfile(
    const glm::vec3& cellScale, bool snapScale = false) noexcept;
[[nodiscard]] TransformSnapProfile placeableSnapProfile(
    std::optional<float> surfaceY = std::nullopt,
    bool snapScale = false) noexcept;
[[nodiscard]] TransformSnapProfile freeformSnapProfile() noexcept;

enum class TransformSnapError
{
    None,
    InvalidProfile,
    InvalidTransform,
};

struct TransformSnapResult
{
    TransformState value{};
    TransformSnapError error = TransformSnapError::None;
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == TransformSnapError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// canonicalizes and validates the transform through PlaceableTransform, then
// applies only the requested snapping channels. unrequested channel values are
// preserved in canonical form.
[[nodiscard]] TransformSnapResult snapTransform(
    const TransformState& input, const TransformSnapProfile& profile,
    TransformChannel channels) noexcept;

struct PlaceableSnapResult
{
    PlaceableInstance canonical{};
    TransformSnapError error = TransformSnapError::None;
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == TransformSnapError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// convenience adapter that keeps all placeable identity/prototype fields and
// uses the same canonical transform validation as scene documents.
[[nodiscard]] PlaceableSnapResult snapPlaceableInstance(
    const PlaceableInstance& input, const TransformSnapProfile& profile,
    TransformChannel channels);

[[nodiscard]] const char* transformSnapErrorLabel(
    TransformSnapError error) noexcept;

} // namespace engine::editor
