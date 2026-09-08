#include "engine/editor/PlaceableAuthoringPlacement.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/editor/PlaceableAuthoringCommands.h"
#include "engine/editor/ViewportToolSession.h"
#include "engine/game/PlaceableEditJournal.h"
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

PlaceableInstance placeable(std::string uuid, float x, float z,
                            glm::vec3 scale = glm::vec3(1.0f))
{
    PlaceableInstance instance{};
    instance.uuid = std::move(uuid);
    instance.prototypeSlug = "eelgrass";
    instance.prototypeVersion = 1;
    instance.position = {x, 5.03f, z};
    instance.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.scale = scale;
    instance.seed = 7;
    return instance;
}

PlaceablePrototype prototype()
{
    PlaceablePrototype value{};
    value.slug = "eelgrass";
    value.version = 1;
    value.placement.footprintRadius = 1.0f;
    return value;
}

const engine::editor::PlaceableAuthoringSurface kSurface{
    {-5.0f, -5.0f}, {5.0f, 5.0f}};

void testCreatePlacementAndJournalSelection()
{
    engine::editor::PlaceableCreateRequest request{};
    request.proposedTransform.translation = {0.12f, 5.03f, -0.12f};
    request.surfaceY = 5.03f;
    request.identityEntropy = 100;
    const auto command = engine::editor::buildCreatePlaceableCommand(
        {}, prototype(), request);
    require(command.valid(), "A valid create command should be prepared");

    const auto placement =
        engine::editor::validatePlaceableAuthoringPlacement(
            *command.affectedPlaceable, 1.0f, kSurface, {});
    require(placement.valid(),
            "A snapped create candidate inside a clear surface should pass");

    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = false;
    engine::game::PlaceableEditJournal journal{};
    const auto edit = journal.beginExecute(scene, *command.command);
    require(edit.valid && journal.apply(scene, edit) &&
                journal.commit(edit, scene) && scene.placeables.size() == 1,
            "The validated command should execute through the journal exactly once");

    engine::editor::ViewportToolSession selection{};
    selection.select(engine::editor::SelectionTarget::placeable(
        command.affectedPlaceable->uuid));
    require(selection.reconcile(true, 1),
            "The new persistent UUID should support retained selection");
    require(!selection.reconcile(false, 2) && !selection.hasSelection(),
            "Selection should clear when a later delete removes that UUID");
}

void testFootprintBoundsOverlapAndScale()
{
    const PlaceableInstance existing = placeable("existing", 0.0f, 0.0f);
    const std::vector<engine::editor::PlaceableAuthoringObstacle> obstacles{
        {existing, 1.0f}};

    auto result = engine::editor::validatePlaceableAuthoringPlacement(
        placeable("candidate", 1.75f, 0.0f), 1.0f, kSurface, obstacles);
    require(!result && result.error == engine::editor::
                                          PlaceableAuthoringPlacementError::
                                              FootprintOccupied &&
                result.obstacleIndex == 0 &&
                result.obstacleUuid == existing.uuid,
            "A true footprint overlap should identify its exact obstacle");

    result = engine::editor::validatePlaceableAuthoringPlacement(
        placeable("candidate", 2.0f, 0.0f), 1.0f, kSurface, obstacles);
    require(result.valid(), "Exactly touching footprints should remain valid");

    result = engine::editor::validatePlaceableAuthoringPlacement(
        placeable("candidate", 3.0f, 0.0f, {2.0f, 1.0f, 2.0f}),
        1.0f, kSurface, obstacles);
    require(result.valid(),
            "Scale-aware candidate radius should accept exact touching");

    result = engine::editor::validatePlaceableAuthoringPlacement(
        placeable("candidate", 4.25f, 0.0f), 1.0f, kSurface, {});
    require(!result && result.error == engine::editor::
                                          PlaceableAuthoringPlacementError::
                                              OutsideSurface,
            "A footprint crossing the scene surface should fail closed");
}

void testDuplicateCollisionAndClearOffset()
{
    const PlaceableInstance source = placeable("source", 0.0f, 0.0f);
    const std::vector<PlaceableInstance> existing{source};
    const std::vector<engine::editor::PlaceableAuthoringObstacle> obstacles{
        {source, 1.0f}};

    engine::editor::PlaceableDuplicateRequest request{};
    request.sourceUuid = source.uuid;
    request.identityEntropy = 77;
    auto duplicate = engine::editor::buildDuplicatePlaceableCommand(
        existing, request);
    require(duplicate.valid(),
            "The command builder should prepare the snapped duplicate");
    auto placement = engine::editor::validatePlaceableAuthoringPlacement(
        *duplicate.affectedPlaceable, 1.0f, kSurface, obstacles);
    require(!placement && placement.error == engine::editor::
                                                PlaceableAuthoringPlacementError::
                                                    FootprintOccupied,
            "Integration must reject the command builder's small default offset when occupied");

    request.translationOffset = {2.0f, 0.0f, 0.0f};
    duplicate = engine::editor::buildDuplicatePlaceableCommand(existing,
                                                                request);
    placement = engine::editor::validatePlaceableAuthoringPlacement(
        *duplicate.affectedPlaceable, 1.0f, kSurface, obstacles);
    require(placement.valid(),
            "A user-selected snapped clear offset should validate");
}

void testMalformedPlacementInputsFailClosed()
{
    auto candidate = placeable("candidate", 0.0f, 0.0f);
    auto result = engine::editor::validatePlaceableAuthoringPlacement(
        candidate, -1.0f, kSurface, {});
    require(!result && result.error == engine::editor::
                                          PlaceableAuthoringPlacementError::
                                              InvalidCandidateRadius,
            "Negative footprint radius should be rejected");

    std::vector<engine::editor::PlaceableAuthoringObstacle> malformed{
        {placeable("", 2.0f, 0.0f), 1.0f}};
    result = engine::editor::validatePlaceableAuthoringPlacement(
        candidate, 1.0f, kSurface, malformed);
    require(!result && result.error == engine::editor::
                                          PlaceableAuthoringPlacementError::
                                              InvalidObstacle,
            "Malformed existing footprints should reject the complete request");

    malformed = {{candidate, 1.0f}};
    result = engine::editor::validatePlaceableAuthoringPlacement(
        candidate, 1.0f, kSurface, malformed);
    require(!result && result.error == engine::editor::
                                          PlaceableAuthoringPlacementError::
                                              DuplicateIdentity,
            "A candidate may never reuse an existing persistent identity");
}

} // namespace

int main()
{
    try
    {
        testCreatePlacementAndJournalSelection();
        testFootprintBoundsOverlapAndScale();
        testDuplicateCollisionAndClearOffset();
        testMalformedPlacementInputsFailClosed();
        std::cout << "Placeable authoring integration tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Placeable authoring integration tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
