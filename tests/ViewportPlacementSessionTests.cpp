#include "engine/editor/ViewportPlacementSession.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "engine/editor/PlaceableAuthoringPlacement.h"
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

bool nearlyEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

PlaceablePrototype prototype(std::string slug = "eelgrass")
{
    PlaceablePrototype value{};
    value.slug = std::move(slug);
    value.version = 1;
    value.placement.footprintRadius = 1.0f;
    return value;
}

PlaceableInstance placeable(std::string uuid, float x, float z)
{
    PlaceableInstance value{};
    value.uuid = std::move(uuid);
    value.prototypeSlug = "eelgrass";
    value.prototypeVersion = 1;
    value.position = {x, 5.03f, z};
    value.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    value.scale = glm::vec3(1.0f);
    value.seed = 9;
    return value;
}

engine::editor::ViewportPlacementValidator clearValidator(int* calls = nullptr)
{
    return [calls](const PlaceableInstance&) {
        if (calls != nullptr)
        {
            ++*calls;
        }
        return engine::editor::ViewportPlacementDecision::accept();
    };
}

void testArmUpdateAndSnappedPreview()
{
    engine::editor::ViewportPlacementSession session{};
    engine::editor::TransformState transform{};
    transform.rotation = glm::angleAxis(
        glm::radians(22.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    transform.scale = glm::vec3(1.06f);

    const auto armed = session.arm(prototype(), transform, true);
    require(armed.valid() && armed.state ==
                                 engine::editor::ViewportPlacementState::Armed &&
                session.armed() && session.preview() == nullptr,
            "A valid prototype/template should arm without creating a preview");

    int validationCalls = 0;
    PlaceableInstance validated{};
    const auto updated = session.update(
        {true, {1.13f, 5.03f, -2.12f}},
        [&](const PlaceableInstance& candidate) {
            ++validationCalls;
            validated = candidate;
            return engine::editor::ViewportPlacementDecision::accept();
        });
    require(updated.valid() && updated.preview &&
                updated.state ==
                    engine::editor::ViewportPlacementState::PreviewValid &&
                validationCalls == 1,
            "A surface hit should validate one retained preview");
    require(validated.position == glm::vec3(1.25f, 5.03f, -2.0f) &&
                nearlyEqual(glm::degrees(glm::yaw(validated.rotation)), 15.0f) &&
                validated.scale == glm::vec3(1.1f),
            "The preview should use the established XZ, surface, yaw, and scale snaps");
}

void testBlockedPreviewMissAndRecovery()
{
    engine::editor::ViewportPlacementSession session{};
    require(session.arm(prototype()).valid(), "Expected an armed session");

    auto blocked = session.update(
        {true, {2.0f, 5.03f, 2.0f}},
        [](const PlaceableInstance&) {
            return engine::editor::ViewportPlacementDecision::reject(
                "footprint occupied");
        });
    require(!blocked && blocked.preview &&
                blocked.state ==
                    engine::editor::ViewportPlacementState::PreviewBlocked &&
                blocked.error ==
                    engine::editor::ViewportPlacementError::PlacementRejected &&
                blocked.rejectionReason == "footprint occupied",
            "A rejected candidate should remain available for a future blocked ghost");
    require(!session.commit({}, 1, clearValidator()) &&
                session.state() ==
                    engine::editor::ViewportPlacementState::PreviewBlocked,
            "A blocked preview may not commit");

    const uint64_t blockedRevision = session.revision();
    const auto miss = session.update({}, clearValidator());
    require(!miss && miss.error ==
                         engine::editor::ViewportPlacementError::SurfaceMiss &&
                miss.state == engine::editor::ViewportPlacementState::Armed &&
                !miss.preview && session.revision() > blockedRevision,
            "A ray miss should clear the ghost but keep the tool armed");

    engine::editor::ViewportPlacementSurfaceHit invalid{};
    invalid.hit = true;
    invalid.position.x = std::numeric_limits<float>::quiet_NaN();
    const auto malformed = session.update(invalid, clearValidator());
    require(!malformed && malformed.error ==
                              engine::editor::ViewportPlacementError::
                                  InvalidSurfaceHit &&
                !malformed.preview,
            "A non-finite surface hit should fail without retaining invalid geometry");

    const auto recovered = session.update(
        {true, {-1.0f, 5.03f, 1.0f}}, clearValidator());
    require(recovered.valid() && recovered.preview,
            "A later valid hit should recover the same armed session");
}

void testCommitRevalidatesAndRoundTripsJournal()
{
    engine::editor::ViewportPlacementSession session{};
    require(session.arm(prototype()).valid(), "Expected an armed session");
    const std::vector<PlaceableInstance> effective{
        placeable("existing", -3.0f, -3.0f)};

    int validationCalls = 0;
    const auto validator = clearValidator(&validationCalls);
    require(session.update({true, {1.12f, 5.03f, 1.12f}}, validator).valid(),
            "Expected a valid preview");
    const PlaceableInstance preview = *session.preview();
    const auto committed = session.commit(effective, 42, validator);
    require(committed.valid() && committed.command &&
                committed.affectedPlaceable &&
                committed.command->op == PlaceableEditCommand::Op::Add &&
                committed.state ==
                    engine::editor::ViewportPlacementState::Armed &&
                !committed.preview && session.preview() == nullptr &&
                validationCalls == 2,
            "Commit should revalidate once, emit one Add, clear the preview, and stay armed");
    require(engine::game::placeableTransformEquivalent(
                preview, *committed.affectedPlaceable) &&
                committed.affectedPlaceable->uuid != preview.uuid,
            "The persistent command must match the preview transform while replacing its transient identity");
    require(effective.size() == 1 && effective.front().uuid == "existing",
            "Preview and commit preparation must not mutate the source scene");

    SceneConfig scene{};
    scene.loadAquariumTest = true;
    scene.useDefaultPlaceables = false;
    scene.placeables = effective;
    const engine::game::PlaceableCollectionSnapshot sourceSnapshot =
        engine::game::capturePlaceableCollection(scene);
    engine::game::PlaceableEditJournal journal{};
    const auto edit = journal.beginExecute(scene, *committed.command);
    require(edit.valid && journal.apply(scene, edit) &&
                journal.commit(edit, scene) && scene.placeables.size() == 2,
            "The returned command should execute through the existing journal");
    const auto undo = journal.beginUndo(scene);
    require(undo.valid && journal.apply(scene, undo) &&
                journal.commit(undo, scene) &&
                engine::game::placeableCollectionExactlyEqual(
                    engine::game::capturePlaceableCollection(scene),
                    sourceSnapshot),
            "Placement undo should restore the exact source collection");

    require(session.update({true, {3.12f, 5.03f, 1.12f}}, validator).valid(),
            "Continuous placement should accept a second preview without re-arming");
    const auto second = session.commit(effective, 43, validator);
    require(second.valid() && second.affectedPlaceable->uuid !=
                                  committed.affectedPlaceable->uuid,
            "Fresh entropy should produce a second persistent identity");
}

void testCommitFailureAndStaleRevalidation()
{
    engine::editor::ViewportPlacementSession session{};
    require(session.arm(prototype()).valid() &&
                session.update({true, {0.0f, 5.03f, 0.0f}},
                               clearValidator()).valid(),
            "Expected a committable preview");

    const auto missingEntropy =
        session.commit({}, std::nullopt, clearValidator());
    require(!missingEntropy && missingEntropy.error ==
                                   engine::editor::ViewportPlacementError::
                                       CommandRejected &&
                missingEntropy.commandError ==
                    engine::editor::PlaceableAuthoringCommandError::
                        MissingIdentityEntropy &&
                session.state() ==
                    engine::editor::ViewportPlacementState::PreviewValid,
            "Command rejection should retain a valid preview for a corrected retry");

    const uint64_t staleRevision = missingEntropy.revision;
    const auto stale = session.commit(
        {}, 4,
        [](const PlaceableInstance&) {
            return engine::editor::ViewportPlacementDecision::reject(
                "scene changed under preview");
        });
    require(!stale && stale.error ==
                          engine::editor::ViewportPlacementError::
                              PlacementRejected &&
                stale.rejectionReason == "scene changed under preview" &&
                stale.preview && !stale.command &&
                stale.revision > staleRevision &&
                session.state() ==
                    engine::editor::ViewportPlacementState::PreviewBlocked,
            "Commit-time revalidation should stop a stale placement without emitting a command");
}

void testLifecycleAndInvalidRequests()
{
    engine::editor::ViewportPlacementSession session{};
    require(!session.update({true, {}}, clearValidator()) &&
                !session.cancelPreview(),
            "Update and cancel should reject an idle session");

    PlaceablePrototype malformed{};
    require(!session.arm(malformed) && !session.armed(),
            "A malformed prototype should not change idle state");

    engine::editor::TransformState badTemplate{};
    badTemplate.scale.y = 0.0f;
    require(!session.arm(prototype(), badTemplate) && !session.armed(),
            "An invalid transform template should fail closed");

    require(session.arm(prototype()).valid() &&
                session.update({true, {1.0f, 5.03f, 1.0f}},
                               clearValidator()).valid(),
            "Expected a valid preview before cancellation");
    const uint64_t beforeCancel = session.revision();
    const auto cancelled = session.cancelPreview();
    require(cancelled.valid() && !cancelled.preview &&
                cancelled.state ==
                    engine::editor::ViewportPlacementState::Armed &&
                session.revision() > beforeCancel,
            "Preview cancellation should retain the armed prototype");

    const auto missingValidator = session.update(
        {true, {1.0f, 5.03f, 1.0f}}, {});
    require(!missingValidator && missingValidator.preview &&
                missingValidator.error ==
                    engine::editor::ViewportPlacementError::MissingValidator &&
                session.state() ==
                    engine::editor::ViewportPlacementState::PreviewBlocked,
            "Missing placement authority should retain only a blocked preview");

    session.disarm();
    require(!session.armed() && session.preview() == nullptr &&
                session.prototype() == nullptr &&
                session.state() == engine::editor::ViewportPlacementState::Idle,
            "Disarm should clear all retained placement state");
}

} // namespace

int main()
{
    try
    {
        testArmUpdateAndSnappedPreview();
        testBlockedPreviewMissAndRecovery();
        testCommitRevalidatesAndRoundTripsJournal();
        testCommitFailureAndStaleRevalidation();
        testLifecycleAndInvalidRequests();
        std::cout << "Viewport placement session tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Viewport placement session tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
