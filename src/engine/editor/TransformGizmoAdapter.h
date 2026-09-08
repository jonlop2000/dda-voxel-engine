#pragma once

#include <cstdint>
#include <optional>

#include "engine/editor/SelectionPicking.h"
#include "engine/editor/TransformSnapping.h"
#include "engine/game/Placeables.h"

namespace engine::editor
{

enum class TransformGizmoOperation
{
    Translate,
    Rotate,
    Scale,
};

struct TransformGizmoBeginRequest
{
    uint64_t gestureId = 0;
    SelectionTarget target{};
    PlaceableInstance selected{};
    TransformGizmoOperation operation = TransformGizmoOperation::Translate;
    TransformSnapProfile snapProfile = placeableSnapProfile();
};

struct TransformGizmoUpdateRequest
{
    uint64_t gestureId = 0;
    SelectionTarget target{};
    TransformState proposed{};
};

enum class TransformGizmoError
{
    None,
    InvalidGestureId,
    GestureAlreadyActive,
    NoActiveGesture,
    StaleGesture,
    InvalidSelection,
    UnsupportedTarget,
    SelectionMismatch,
    InvalidPlaceableIdentity,
    InvalidOperation,
    InvalidSnapProfile,
    InvalidTransform,
    UnsupportedRotation,
};

struct TransformGizmoResult
{
    TransformGizmoError error = TransformGizmoError::None;
    uint64_t gestureId = 0;
    bool active = false;
    std::optional<PlaceableInstance> canonicalPreview{};
    std::optional<PlaceableEditCommand> command{};
    TransformSnapError snapError = TransformSnapError::None;
    engine::game::PlaceableTransformError transformError =
        engine::game::PlaceableTransformError::None;

    [[nodiscard]] bool valid() const noexcept
    {
        return error == TransformGizmoError::None;
    }

    explicit operator bool() const noexcept { return valid(); }
};

// pure cpu gesture adapter. updates produce canonical previews only. a commit
// produces at most one exact move command, which the app must submit through
// PlaceableEditJournal/GameRuntime after the gesture has ended.
class TransformGizmoAdapter
{
public:
    [[nodiscard]] TransformGizmoResult begin(
        const TransformGizmoBeginRequest& request);
    [[nodiscard]] TransformGizmoResult update(
        const TransformGizmoUpdateRequest& request);
    [[nodiscard]] TransformGizmoResult commit(uint64_t gestureId);
    [[nodiscard]] TransformGizmoResult cancel(uint64_t gestureId);

    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept { return session_.has_value(); }
    [[nodiscard]] uint64_t activeGestureId() const noexcept
    {
        return session_ ? session_->gestureId : 0;
    }
    [[nodiscard]] const SelectionTarget* activeTarget() const noexcept
    {
        return session_ ? &session_->target : nullptr;
    }
    [[nodiscard]] const PlaceableInstance* preview() const noexcept
    {
        return session_ ? &session_->latest : nullptr;
    }

private:
    struct Session
    {
        uint64_t gestureId = 0;
        SelectionTarget target{};
        PlaceableInstance before{};
        PlaceableInstance latest{};
        TransformGizmoOperation operation = TransformGizmoOperation::Translate;
        TransformSnapProfile snapProfile{};
    };

    [[nodiscard]] TransformGizmoResult sessionError(
        TransformGizmoError error, uint64_t gestureId) const;
    [[nodiscard]] TransformGizmoResult sessionResult() const;

    std::optional<Session> session_{};
};

[[nodiscard]] const char* transformGizmoErrorLabel(
    TransformGizmoError error) noexcept;

} // namespace engine::editor
