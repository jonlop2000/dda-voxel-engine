#pragma once

#include <cstdint>

#include "engine/editor/TransformGizmoAdapter.h"

namespace engine::editor
{

// retained, renderer-independent state for the editor viewport tool. authored
// placeables retain only their uuid-backed SelectionTarget; runtime volume
// selections remain valid only for the VoxelWorld structure revision that
// produced them.
class ViewportToolSession
{
public:
    void select(SelectionTarget target);
    void clearSelection() noexcept;

    [[nodiscard]] const SelectionTarget& selection() const noexcept
    {
        return selection_;
    }
    [[nodiscard]] bool hasSelection() const noexcept
    {
        return selection_.valid();
    }

    // returns false and clears stale state when the selected authored uuid no
    // longer resolves or a revision-scoped runtime volume has been rebuilt.
    [[nodiscard]] bool reconcile(bool selectedPlaceableExists,
                                 uint64_t voxelStructureRevision) noexcept;

    void setOperation(TransformGizmoOperation operation) noexcept;
    [[nodiscard]] TransformGizmoOperation operation() const noexcept
    {
        return operation_;
    }

    [[nodiscard]] TransformGizmoResult begin(const PlaceableInstance& selected,
                                             TransformSnapProfile snapProfile);
    [[nodiscard]] TransformGizmoResult update(const TransformState& proposed);
    [[nodiscard]] TransformGizmoResult commit();
    [[nodiscard]] TransformGizmoResult cancel();

    [[nodiscard]] bool gestureActive() const noexcept
    {
        return gizmo_.active();
    }
    [[nodiscard]] const PlaceableInstance* preview() const noexcept
    {
        return gizmo_.preview();
    }

private:
    [[nodiscard]] uint64_t allocateGestureId() noexcept;

    SelectionTarget selection_{};
    TransformGizmoAdapter gizmo_{};
    TransformGizmoOperation operation_ = TransformGizmoOperation::Translate;
    uint64_t nextGestureId_ = 1;
};

} // namespace engine::editor
