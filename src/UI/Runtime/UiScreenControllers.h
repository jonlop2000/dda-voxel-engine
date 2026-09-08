#pragma once

#include <string>

#include <glm/glm.hpp>

#include "UI/Runtime/UiActions.h"
#include "UI/Runtime/UiBinding.h"
#include "UI/Runtime/UiScreens.h"

namespace ui
{

enum class BuildModeState
{
    Inactive,
    PendingPlacement,
    PendingRemoval,
};

const char* buildModeStateLabel(BuildModeState state);

struct BuildPlacementProbe
{
    bool active = false;
    bool rayHit = false;
    bool hasPlacementCandidate = false;
    bool snappedToGrid = false;
    bool placementValid = false;
    std::string itemKey{};
    std::string invalidReason{};
    glm::ivec3 hitVoxel{0};
    glm::ivec3 hitNormal{0};
    glm::ivec3 placementVoxel{0};
    glm::ivec3 footprintVoxels{1};
    glm::vec3 ghostWorldPosition{0.0f};
    std::string targetPlaceableUuid{};
    std::string targetPlaceablePrototype{};
    int rotationSteps = 0;
    float distance = 0.0f;
};

struct OverlaySmokeScreenState
{
    bool buildCatalogOpen = false;
    std::string selectedCatalogItem{};
    std::string selectedCatalogKey{};
    std::string pendingBuildItemKey{};
    float buildCatalogScrollOffsetY = 0.0f;
    int placementRotationSteps = 0;
    BuildModeState buildModeState = BuildModeState::Inactive;
    BuildPlacementProbe placementProbe{};
};

class OverlaySmokeScreenController
{
public:
    void registerActions(UiActionDispatcher& dispatcher);
    // applies state to the tree through version-counter bindings: only state that
    // changed since the last sync (or a rebuilt tree) is re-applied.
    void syncTree(UiTree& tree, uint64_t treeRevision = 0);
    UiBindingStats lastBindingStats() const { return lastBindingStats_; }

    const OverlaySmokeScreenState& state() const { return state_; }

    bool pendingPlacementActive() const;
    bool scrollBuildCatalog(double wheelYOffset);
    bool rotatePendingPlacement();
    bool updatePlacementProbe(const BuildPlacementProbe& probe);

    // clears state that belongs to the current play session while preserving
    // longer-lived controller data such as the current fish-list binding.
    // returns true when any visible/transient state changed.
    bool resetTransientSessionState();

    // true exactly once after the catalog transitions closed -> open; the caller
    // starts the open fade/slide tweens.
    bool consumeCatalogOpenedTransition();

    // replaces the fish list when it differs from the cached entries; returns true
    // (and bumps the fish-list binding version) only on a real change.
    bool setFishList(std::vector<UiFishListEntry> entries);
    size_t fishListCount() const { return fishList_.size(); }
    bool shouldLogFishList() const { return !fishListLogged_; }
    void markFishListLogged() { fishListLogged_ = true; }

    bool shouldLogBuildCatalogShell() const;
    void markBuildCatalogShellLogged();

    bool shouldLogBuildCatalogScroll() const;
    void markBuildCatalogScrollLogged();

    bool shouldLogBuildGhostPreview() const;
    void markBuildGhostPreviewLogged();

    bool shouldLogBuildPlacementProbe() const;
    void markBuildPlacementProbeLogged();

    bool shouldLogBuildPlacementGhost() const;
    void markBuildPlacementGhostLogged();

    std::string selectedCatalogItemLogValue() const;
    std::string selectedCatalogKeyLogValue() const;
    std::string pendingBuildItemKeyLogValue() const;
    float buildCatalogScrollOffsetY() const;
    int placementRotationSteps() const;
    const char* buildModeStateLogValue() const;

private:
    void clearPendingPlacement();

    OverlaySmokeScreenState state_{};
    std::vector<UiFishListEntry> fishList_{};
    UiDataVersion catalogOpenVersion_{};
    UiDataVersion catalogScrollVersion_{};
    UiDataVersion ghostPreviewVersion_{};
    UiDataVersion fishListVersion_{};
    bool fishListLogged_ = false;
    UiBindingSet bindings_{};
    UiBindingStats lastBindingStats_{};
    bool catalogOpenedTransition_ = false;
    bool buildCatalogLogged_ = false;
    bool buildCatalogScrollLogged_ = false;
    bool buildGhostPreviewLogged_ = false;
    bool buildPlacementProbeLogged_ = false;
    bool buildPlacementGhostLogged_ = false;
};

} // namespace ui
