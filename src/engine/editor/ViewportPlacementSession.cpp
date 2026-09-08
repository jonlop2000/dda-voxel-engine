#include "engine/editor/ViewportPlacementSession.h"

#include <cmath>
#include <utility>

#include "engine/game/PlaceableTransform.h"

namespace engine::editor
{
namespace
{

constexpr const char* kPreviewIdentity =
    "00000000-0000-4000-8000-000000000000";

bool finite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

PlaceableInstance previewInstance(const PlaceablePrototype& prototype,
                                  const TransformState& transform)
{
    PlaceableInstance instance{};
    instance.uuid = kPreviewIdentity;
    instance.prototypeSlug = prototype.slug;
    instance.prototypeVersion = prototype.version;
    instance.position = transform.translation;
    instance.rotation = transform.rotation;
    instance.scale = transform.scale;
    return instance;
}

TransformState transformFrom(const PlaceableInstance& instance) noexcept
{
    return {instance.position, instance.rotation, instance.scale};
}

} // namespace

ViewportPlacementResult ViewportPlacementSession::arm(
    const PlaceablePrototype& prototype,
    const TransformState& transformTemplate, bool snapScale)
{
    if (prototype.slug.empty() || prototype.version == 0)
    {
        return result(ViewportPlacementError::InvalidPrototype);
    }

    const PlaceableSnapResult canonical = snapPlaceableInstance(
        previewInstance(prototype, transformTemplate),
        placeableSnapProfile(std::nullopt, snapScale),
        TransformChannel::Rotation | TransformChannel::Scale);
    if (!canonical)
    {
        ViewportPlacementResult rejected =
            result(ViewportPlacementError::InvalidTemplate);
        rejected.snapError = canonical.error;
        rejected.transformError = canonical.transformError;
        return rejected;
    }

    prototype_ = prototype;
    transformTemplate_ = transformFrom(canonical.canonical);
    preview_.reset();
    snapScale_ = snapScale;
    state_ = ViewportPlacementState::Armed;
    ++revision_;
    return result();
}

ViewportPlacementResult ViewportPlacementSession::update(
    const ViewportPlacementSurfaceHit& surfaceHit,
    const ViewportPlacementValidator& validator)
{
    if (!armed() || !prototype_)
    {
        return result(ViewportPlacementError::NotArmed);
    }
    if (!surfaceHit.hit)
    {
        preview_.reset();
        state_ = ViewportPlacementState::Armed;
        ++revision_;
        return result(ViewportPlacementError::SurfaceMiss);
    }
    if (!finite(surfaceHit.position))
    {
        preview_.reset();
        state_ = ViewportPlacementState::Armed;
        ++revision_;
        return result(ViewportPlacementError::InvalidSurfaceHit);
    }

    TransformState proposed = transformTemplate_;
    proposed.translation = surfaceHit.position;
    const PlaceableSnapResult snapped = snapPlaceableInstance(
        previewInstance(*prototype_, proposed),
        placeableSnapProfile(surfaceHit.position.y, snapScale_),
        TransformChannel::All);
    if (!snapped)
    {
        preview_.reset();
        state_ = ViewportPlacementState::Armed;
        ++revision_;
        return snapFailure(snapped);
    }

    preview_ = snapped.canonical;
    ++revision_;
    if (!validator)
    {
        state_ = ViewportPlacementState::PreviewBlocked;
        return result(ViewportPlacementError::MissingValidator);
    }
    const ViewportPlacementDecision decision = validator(*preview_);
    if (!decision.accepted)
    {
        state_ = ViewportPlacementState::PreviewBlocked;
        ViewportPlacementResult rejected =
            result(ViewportPlacementError::PlacementRejected);
        rejected.rejectionReason = decision.reason;
        return rejected;
    }

    state_ = ViewportPlacementState::PreviewValid;
    return result();
}

ViewportPlacementResult ViewportPlacementSession::commit(
    std::span<const PlaceableInstance> effectivePlaceables,
    std::optional<uint64_t> identityEntropy,
    const ViewportPlacementValidator& validator)
{
    if (state_ != ViewportPlacementState::PreviewValid || !prototype_ ||
        !preview_)
    {
        return result(ViewportPlacementError::NoValidPreview);
    }
    if (!validator)
    {
        state_ = ViewportPlacementState::PreviewBlocked;
        ++revision_;
        return result(ViewportPlacementError::MissingValidator);
    }

    PlaceableCreateRequest request{};
    request.proposedTransform = transformFrom(*preview_);
    request.surfaceY = preview_->position.y;
    request.snapScale = snapScale_;
    request.identityEntropy = identityEntropy;
    const PlaceableAuthoringCommandResult prepared =
        buildCreatePlaceableCommand(effectivePlaceables, *prototype_, request);
    if (!prepared)
    {
        ViewportPlacementResult rejected =
            result(ViewportPlacementError::CommandRejected);
        rejected.commandError = prepared.error;
        rejected.snapError = prepared.snapError;
        rejected.transformError = prepared.transformError;
        return rejected;
    }
    if (prepared.affectedPlaceable->prototypeSlug != preview_->prototypeSlug ||
        prepared.affectedPlaceable->prototypeVersion !=
            preview_->prototypeVersion ||
        !engine::game::placeableTransformEquivalent(
            *prepared.affectedPlaceable, *preview_))
    {
        return result(ViewportPlacementError::PreviewMismatch);
    }

    const ViewportPlacementDecision decision =
        validator(*prepared.affectedPlaceable);
    if (!decision.accepted)
    {
        state_ = ViewportPlacementState::PreviewBlocked;
        ++revision_;
        ViewportPlacementResult rejected =
            result(ViewportPlacementError::PlacementRejected);
        rejected.rejectionReason = decision.reason;
        return rejected;
    }

    ViewportPlacementResult committed = result();
    committed.command = prepared.command;
    committed.affectedPlaceable = prepared.affectedPlaceable;
    preview_.reset();
    state_ = ViewportPlacementState::Armed;
    ++revision_;
    committed.state = state_;
    committed.revision = revision_;
    committed.preview.reset();
    return committed;
}

ViewportPlacementResult ViewportPlacementSession::cancelPreview()
{
    if (!armed())
    {
        return result(ViewportPlacementError::NotArmed);
    }
    preview_.reset();
    state_ = ViewportPlacementState::Armed;
    ++revision_;
    return result();
}

void ViewportPlacementSession::disarm() noexcept
{
    prototype_.reset();
    preview_.reset();
    transformTemplate_ = {};
    snapScale_ = true;
    state_ = ViewportPlacementState::Idle;
    ++revision_;
}

ViewportPlacementResult ViewportPlacementSession::result(
    ViewportPlacementError error) const
{
    ViewportPlacementResult value{};
    value.state = state_;
    value.error = error;
    value.revision = revision_;
    value.preview = preview_;
    return value;
}

ViewportPlacementResult ViewportPlacementSession::snapFailure(
    const PlaceableSnapResult& snapped) const
{
    ViewportPlacementResult rejected =
        result(ViewportPlacementError::InvalidSurfaceHit);
    rejected.snapError = snapped.error;
    rejected.transformError = snapped.transformError;
    return rejected;
}

const char* viewportPlacementErrorLabel(
    ViewportPlacementError error) noexcept
{
    switch (error)
    {
    case ViewportPlacementError::None:
        return "none";
    case ViewportPlacementError::InvalidPrototype:
        return "invalid prototype";
    case ViewportPlacementError::InvalidTemplate:
        return "invalid placement transform template";
    case ViewportPlacementError::NotArmed:
        return "placement session is not armed";
    case ViewportPlacementError::SurfaceMiss:
        return "placement ray missed the authoring surface";
    case ViewportPlacementError::InvalidSurfaceHit:
        return "invalid placement surface hit";
    case ViewportPlacementError::MissingValidator:
        return "placement validator is unavailable";
    case ViewportPlacementError::PlacementRejected:
        return "placement candidate rejected";
    case ViewportPlacementError::NoValidPreview:
        return "no valid placement preview";
    case ViewportPlacementError::CommandRejected:
        return "placement command rejected";
    case ViewportPlacementError::PreviewMismatch:
        return "placement command does not match preview";
    }
    return "unknown viewport placement error";
}

} // namespace engine::editor
