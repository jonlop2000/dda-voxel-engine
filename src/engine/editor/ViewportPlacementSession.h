#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "engine/editor/PlaceableAuthoringCommands.h"

namespace engine::editor
{

enum class ViewportPlacementState
{
    Idle,
    Armed,
    PreviewValid,
    PreviewBlocked,
};

enum class ViewportPlacementError
{
    None,
    InvalidPrototype,
    InvalidTemplate,
    NotArmed,
    SurfaceMiss,
    InvalidSurfaceHit,
    MissingValidator,
    PlacementRejected,
    NoValidPreview,
    CommandRejected,
    PreviewMismatch,
};

struct ViewportPlacementSurfaceHit
{
    bool hit = false;
    glm::vec3 position{0.0f};
};

struct ViewportPlacementDecision
{
    bool accepted = false;
    std::string reason{};

    [[nodiscard]] static ViewportPlacementDecision accept()
    {
        return {true, {}};
    }

    [[nodiscard]] static ViewportPlacementDecision reject(
        std::string reason)
    {
        return {false, std::move(reason)};
    }
};

using ViewportPlacementValidator =
    std::function<ViewportPlacementDecision(const PlaceableInstance&)>;

struct ViewportPlacementResult
{
    ViewportPlacementState state = ViewportPlacementState::Idle;
    ViewportPlacementError error = ViewportPlacementError::None;
    uint64_t revision = 0;
    std::optional<PlaceableInstance> preview{};
    std::optional<PlaceableEditCommand> command{};
    std::optional<PlaceableInstance> affectedPlaceable{};
    std::string rejectionReason{};
    PlaceableAuthoringCommandError commandError =
        PlaceableAuthoringCommandError::None;
    TransformSnapError snapError = TransformSnapError::None;
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == ViewportPlacementError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// pure retained state for a future viewport click-to-place tool. it owns no
// SceneConfig, journal, renderer, selection, raycast, or surface authority.
// callers provide authoritative surface hits and a synchronous validator.
class ViewportPlacementSession
{
public:
    [[nodiscard]] ViewportPlacementResult arm(
        const PlaceablePrototype& prototype,
        const TransformState& transformTemplate = {},
        bool snapScale = true);

    [[nodiscard]] ViewportPlacementResult update(
        const ViewportPlacementSurfaceHit& surfaceHit,
        const ViewportPlacementValidator& validator);

    // revalidates the exact generated identity against the current scene view
    // before returning one journal-ready add command. success clears only the
    // preview and stays Armed for continuous placement.
    [[nodiscard]] ViewportPlacementResult commit(
        std::span<const PlaceableInstance> effectivePlaceables,
        std::optional<uint64_t> identityEntropy,
        const ViewportPlacementValidator& validator);

    [[nodiscard]] ViewportPlacementResult cancelPreview();
    void disarm() noexcept;

    [[nodiscard]] ViewportPlacementState state() const noexcept
    {
        return state_;
    }
    [[nodiscard]] bool armed() const noexcept
    {
        return state_ != ViewportPlacementState::Idle;
    }
    [[nodiscard]] const PlaceableInstance* preview() const noexcept
    {
        return preview_ ? &*preview_ : nullptr;
    }
    [[nodiscard]] const PlaceablePrototype* prototype() const noexcept
    {
        return prototype_ ? &*prototype_ : nullptr;
    }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] ViewportPlacementResult result(
        ViewportPlacementError error = ViewportPlacementError::None) const;
    [[nodiscard]] ViewportPlacementResult snapFailure(
        const PlaceableSnapResult& snapped) const;

    ViewportPlacementState state_ = ViewportPlacementState::Idle;
    std::optional<PlaceablePrototype> prototype_{};
    std::optional<PlaceableInstance> preview_{};
    TransformState transformTemplate_{};
    bool snapScale_ = true;
    uint64_t revision_ = 0;
};

[[nodiscard]] const char* viewportPlacementErrorLabel(
    ViewportPlacementError error) noexcept;

} // namespace engine::editor
