#include "engine/editor/ViewportToolSession.h"

#include <utility>

namespace engine::editor
{

void ViewportToolSession::select(SelectionTarget target)
{
    if (!target.valid())
    {
        clearSelection();
        return;
    }
    if (selectionTargetEqual(selection_, target))
    {
        return;
    }
    gizmo_.reset();
    selection_ = std::move(target);
}

void ViewportToolSession::clearSelection() noexcept
{
    gizmo_.reset();
    selection_ = {};
}

bool ViewportToolSession::reconcile(bool selectedPlaceableExists,
                                    uint64_t voxelStructureRevision) noexcept
{
    bool valid = true;
    if (const auto* placeable =
            std::get_if<PlaceableSelection>(&selection_.value))
    {
        valid = !placeable->uuid.empty() && selectedPlaceableExists;
    }
    else if (const auto* volume =
                 std::get_if<VoxelVolumeSelection>(&selection_.value))
    {
        valid = volume->structureRevision == voxelStructureRevision;
    }
    if (!valid)
    {
        clearSelection();
    }
    return valid;
}

void ViewportToolSession::setOperation(
    TransformGizmoOperation operation) noexcept
{
    if (operation_ == operation)
    {
        return;
    }
    gizmo_.reset();
    operation_ = operation;
}

TransformGizmoResult ViewportToolSession::begin(
    const PlaceableInstance& selected, TransformSnapProfile snapProfile)
{
    TransformGizmoBeginRequest request{};
    request.gestureId = allocateGestureId();
    request.target = selection_;
    request.selected = selected;
    request.operation = operation_;
    request.snapProfile = std::move(snapProfile);
    return gizmo_.begin(request);
}

TransformGizmoResult ViewportToolSession::update(
    const TransformState& proposed)
{
    return gizmo_.update(
        {gizmo_.activeGestureId(), selection_, proposed});
}

TransformGizmoResult ViewportToolSession::commit()
{
    return gizmo_.commit(gizmo_.activeGestureId());
}

TransformGizmoResult ViewportToolSession::cancel()
{
    return gizmo_.cancel(gizmo_.activeGestureId());
}

uint64_t ViewportToolSession::allocateGestureId() noexcept
{
    const uint64_t id = nextGestureId_++;
    if (nextGestureId_ == 0)
    {
        nextGestureId_ = 1;
    }
    return id == 0 ? nextGestureId_++ : id;
}

} // namespace engine::editor
