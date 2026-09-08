#include "engine/editor/TransformGizmoAdapter.h"

#include "engine/game/PlaceableEditJournal.h"
#include "engine/game/PlaceableTransform.h"
#include "engine/scene/SceneConfig.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

namespace
{

using engine::editor::SelectionTarget;
using engine::editor::TransformGizmoAdapter;
using engine::editor::TransformGizmoBeginRequest;
using engine::editor::TransformGizmoError;
using engine::editor::TransformGizmoOperation;
using engine::editor::TransformGizmoUpdateRequest;
using engine::editor::TransformSnapTarget;
using engine::editor::TransformState;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool vec3NearlyEqual(const glm::vec3& lhs, const glm::vec3& rhs,
                     float epsilon = 1e-5f)
{
    return nearlyEqual(lhs.x, rhs.x, epsilon) &&
           nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

PlaceableInstance makePlaceable(std::string uuid = "selected")
{
    PlaceableInstance instance{};
    instance.uuid = std::move(uuid);
    instance.prototypeSlug = "eelgrass";
    instance.prototypeVersion = 1;
    instance.position = {1.13f, 4.4f, -2.12f};
    instance.rotation = glm::angleAxis(glm::radians(30.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    instance.scale = {0.6f, 1.2f, 2.0f};
    instance.seed = 91u;
    return instance;
}

TransformGizmoBeginRequest beginRequest(
    uint64_t gestureId, const PlaceableInstance& selected,
    TransformGizmoOperation operation,
    engine::editor::TransformSnapProfile profile =
        engine::editor::placeableSnapProfile())
{
    TransformGizmoBeginRequest request{};
    request.gestureId = gestureId;
    request.target = SelectionTarget::placeable(selected.uuid);
    request.selected = selected;
    request.operation = operation;
    request.snapProfile = profile;
    return request;
}

TransformGizmoUpdateRequest updateRequest(
    uint64_t gestureId, const PlaceableInstance& selected,
    const TransformState& proposed)
{
    return {gestureId, SelectionTarget::placeable(selected.uuid), proposed};
}

TransformState transformState(const PlaceableInstance& instance)
{
    return {instance.position, instance.rotation, instance.scale};
}

const PlaceableInstance& previewOf(
    const engine::editor::TransformGizmoResult& result,
    const std::string& message)
{
    require(result.canonicalPreview.has_value(), message);
    return *result.canonicalPreview;
}

void requireSameIdentity(const PlaceableInstance& lhs,
                         const PlaceableInstance& rhs,
                         const std::string& message)
{
    require(lhs.uuid == rhs.uuid &&
                lhs.prototypeSlug == rhs.prototypeSlug &&
                lhs.prototypeVersion == rhs.prototypeVersion &&
                lhs.seed == rhs.seed,
            message);
}

void testGestureLifecycleAndStaleRequests()
{
    const PlaceableInstance selected = makePlaceable();
    TransformGizmoAdapter adapter{};

    require(adapter.update(updateRequest(7u, selected,
                                         transformState(selected)))
                    .error == TransformGizmoError::NoActiveGesture &&
                adapter.commit(7u).error ==
                    TransformGizmoError::NoActiveGesture &&
                adapter.cancel(7u).error ==
                    TransformGizmoError::NoActiveGesture,
            "Update, commit, and cancel should reject a missing gesture");

    TransformGizmoBeginRequest invalidId = beginRequest(
        0u, selected, TransformGizmoOperation::Translate);
    require(adapter.begin(invalidId).error ==
                TransformGizmoError::InvalidGestureId,
            "Gesture zero should be reserved and rejected");

    const auto begun = adapter.begin(beginRequest(
        7u, selected, TransformGizmoOperation::Translate));
    require(begun.valid() && begun.active && begun.gestureId == 7u &&
                adapter.active() && adapter.activeGestureId() == 7u &&
                adapter.activeTarget() != nullptr &&
                adapter.preview() != nullptr && !begun.command,
            "Begin should establish exactly one preview-only gesture");

    const auto duplicate = adapter.begin(beginRequest(
        8u, selected, TransformGizmoOperation::Rotate));
    require(duplicate.error == TransformGizmoError::GestureAlreadyActive &&
                duplicate.active && adapter.activeGestureId() == 7u,
            "A second begin should preserve the authoritative active gesture");

    TransformState moved = transformState(selected);
    moved.translation.x += 1.0f;
    const auto staleUpdate = adapter.update(updateRequest(8u, selected, moved));
    require(staleUpdate.error == TransformGizmoError::StaleGesture &&
                staleUpdate.active && adapter.activeGestureId() == 7u,
            "A stale update ID should not disturb the active gesture");
    require(adapter.commit(8u).error == TransformGizmoError::StaleGesture &&
                adapter.cancel(8u).error == TransformGizmoError::StaleGesture &&
                adapter.active(),
            "Stale commit and cancel IDs should not end the active gesture");

    TransformGizmoUpdateRequest wrongSelection =
        updateRequest(7u, selected, moved);
    wrongSelection.target = SelectionTarget::placeable("replacement");
    const auto mismatched = adapter.update(wrongSelection);
    require(mismatched.error == TransformGizmoError::SelectionMismatch &&
                mismatched.active && adapter.active(),
            "Reselection during a gesture should fail without replacing its baseline");

    const auto cancelled = adapter.cancel(7u);
    require(cancelled.valid() && !cancelled.active && !cancelled.command &&
                !adapter.active() &&
                engine::game::placeableTransformEquivalent(
                    previewOf(cancelled,
                              "Cancel should expose the canonical baseline"),
                    selected),
            "A matching cancel should restore the preview and end the gesture");
    require(adapter.cancel(7u).error ==
                TransformGizmoError::NoActiveGesture,
            "A gesture should end exactly once");

    require(adapter.begin(beginRequest(
                9u, selected, TransformGizmoOperation::Translate)).valid(),
            "A new gesture should begin after cancellation");
    adapter.reset();
    require(!adapter.active() && adapter.activeGestureId() == 0u &&
                adapter.preview() == nullptr &&
                adapter.commit(9u).error ==
                    TransformGizmoError::NoActiveGesture,
            "Reset should invalidate every handle to a prior gesture");
}

void testTranslationSnappingAndUntouchedChannelIsolation()
{
    const PlaceableInstance selected = makePlaceable("translate");
    TransformGizmoAdapter adapter{};
    require(adapter.begin(beginRequest(
                11u, selected, TransformGizmoOperation::Translate)).valid(),
            "Translation gesture should begin");

    TransformState proposed{};
    proposed.translation = {2.38f, 99.0f, -3.62f};
    proposed.rotation = glm::angleAxis(glm::radians(43.0f),
                                       glm::vec3(1.0f, 0.0f, 0.0f));
    proposed.scale = {-4.0f, 0.0f, -8.0f};
    const auto updated = adapter.update(updateRequest(11u, selected, proposed));
    const PlaceableInstance& preview = previewOf(
        updated, "Translation update should return a canonical preview");
    require(updated.valid() && updated.active && !updated.command &&
                vec3NearlyEqual(preview.position,
                                {2.5f, selected.position.y, -3.5f}) &&
                vec3NearlyEqual(preview.scale, selected.scale) &&
                engine::game::placeableTransformEquivalent(
                    PlaceableInstance{"", "", 1u, {}, preview.rotation,
                                      selected.scale, 0u},
                    PlaceableInstance{"", "", 1u, {}, selected.rotation,
                                      selected.scale, 0u}),
            "Translate should snap XZ, preserve Y, and ignore proposed rotation/scale drift");

    const auto committed = adapter.commit(11u);
    require(committed.valid() && !committed.active &&
                committed.command.has_value() &&
                committed.command->op == PlaceableEditCommand::Op::Move &&
                committed.command->before.has_value() &&
                committed.command->after.has_value(),
            "A changed translation should emit one exact Move on commit");
    requireSameIdentity(*committed.command->before, selected,
                        "Move before should preserve persistent identity");
    requireSameIdentity(*committed.command->after, selected,
                        "Move after should preserve persistent identity");
    require(vec3NearlyEqual(committed.command->before->position,
                            selected.position) &&
                vec3NearlyEqual(committed.command->after->position,
                                preview.position) &&
                vec3NearlyEqual(committed.command->after->scale,
                                selected.scale),
            "The committed Move should retain the canonical baseline and latest preview");

    TransformGizmoAdapter surfaceAdapter{};
    const auto surfaceProfile =
        engine::editor::placeableSnapProfile(1.375f);
    require(surfaceAdapter.begin(beginRequest(
                12u, selected, TransformGizmoOperation::Translate,
                surfaceProfile)).valid(),
            "A surface-constrained translation should begin");
    proposed.translation = {0.37f, -200.0f, 0.63f};
    const auto surfaceUpdate =
        surfaceAdapter.update(updateRequest(12u, selected, proposed));
    require(surfaceUpdate.valid() &&
                vec3NearlyEqual(
                    previewOf(surfaceUpdate,
                              "Surface update should expose its preview")
                        .position,
                    {0.25f, 1.375f, 0.75f}),
            "An explicit surface should author Y while XZ remain quarter-unit snapped");
}

void testYawOnlyRotationAndQuaternionNoOp()
{
    const PlaceableInstance selected = makePlaceable("rotate");
    TransformGizmoAdapter adapter{};
    require(adapter.begin(beginRequest(
                21u, selected, TransformGizmoOperation::Rotate)).valid(),
            "Rotation gesture should begin");

    TransformState proposed{};
    proposed.translation = {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), -99.0f};
    proposed.rotation = glm::angleAxis(glm::radians(23.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    proposed.scale = glm::vec3(-1.0f);
    const auto updated = adapter.update(updateRequest(21u, selected, proposed));
    const PlaceableInstance& preview = previewOf(
        updated, "Yaw update should return a canonical preview");
    const glm::quat expected = glm::angleAxis(
        glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    PlaceableInstance expectedInstance = selected;
    expectedInstance.rotation = expected;
    require(updated.valid() &&
                engine::game::placeableTransformEquivalent(preview,
                                                            expectedInstance) &&
                vec3NearlyEqual(preview.position, selected.position) &&
                vec3NearlyEqual(preview.scale, selected.scale),
            "Rotate should snap yaw to 15 degrees and ignore translation/scale drift");

    TransformState nonFinite = transformState(selected);
    nonFinite.rotation.w = std::numeric_limits<float>::quiet_NaN();
    const auto rejectedNonFinite =
        adapter.update(updateRequest(21u, selected, nonFinite));
    require(rejectedNonFinite.error == TransformGizmoError::InvalidTransform &&
                rejectedNonFinite.snapError ==
                    engine::editor::TransformSnapError::InvalidTransform &&
                rejectedNonFinite.transformError ==
                    engine::game::PlaceableTransformError::NonFiniteRotation &&
                rejectedNonFinite.active &&
                engine::game::placeableTransformEquivalent(
                    previewOf(rejectedNonFinite,
                              "Rejected non-finite rotation should retain the preview"),
                    preview),
            "A non-finite rotation should preserve its typed transform failure");

    TransformState degenerate = transformState(selected);
    degenerate.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    const auto rejectedDegenerate =
        adapter.update(updateRequest(21u, selected, degenerate));
    require(rejectedDegenerate.error == TransformGizmoError::InvalidTransform &&
                rejectedDegenerate.snapError ==
                    engine::editor::TransformSnapError::InvalidTransform &&
                rejectedDegenerate.transformError ==
                    engine::game::PlaceableTransformError::DegenerateRotation &&
                rejectedDegenerate.active &&
                engine::game::placeableTransformEquivalent(
                    previewOf(rejectedDegenerate,
                              "Rejected degenerate rotation should retain the preview"),
                    preview),
            "A degenerate rotation should preserve its typed transform failure");

    TransformState pitchRoll = transformState(selected);
    pitchRoll.rotation =
        glm::angleAxis(glm::radians(10.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::angleAxis(glm::radians(12.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const auto unsupported =
        adapter.update(updateRequest(21u, selected, pitchRoll));
    require(unsupported.error == TransformGizmoError::UnsupportedRotation &&
                unsupported.active &&
                engine::game::placeableTransformEquivalent(
                    previewOf(unsupported,
                              "Rejected rotation should retain latest preview"),
                    preview),
            "Pitch/roll should be rejected without corrupting the prior yaw preview");
    require(adapter.cancel(21u).valid(),
            "A gesture should remain cancellable after a rejected update");

    PlaceableInstance signSelected = selected;
    signSelected.rotation = expected;
    TransformGizmoAdapter signAdapter{};
    require(signAdapter.begin(beginRequest(
                22u, signSelected, TransformGizmoOperation::Rotate)).valid(),
            "Quaternion-sign no-op gesture should begin");
    TransformState opposite = transformState(signSelected);
    opposite.rotation = {-opposite.rotation.w, -opposite.rotation.x,
                         -opposite.rotation.y, -opposite.rotation.z};
    require(signAdapter.update(updateRequest(22u, signSelected, opposite)).valid(),
            "The opposite sign of the same yaw should canonicalize");
    const auto noOp = signAdapter.commit(22u);
    require(noOp.valid() && !noOp.active && !noOp.command &&
                noOp.canonicalPreview.has_value(),
            "q and -q must end as a clean no-op without a journal command");
}

void testUniformScaleAndInvalidScale()
{
    const PlaceableInstance selected = makePlaceable("scale");
    TransformGizmoAdapter adapter{};
    require(adapter.begin(beginRequest(
                31u, selected, TransformGizmoOperation::Scale,
                engine::editor::placeableSnapProfile(std::nullopt, true))).valid(),
            "Scale gesture should require and accept the typed scale profile");

    TransformState proposed{};
    proposed.translation =
        glm::vec3(std::numeric_limits<float>::quiet_NaN());
    proposed.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    proposed.scale = {1.04f, 1.17f, 1.29f};
    const auto updated = adapter.update(updateRequest(31u, selected, proposed));
    const PlaceableInstance& preview = previewOf(
        updated, "Scale update should return a canonical preview");
    require(updated.valid() && vec3NearlyEqual(preview.scale, {1.2f, 1.2f, 1.2f}) &&
                vec3NearlyEqual(preview.position, selected.position) &&
                nearlyEqual(glm::abs(glm::dot(preview.rotation,
                                               selected.rotation)),
                            1.0f),
            "Scale should snap the mean uniformly and isolate position/rotation");

    TransformState invalid = proposed;
    invalid.scale = {1.0f, -0.5f, 1.0f};
    const auto rejected = adapter.update(updateRequest(31u, selected, invalid));
    require(rejected.error == TransformGizmoError::InvalidTransform &&
                rejected.transformError ==
                    engine::game::PlaceableTransformError::NonPositiveScale &&
                rejected.active &&
                vec3NearlyEqual(
                    previewOf(rejected,
                              "Rejected scale should retain latest preview")
                        .scale,
                    preview.scale),
            "An invalid scale should fail closed without losing the valid preview");
    require(adapter.commit(31u).command.has_value(),
            "A rejected update should not erase the prior valid scale edit");

    TransformGizmoAdapter missingScaleProfile{};
    require(missingScaleProfile
                    .begin(beginRequest(32u, selected,
                                        TransformGizmoOperation::Scale))
                    .error == TransformGizmoError::InvalidSnapProfile,
            "Scale should reject a placeable profile with scaling disabled");
}

void testInvalidSelectionIdentityProfileAndTransform()
{
    const PlaceableInstance selected = makePlaceable("validation");

    TransformGizmoAdapter adapter{};
    TransformGizmoBeginRequest request = beginRequest(
        41u, selected, TransformGizmoOperation::Translate);
    request.target = {};
    require(adapter.begin(request).error ==
                TransformGizmoError::InvalidSelection,
            "An empty selection target should fail closed");

    request = beginRequest(42u, selected, TransformGizmoOperation::Translate);
    request.target = SelectionTarget::terrainCell("terrain", {0, 0, 0});
    require(adapter.begin(request).error ==
                TransformGizmoError::UnsupportedTarget,
            "The placeable adapter should reject terrain and volume targets");

    request = beginRequest(43u, selected, TransformGizmoOperation::Translate);
    request.target = SelectionTarget::placeable("another");
    require(adapter.begin(request).error ==
                TransformGizmoError::SelectionMismatch,
            "The selected UUID must match the stable selection target");

    PlaceableInstance emptyUuid = selected;
    emptyUuid.uuid.clear();
    require(adapter.begin(beginRequest(
                44u, emptyUuid, TransformGizmoOperation::Translate))
                .error == TransformGizmoError::InvalidSelection,
            "An empty UUID should fail closed through the stable selection boundary");

    PlaceableInstance invalidIdentity = selected;
    invalidIdentity.prototypeSlug.clear();
    require(adapter.begin(beginRequest(
                45u, invalidIdentity, TransformGizmoOperation::Translate))
                .error == TransformGizmoError::InvalidPlaceableIdentity,
            "An empty prototype identity should fail before a gesture begins");
    invalidIdentity = selected;
    invalidIdentity.prototypeVersion = 0;
    require(adapter.begin(beginRequest(
                46u, invalidIdentity, TransformGizmoOperation::Translate))
                .error == TransformGizmoError::InvalidPlaceableIdentity,
            "A zero prototype version should fail before a gesture begins");

    request = beginRequest(461u, selected,
                           static_cast<TransformGizmoOperation>(255));
    require(adapter.begin(request).error ==
                TransformGizmoError::InvalidOperation,
            "An out-of-domain operation should fail before a gesture begins");

    auto invalidProfile = engine::editor::placeableSnapProfile();
    invalidProfile.target = TransformSnapTarget::TerrainCell;
    require(adapter
                    .begin(beginRequest(47u, selected,
                                        TransformGizmoOperation::Translate,
                                        invalidProfile))
                    .error == TransformGizmoError::InvalidSnapProfile,
            "A non-placeable snap profile should fail closed");
    invalidProfile = engine::editor::placeableSnapProfile();
    invalidProfile.translationStep.x = 0.5f;
    const auto malformedProfile = adapter.begin(beginRequest(
        48u, selected, TransformGizmoOperation::Translate, invalidProfile));
    require(malformedProfile.error == TransformGizmoError::InvalidSnapProfile &&
                malformedProfile.snapError ==
                    engine::editor::TransformSnapError::InvalidProfile,
            "A malformed placeable profile should preserve the typed snap error");

    PlaceableInstance invalidTransform = selected;
    invalidTransform.position.x = std::numeric_limits<float>::quiet_NaN();
    const auto rejectedTransform = adapter.begin(beginRequest(
        49u, invalidTransform, TransformGizmoOperation::Translate));
    require(rejectedTransform.error == TransformGizmoError::InvalidTransform &&
                rejectedTransform.snapError ==
                    engine::editor::TransformSnapError::InvalidTransform &&
                rejectedTransform.transformError ==
                    engine::game::PlaceableTransformError::NonFinitePosition,
            "Invalid baseline transforms should preserve their typed transform failure");
}

void testSingleMoveAndJournalRoundTrip()
{
    const PlaceableInstance selected = makePlaceable("journal-target");
    const PlaceableInstance sibling = makePlaceable("sibling");
    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = false;
    scene.placeables = {selected, sibling};
    const engine::game::PlaceableCollectionSnapshot original =
        engine::game::capturePlaceableCollection(scene);

    TransformGizmoAdapter adapter{};
    require(adapter.begin(beginRequest(
                51u, selected, TransformGizmoOperation::Translate)).valid(),
            "Journal integration gesture should begin");
    TransformState proposed = transformState(selected);
    proposed.translation = {3.12f, 999.0f, -4.88f};
    require(adapter.update(updateRequest(51u, selected, proposed)).valid(),
            "Journal integration gesture should update");
    require(engine::game::placeableCollectionExactlyEqual(
                original, engine::game::capturePlaceableCollection(scene)),
            "Begin and update must not mutate the scene collection");

    const auto committed = adapter.commit(51u);
    require(committed.valid() && committed.command.has_value() &&
                engine::game::placeableCollectionExactlyEqual(
                    original,
                    engine::game::capturePlaceableCollection(scene)),
            "Commit should emit one command without applying it to the scene");
    require(adapter.commit(51u).error ==
                TransformGizmoError::NoActiveGesture,
            "A committed gesture cannot emit a second Move command");

    const PlaceableEditCommand& command = *committed.command;
    require(command.op == PlaceableEditCommand::Op::Move &&
                command.before.has_value() && command.after.has_value() &&
                command.before->uuid == selected.uuid &&
                command.after->uuid == selected.uuid &&
                vec3NearlyEqual(command.after->position,
                                {3.0f, selected.position.y, -5.0f}),
            "Commit should produce the exact canonical single-target Move");

    engine::game::PlaceableEditJournal journal{};
    const engine::game::PreparedPlaceableEdit execute =
        journal.beginExecute(scene, command);
    require(execute.valid &&
                engine::game::placeableCollectionExactlyEqual(
                    original,
                    engine::game::capturePlaceableCollection(scene)),
            "Journal preparation should also leave the authored scene untouched");
    require(journal.apply(scene, execute) && journal.commit(execute, scene),
            "The emitted Move should execute through the authoritative journal");
    const engine::game::PlaceableCollectionSnapshot moved =
        engine::game::capturePlaceableCollection(scene);
    require(moved.placeables.size() == 2u &&
                moved.placeables[0].uuid == selected.uuid &&
                moved.placeables[1].uuid == sibling.uuid &&
                vec3NearlyEqual(moved.placeables[0].position,
                                command.after->position),
            "Journal execution should replace in place without reordering siblings");

    const engine::game::PreparedPlaceableEdit undo = journal.beginUndo(scene);
    require(undo.valid && journal.apply(scene, undo) &&
                journal.commit(undo, scene) &&
                engine::game::placeableCollectionExactlyEqual(
                    original,
                    engine::game::capturePlaceableCollection(scene)),
            "Undo should restore the exact pre-gesture collection");
    const engine::game::PreparedPlaceableEdit redo = journal.beginRedo(scene);
    require(redo.valid && journal.apply(scene, redo) &&
                journal.commit(redo, scene) &&
                engine::game::placeableCollectionExactlyEqual(
                    moved, engine::game::capturePlaceableCollection(scene)),
            "Redo should restore the exact committed transform and ordering");
}

void testErrorLabelsAreComplete()
{
    const std::vector<TransformGizmoError> errors = {
        TransformGizmoError::None,
        TransformGizmoError::InvalidGestureId,
        TransformGizmoError::GestureAlreadyActive,
        TransformGizmoError::NoActiveGesture,
        TransformGizmoError::StaleGesture,
        TransformGizmoError::InvalidSelection,
        TransformGizmoError::UnsupportedTarget,
        TransformGizmoError::SelectionMismatch,
        TransformGizmoError::InvalidPlaceableIdentity,
        TransformGizmoError::InvalidOperation,
        TransformGizmoError::InvalidSnapProfile,
        TransformGizmoError::InvalidTransform,
        TransformGizmoError::UnsupportedRotation,
    };
    for (const TransformGizmoError error : errors)
    {
        require(std::string(engine::editor::transformGizmoErrorLabel(error)) !=
                    "unknown",
                "Every public gizmo error should have a runtime label");
    }
}

} // namespace

int main()
{
    try
    {
        testGestureLifecycleAndStaleRequests();
        testTranslationSnappingAndUntouchedChannelIsolation();
        testYawOnlyRotationAndQuaternionNoOp();
        testUniformScaleAndInvalidScale();
        testInvalidSelectionIdentityProfileAndTransform();
        testSingleMoveAndJournalRoundTrip();
        testErrorLabelsAreComplete();
        std::cout << "Transform gizmo adapter tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Transform gizmo adapter test failure: " << error.what()
                  << "\n";
        return 1;
    }
}
