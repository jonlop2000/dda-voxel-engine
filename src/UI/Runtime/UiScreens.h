#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "UI/Runtime/BuildPlacement.h"
#include "UI/Runtime/CollectionCodexScreen.h"
#include "UI/Runtime/UiElement.h"

namespace ui
{

inline constexpr std::string_view kOverlaySmokeScreenId = "ui_overlay_smoke";
inline constexpr std::string_view kPauseScreenId = "ui_pause_smoke";
inline constexpr std::string_view kMainMenuScreenId = "ui_main_menu";
inline constexpr std::string_view kMainMenuOptionsScreenId = "ui_main_menu_options";
inline constexpr std::string_view kResumeGameAction = "resume_game";
inline constexpr std::string_view kReturnMainMenuAction = "return_to_main_menu";
inline constexpr std::string_view kStartAquariumAction = "start_aquarium";
inline constexpr std::string_view kOpenMainMenuOptionsAction = "open_main_menu_options";
inline constexpr std::string_view kCloseMainMenuOptionsAction = "close_main_menu_options";
inline constexpr std::string_view kToggleFishWorldLabelsAction =
    "toggle_fish_world_labels";
inline constexpr std::string_view kMaintainWaterAction = "maintain_water";
inline constexpr std::string_view kFeedPrimaryCreatureAction =
    "feed_primary_creature";
inline constexpr float kMaintainWaterCleanlinessThreshold = 0.95f;
inline constexpr float kMaintainWaterOxygenBaseline = 0.82f;
inline constexpr std::string_view kPauseResumeButtonId = "pause_resume";
inline constexpr std::string_view kPauseOptionsButtonId = "pause_options";
inline constexpr std::string_view kPauseMainMenuButtonId = "pause_main_menu";
inline constexpr std::string_view kPauseBackdropId = "pause_backdrop";
inline constexpr std::string_view kMainMenuStartButtonId = "main_menu_start";
inline constexpr std::string_view kMainMenuOptionsButtonId = "main_menu_options";
inline constexpr std::string_view kMainMenuOptionsBackButtonId = "main_menu_options_back";
inline constexpr std::string_view kMainMenuOptionLabelsButtonId =
    "main_menu_option_labels";
inline constexpr std::string_view kFishListPanelId = "fish_list_panel";
inline constexpr std::string_view kFishListContentId = "fish_list_content";
inline constexpr std::string_view kFocusFishAction = "focus_on_fish";
inline constexpr std::string_view kReleaseFishFocusAction = "release_fish_focus";
inline constexpr std::string_view kFishFocusReleaseButtonId = "fish_focus_release";
inline constexpr std::string_view kFishWorldLabelLayerId = "fish_world_label_layer";
inline constexpr std::string_view kMaintainWaterButtonId = "water_maintain";
inline constexpr std::string_view kFeedPrimaryCreatureButtonId =
    "primary_creature_feed";

enum class UiCreatureFeedState
{
    NoCreature,
    CreatureUnavailable,
    Ready,
    NotHungry,
    Cooldown,
    FedFeedback,
};

// one row in the runtime fish list sidebar; rows are rebuilt through the binding
// layer whenever the engine fish list changes.
struct UiFishListEntry
{
    uint64_t id = 0;
    std::string label{};
    bool focused = false;

    bool operator==(const UiFishListEntry&) const = default;
};

struct UiFishWorldLabelEntry
{
    uint64_t id = 0;
    std::string label{};
    float screenX = 0.0f;
    float screenY = 0.0f;
    bool focused = false;
    // focused-axolotl celebration bubble: mexico flag vs england flag with the
    // label rendered as a typing banner (the caller passes the typed substring).
    bool axolotlVersus = false;

    bool operator==(const UiFishWorldLabelEntry&) const = default;
};

struct UiMainMenuOptionsState
{
    bool fishWorldLabelsVisible = true;

    bool operator==(const UiMainMenuOptionsState&) const = default;
};

struct UiOverlaySmokeHudStatus
{
    uint32_t fishCount = 0;
    std::string timeOfDayLabel{"DAY"};
    float waterTemperatureC = 24.0f;
    float waterOxygen = 0.82f;
    float waterFlow = 0.45f;
    float waterCleanliness = 1.0f;
    bool waterMaintenanceFeedbackActive = false;
    std::string primaryCreatureName{"NO CREATURE"};
    float primaryCreatureHunger = 0.0f;
    float primaryCreatureCleanliness = 1.0f;
    float primaryCreatureHappiness = 1.0f;
    float primaryCreatureHealth = 1.0f;
    UiCreatureFeedState creatureFeedState = UiCreatureFeedState::NoCreature;
    float creatureFeedCooldownRemaining = 0.0f;

    bool operator==(const UiOverlaySmokeHudStatus&) const = default;
};
inline constexpr std::string_view kOpenBuildCatalogAction = "open_build_catalog";
inline constexpr std::string_view kCloseBuildCatalogAction = "close_build_catalog";
inline constexpr std::string_view kSelectBuildCatalogItemAction =
    "select_build_catalog_item";
inline constexpr std::string_view kSelectRemovePlacementAction =
    "select_remove_placement";
inline constexpr std::string_view kCancelBuildPlacementAction =
    "cancel_build_placement";
inline constexpr std::string_view kCommitBuildPlacementAction =
    "commit_build_placement";
inline constexpr std::string_view kRemoveBuildPlacementAction =
    "remove_build_placement";
inline constexpr std::string_view kPendingPlacementShortcutId =
    "pending_placement_shortcut";
inline constexpr std::string_view kPendingPlacementWorldId =
    "pending_placement_world";
inline constexpr std::string_view kBuildCatalogCancelId = "build_catalog_cancel";
inline constexpr std::string_view kBuildCatalogRemoveId = "build_catalog_remove";
inline constexpr std::string_view kBuildCatalogListViewportId =
    "build_catalog_list_view";
inline constexpr std::string_view kBuildCatalogListContentId =
    "build_catalog_list_content";
inline constexpr std::string_view kBuildRemoveKey = "remove";
inline constexpr std::string_view kBuildGhostPreviewId = "build_ghost_preview";
inline constexpr std::string_view kBuildGhostPreviewLabelId =
    "build_ghost_preview_label";
inline constexpr std::string_view kBuildGhostPreviewHintId =
    "build_ghost_preview_hint";
bool isRuntimeUiScreenRegistered(std::string_view screenId);
std::span<const BuildCatalogItemDefinition> overlaySmokeBuildCatalogItems();
UiTree makeRuntimeUiScreenTree(std::string_view screenId);
UiTree makeOverlaySmokeTree();
UiTree makePauseScreenTree();
UiTree makeMainMenuScreenTree();
UiTree makeMainMenuOptionsScreenTree();
bool setOverlaySmokeViewport(UiTree& tree, float width, float height);
bool setPauseScreenViewport(UiTree& tree, float width, float height);
bool setMainMenuViewport(UiTree& tree, float width, float height);
bool setRuntimeUiOverlayViewport(UiTree& tree, float width, float height);
bool setMainMenuOptionsState(UiTree& tree, const UiMainMenuOptionsState& state);
bool setOverlaySmokeHudStatus(UiTree& tree, const UiOverlaySmokeHudStatus& status);
void setOverlaySmokeBuildCatalogOpen(UiTree& tree, bool open);
float overlaySmokeBuildCatalogMaxScrollOffset();
void setOverlaySmokeBuildCatalogScrollOffset(UiTree& tree, float scrollOffsetY);
float overlaySmokeFishListMaxScrollOffset(const UiTree& tree);
float overlaySmokeFishListScrollOffset(const UiTree& tree);
bool setOverlaySmokeFishListScrollOffset(UiTree& tree, float scrollOffsetY);
// consumes a wheel event only while the pointer is inside the clipped fish-list
// viewport. the retained-tree offset is clamped to the current roster extent.
bool handleOverlaySmokeFishListWheel(UiTree& tree, float pointerX, float pointerY,
                                     double wheelYOffset);
void setOverlaySmokeBuildGhostPreview(UiTree& tree, bool visible,
                                      std::string_view itemKey,
                                      int rotationSteps = 0);
void setOverlaySmokeFishList(UiTree& tree, std::span<const UiFishListEntry> entries);
bool setOverlaySmokeFishWorldLabels(UiTree& tree,
                                    std::span<const UiFishWorldLabelEntry> entries);

} // namespace ui
