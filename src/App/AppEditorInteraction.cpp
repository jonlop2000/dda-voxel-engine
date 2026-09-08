#include "App/App.h"

#if VOXEL_WITH_EDITOR

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <imgui.h>

#include "Core/Logger.h"
#include "App/AppSceneVolume.h"
#include "UI/Editor.h"
#include "engine/editor/PlaceableAuthoringCommands.h"
#include "engine/editor/PlaceableAuthoringPlacement.h"
#include "engine/editor/SelectionPicking.h"
#include "engine/game/FoliageCatalog.h"
#include "engine/scene/AquariumScene.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/SceneSerializer.h"

namespace
{

bool insideViewport(double x, double y, const ImVec4& viewport) noexcept
{
    return viewport.z > 0.0f && viewport.w > 0.0f &&
           x >= static_cast<double>(viewport.x) &&
           y >= static_cast<double>(viewport.y) &&
           x < static_cast<double>(viewport.x + viewport.z) &&
           y < static_cast<double>(viewport.y + viewport.w);
}

uint64_t nextPlaceableAuthoringEntropy(EditorState& state) noexcept
{
    ++state.placeableAuthoringSerial;
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return now ^
           (state.placeableAuthoringSerial * 0x9e3779b97f4a7c15ull);
}

PlacementEvaluation evaluateEditorPlacement(
    const SceneConfig& config, const glm::vec2& positionXZ,
    const PlaceablePrototype& prototype)
{
    return AquariumScene::evaluateFoliagePlacementAtPosition(
        {positionXZ.x, 0.0f, positionXZ.y}, prototype.slug,
        prototype.version, &config.placeables, config.useDefaultPlaceables,
        aquariumLayoutForScene(config));
}

engine::editor::PlaceableAuthoringPlacementResult
validateEditorPlacementFootprint(
    const PlaceableInstance& candidate, const PlaceablePrototype& prototype,
    const std::vector<PlaceableInstance>& effectivePlaceables)
{
    glm::vec3 waterMin(0.0f);
    glm::vec3 waterMax(0.0f);
    if (!AquariumScene::getWaterBounds(waterMin, waterMax))
    {
        return engine::editor::validatePlaceableAuthoringPlacement(
            candidate, prototype.placement.footprintRadius,
            {{1.0f, 1.0f}, {0.0f, 0.0f}}, {});
    }

    std::vector<engine::editor::PlaceableAuthoringObstacle> obstacles{};
    obstacles.reserve(effectivePlaceables.size());
    for (const PlaceableInstance& instance : effectivePlaceables)
    {
        const FoliagePrototype* existing = FoliageCatalog::findPrototype(
            instance.prototypeSlug, instance.prototypeVersion);
        obstacles.push_back(
            {instance,
             existing != nullptr
                 ? existing->placeable.placement.footprintRadius
                 : -1.0f});
    }

    return engine::editor::validatePlaceableAuthoringPlacement(
        candidate, prototype.placement.footprintRadius,
        {{waterMin.x, waterMin.z}, {waterMax.x, waterMax.z}}, obstacles);
}

} // namespace

bool App::resolveEditorSelectedPlaceable(PlaceableInstance& instance) const
{
    if (!sceneConfig().loadAquariumTest)
    {
        return false;
    }
    const auto* selection = std::get_if<engine::editor::PlaceableSelection>(
        &editorState_.viewportTool.selection().value);
    if (selection == nullptr)
    {
        return false;
    }

    const std::vector<PlaceableInstance> authored =
        FoliageCatalog::aquariumHeroFoliageInstances(
            &sceneConfig().placeables, sceneConfig().useDefaultPlaceables);
    const auto found = std::find_if(
        authored.begin(), authored.end(), [&](const PlaceableInstance& candidate) {
            return candidate.uuid == selection->uuid;
        });
    if (found == authored.end())
    {
        return false;
    }
    instance = *found;
    return true;
}

std::vector<PlaceableInstance> App::editorPlaceables() const
{
    if (!sceneConfig().loadAquariumTest)
    {
        return {};
    }
    return FoliageCatalog::aquariumHeroFoliageInstances(
        &sceneConfig().placeables, sceneConfig().useDefaultPlaceables);
}

bool App::editorPlaceableAuthoringAvailable() const
{
    glm::vec3 waterMin(0.0f);
    glm::vec3 waterMax(0.0f);
    return sceneConfig().loadAquariumTest &&
           aquariumLayoutForScene(sceneConfig()) == AquariumLayout::Default &&
           AquariumScene::getWaterBounds(waterMin, waterMax);
}

std::vector<PlaceablePrototype> App::editorPlaceablePrototypes() const
{
    std::vector<PlaceablePrototype> result{};
    const std::span<const FoliagePrototype> prototypes =
        FoliageCatalog::prototypes();
    result.reserve(prototypes.size());
    for (const FoliagePrototype& prototype : prototypes)
    {
        result.push_back(prototype.placeable);
    }
    return result;
}

bool App::completeEditorPlaceableCommand(
    const PlaceableEditCommand& command, std::string successStatus)
{
    if (editorState_.viewportTool.gestureActive())
    {
        (void)editorState_.viewportTool.cancel();
    }
    if (!applyPlaceableEditCommand(command, false))
    {
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    if (!gameRuntime_.completeExternalPlaceableEdit(
            command, std::move(successStatus), sceneConfigMutable()))
    {
        (void)rebuildVolumeScene();
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
    return true;
}

bool App::editorCreatePlaceable(const std::string& prototypeSlug,
                                uint32_t prototypeVersion,
                                const glm::vec2& positionXZ)
{
    if (!editorPlaceableAuthoringAvailable())
    {
        gameRuntime_.setPlaceableEditStatus(
            "Create unavailable: placeable authoring requires the default aquarium tank.");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    const FoliagePrototype* foliage =
        FoliageCatalog::findPrototype(prototypeSlug, prototypeVersion);
    if (foliage == nullptr)
    {
        gameRuntime_.setPlaceableEditStatus(
            "Create rejected: unknown placeable prototype.");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const PlacementEvaluation surface =
        evaluateEditorPlacement(sceneConfig(), positionXZ, foliage->placeable);
    if (!surface.valid)
    {
        gameRuntime_.setPlaceableEditStatus(
            "Create rejected: " + surface.rejectReason + ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const std::vector<PlaceableInstance> effective = editorPlaceables();
    engine::editor::PlaceableCreateRequest request{};
    request.proposedTransform.translation = surface.hit.position;
    request.surfaceY = surface.hit.position.y;
    request.identityEntropy = nextPlaceableAuthoringEntropy(editorState_);
    const engine::editor::PlaceableAuthoringCommandResult prepared =
        engine::editor::buildCreatePlaceableCommand(
            effective, foliage->placeable, request);
    if (!prepared)
    {
        gameRuntime_.setPlaceableEditStatus(
            std::string("Create rejected: ") +
            engine::editor::placeableAuthoringCommandErrorLabel(
                prepared.error) + ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const glm::vec2 snappedXZ{prepared.affectedPlaceable->position.x,
                              prepared.affectedPlaceable->position.z};
    const PlacementEvaluation snappedSurface = evaluateEditorPlacement(
        sceneConfig(), snappedXZ, foliage->placeable);
    const engine::editor::PlaceableAuthoringPlacementResult footprint =
        validateEditorPlacementFootprint(*prepared.affectedPlaceable,
                                         foliage->placeable, effective);
    if (!snappedSurface.valid || !footprint)
    {
        const std::string reason =
            !snappedSurface.valid
                ? snappedSurface.rejectReason
                : engine::editor::placeableAuthoringPlacementErrorLabel(
                      footprint.error);
        gameRuntime_.setPlaceableEditStatus("Create rejected: " + reason + ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    if (!completeEditorPlaceableCommand(
            *prepared.command,
            "Created " + prepared.affectedPlaceable->prototypeSlug + "."))
    {
        return false;
    }
    editorState_.viewportTool.select(
        engine::editor::SelectionTarget::placeable(
            prepared.affectedPlaceable->uuid));
    logInfo("Editor", makeLogMessage(
                          "Created placeable uuid=",
                          prepared.affectedPlaceable->uuid, "."));
    return true;
}

bool App::editorDuplicateSelectedPlaceable(const glm::vec2& offsetXZ)
{
    PlaceableInstance source{};
    if (!editorPlaceableAuthoringAvailable() ||
        !resolveEditorSelectedPlaceable(source))
    {
        gameRuntime_.setPlaceableEditStatus(
            "Duplicate unavailable: select an authored placeable in the default aquarium tank.");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    const FoliagePrototype* foliage = FoliageCatalog::findPrototype(
        source.prototypeSlug, source.prototypeVersion);
    if (foliage == nullptr)
    {
        gameRuntime_.setPlaceableEditStatus(
            "Duplicate rejected: source prototype is unavailable.");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const glm::vec2 proposedXZ{source.position.x + offsetXZ.x,
                               source.position.z + offsetXZ.y};
    const PlacementEvaluation surface =
        evaluateEditorPlacement(sceneConfig(), proposedXZ, foliage->placeable);
    if (!surface.valid)
    {
        gameRuntime_.setPlaceableEditStatus(
            "Duplicate rejected: " + surface.rejectReason + ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const std::vector<PlaceableInstance> effective = editorPlaceables();
    engine::editor::PlaceableDuplicateRequest request{};
    request.sourceUuid = source.uuid;
    request.translationOffset = {offsetXZ.x,
                                 surface.hit.position.y - source.position.y,
                                 offsetXZ.y};
    request.surfaceY = surface.hit.position.y;
    request.identityEntropy = nextPlaceableAuthoringEntropy(editorState_);
    const engine::editor::PlaceableAuthoringCommandResult prepared =
        engine::editor::buildDuplicatePlaceableCommand(effective, request);
    if (!prepared)
    {
        gameRuntime_.setPlaceableEditStatus(
            std::string("Duplicate rejected: ") +
            engine::editor::placeableAuthoringCommandErrorLabel(
                prepared.error) + ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const glm::vec2 snappedXZ{prepared.affectedPlaceable->position.x,
                              prepared.affectedPlaceable->position.z};
    const PlacementEvaluation snappedSurface = evaluateEditorPlacement(
        sceneConfig(), snappedXZ, foliage->placeable);
    const engine::editor::PlaceableAuthoringPlacementResult footprint =
        validateEditorPlacementFootprint(*prepared.affectedPlaceable,
                                         foliage->placeable, effective);
    if (!snappedSurface.valid || !footprint)
    {
        const std::string reason =
            !snappedSurface.valid
                ? snappedSurface.rejectReason
                : engine::editor::placeableAuthoringPlacementErrorLabel(
                      footprint.error);
        gameRuntime_.setPlaceableEditStatus("Duplicate rejected: " + reason +
                                            ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    if (!completeEditorPlaceableCommand(
            *prepared.command,
            "Duplicated " + prepared.affectedPlaceable->prototypeSlug + "."))
    {
        return false;
    }
    editorState_.viewportTool.select(
        engine::editor::SelectionTarget::placeable(
            prepared.affectedPlaceable->uuid));
    logInfo("Editor", makeLogMessage(
                          "Duplicated placeable source=", source.uuid,
                          " uuid=", prepared.affectedPlaceable->uuid, "."));
    return true;
}

bool App::editorDeleteSelectedPlaceable()
{
    PlaceableInstance selected{};
    if (!resolveEditorSelectedPlaceable(selected))
    {
        gameRuntime_.setPlaceableEditStatus(
            "Delete unavailable: select an authored placeable.");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    const engine::editor::PlaceableAuthoringCommandResult prepared =
        engine::editor::buildDeletePlaceableCommand(
            editorPlaceables(), {selected.uuid});
    if (!prepared)
    {
        gameRuntime_.setPlaceableEditStatus(
            std::string("Delete rejected: ") +
            engine::editor::placeableAuthoringCommandErrorLabel(
                prepared.error) + ".");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    if (!completeEditorPlaceableCommand(
            *prepared.command, "Deleted " + selected.prototypeSlug + "."))
    {
        return false;
    }
    editorState_.viewportTool.clearSelection();
    logInfo("Editor", makeLogMessage("Deleted placeable uuid=", selected.uuid,
                                      "."));
    return true;
}

uint64_t App::editorVoxelStructureRevision() const
{
    return voxelWorld_.structureRevision();
}

bool App::commitEditorPlaceableEdit(const PlaceableEditCommand& command)
{
    if (command.op != PlaceableEditCommand::Op::Move ||
        !command.before || !command.after)
    {
        return false;
    }

    const std::string status = "Moved " + command.after->prototypeSlug + ".";
    if (!completeEditorPlaceableCommand(command, status))
    {
        return false;
    }

    logInfo("Editor", makeLogMessage("Committed placeable transform uuid=",
                                      command.after->uuid, "."));
    return true;
}

bool App::editorUndoPlaceableEdit()
{
    if (editorState_.viewportTool.gestureActive())
    {
        (void)editorState_.viewportTool.cancel();
    }
    const bool succeeded = undoLastPlaceableEdit();
    PlaceableInstance selected{};
    (void)editorState_.viewportTool.reconcile(
        resolveEditorSelectedPlaceable(selected),
        editorVoxelStructureRevision());
    editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
    return succeeded;
}

bool App::editorRedoPlaceableEdit()
{
    if (editorState_.viewportTool.gestureActive())
    {
        (void)editorState_.viewportTool.cancel();
    }
    const engine::game::GameRuntime::PendingPlaceableEdit edit =
        gameRuntime_.beginRedoLastPlaceableEdit();
    if (!edit.valid ||
        !applyPlaceableEditCommand(edit.command, edit.undo, edit.redo))
    {
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    if (!gameRuntime_.completePlaceableEdit(edit, sceneConfigMutable()))
    {
        (void)rebuildVolumeScene();
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }
    PlaceableInstance selected{};
    (void)editorState_.viewportTool.reconcile(
        resolveEditorSelectedPlaceable(selected),
        editorVoxelStructureRevision());
    editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
    logInfo("Editor", "Redid last placeable edit.");
    return true;
}

bool App::editorSaveCurrentScene()
{
    if (editorState_.viewportTool.gestureActive())
    {
        (void)editorState_.viewportTool.cancel();
    }
    const std::filesystem::path savePath = sceneManager_.currentScenePath();
    if (savePath.empty())
    {
        gameRuntime_.setPlaceableEditStatus(
            "Save unavailable: this scene has no authored file path.");
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        return false;
    }

    sceneConfigMutable().cameraPosition = camera_.position;
    sceneConfigMutable().cameraYaw = camera_.yaw;
    sceneConfigMutable().cameraPitch = camera_.pitch;
    sceneConfigMutable().worldSeed = worldSeed_;
    sceneConfigMutable().presentationProfile =
        engine::scene::captureScenePresentationProfile(
            sceneConfig().name, skyPreset_, skyColor_, lightingSettings_,
            shadowSettings_, aoSettings_, postFxSettings_, voxelDebugSettings_,
            waterSettings_, glassSettings_);
    gameRuntime_.stampLastPlayedUtc(sceneConfigMutable().gameState);

    std::string saveError{};
    if (!saveSceneConfigToFile(savePath, sceneConfig(), &saveError,
                               SceneSerializerLogMode::Quiet))
    {
        gameRuntime_.setPlaceableEditStatus(
            "Save failed" +
            (saveError.empty() ? std::string(".") : ": " + saveError));
        editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
        logWarning("Editor", makeLogMessage("Failed to save scene to ",
                                             savePath.string(), "."));
        return false;
    }

    gameRuntime_.markPlaceableSceneSaved();
    gameRuntime_.setPlaceableEditStatus(
        "Saved scene to " + savePath.filename().string() + ".");
    editorState_.viewportToolStatus = gameRuntime_.placeableEditStatus();
    logInfo("Editor", makeLogMessage("Saved authored scene to ",
                                      savePath.string(), "."));
    return true;
}

bool App::editorHasPlaceableUndo() const
{
    return gameRuntime_.hasPlaceableUndo();
}

bool App::editorHasPlaceableRedo() const
{
    return gameRuntime_.hasPlaceableRedo();
}

bool App::editorPlaceableSceneDirty() const
{
    return gameRuntime_.placeableSceneDirty();
}

const std::string& App::editorPlaceableEditStatus() const
{
    return gameRuntime_.placeableEditStatus();
}

bool App::handleEditorWorldPointerPress(double x, double y, int mods)
{
    (void)mods;
    if (appMode_ != AppMode::Editor || !editorState_.enabled ||
        ImGui::GetCurrentContext() == nullptr)
    {
        return false;
    }

    const ImVec4 viewport = Editor::GetViewportRect(editorState_);
    if (!insideViewport(x, y, viewport))
    {
        return false;
    }

    // every left press inside the world viewport is editor-owned, including a
    // miss or malformed frame. consuming here prevents the same press from
    // reaching input's legacy edit-mode block-removal request.
    const VkExtent2D extent = renderer_.swapchainExtent();
    if (extent.width == 0 || extent.height == 0)
    {
        editorState_.viewportTool.clearSelection();
        editorState_.viewportToolStatus = "Viewport is not ready for picking.";
        return true;
    }

    const engine::input::FramebufferPoint cursor =
        inputController_.framebufferPoint(x, y, extent.width, extent.height);
    const float aspect = static_cast<float>(extent.width) /
                         static_cast<float>(extent.height);
    const glm::mat4 viewProjection =
        camera_.projMatrix(aspect) * camera_.viewMatrix();
    const engine::editor::SelectionRayBuildResult ray =
        engine::editor::selectionRayFromViewport(
            {cursor.x, cursor.y}, {extent.width, extent.height},
            glm::inverse(viewProjection), camera_.farZ);
    if (!ray)
    {
        editorState_.viewportTool.clearSelection();
        editorState_.viewportToolStatus =
            std::string("Selection ray rejected: ") +
            engine::editor::selectionRayBuildErrorLabel(ray.error);
        return true;
    }

    std::vector<engine::editor::SelectionCandidate> candidates{};
    candidates.reserve(heroFoliage_.size());
    for (const HeroFoliageInstance& foliage : heroFoliage_)
    {
        if (foliage.placeableUuid.empty() || foliage.volumeIndex < 0 ||
            static_cast<size_t>(foliage.volumeIndex) >=
                voxelWorld_.instances().size())
        {
            continue;
        }
        const engine::VoxelVolume& volume =
            voxelWorld_.instances()[static_cast<size_t>(foliage.volumeIndex)].volume;
        if (!volume.hasOccupiedVoxels())
        {
            continue;
        }

        engine::editor::SelectionCandidate candidate{};
        candidate.target = engine::editor::SelectionTarget::placeable(
            foliage.placeableUuid);
        candidate.localBounds = {
            glm::vec3(volume.occupiedLocalMin()),
            glm::vec3(volume.occupiedLocalMaxExclusive())};
        candidate.worldFromLocal = volume.worldFromLocal();
        candidate.priority = 100;
        candidates.push_back(std::move(candidate));
    }

    const engine::editor::SelectionPickResult picked =
        engine::editor::pickSelection(ray.ray, candidates);
    if (!picked)
    {
        editorState_.viewportTool.clearSelection();
        editorState_.viewportToolStatus =
            std::string("Selection candidates rejected: ") +
            engine::editor::selectionPickErrorLabel(picked.error);
        return true;
    }
    if (!picked.hit)
    {
        editorState_.viewportTool.clearSelection();
        editorState_.viewportToolStatus = "No placeable selected.";
        return true;
    }

    editorState_.viewportTool.select(picked.hit->target);
    PlaceableInstance selected{};
    editorState_.viewportToolStatus =
        resolveEditorSelectedPlaceable(selected)
            ? "Selected " + selected.prototypeSlug + "."
            : "Selected placeable is no longer available.";
    return true;
}

#endif
