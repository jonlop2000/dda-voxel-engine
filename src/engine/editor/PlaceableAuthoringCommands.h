#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "engine/editor/TransformSnapping.h"

namespace engine::editor
{

enum class PlaceableAuthoringCommandError
{
    None,
    InvalidExistingIdentity,
    DuplicateExistingIdentity,
    InvalidExistingTransform,
    InvalidPrototype,
    MissingIdentityEntropy,
    InvalidTargetIdentity,
    TargetNotFound,
    InvalidRequestedTransform,
    IdentityExhausted,
};

struct PlaceableAuthoringCommandResult
{
    std::optional<PlaceableEditCommand> command{};
    std::optional<PlaceableInstance> affectedPlaceable{};
    PlaceableAuthoringCommandError error =
        PlaceableAuthoringCommandError::None;
    TransformSnapError snapError = TransformSnapError::None;
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None;
    size_t invalidPlaceableIndex = static_cast<size_t>(-1);

    [[nodiscard]] bool valid() const noexcept
    {
        return error == PlaceableAuthoringCommandError::None &&
               command.has_value() && affectedPlaceable.has_value();
    }

    explicit operator bool() const noexcept { return valid(); }
};

struct PlaceableCreateRequest
{
    TransformState proposedTransform{};
    std::optional<float> surfaceY{};
    bool snapScale = true;

    // supplied by the document/controller boundary. a fixed value produces a
    // repeatable identity; collisions with the effective collection advance a
    // deterministic retry sequence instead of leaking a duplicate uuid.
    std::optional<uint64_t> identityEntropy{};
};

struct PlaceableDuplicateRequest
{
    std::string sourceUuid{};
    glm::vec3 translationOffset{0.25f, 0.0f, 0.25f};
    std::optional<float> surfaceY{};
    std::optional<uint64_t> identityEntropy{};
};

struct PlaceableDeleteRequest
{
    std::string targetUuid{};
};

// these functions only prepare exact journal-ready commands. they never mutate
// SceneConfig, journal history, runtime projections, renderer state, or selection.
[[nodiscard]] PlaceableAuthoringCommandResult buildCreatePlaceableCommand(
    std::span<const PlaceableInstance> effectivePlaceables,
    const PlaceablePrototype& prototype,
    const PlaceableCreateRequest& request);

[[nodiscard]] PlaceableAuthoringCommandResult buildDuplicatePlaceableCommand(
    std::span<const PlaceableInstance> effectivePlaceables,
    const PlaceableDuplicateRequest& request);

[[nodiscard]] PlaceableAuthoringCommandResult buildDeletePlaceableCommand(
    std::span<const PlaceableInstance> effectivePlaceables,
    const PlaceableDeleteRequest& request);

[[nodiscard]] const char* placeableAuthoringCommandErrorLabel(
    PlaceableAuthoringCommandError error) noexcept;

} // namespace engine::editor
