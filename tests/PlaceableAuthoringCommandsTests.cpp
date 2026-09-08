#include "engine/editor/PlaceableAuthoringCommands.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "engine/game/PlaceableEditJournal.h"
#include "engine/game/PlaceableTransform.h"
#include "engine/scene/SceneConfig.h"

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

PlaceableInstance placeable(std::string uuid, float x)
{
    PlaceableInstance instance{};
    instance.uuid = std::move(uuid);
    instance.prototypeSlug = "eelgrass";
    instance.prototypeVersion = 1;
    instance.position = {x, 2.0f, 3.0f};
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = glm::vec3(1.0f);
    instance.seed = 17;
    return instance;
}

PlaceablePrototype prototype()
{
    PlaceablePrototype result{};
    result.slug = "eelgrass";
    result.version = 1;
    return result;
}

SceneConfig explicitScene(std::vector<PlaceableInstance> placeables)
{
    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = false;
    scene.placeables = std::move(placeables);
    return scene;
}

void applyAndCommit(engine::game::PlaceableEditJournal& journal,
                    SceneConfig& scene,
                    const engine::game::PreparedPlaceableEdit& edit)
{
    require(edit.valid, "Expected the journal to prepare the authoring command");
    require(journal.apply(scene, edit),
            "The authoring command should apply to its exact source collection");
    require(journal.commit(edit, scene),
            "The applied authoring command should commit exactly once");
}

void testCreateSnapsAllocatesAndRoundTripsImplicitDefaults()
{
    const std::vector<PlaceableInstance> defaults{placeable("default-a", 0.0f)};
    engine::editor::PlaceableCreateRequest request{};
    request.proposedTransform.translation = {1.13f, 99.0f, -2.12f};
    request.proposedTransform.rotation = glm::angleAxis(
        glm::radians(22.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    request.proposedTransform.scale = glm::vec3(1.06f);
    request.surfaceY = 5.03f;
    request.identityEntropy = 42;

    const auto created = engine::editor::buildCreatePlaceableCommand(
        defaults, prototype(), request);
    require(created && created.command->op == PlaceableEditCommand::Op::Add &&
                !created.command->before && created.command->after,
            "Create should emit one exact Add command");
    const PlaceableInstance instance = *created.affectedPlaceable;
    require(instance.uuid.size() == 36 && instance.uuid[14] == '4' &&
                !instance.prototypeSlug.empty() && instance.seed != 0,
            "Create should allocate a persistent UUID-shaped identity and seed");
    require(instance.position == glm::vec3(1.25f, 5.03f, -2.0f) &&
                nearlyEqual(glm::degrees(glm::yaw(instance.rotation)), 15.0f) &&
                instance.scale == glm::vec3(1.1f),
            "Create should apply the established placeable translation, yaw, surface, and scale snaps");

    const auto repeated = engine::editor::buildCreatePlaceableCommand(
        defaults, prototype(), request);
    require(repeated && repeated.affectedPlaceable->uuid == instance.uuid &&
                repeated.affectedPlaceable->seed == instance.seed,
            "A fixed document entropy and collection should produce a repeatable identity and seed");

    std::vector<PlaceableInstance> collisionSet = defaults;
    collisionSet.push_back(instance);
    const auto retried = engine::editor::buildCreatePlaceableCommand(
        collisionSet, prototype(), request);
    require(retried && retried.affectedPlaceable->uuid != instance.uuid,
            "Identity allocation should retry deterministically around an occupied UUID");

    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = true;
    scene.placeables.clear();
    engine::game::PlaceableEditJournal journal{};
    applyAndCommit(journal, scene,
                   journal.beginExecute(scene, *created.command, defaults));
    require(!scene.useDefaultPlaceables && scene.placeables.size() == 2 &&
                scene.placeables.back().uuid == instance.uuid,
            "Executing Create should materialize implicit defaults and append the new identity");
    applyAndCommit(journal, scene, journal.beginUndo(scene));
    require(scene.useDefaultPlaceables && scene.placeables.empty(),
            "Create undo should restore the exact implicit-default representation");
    applyAndCommit(journal, scene, journal.beginRedo(scene));
    require(scene.placeables.size() == 2 &&
                scene.placeables.back().uuid == instance.uuid,
            "Create redo should reproduce the exact authored collection");
}

void testDuplicatePreservesContentAndRoundTrips()
{
    PlaceableInstance source = placeable("source", 1.0f);
    source.rotation = glm::angleAxis(0.37f, glm::vec3(1.0f, 0.0f, 0.0f));
    source.scale = {0.75f, 1.25f, 1.5f};
    source.seed = 91;
    const std::vector<PlaceableInstance> existing{source};

    engine::editor::PlaceableDuplicateRequest request{};
    request.sourceUuid = source.uuid;
    request.translationOffset = {0.26f, 0.0f, -0.26f};
    request.identityEntropy = 99;
    const auto duplicated =
        engine::editor::buildDuplicatePlaceableCommand(existing, request);
    require(duplicated && duplicated.command->op == PlaceableEditCommand::Op::Add,
            "Duplicate should emit one Add command for a new persistent identity");
    const PlaceableInstance copy = *duplicated.affectedPlaceable;
    require(copy.uuid != source.uuid && copy.prototypeSlug == source.prototypeSlug &&
                copy.prototypeVersion == source.prototypeVersion &&
                copy.seed == source.seed && copy.rotation == source.rotation &&
                copy.scale == source.scale &&
                copy.position == glm::vec3(1.25f, 2.0f, 2.75f),
            "Duplicate should preserve authored content while snapping only its offset position");

    SceneConfig scene = explicitScene(existing);
    engine::game::PlaceableEditJournal journal{};
    applyAndCommit(journal, scene,
                   journal.beginExecute(scene, *duplicated.command));
    require(scene.placeables.size() == 2 && scene.placeables.back().uuid == copy.uuid,
            "Duplicate execution should append without disturbing the source");
    applyAndCommit(journal, scene, journal.beginUndo(scene));
    applyAndCommit(journal, scene, journal.beginRedo(scene));
    require(scene.placeables.size() == 2 &&
                scene.placeables.front().uuid == source.uuid &&
                scene.placeables.back().uuid == copy.uuid,
            "Duplicate undo/redo should retain exact source and append order");
}

void testDeleteUsesExactTargetAndRestoresOrder()
{
    const PlaceableInstance first = placeable("first", 1.0f);
    const PlaceableInstance middle = placeable("middle", 2.0f);
    const PlaceableInstance last = placeable("last", 3.0f);
    SceneConfig scene = explicitScene({first, middle, last});

    const auto deleted = engine::editor::buildDeletePlaceableCommand(
        scene.placeables, {middle.uuid});
    require(deleted && deleted.command->op == PlaceableEditCommand::Op::Remove &&
                deleted.command->before && !deleted.command->after &&
                deleted.affectedPlaceable->uuid == middle.uuid,
            "Delete should emit one exact Remove command for the selected UUID");

    engine::game::PlaceableEditJournal journal{};
    applyAndCommit(journal, scene,
                   journal.beginExecute(scene, *deleted.command));
    require(scene.placeables.size() == 2 &&
                scene.placeables[0].uuid == first.uuid &&
                scene.placeables[1].uuid == last.uuid,
            "Delete should remove only its stable target");
    applyAndCommit(journal, scene, journal.beginUndo(scene));
    require(scene.placeables.size() == 3 &&
                scene.placeables[1].uuid == middle.uuid,
            "Delete undo should restore the exact original index");
    applyAndCommit(journal, scene, journal.beginRedo(scene));
    require(scene.placeables.size() == 2 &&
                scene.placeables[1].uuid == last.uuid,
            "Delete redo should reproduce the exact post-delete order");
}

void testInvalidRequestsFailClosed()
{
    const PlaceableInstance source = placeable("source", 1.0f);
    const std::vector<PlaceableInstance> existing{source};

    PlaceablePrototype invalidPrototype{};
    const auto invalidCreate = engine::editor::buildCreatePlaceableCommand(
        existing, invalidPrototype, {});
    require(!invalidCreate &&
                invalidCreate.error == engine::editor::
                                           PlaceableAuthoringCommandError::InvalidPrototype,
            "Create should reject a malformed resolved prototype");

    engine::editor::PlaceableCreateRequest malformedTransform{};
    malformedTransform.identityEntropy = 1;
    malformedTransform.proposedTransform.translation.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalidTransform = engine::editor::buildCreatePlaceableCommand(
        existing, prototype(), malformedTransform);
    require(!invalidTransform &&
                invalidTransform.error ==
                    engine::editor::PlaceableAuthoringCommandError::
                        InvalidRequestedTransform &&
                invalidTransform.snapError ==
                    engine::editor::TransformSnapError::InvalidTransform,
            "Create should reject a non-finite proposed transform before emitting a command");

    const auto missingEntropy = engine::editor::buildCreatePlaceableCommand(
        existing, prototype(), {});
    require(!missingEntropy &&
                missingEntropy.error == engine::editor::
                                            PlaceableAuthoringCommandError::
                                                MissingIdentityEntropy,
            "Create should require an explicit controller-supplied identity entropy");

    const auto emptyTarget = engine::editor::buildDeletePlaceableCommand(
        existing, {});
    const auto missingTarget = engine::editor::buildDuplicatePlaceableCommand(
        existing, {"missing", {0.25f, 0.0f, 0.25f}, std::nullopt, 1});
    require(!emptyTarget &&
                emptyTarget.error == engine::editor::
                                         PlaceableAuthoringCommandError::InvalidTargetIdentity &&
                !missingTarget &&
                missingTarget.error ==
                    engine::editor::PlaceableAuthoringCommandError::TargetNotFound,
            "Delete/duplicate should reject empty and unresolved stable identities");

    std::vector<PlaceableInstance> duplicateCollection{source, source};
    const auto invalidCollection =
        engine::editor::buildDeletePlaceableCommand(duplicateCollection,
                                                     {source.uuid});
    require(!invalidCollection &&
                invalidCollection.error == engine::editor::
                                               PlaceableAuthoringCommandError::
                                                   DuplicateExistingIdentity &&
                invalidCollection.invalidPlaceableIndex == 1,
            "A malformed collection should reject the complete authoring request");

    std::vector<PlaceableInstance> badTransform{source};
    badTransform[0].scale.y = 0.0f;
    const auto invalidExisting =
        engine::editor::buildDeletePlaceableCommand(badTransform,
                                                     {source.uuid});
    require(!invalidExisting &&
                invalidExisting.error == engine::editor::
                                             PlaceableAuthoringCommandError::
                                                 InvalidExistingTransform &&
                invalidExisting.transformError ==
                    engine::game::PlaceableTransformError::NonPositiveScale,
            "An invalid existing transform should fail closed with its typed cause");

    require(existing.size() == 1 && existing.front().uuid == source.uuid,
            "Command preparation must never mutate its source collection");
}

} // namespace

int main()
{
    try
    {
        testCreateSnapsAllocatesAndRoundTripsImplicitDefaults();
        testDuplicatePreservesContentAndRoundTrips();
        testDeleteUsesExactTargetAndRestoresOrder();
        testInvalidRequestsFailClosed();
        std::cout << "Placeable authoring command tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Placeable authoring command tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
