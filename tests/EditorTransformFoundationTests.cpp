#include "engine/editor/SceneDocument.h"
#include "engine/editor/TransformSnapping.h"
#include "engine/game/PlaceableEditJournal.h"
#include "engine/game/PlaceableTransform.h"
#include "engine/scene/SceneConfig.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

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

bool quatNearlyEqual(const glm::quat& lhs, const glm::quat& rhs,
                     float epsilon = 1e-5f)
{
    return nearlyEqual(lhs.w, rhs.w, epsilon) &&
           nearlyEqual(lhs.x, rhs.x, epsilon) &&
           nearlyEqual(lhs.y, rhs.y, epsilon) &&
           nearlyEqual(lhs.z, rhs.z, epsilon);
}

PlaceableInstance makePlaceable(std::string uuid, float x)
{
    PlaceableInstance instance{};
    instance.uuid = std::move(uuid);
    instance.prototypeSlug = "eelgrass";
    instance.prototypeVersion = 1;
    instance.position = glm::vec3(x, 2.0f, 3.0f);
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = glm::vec3(1.0f);
    instance.seed = static_cast<uint32_t>(100.0f + x);
    return instance;
}

SceneConfig makeExplicitScene(std::vector<PlaceableInstance> placeables)
{
    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = false;
    scene.placeables = std::move(placeables);
    return scene;
}

PlaceableEditCommand makeMove(const PlaceableInstance& before,
                              const PlaceableInstance& after)
{
    PlaceableEditCommand command{};
    command.op = PlaceableEditCommand::Op::Move;
    command.before = before;
    command.after = after;
    return command;
}

void applyAndCommit(engine::game::PlaceableEditJournal& journal,
                    SceneConfig& scene,
                    const engine::game::PreparedPlaceableEdit& edit)
{
    require(edit.valid, "Expected a valid prepared placeable edit");
    require(journal.apply(scene, edit),
            "Prepared placeable edit should apply to its exact before snapshot");
    require(journal.commit(edit, scene),
            "Applied placeable edit should commit after projection succeeds");
}

void testPlaceableTransformValidationAndCanonicalization()
{
    PlaceableInstance input = makePlaceable("transform", 1.0f);
    input.position = glm::vec3(-0.0f, 4.0f, -3.0f);
    input.rotation = glm::quat(-2.0f, 0.0f, -2.0f, 0.0f);
    input.scale = glm::vec3(0.25f, 1.5f, 3.75f);

    const engine::game::PlaceableTransformValidation validation =
        engine::game::validatePlaceableTransform(input);
    require(validation.valid(),
            "Finite transforms with positive nonuniform scale should validate");
    require(validation.canonical.uuid == input.uuid &&
                validation.canonical.prototypeSlug == input.prototypeSlug &&
                validation.canonical.prototypeVersion == input.prototypeVersion &&
                validation.canonical.seed == input.seed,
            "Transform validation must preserve persistent identity fields");
    require(vec3NearlyEqual(validation.canonical.scale, input.scale),
            "Canonicalization must preserve positive nonuniform scale");
    require(nearlyEqual(validation.canonical.rotation.w,
                        0.70710678f) &&
                nearlyEqual(validation.canonical.rotation.y, 0.70710678f),
            "Canonicalization should normalize rotation and choose one q/-q sign");

    PlaceableInstance oppositeSign = validation.canonical;
    oppositeSign.rotation = glm::quat(-oppositeSign.rotation.w,
                                      -oppositeSign.rotation.x,
                                      -oppositeSign.rotation.y,
                                      -oppositeSign.rotation.z);
    require(engine::game::placeableTransformEquivalent(validation.canonical,
                                                        oppositeSign),
            "Quaternion sign should not change transform equivalence");

    PlaceableInstance invalid = input;
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::NonFinitePosition,
            "Non-finite position should fail closed");

    invalid = input;
    invalid.rotation.w = std::numeric_limits<float>::infinity();
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::NonFiniteRotation,
            "Non-finite rotation should fail closed");

    invalid = input;
    invalid.rotation = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::DegenerateRotation,
            "A zero-length quaternion should fail closed");

    invalid = input;
    invalid.scale.z = std::numeric_limits<float>::infinity();
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::NonFiniteScale,
            "Non-finite scale should fail closed");

    invalid = input;
    invalid.scale.y = 0.0f;
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::NonPositiveScale,
            "Zero scale should fail closed");

    invalid.scale.y = -0.5f;
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::NonPositiveScale,
            "Negative scale should fail closed");

    invalid = input;
    invalid.scale.x = std::numeric_limits<float>::denorm_min();
    require(engine::game::validatePlaceableTransform(invalid).error ==
                engine::game::PlaceableTransformError::NonInvertibleScale,
            "A positive scale whose reciprocal overflows should fail closed");
}

glm::quat axisRotation(const glm::vec3& axis, float radians)
{
    const float halfAngle = radians * 0.5f;
    const float sine = std::sin(halfAngle);
    return glm::quat(std::cos(halfAngle), axis.x * sine, axis.y * sine,
                     axis.z * sine);
}

void testPlaceableRotationCompositionOrder()
{
    const glm::quat authored =
        axisRotation(glm::vec3(0.0f, 1.0f, 0.0f), 1.10f);
    const glm::quat animationOffset =
        axisRotation(glm::vec3(1.0f, 0.0f, 0.0f), -0.45f);
    const glm::quat seededVariation =
        axisRotation(glm::vec3(0.0f, 0.0f, 1.0f), 0.32f);

    const engine::game::PlaceableRotationComposition composed =
        engine::game::composePlaceableRotation(
            authored, animationOffset, seededVariation);
    require(composed.valid(),
            "Finite non-degenerate rotation inputs should compose");

    PlaceableInstance expected = makePlaceable("expected", 0.0f);
    expected.rotation = animationOffset * authored * seededVariation;
    const engine::game::PlaceableTransformValidation expectedValidation =
        engine::game::validatePlaceableTransform(expected);
    require(expectedValidation.valid() &&
                quatNearlyEqual(composed.canonical,
                                expectedValidation.canonical.rotation),
            "Composition must remain animationOffset * authored * seededVariation");

    PlaceableInstance wrongOrder = expected;
    wrongOrder.rotation = authored * animationOffset * seededVariation;
    const engine::game::PlaceableTransformValidation wrongValidation =
        engine::game::validatePlaceableTransform(wrongOrder);
    require(wrongValidation.valid() &&
                !quatNearlyEqual(composed.canonical,
                                 wrongValidation.canonical.rotation, 1e-4f),
            "Non-commuting authored and animation axes should detect order drift");

    const engine::game::PlaceableRotationComposition degenerate =
        engine::game::composePlaceableRotation(
            glm::quat(0.0f, 0.0f, 0.0f, 0.0f), animationOffset,
            seededVariation);
    require(!degenerate.valid() &&
                degenerate.error ==
                    engine::game::PlaceableTransformError::DegenerateRotation,
            "A degenerate composition input should fail closed");

    glm::quat nonFinite = animationOffset;
    nonFinite.x = std::numeric_limits<float>::quiet_NaN();
    const engine::game::PlaceableRotationComposition invalid =
        engine::game::composePlaceableRotation(authored, nonFinite,
                                               seededVariation);
    require(!invalid.valid() &&
                invalid.error ==
                    engine::game::PlaceableTransformError::NonFiniteRotation,
            "A non-finite composition input should fail closed");
}

void testMoveUndoRedoPreservesOrderAndSavepoint()
{
    const PlaceableInstance first = makePlaceable("first", 1.0f);
    const PlaceableInstance middle = makePlaceable("middle", 2.0f);
    const PlaceableInstance last = makePlaceable("last", 3.0f);
    SceneConfig scene = makeExplicitScene({first, middle, last});

    PlaceableInstance moved = middle;
    moved.position = glm::vec3(9.25f, 4.5f, -2.75f);
    moved.rotation = glm::quat(1.847759f, 0.0f, 0.7653669f, 0.0f);
    moved.scale = glm::vec3(0.5f, 1.25f, 2.0f);

    engine::game::PlaceableEditJournal journal{};
    const engine::game::PreparedPlaceableEdit execute =
        journal.beginExecute(scene, makeMove(middle, moved));
    require(execute.valid && journal.hasPendingEdit() && journal.size() == 0 &&
                journal.cursor() == 0 && !journal.dirty(),
            "Preparing an edit must not advance history or dirty the savepoint");
    applyAndCommit(journal, scene, execute);

    require(scene.placeables.size() == 3 &&
                scene.placeables[0].uuid == "first" &&
                scene.placeables[1].uuid == "middle" &&
                scene.placeables[2].uuid == "last",
            "Move should replace the target in place without changing collection order");
    require(vec3NearlyEqual(scene.placeables[1].position, moved.position) &&
                vec3NearlyEqual(scene.placeables[1].scale, moved.scale),
            "Move should apply position and positive nonuniform scale together");
    const engine::game::PlaceableTransformValidation movedValidation =
        engine::game::validatePlaceableTransform(moved);
    require(movedValidation.valid() &&
                quatNearlyEqual(scene.placeables[1].rotation,
                                movedValidation.canonical.rotation),
            "Move should store the canonical authored rotation");
    require(journal.canUndo() && !journal.canRedo() && journal.dirty(),
            "Committed execute should advance undo history and mark the document dirty");
    const PlaceableEditCommand* nextUndo = journal.nextUndoCommand();
    require(nextUndo != nullptr && nextUndo->op == PlaceableEditCommand::Op::Move &&
                nextUndo->after.has_value() &&
                quatNearlyEqual(nextUndo->after->rotation,
                                scene.placeables[1].rotation),
            "The journal should expose its canonical next undo command read-only");

    journal.markSaved();
    require(!journal.dirty(), "markSaved should capture the current history cursor");

    const engine::game::PreparedPlaceableEdit undo = journal.beginUndo(scene);
    applyAndCommit(journal, scene, undo);
    require(scene.placeables.size() == 3 &&
                scene.placeables[0].uuid == "first" &&
                scene.placeables[1].uuid == "middle" &&
                scene.placeables[2].uuid == "last" &&
                vec3NearlyEqual(scene.placeables[1].position, middle.position) &&
                quatNearlyEqual(scene.placeables[1].rotation, middle.rotation) &&
                vec3NearlyEqual(scene.placeables[1].scale, middle.scale),
            "Undo should restore the exact prior transform and collection order");
    require(journal.canRedo() && journal.dirty(),
            "Undoing away from the saved cursor should expose redo and become dirty");
    const PlaceableEditCommand* nextRedo = journal.nextRedoCommand();
    require(nextRedo != nullptr && nextRedo->after.has_value() &&
                vec3NearlyEqual(nextRedo->after->position, moved.position),
            "The journal should expose the canonical command at the redo cursor");

    const engine::game::PreparedPlaceableEdit redo = journal.beginRedo(scene);
    applyAndCommit(journal, scene, redo);
    require(vec3NearlyEqual(scene.placeables[1].position, moved.position) &&
                vec3NearlyEqual(scene.placeables[1].scale, moved.scale) &&
                !journal.dirty(),
            "Redo should restore the saved transform and saved-cursor cleanliness");

    const engine::game::PreparedPlaceableEdit undoForBranch =
        journal.beginUndo(scene);
    applyAndCommit(journal, scene, undoForBranch);
    PlaceableInstance branch = middle;
    branch.position.x = -7.0f;
    const engine::game::PreparedPlaceableEdit branchEdit =
        journal.beginExecute(scene, makeMove(middle, branch));
    applyAndCommit(journal, scene, branchEdit);
    require(!journal.canRedo() && journal.size() == 1 && journal.cursor() == 1 &&
                journal.dirty(),
            "Executing after undo should truncate redo and invalidate an unreachable savepoint");
}

void testPendingEditRollbackAndStaleProtection()
{
    const PlaceableInstance original = makePlaceable("target", 1.0f);
    SceneConfig scene = makeExplicitScene({original});
    PlaceableInstance moved = original;
    moved.position.x = 8.0f;

    engine::game::PlaceableEditJournal journal{};
    const engine::game::PreparedPlaceableEdit pending =
        journal.beginExecute(scene, makeMove(original, moved));
    require(pending.valid && journal.apply(scene, pending),
            "A valid pending edit should apply before external projection");
    require(journal.size() == 0 && journal.cursor() == 0 && !journal.dirty(),
            "Applied but uncommitted edits must not enter history");
    require(journal.restore(scene, pending) && journal.cancel(pending),
            "Failed projection should restore the exact before snapshot and cancel the ticket");
    require(vec3NearlyEqual(scene.placeables.front().position, original.position) &&
                !journal.canUndo() && !journal.dirty(),
            "Cancelled projection must leave scene and history unchanged");

    PlaceableInstance staleBefore = original;
    staleBefore.position.x += 0.25f;
    const engine::game::PreparedPlaceableEdit stale =
        journal.beginExecute(scene, makeMove(staleBefore, moved));
    require(!stale.valid &&
                stale.error ==
                    engine::game::PlaceableEditJournalError::StaleBeforeState &&
                vec3NearlyEqual(scene.placeables.front().position, original.position),
            "A stale before snapshot should be rejected without mutation");

    const engine::game::PreparedPlaceableEdit valid =
        journal.beginExecute(scene, makeMove(original, moved));
    require(valid.valid, "A fresh move should prepare after stale rejection");
    scene.placeables.front().position.z += 1.0f;
    require(!journal.apply(scene, valid),
            "A scene changed after prepare should reject application");
    scene.placeables.front() = original;
    require(journal.cancel(valid), "The stale pending ticket should remain cancellable");
}

void testAddRemoveUndoRedoRestoresExactOrder()
{
    const PlaceableInstance first = makePlaceable("first", 1.0f);
    const PlaceableInstance middle = makePlaceable("middle", 2.0f);
    const PlaceableInstance last = makePlaceable("last", 3.0f);
    SceneConfig scene = makeExplicitScene({first, middle, last});
    engine::game::PlaceableEditJournal journal{};

    PlaceableEditCommand remove{};
    remove.op = PlaceableEditCommand::Op::Remove;
    remove.before = middle;
    applyAndCommit(journal, scene, journal.beginExecute(scene, remove));
    require(scene.placeables.size() == 2 &&
                scene.placeables[0].uuid == "first" &&
                scene.placeables[1].uuid == "last",
            "Remove should close only the target slot");

    applyAndCommit(journal, scene, journal.beginUndo(scene));
    require(scene.placeables.size() == 3 &&
                scene.placeables[0].uuid == "first" &&
                scene.placeables[1].uuid == "middle" &&
                scene.placeables[2].uuid == "last",
            "Remove undo should restore the target at its exact original index");
    applyAndCommit(journal, scene, journal.beginRedo(scene));
    require(scene.placeables.size() == 2 &&
                scene.placeables[0].uuid == "first" &&
                scene.placeables[1].uuid == "last",
            "Remove redo should reproduce the exact post-remove order");

    applyAndCommit(journal, scene, journal.beginUndo(scene));
    PlaceableInstance appended = makePlaceable("appended", 4.0f);
    PlaceableEditCommand add{};
    add.op = PlaceableEditCommand::Op::Add;
    add.after = appended;
    applyAndCommit(journal, scene, journal.beginExecute(scene, add));
    require(scene.placeables.size() == 4 &&
                scene.placeables[0].uuid == "first" &&
                scene.placeables[1].uuid == "middle" &&
                scene.placeables[2].uuid == "last" &&
                scene.placeables[3].uuid == "appended",
            "Add should preserve existing order and append its new stable identity");
    applyAndCommit(journal, scene, journal.beginUndo(scene));
    applyAndCommit(journal, scene, journal.beginRedo(scene));
    require(scene.placeables.size() == 4 &&
                scene.placeables[1].uuid == "middle" &&
                scene.placeables[3].uuid == "appended",
            "Add undo/redo should restore its exact before/after collection snapshots");

    PlaceableInstance migrated = scene.placeables[3];
    migrated.prototypeVersion = 2;
    PlaceableEditCommand migrate{};
    migrate.op = PlaceableEditCommand::Op::MigratePrototypeVersion;
    migrate.before = scene.placeables[3];
    migrate.after = migrated;
    applyAndCommit(journal, scene, journal.beginExecute(scene, migrate));
    require(scene.placeables[3].uuid == "appended" &&
                scene.placeables[3].prototypeVersion == 2,
            "Prototype migration should replace its target in place");
    applyAndCommit(journal, scene, journal.beginUndo(scene));
    require(scene.placeables[3].prototypeVersion == 1,
            "Prototype migration undo should restore the exact prior version");
}

void testInvalidAndDuplicateCommandsFailClosed()
{
    const PlaceableInstance original = makePlaceable("target", 1.0f);
    engine::game::PlaceableEditJournal journal{};

    SceneConfig duplicateScene = makeExplicitScene({original, original});
    PlaceableInstance moved = original;
    moved.position.x = 3.0f;
    const engine::game::PreparedPlaceableEdit duplicate =
        journal.beginExecute(duplicateScene, makeMove(original, moved));
    require(!duplicate.valid &&
                duplicate.error ==
                    engine::game::PlaceableEditJournalError::DuplicateUuid &&
                duplicateScene.placeables.size() == 2,
            "Ambiguous duplicate UUID collections should fail without mutation");

    SceneConfig scene = makeExplicitScene({original});
    PlaceableInstance invalidAfter = moved;
    invalidAfter.scale = glm::vec3(1.0f, -1.0f, 2.0f);
    const engine::game::PreparedPlaceableEdit invalid =
        journal.beginExecute(scene, makeMove(original, invalidAfter));
    require(!invalid.valid &&
                invalid.error ==
                    engine::game::PlaceableEditJournalError::InvalidTransform &&
                invalid.transformError ==
                    engine::game::PlaceableTransformError::NonPositiveScale &&
                scene.placeables.front().scale == original.scale,
            "Invalid requested transforms should fail closed with a typed reason");

    PlaceableEditCommand malformed{};
    malformed.op = PlaceableEditCommand::Op::Move;
    malformed.after = moved;
    const engine::game::PreparedPlaceableEdit invalidShape =
        journal.beginExecute(scene, malformed);
    require(!invalidShape.valid &&
                invalidShape.error ==
                    engine::game::PlaceableEditJournalError::InvalidCommandShape,
            "Move commands require complete before and after snapshots");

    const engine::game::PreparedPlaceableEdit noChange =
        journal.beginExecute(scene, makeMove(original, original));
    require(!noChange.valid &&
                noChange.error == engine::game::PlaceableEditJournalError::NoChange,
            "Semantically empty transform commands should not enter history");
}

void testImplicitDefaultsUndoRestoresExactCollectionState()
{
    const PlaceableInstance firstDefault = makePlaceable("default-first", 1.0f);
    const PlaceableInstance secondDefault = makePlaceable("default-second", 2.0f);
    const std::vector<PlaceableInstance> defaults{firstDefault, secondDefault};

    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = true;
    scene.placeables.clear();
    const engine::game::PlaceableCollectionSnapshot loadedState =
        engine::game::capturePlaceableCollection(scene);

    PlaceableInstance moved = secondDefault;
    moved.position.z = -9.0f;
    moved.rotation = glm::quat(0.9238795f, 0.0f, 0.3826834f, 0.0f);
    moved.scale = glm::vec3(0.75f, 1.5f, 1.25f);

    engine::game::PlaceableEditJournal journal{};
    const engine::game::PreparedPlaceableEdit execute = journal.beginExecute(
        scene, makeMove(secondDefault, moved), defaults);
    applyAndCommit(journal, scene, execute);
    require(!scene.useDefaultPlaceables && scene.placeables.size() == 2 &&
                scene.placeables[0].uuid == "default-first" &&
                scene.placeables[1].uuid == "default-second" &&
                vec3NearlyEqual(scene.placeables[1].scale, moved.scale),
            "First edit should materialize implicit defaults and preserve their order");

    const engine::game::PreparedPlaceableEdit undo = journal.beginUndo(scene);
    applyAndCommit(journal, scene, undo);
    require(engine::game::placeableCollectionExactlyEqual(
                engine::game::capturePlaceableCollection(scene), loadedState) &&
                !journal.dirty(),
            "Undo should restore the exact implicit-default flag and empty stored vector");

    const engine::game::PreparedPlaceableEdit redo = journal.beginRedo(scene);
    applyAndCommit(journal, scene, redo);
    require(!scene.useDefaultPlaceables && scene.placeables.size() == 2 &&
                vec3NearlyEqual(scene.placeables[1].position, moved.position),
            "Redo should restore the exact materialized edited collection");

    engine::game::PlaceableEditJournal missingDefaultsJournal{};
    SceneConfig implicitScene{};
    implicitScene.useDefaultPlaceables = true;
    implicitScene.placeables.clear();
    const engine::game::PreparedPlaceableEdit missingDefaults =
        missingDefaultsJournal.beginExecute(
            implicitScene, makeMove(secondDefault, moved));
    require(!missingDefaults.valid &&
                missingDefaults.error ==
                    engine::game::PlaceableEditJournalError::ImplicitDefaultsUnavailable,
            "Implicit collections must not silently materialize as empty when defaults are unavailable");
}

void testTypedTransformSnappingAndChannelIsolation()
{
    using engine::editor::TransformChannel;
    using engine::editor::TransformState;

    TransformState input{};
    input.translation = glm::vec3(1.49f, -1.51f, 2.51f);
    input.rotation = glm::angleAxis(glm::radians(37.0f),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
    input.scale = glm::vec3(0.75f, 1.25f, 2.0f);

    const engine::editor::TransformSnapResult terrainTranslation =
        engine::editor::snapTransform(
            input, engine::editor::terrainCellSnapProfile(),
            TransformChannel::Translation);
    require(terrainTranslation.valid() &&
                vec3NearlyEqual(terrainTranslation.value.translation,
                                glm::vec3(1.0f, -2.0f, 3.0f)) &&
                quatNearlyEqual(terrainTranslation.value.rotation,
                                input.rotation) &&
                vec3NearlyEqual(terrainTranslation.value.scale, input.scale),
            "Terrain translation snapping should use unit cells without changing unrequested channels");

    const engine::editor::TransformSnapResult terrainRotation =
        engine::editor::snapTransform(
            input, engine::editor::terrainCellSnapProfile(),
            TransformChannel::Rotation);
    require(terrainRotation.valid() &&
                vec3NearlyEqual(terrainRotation.value.translation,
                                input.translation) &&
                quatNearlyEqual(terrainRotation.value.rotation,
                                glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) &&
                vec3NearlyEqual(terrainRotation.value.scale, input.scale),
            "Terrain rotation should become identity only when rotation is requested");

    const engine::editor::TransformSnapResult terrainScale =
        engine::editor::snapTransform(
            input, engine::editor::terrainCellSnapProfile(),
            TransformChannel::Scale);
    require(terrainScale.valid() &&
                vec3NearlyEqual(terrainScale.value.translation,
                                input.translation) &&
                quatNearlyEqual(terrainScale.value.rotation, input.rotation) &&
                vec3NearlyEqual(terrainScale.value.scale, glm::vec3(1.0f)),
            "Terrain scale should become identity only when scale is requested");

    const engine::editor::TransformSnapResult freeform =
        engine::editor::snapTransform(
            input, engine::editor::freeformSnapProfile(),
            TransformChannel::All);
    require(freeform.valid() &&
                vec3NearlyEqual(freeform.value.translation, input.translation) &&
                quatNearlyEqual(freeform.value.rotation, input.rotation) &&
                vec3NearlyEqual(freeform.value.scale, input.scale),
            "Freeform targets should validate but never snap any transform channel");
}

void testVoxelAndPlaceableSnapProfiles()
{
    using engine::editor::TransformChannel;
    using engine::editor::TransformState;

    TransformState volume{};
    volume.translation = glm::vec3(0.31f, 0.46f, 0.74f);
    volume.rotation = glm::angleAxis(glm::radians(100.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
    volume.scale = glm::vec3(0.8f, 1.1f, 1.4f);
    const engine::editor::TransformSnapProfile volumeProfile =
        engine::editor::voxelVolumeSnapProfile(
            glm::vec3(0.2f, 0.3f, 0.5f), true);

    const engine::editor::TransformSnapResult volumeTranslation =
        engine::editor::snapTransform(volume, volumeProfile,
                                      TransformChannel::Translation |
                                          TransformChannel::Rotation);
    const glm::quat expectedQuarterTurn = glm::angleAxis(
        glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    require(volumeTranslation.valid() &&
                vec3NearlyEqual(volumeTranslation.value.translation,
                                glm::vec3(0.4f, 0.6f, 0.5f)) &&
                quatNearlyEqual(volumeTranslation.value.rotation,
                                expectedQuarterTurn) &&
                vec3NearlyEqual(volumeTranslation.value.scale, volume.scale),
            "Voxel volumes should snap to their cell pitch and quarter-turn yaw while preserving unrequested nonuniform scale");

    const engine::editor::TransformSnapResult volumeScale =
        engine::editor::snapTransform(volume, volumeProfile,
                                      TransformChannel::Scale);
    require(volumeScale.valid() &&
                vec3NearlyEqual(volumeScale.value.scale, glm::vec3(1.1f)) &&
                vec3NearlyEqual(volumeScale.value.translation,
                                volume.translation) &&
                quatNearlyEqual(volumeScale.value.rotation, volume.rotation),
            "Requested voxel-volume scale should snap the mean multiplier uniformly to 0.1");

    TransformState placeable{};
    placeable.translation = glm::vec3(0.36f, 7.3f, -0.36f);
    placeable.rotation = glm::angleAxis(glm::radians(22.0f),
                                        glm::vec3(0.0f, 1.0f, 0.0f));
    placeable.scale = glm::vec3(0.91f, 1.08f, 1.19f);
    const engine::editor::TransformSnapProfile placeableProfile =
        engine::editor::placeableSnapProfile(4.25f, true);
    const engine::editor::TransformSnapResult snappedPlaceable =
        engine::editor::snapTransform(placeable, placeableProfile,
                                      TransformChannel::All);
    const glm::quat expectedFifteenDegrees = glm::angleAxis(
        glm::radians(15.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    require(snappedPlaceable.valid() &&
                vec3NearlyEqual(snappedPlaceable.value.translation,
                                glm::vec3(0.25f, 4.25f, -0.25f)) &&
                quatNearlyEqual(snappedPlaceable.value.rotation,
                                expectedFifteenDegrees) &&
                vec3NearlyEqual(snappedPlaceable.value.scale,
                                glm::vec3(1.1f)),
            "Placeables should use quarter-unit XZ, optional surface Y, 15-degree yaw, and requested uniform scale snapping");

    const engine::editor::TransformSnapResult noSurfaceTranslation =
        engine::editor::snapTransform(
            placeable, engine::editor::placeableSnapProfile(),
            TransformChannel::Translation);
    require(noSurfaceTranslation.valid() &&
                nearlyEqual(noSurfaceTranslation.value.translation.y,
                            placeable.translation.y) &&
                vec3NearlyEqual(noSurfaceTranslation.value.scale,
                                placeable.scale),
            "Placeable Y and nonuniform scale should remain authored when their optional channels are not requested");

    const engine::editor::TransformSnapResult invalidProfile =
        engine::editor::snapTransform(
            volume,
            engine::editor::voxelVolumeSnapProfile(glm::vec3(0.2f, 0.0f,
                                                              0.5f)),
            TransformChannel::Translation);
    require(!invalidProfile.valid() &&
                invalidProfile.error ==
                    engine::editor::TransformSnapError::InvalidProfile,
            "A non-positive voxel cell pitch should fail closed");
}

void testPlaceableSnapAdapterPreservesIdentity()
{
    PlaceableInstance input = makePlaceable("snap-adapter", 0.36f);
    input.rotation = glm::quat(2.0f, 0.0f, 0.0f, 0.0f);
    input.scale = glm::vec3(0.8f, 1.1f, 1.4f);

    const engine::editor::PlaceableSnapResult result =
        engine::editor::snapPlaceableInstance(
            input, engine::editor::placeableSnapProfile(),
            engine::editor::TransformChannel::Translation);
    require(result.valid() && result.canonical.uuid == input.uuid &&
                result.canonical.prototypeSlug == input.prototypeSlug &&
                result.canonical.prototypeVersion == input.prototypeVersion &&
                result.canonical.seed == input.seed &&
                nearlyEqual(result.canonical.position.x, 0.25f) &&
                vec3NearlyEqual(result.canonical.scale, input.scale) &&
                quatNearlyEqual(result.canonical.rotation,
                                glm::quat(1.0f, 0.0f, 0.0f, 0.0f)),
            "The placeable adapter should preserve identity and unrequested nonuniform scale while canonicalizing rotation");
}

void testSceneDocumentPlayIsolationAndValidation()
{
    PlaceableInstance authoredPlaceable = makePlaceable("document", 1.25f);
    authoredPlaceable.rotation = glm::quat(-2.0f, 0.0f, -2.0f, 0.0f);
    authoredPlaceable.scale = glm::vec3(0.75f, 1.25f, 2.0f);
    SceneConfig authored = makeExplicitScene({authoredPlaceable});
    authored.name = "authored-level";
    authored.description = "stable authored description";
    authored.worldSeed = 777u;
    authored.gameState.progression.coins = 123u;

    engine::editor::SceneDocument document(authored);
    const engine::game::PlaceableTransformValidation expectedTransform =
        engine::game::validatePlaceableTransform(authoredPlaceable);
    require(expectedTransform.valid() &&
                quatNearlyEqual(document.authoredConfig().placeables[0].rotation,
                                expectedTransform.canonical.rotation) &&
                vec3NearlyEqual(document.authoredConfig().placeables[0].scale,
                                authoredPlaceable.scale),
            "Scene documents should canonicalize valid placeables without flattening nonuniform scale");

    require(document.beginPlaySnapshot() &&
                !document.beginPlaySnapshot() &&
                document.playSnapshotActive(),
            "A scene document should create exactly one disposable play snapshot");
    SceneConfig* play = document.mutablePlaySnapshot();
    require(play != nullptr, "An active play snapshot should be mutable by runtime code");
    play->name = "runtime-mutated";
    play->description.clear();
    play->worldSeed = 999u;
    play->gameState.progression.coins = 0u;
    play->placeables[0].uuid = "runtime-only";
    play->placeables[0].position.x = -42.0f;

    require(document.authoredConfig().name == "authored-level" &&
                document.authoredConfig().description ==
                    "stable authored description" &&
                document.authoredConfig().worldSeed == 777u &&
                document.authoredConfig().gameState.progression.coins == 123u &&
                document.authoredConfig().placeables[0].uuid == "document" &&
                nearlyEqual(document.authoredConfig().placeables[0].position.x,
                            1.25f),
            "Runtime mutation of the play copy must not alias authored scene state");

    SceneConfig replacement = authored;
    replacement.name = "replacement-during-play";
    const engine::editor::SceneDocumentValidation activeReplacement =
        document.replaceAuthoredConfig(replacement);
    require(!activeReplacement.valid() &&
                activeReplacement.error ==
                    engine::editor::SceneDocumentError::PlaySnapshotActive,
            "Authored replacement should not change the play-start baseline while a snapshot is active");

    const SceneConfig restored = document.endPlayAndRestoreAuthored();
    require(!document.playSnapshotActive() && document.playSnapshot() == nullptr &&
                restored.name == "authored-level" &&
                restored.description == "stable authored description" &&
                restored.worldSeed == 777u &&
                restored.gameState.progression.coins == 123u &&
                restored.placeables[0].uuid == "document" &&
                nearlyEqual(restored.placeables[0].position.x, 1.25f) &&
                vec3NearlyEqual(restored.placeables[0].scale,
                                authoredPlaceable.scale),
            "Stopping play should discard runtime changes and return the exact canonical authored state");

    SceneConfig invalid = restored;
    invalid.placeables[0].scale.y = 0.0f;
    const engine::editor::SceneDocumentValidation invalidReplacement =
        document.replaceAuthoredConfig(invalid);
    require(!invalidReplacement.valid() &&
                invalidReplacement.error ==
                    engine::editor::SceneDocumentError::InvalidPlaceableTransform &&
                invalidReplacement.placeableIndex == 0 &&
                invalidReplacement.transformError ==
                    engine::game::PlaceableTransformError::NonPositiveScale &&
                document.authoredConfig().name == "authored-level" &&
                vec3NearlyEqual(document.authoredConfig().placeables[0].scale,
                                authoredPlaceable.scale),
            "Invalid authored replacement should fail transactionally with its exact placeable index and transform error");

    auto requireInvalidIdentity = [&](PlaceableInstance invalidPlaceable,
                                      const std::string& requirement) {
        SceneConfig invalidIdentity = restored;
        invalidIdentity.placeables.push_back(std::move(invalidPlaceable));
        const engine::editor::SceneDocumentValidation validation =
            document.replaceAuthoredConfig(invalidIdentity);
        require(!validation.valid() &&
                    validation.error ==
                        engine::editor::SceneDocumentError::InvalidPlaceableIdentity &&
                    validation.placeableIndex == 1 &&
                    document.authoredConfig().placeables.size() == 1,
                requirement);
    };
    PlaceableInstance emptyUuid = makePlaceable("invalid-uuid", 2.0f);
    emptyUuid.uuid.clear();
    requireInvalidIdentity(
        emptyUuid,
        "Scene documents should reject empty placeable UUIDs transactionally with their exact index");
    PlaceableInstance emptySlug = makePlaceable("invalid-slug", 2.0f);
    emptySlug.prototypeSlug.clear();
    requireInvalidIdentity(
        emptySlug,
        "Scene documents should reject empty prototype slugs transactionally with their exact index");
    PlaceableInstance versionZero = makePlaceable("invalid-version", 2.0f);
    versionZero.prototypeVersion = 0;
    requireInvalidIdentity(
        versionZero,
        "Scene documents should reject version-zero placeable identities transactionally with their exact index");

    SceneConfig duplicateIdentity = restored;
    duplicateIdentity.placeables.push_back(duplicateIdentity.placeables.front());
    duplicateIdentity.placeables.back().position.x = 3.0f;
    const engine::editor::SceneDocumentValidation duplicateReplacement =
        document.replaceAuthoredConfig(duplicateIdentity);
    require(!duplicateReplacement.valid() &&
                duplicateReplacement.error ==
                    engine::editor::SceneDocumentError::DuplicatePlaceableUuid &&
                duplicateReplacement.placeableIndex == 1 &&
                document.authoredConfig().placeables.size() == 1,
            "Scene documents should reject duplicate placeable UUIDs transactionally at the duplicate index");
}

} // namespace

int main()
{
    try
    {
        testPlaceableTransformValidationAndCanonicalization();
        testPlaceableRotationCompositionOrder();
        testMoveUndoRedoPreservesOrderAndSavepoint();
        testPendingEditRollbackAndStaleProtection();
        testAddRemoveUndoRedoRestoresExactOrder();
        testInvalidAndDuplicateCommandsFailClosed();
        testImplicitDefaultsUndoRestoresExactCollectionState();
        testTypedTransformSnappingAndChannelIsolation();
        testVoxelAndPlaceableSnapProfiles();
        testPlaceableSnapAdapterPreservesIdentity();
        testSceneDocumentPlayIsolationAndValidation();
        std::cout << "Editor transform foundation tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor transform foundation test failure: "
                  << error.what() << "\n";
        return 1;
    }
}
