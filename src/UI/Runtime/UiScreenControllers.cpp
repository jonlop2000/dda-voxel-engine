#include "UI/Runtime/UiScreenControllers.h"

#include <algorithm>

namespace ui
{
namespace
{

std::string logValueOrNone(const std::string& value)
{
    return value.empty() ? std::string("<none>") : value;
}

bool samePlacementProbe(const BuildPlacementProbe& lhs, const BuildPlacementProbe& rhs)
{
    return lhs.active == rhs.active && lhs.rayHit == rhs.rayHit &&
           lhs.hasPlacementCandidate == rhs.hasPlacementCandidate &&
           lhs.snappedToGrid == rhs.snappedToGrid &&
           lhs.placementValid == rhs.placementValid && lhs.itemKey == rhs.itemKey &&
           lhs.invalidReason == rhs.invalidReason &&
           lhs.hitVoxel == rhs.hitVoxel && lhs.hitNormal == rhs.hitNormal &&
           lhs.placementVoxel == rhs.placementVoxel &&
           lhs.footprintVoxels == rhs.footprintVoxels &&
           lhs.ghostWorldPosition == rhs.ghostWorldPosition &&
           lhs.targetPlaceableUuid == rhs.targetPlaceableUuid &&
           lhs.targetPlaceablePrototype == rhs.targetPlaceablePrototype &&
           lhs.rotationSteps == rhs.rotationSteps &&
           lhs.distance == rhs.distance;
}

} // namespace

const char* buildModeStateLabel(BuildModeState state)
{
    switch (state)
    {
    case BuildModeState::Inactive:
        return "inactive";
    case BuildModeState::PendingPlacement:
        return "pending-placement";
    case BuildModeState::PendingRemoval:
        return "pending-removal";
    }

    return "unknown";
}

void OverlaySmokeScreenController::registerActions(UiActionDispatcher& dispatcher)
{
    dispatcher.registerHandler(
        std::string(kOpenBuildCatalogAction),
        [this](const UiActionEvent&) {
            if (!state_.buildCatalogOpen)
            {
                catalogOpenedTransition_ = true;
            }
            state_.buildCatalogOpen = true;
            state_.buildCatalogScrollOffsetY = 0.0f;
            catalogOpenVersion_.bump();
            catalogScrollVersion_.bump();
            buildCatalogLogged_ = false;
            buildCatalogScrollLogged_ = false;
        });
    dispatcher.registerHandler(
        std::string(kCloseBuildCatalogAction),
        [this](const UiActionEvent&) {
            clearPendingPlacement();
            state_.buildCatalogOpen = false;
            state_.buildCatalogScrollOffsetY = 0.0f;
            catalogOpenVersion_.bump();
            catalogScrollVersion_.bump();
            buildCatalogLogged_ = false;
            buildCatalogScrollLogged_ = false;
        });
    dispatcher.registerHandler(
        std::string(kSelectBuildCatalogItemAction),
        [this](const UiActionEvent& event) {
            state_.selectedCatalogItem = event.elementId;
            state_.selectedCatalogKey = event.payload;
            state_.pendingBuildItemKey = event.payload;
            state_.placementRotationSteps = 0;
            ghostPreviewVersion_.bump();
            if (!state_.pendingBuildItemKey.empty())
            {
                state_.buildModeState = BuildModeState::PendingPlacement;
                buildGhostPreviewLogged_ = false;
                buildPlacementProbeLogged_ = false;
                buildPlacementGhostLogged_ = false;
            }
        });
    dispatcher.registerHandler(
        std::string(kSelectRemovePlacementAction),
        [this](const UiActionEvent& event) {
            state_.selectedCatalogItem = event.elementId;
            state_.selectedCatalogKey = std::string(kBuildRemoveKey);
            state_.pendingBuildItemKey = std::string(kBuildRemoveKey);
            state_.placementRotationSteps = 0;
            ghostPreviewVersion_.bump();
            state_.buildModeState = BuildModeState::PendingRemoval;
            buildGhostPreviewLogged_ = false;
            buildPlacementProbeLogged_ = false;
            buildPlacementGhostLogged_ = false;
        });
    dispatcher.registerHandler(
        std::string(kCancelBuildPlacementAction),
        [this](const UiActionEvent&) {
            clearPendingPlacement();
        });
    dispatcher.registerHandler(
        std::string(kCommitBuildPlacementAction),
        [this](const UiActionEvent&) {
            clearPendingPlacement();
        });
    dispatcher.registerHandler(
        std::string(kRemoveBuildPlacementAction),
        [this](const UiActionEvent&) {
            clearPendingPlacement();
        });
}

bool OverlaySmokeScreenController::setFishList(std::vector<UiFishListEntry> entries)
{
    if (entries == fishList_)
    {
        return false;
    }

    fishList_ = std::move(entries);
    fishListVersion_.bump();
    fishListLogged_ = false;
    return true;
}

bool OverlaySmokeScreenController::consumeCatalogOpenedTransition()
{
    const bool transition = catalogOpenedTransition_;
    catalogOpenedTransition_ = false;
    return transition;
}

void OverlaySmokeScreenController::clearPendingPlacement()
{
    state_.selectedCatalogItem.clear();
    state_.selectedCatalogKey.clear();
    state_.pendingBuildItemKey.clear();
    state_.placementRotationSteps = 0;
    state_.buildModeState = BuildModeState::Inactive;
    state_.placementProbe = {};
    ghostPreviewVersion_.bump();
    buildGhostPreviewLogged_ = false;
    buildPlacementProbeLogged_ = false;
    buildPlacementGhostLogged_ = false;
}

void OverlaySmokeScreenController::syncTree(UiTree& tree, uint64_t treeRevision)
{
    if (bindings_.bindingCount() == 0)
    {
        bindings_.bind("build_catalog_open", &catalogOpenVersion_, [this](UiTree& t) {
            setOverlaySmokeBuildCatalogOpen(t, state_.buildCatalogOpen);
        });
        bindings_.bind("build_catalog_scroll", &catalogScrollVersion_, [this](UiTree& t) {
            setOverlaySmokeBuildCatalogScrollOffset(t, state_.buildCatalogScrollOffsetY);
        });
        bindings_.bind("build_ghost_preview", &ghostPreviewVersion_, [this](UiTree& t) {
            setOverlaySmokeBuildGhostPreview(t, pendingPlacementActive(),
                                             state_.pendingBuildItemKey,
                                             state_.placementRotationSteps);
        });
        bindings_.bind("fish_list", &fishListVersion_, [this](UiTree& t) {
            setOverlaySmokeFishList(t, fishList_);
        });
    }

    lastBindingStats_ = bindings_.sync(tree, treeRevision);
}

bool OverlaySmokeScreenController::pendingPlacementActive() const
{
    return state_.buildModeState != BuildModeState::Inactive &&
           !state_.pendingBuildItemKey.empty();
}

bool OverlaySmokeScreenController::resetTransientSessionState()
{
    const bool catalogVisibilityChanged = state_.buildCatalogOpen;
    const bool catalogScrollChanged = state_.buildCatalogScrollOffsetY != 0.0f;
    const bool placementChanged = !state_.selectedCatalogItem.empty() ||
                                  !state_.selectedCatalogKey.empty() ||
                                  !state_.pendingBuildItemKey.empty() ||
                                  state_.placementRotationSteps != 0 ||
                                  state_.buildModeState != BuildModeState::Inactive ||
                                  !samePlacementProbe(state_.placementProbe,
                                                      BuildPlacementProbe{});

    state_.buildCatalogOpen = false;
    state_.buildCatalogScrollOffsetY = 0.0f;
    catalogOpenedTransition_ = false;
    if (catalogVisibilityChanged)
    {
        catalogOpenVersion_.bump();
    }
    if (catalogScrollChanged)
    {
        catalogScrollVersion_.bump();
    }

    if (placementChanged)
    {
        clearPendingPlacement();
    }

    // logging markers are session-local diagnostics. resetting them makes the
    // next session observable without discarding the independently managed fish
    // list or its binding version.
    buildCatalogLogged_ = false;
    buildCatalogScrollLogged_ = false;
    buildGhostPreviewLogged_ = false;
    buildPlacementProbeLogged_ = false;
    buildPlacementGhostLogged_ = false;
    fishListLogged_ = false;

    return catalogVisibilityChanged || catalogScrollChanged || placementChanged;
}

bool OverlaySmokeScreenController::scrollBuildCatalog(double wheelYOffset)
{
    if (!state_.buildCatalogOpen || wheelYOffset == 0.0)
    {
        return false;
    }

    constexpr float kScrollStepPx = 38.0f;
    const float maxOffset = overlaySmokeBuildCatalogMaxScrollOffset();
    const float nextOffset =
        std::clamp(state_.buildCatalogScrollOffsetY -
                       static_cast<float>(wheelYOffset) * kScrollStepPx,
                   0.0f, maxOffset);
    const bool changed = nextOffset != state_.buildCatalogScrollOffsetY;
    state_.buildCatalogScrollOffsetY = nextOffset;
    if (changed)
    {
        catalogScrollVersion_.bump();
        buildCatalogScrollLogged_ = false;
    }
    return changed;
}

bool OverlaySmokeScreenController::rotatePendingPlacement()
{
    if (state_.buildModeState != BuildModeState::PendingPlacement ||
        state_.pendingBuildItemKey.empty())
    {
        return false;
    }

    state_.placementRotationSteps =
        (state_.placementRotationSteps + 1) % 4;
    state_.placementProbe = {};
    ghostPreviewVersion_.bump();
    buildPlacementProbeLogged_ = false;
    buildPlacementGhostLogged_ = false;
    return true;
}

bool OverlaySmokeScreenController::updatePlacementProbe(
    const BuildPlacementProbe& probe)
{
    const bool changed = !samePlacementProbe(state_.placementProbe, probe);
    if (changed)
    {
        buildPlacementProbeLogged_ = false;
        buildPlacementGhostLogged_ = false;
    }
    state_.placementProbe = probe;
    return changed;
}

bool OverlaySmokeScreenController::shouldLogBuildCatalogShell() const
{
    return state_.buildCatalogOpen && !buildCatalogLogged_;
}

void OverlaySmokeScreenController::markBuildCatalogShellLogged()
{
    buildCatalogLogged_ = true;
}

bool OverlaySmokeScreenController::shouldLogBuildCatalogScroll() const
{
    return state_.buildCatalogOpen && !buildCatalogScrollLogged_;
}

void OverlaySmokeScreenController::markBuildCatalogScrollLogged()
{
    buildCatalogScrollLogged_ = true;
}

bool OverlaySmokeScreenController::shouldLogBuildGhostPreview() const
{
    return pendingPlacementActive() &&
           !buildGhostPreviewLogged_;
}

void OverlaySmokeScreenController::markBuildGhostPreviewLogged()
{
    buildGhostPreviewLogged_ = true;
}

bool OverlaySmokeScreenController::shouldLogBuildPlacementProbe() const
{
    return state_.placementProbe.active && !buildPlacementProbeLogged_;
}

void OverlaySmokeScreenController::markBuildPlacementProbeLogged()
{
    buildPlacementProbeLogged_ = true;
}

bool OverlaySmokeScreenController::shouldLogBuildPlacementGhost() const
{
    return state_.placementProbe.active && !buildPlacementGhostLogged_;
}

void OverlaySmokeScreenController::markBuildPlacementGhostLogged()
{
    buildPlacementGhostLogged_ = true;
}

std::string OverlaySmokeScreenController::selectedCatalogItemLogValue() const
{
    return logValueOrNone(state_.selectedCatalogItem);
}

std::string OverlaySmokeScreenController::selectedCatalogKeyLogValue() const
{
    return logValueOrNone(state_.selectedCatalogKey);
}

std::string OverlaySmokeScreenController::pendingBuildItemKeyLogValue() const
{
    return logValueOrNone(state_.pendingBuildItemKey);
}

float OverlaySmokeScreenController::buildCatalogScrollOffsetY() const
{
    return state_.buildCatalogScrollOffsetY;
}

int OverlaySmokeScreenController::placementRotationSteps() const
{
    return state_.placementRotationSteps;
}

const char* OverlaySmokeScreenController::buildModeStateLogValue() const
{
    return buildModeStateLabel(state_.buildModeState);
}

} // namespace ui
