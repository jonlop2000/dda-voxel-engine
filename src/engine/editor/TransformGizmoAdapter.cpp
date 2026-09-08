#include "engine/editor/TransformGizmoAdapter.h"

#include <cmath>
#include <utility>

#include "engine/game/PlaceableTransform.h"

namespace engine::editor
{
namespace
{

bool validPlaceableIdentity(const PlaceableInstance& instance) noexcept
{
    return !instance.uuid.empty() && !instance.prototypeSlug.empty() &&
           instance.prototypeVersion != 0;
}

TransformChannel operationChannel(TransformGizmoOperation operation) noexcept
{
    switch (operation)
    {
    case TransformGizmoOperation::Translate:
        return TransformChannel::Translation;
    case TransformGizmoOperation::Rotate:
        return TransformChannel::Rotation;
    case TransformGizmoOperation::Scale:
        return TransformChannel::Scale;
    }
    return TransformChannel::None;
}

bool validOperation(TransformGizmoOperation operation) noexcept
{
    return operation == TransformGizmoOperation::Translate ||
           operation == TransformGizmoOperation::Rotate ||
           operation == TransformGizmoOperation::Scale;
}

TransformGizmoError snapFailureError(TransformSnapError error) noexcept
{
    switch (error)
    {
    case TransformSnapError::InvalidProfile:
        return TransformGizmoError::InvalidSnapProfile;
    case TransformSnapError::InvalidTransform:
        return TransformGizmoError::InvalidTransform;
    case TransformSnapError::None:
        break;
    }
    return TransformGizmoError::InvalidTransform;
}

bool yawOnly(const glm::quat& rotation) noexcept
{
    PlaceableInstance candidate{};
    candidate.position = glm::vec3(0.0f);
    candidate.rotation = rotation;
    candidate.scale = glm::vec3(1.0f);
    const engine::game::PlaceableTransformValidation validation =
        engine::game::validatePlaceableTransform(candidate);
    constexpr float kAxisTolerance = 1e-5f;
    return validation &&
           std::abs(validation.canonical.rotation.x) <= kAxisTolerance &&
           std::abs(validation.canonical.rotation.z) <= kAxisTolerance;
}

} // namespace

TransformGizmoResult TransformGizmoAdapter::begin(
    const TransformGizmoBeginRequest& request)
{
    if (session_)
    {
        return sessionError(TransformGizmoError::GestureAlreadyActive,
                            request.gestureId);
    }
    if (request.gestureId == 0)
    {
        return sessionError(TransformGizmoError::InvalidGestureId,
                            request.gestureId);
    }
    if (!request.target.valid())
    {
        return sessionError(TransformGizmoError::InvalidSelection,
                            request.gestureId);
    }
    if (request.target.kind() != SelectionTargetKind::Placeable)
    {
        return sessionError(TransformGizmoError::UnsupportedTarget,
                            request.gestureId);
    }
    if (!validPlaceableIdentity(request.selected))
    {
        return sessionError(TransformGizmoError::InvalidPlaceableIdentity,
                            request.gestureId);
    }
    if (!validOperation(request.operation))
    {
        return sessionError(TransformGizmoError::InvalidOperation,
                            request.gestureId);
    }
    const auto* selectedTarget =
        std::get_if<PlaceableSelection>(&request.target.value);
    if (selectedTarget == nullptr ||
        selectedTarget->uuid != request.selected.uuid)
    {
        return sessionError(TransformGizmoError::SelectionMismatch,
                            request.gestureId);
    }
    if (request.snapProfile.target != TransformSnapTarget::Placeable ||
        (request.operation == TransformGizmoOperation::Scale &&
         request.snapProfile.scaleStep <= 0.0f))
    {
        return sessionError(TransformGizmoError::InvalidSnapProfile,
                            request.gestureId);
    }

    // a no-channel snap validates the complete profile and canonicalizes the
    // baseline without applying any authored transform operation.
    const PlaceableSnapResult baseline = snapPlaceableInstance(
        request.selected, request.snapProfile, TransformChannel::None);
    if (!baseline)
    {
        TransformGizmoResult result = sessionError(
            snapFailureError(baseline.error), request.gestureId);
        result.snapError = baseline.error;
        result.transformError = baseline.transformError;
        return result;
    }
    if (request.operation == TransformGizmoOperation::Rotate &&
        !yawOnly(baseline.canonical.rotation))
    {
        return sessionError(TransformGizmoError::UnsupportedRotation,
                            request.gestureId);
    }

    session_.emplace(Session{request.gestureId, request.target,
                             baseline.canonical, baseline.canonical,
                             request.operation, request.snapProfile});
    return sessionResult();
}

TransformGizmoResult TransformGizmoAdapter::update(
    const TransformGizmoUpdateRequest& request)
{
    if (!session_)
    {
        return sessionError(TransformGizmoError::NoActiveGesture,
                            request.gestureId);
    }
    if (request.gestureId == 0 || request.gestureId != session_->gestureId)
    {
        return sessionError(TransformGizmoError::StaleGesture,
                            request.gestureId);
    }
    if (!selectionTargetEqual(request.target, session_->target))
    {
        return sessionError(TransformGizmoError::SelectionMismatch,
                            request.gestureId);
    }

    PlaceableInstance candidate = session_->before;
    switch (session_->operation)
    {
    case TransformGizmoOperation::Translate:
        candidate.position = request.proposed.translation;
        if (!session_->snapProfile.surfaceY)
        {
            candidate.position.y = session_->before.position.y;
        }
        break;
    case TransformGizmoOperation::Rotate:
        candidate.rotation = request.proposed.rotation;
        if (const engine::game::PlaceableTransformValidation validation =
                engine::game::validatePlaceableTransform(candidate);
            !validation)
        {
            TransformGizmoResult result = sessionError(
                TransformGizmoError::InvalidTransform, request.gestureId);
            result.snapError = TransformSnapError::InvalidTransform;
            result.transformError = validation.error;
            return result;
        }
        if (!yawOnly(candidate.rotation))
        {
            return sessionError(TransformGizmoError::UnsupportedRotation,
                                request.gestureId);
        }
        break;
    case TransformGizmoOperation::Scale:
        candidate.scale = request.proposed.scale;
        break;
    }

    const PlaceableSnapResult snapped = snapPlaceableInstance(
        candidate, session_->snapProfile,
        operationChannel(session_->operation));
    if (!snapped)
    {
        TransformGizmoResult result = sessionError(
            snapFailureError(snapped.error), request.gestureId);
        result.snapError = snapped.error;
        result.transformError = snapped.transformError;
        return result;
    }

    session_->latest = snapped.canonical;
    return sessionResult();
}

TransformGizmoResult TransformGizmoAdapter::commit(uint64_t gestureId)
{
    if (!session_)
    {
        return sessionError(TransformGizmoError::NoActiveGesture, gestureId);
    }
    if (gestureId == 0 || gestureId != session_->gestureId)
    {
        return sessionError(TransformGizmoError::StaleGesture, gestureId);
    }

    TransformGizmoResult result{};
    result.gestureId = gestureId;
    result.canonicalPreview = session_->latest;
    if (!engine::game::placeableTransformEquivalent(session_->before,
                                                     session_->latest))
    {
        PlaceableEditCommand command{};
        command.op = PlaceableEditCommand::Op::Move;
        command.before = session_->before;
        command.after = session_->latest;
        result.command = std::move(command);
    }
    session_.reset();
    return result;
}

TransformGizmoResult TransformGizmoAdapter::cancel(uint64_t gestureId)
{
    if (!session_)
    {
        return sessionError(TransformGizmoError::NoActiveGesture, gestureId);
    }
    if (gestureId == 0 || gestureId != session_->gestureId)
    {
        return sessionError(TransformGizmoError::StaleGesture, gestureId);
    }

    TransformGizmoResult result{};
    result.gestureId = gestureId;
    result.canonicalPreview = session_->before;
    session_.reset();
    return result;
}

void TransformGizmoAdapter::reset() noexcept
{
    session_.reset();
}

TransformGizmoResult TransformGizmoAdapter::sessionError(
    TransformGizmoError error, uint64_t gestureId) const
{
    TransformGizmoResult result{};
    result.error = error;
    result.gestureId = gestureId;
    result.active = session_.has_value();
    if (session_)
    {
        result.canonicalPreview = session_->latest;
    }
    return result;
}

TransformGizmoResult TransformGizmoAdapter::sessionResult() const
{
    TransformGizmoResult result{};
    if (session_)
    {
        result.gestureId = session_->gestureId;
        result.active = true;
        result.canonicalPreview = session_->latest;
    }
    return result;
}

const char* transformGizmoErrorLabel(TransformGizmoError error) noexcept
{
    switch (error)
    {
    case TransformGizmoError::None:
        return "none";
    case TransformGizmoError::InvalidGestureId:
        return "invalid_gesture_id";
    case TransformGizmoError::GestureAlreadyActive:
        return "gesture_already_active";
    case TransformGizmoError::NoActiveGesture:
        return "no_active_gesture";
    case TransformGizmoError::StaleGesture:
        return "stale_gesture";
    case TransformGizmoError::InvalidSelection:
        return "invalid_selection";
    case TransformGizmoError::UnsupportedTarget:
        return "unsupported_target";
    case TransformGizmoError::SelectionMismatch:
        return "selection_mismatch";
    case TransformGizmoError::InvalidPlaceableIdentity:
        return "invalid_placeable_identity";
    case TransformGizmoError::InvalidOperation:
        return "invalid_operation";
    case TransformGizmoError::InvalidSnapProfile:
        return "invalid_snap_profile";
    case TransformGizmoError::InvalidTransform:
        return "invalid_transform";
    case TransformGizmoError::UnsupportedRotation:
        return "unsupported_rotation";
    }
    return "unknown";
}

} // namespace engine::editor
