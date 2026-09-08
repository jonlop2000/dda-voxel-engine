#include "engine/editor/ViewportToolSession.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <glm/gtc/quaternion.hpp>

namespace
{

using engine::editor::SelectionTarget;
using engine::editor::TransformGizmoError;
using engine::editor::TransformGizmoOperation;
using engine::editor::TransformState;
using engine::editor::ViewportToolSession;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

PlaceableInstance placeable(std::string uuid)
{
    PlaceableInstance instance{};
    instance.uuid = std::move(uuid);
    instance.prototypeSlug = "eelgrass";
    instance.prototypeVersion = 1;
    instance.position = {1.0f, 2.0f, 3.0f};
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = glm::vec3(1.0f);
    instance.seed = 9;
    return instance;
}

void testRetainedIdentityAndReconciliation()
{
    ViewportToolSession session{};
    require(!session.hasSelection() && session.reconcile(false, 4),
            "An empty session should remain valid across world revisions");

    session.select(SelectionTarget::placeable("plant-a"));
    require(session.hasSelection() && session.reconcile(true, 4),
            "A UUID-backed placeable should survive a rebuild while it resolves");
    require(!session.reconcile(false, 5) && !session.hasSelection(),
            "A missing authored UUID should clear retained placeable state");

    session.select(SelectionTarget::voxelVolume(8, 3));
    require(session.reconcile(false, 8) && session.hasSelection(),
            "A read-only runtime volume should survive only its source revision");
    require(!session.reconcile(false, 9) && !session.hasSelection(),
            "A structural rebuild should invalidate ephemeral volume identity");

    session.select(SelectionTarget::terrainCell("main", {2, 4, 6}));
    require(session.reconcile(false, 99) && session.hasSelection(),
            "An authored terrain-cell identity should not depend on volume revision");
}

void testPreviewOnlyGestureAndSingleCommit()
{
    const PlaceableInstance selected = placeable("plant-b");
    ViewportToolSession session{};
    session.select(SelectionTarget::placeable(selected.uuid));

    const auto begun = session.begin(
        selected, engine::editor::placeableSnapProfile());
    require(begun && begun.active && session.gestureActive(),
            "A selected placeable should begin one retained gesture");

    TransformState proposed{selected.position, selected.rotation, selected.scale};
    proposed.translation = {2.38f, 100.0f, 4.62f};
    const auto updated = session.update(proposed);
    require(updated && updated.active && updated.canonicalPreview &&
                updated.canonicalPreview->position == glm::vec3(2.5f, 2.0f, 4.5f) &&
                !updated.command && selected.position == glm::vec3(1.0f, 2.0f, 3.0f),
            "Update should expose a snapped preview without mutating the authored baseline");

    // reselecting the same uuid is idempotent and must not kill an in-flight
    // gesture when the live scene is rebuilt around that persistent identity.
    session.select(SelectionTarget::placeable(selected.uuid));
    require(session.gestureActive(),
            "The same persistent selection should preserve its gesture");

    const auto committed = session.commit();
    require(committed && !committed.active && committed.command &&
                committed.command->op == PlaceableEditCommand::Op::Move &&
                committed.command->before && committed.command->after &&
                committed.command->before->position == selected.position &&
                committed.command->after->position == glm::vec3(2.5f, 2.0f, 4.5f),
            "Commit should emit one exact Move and end preview state");
    require(session.commit().error == TransformGizmoError::NoActiveGesture,
            "The same retained gesture must not commit twice");
}

void testSelectionAndOperationChangesCancelPreview()
{
    const PlaceableInstance selected = placeable("plant-c");
    ViewportToolSession session{};
    session.select(SelectionTarget::placeable(selected.uuid));
    require(session.begin(selected, engine::editor::placeableSnapProfile()).valid(),
            "Translation gesture should begin");

    session.setOperation(TransformGizmoOperation::Rotate);
    require(!session.gestureActive() &&
                session.operation() == TransformGizmoOperation::Rotate,
            "Changing tools should cancel an uncommitted preview");
    require(session.commit().error == TransformGizmoError::NoActiveGesture,
            "A canceled preview should never leak a command");

    require(session.begin(selected, engine::editor::placeableSnapProfile()).valid(),
            "Rotation gesture should begin after tool change");
    session.select(SelectionTarget::placeable("plant-d"));
    require(!session.gestureActive(),
            "Changing persistent selection should cancel the prior gesture");

    session.clearSelection();
    require(!session.hasSelection() && !session.gestureActive(),
            "Explicit clear should reset selection and gesture together");
}

} // namespace

int main()
{
    try
    {
        testRetainedIdentityAndReconciliation();
        testPreviewOnlyGestureAndSingleCommit();
        testSelectionAndOperationChangesCancelPreview();
        std::cout << "Viewport tool session tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Viewport tool session tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
