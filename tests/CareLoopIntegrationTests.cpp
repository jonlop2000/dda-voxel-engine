#include "engine/game/CareRuntime.h"
#include "engine/game/CreatureVitality.h"
#include "engine/scene/SceneConfig.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

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
    return std::fabs(lhs - rhs) <= epsilon;
}

bool sameNeeds(const CreatureNeeds& lhs, const CreatureNeeds& rhs)
{
    return nearlyEqual(lhs.hunger, rhs.hunger) &&
           nearlyEqual(lhs.cleanliness, rhs.cleanliness) &&
           nearlyEqual(lhs.happiness, rhs.happiness) &&
           nearlyEqual(lhs.health, rhs.health);
}

bool sameCreature(const CreatureInstance& lhs, const CreatureInstance& rhs)
{
    return lhs.uuid == rhs.uuid && lhs.speciesId == rhs.speciesId &&
           lhs.displayName == rhs.displayName && lhs.primary == rhs.primary &&
           nearlyEqual(lhs.bond, rhs.bond) &&
           nearlyEqual(lhs.vitality, rhs.vitality) &&
           sameNeeds(lhs.needs, rhs.needs);
}

bool sameWater(const WaterState& lhs, const WaterState& rhs)
{
    return nearlyEqual(lhs.temperatureC, rhs.temperatureC) &&
           nearlyEqual(lhs.oxygen, rhs.oxygen) &&
           nearlyEqual(lhs.flow, rhs.flow) &&
           nearlyEqual(lhs.cleanliness, rhs.cleanliness);
}

bool sameProgression(const ProgressionState& lhs, const ProgressionState& rhs)
{
    return lhs.coins == rhs.coins && lhs.discovery == rhs.discovery &&
           lhs.careMilestone == rhs.careMilestone &&
           lhs.tankTier == rhs.tankTier;
}

engine::game::CareUtcTimePoint utc(std::string_view timestamp)
{
    const auto parsed = engine::game::parseCareUtcTimestamp(timestamp);
    require(parsed.has_value(), "Offline-care test timestamp should be canonical UTC");
    return *parsed;
}

class MutableCareUtcClock final : public engine::game::CareUtcClock
{
public:
    explicit MutableCareUtcClock(engine::game::CareUtcTimePoint value)
        : value_(value)
    {
    }

    engine::game::CareUtcTimePoint now() const override { return value_; }
    void set(engine::game::CareUtcTimePoint value) { value_ = value; }

private:
    engine::game::CareUtcTimePoint value_;
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const fs::path base = fs::temp_directory_path();
        const auto seed = std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count();
        for (uint32_t attempt = 0; attempt < 64; ++attempt)
        {
            const fs::path candidate =
                base / ("dda-voxel-care-loop-" + std::to_string(seed) + "-" +
                        std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error))
            {
                path_ = candidate;
                return;
            }
            if (error)
            {
                throw std::runtime_error("Failed to create offline-care temporary "
                                         "directory: " +
                                         error.message());
            }
        }
        throw std::runtime_error(
            "Could not reserve a unique offline-care temporary directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_{};
};

CreatureInstance makePrimaryCreature()
{
    CreatureInstance primary{};
    primary.uuid = "care-loop-primary-0001";
    primary.speciesId = std::string(engine::game::kStarterFishSpeciesId);
    primary.displayName = "Pip";
    primary.primary = true;
    primary.bond = 0.25f;
    primary.needs.hunger = 0.55f;
    primary.needs.cleanliness = 0.90f;
    primary.needs.happiness = 0.88f;
    primary.needs.health = 0.92f;
    engine::game::refreshCreatureVitality(primary);
    return primary;
}

CreatureInstance makeSecondaryCreature()
{
    CreatureInstance secondary{};
    secondary.uuid = "care-loop-secondary-0001";
    secondary.speciesId = std::string(engine::game::kAxolotlSpeciesId);
    secondary.displayName = "Mochi";
    secondary.primary = false;
    secondary.bond = 0.71f;
    secondary.needs.hunger = 0.32f;
    secondary.needs.cleanliness = 0.77f;
    secondary.needs.happiness = 0.81f;
    secondary.needs.health = 0.86f;
    engine::game::refreshCreatureVitality(secondary);
    return secondary;
}

void requireFiniteCozyCareState(const GameState& state)
{
    const CreatureInstance* primary = engine::game::findPrimaryCreature(state);
    require(primary != nullptr, "Qualified care state should retain its primary");
    const CreatureNeeds& needs = primary->needs;

    require(std::isfinite(state.water.temperatureC) &&
                std::isfinite(state.water.oxygen) &&
                std::isfinite(state.water.flow) &&
                std::isfinite(state.water.cleanliness) &&
                state.water.cleanliness >= 0.60f &&
                state.water.cleanliness <= 1.0f &&
                state.water.oxygen >= 0.55f && state.water.oxygen <= 1.0f,
            "Offline-qualified habitat water should remain finite and recoverable");
    require(std::isfinite(needs.hunger) && needs.hunger >= 0.0f &&
                needs.hunger <= 0.85f &&
                std::isfinite(needs.cleanliness) &&
                needs.cleanliness >= 0.15f && needs.cleanliness <= 1.0f &&
                std::isfinite(needs.happiness) && needs.happiness >= 0.20f &&
                needs.happiness <= 1.0f && std::isfinite(needs.health) &&
                needs.health >= 0.30f && needs.health <= 1.0f &&
                std::isfinite(primary->bond) && primary->bond >= 0.0f &&
                primary->bond <= 1.0f && std::isfinite(primary->vitality) &&
                primary->vitality >= 0.0f && primary->vitality <= 1.0f,
            "Offline-qualified primary care should stay inside finite cozy bounds");
    require(nearlyEqual(primary->vitality,
                        engine::game::deriveCreatureVitality(*primary)),
            "Qualified vitality should match authoritative needs and bond");
}

void testPersistedCareJourney()
{
    TemporaryDirectory tempDirectory;
    const fs::path firstSavePath = tempDirectory.path() / "before_offline.json";
    const fs::path secondSavePath = tempDirectory.path() / "after_offline.json";
    const auto sessionTime = utc("2026-08-13T12:00:00Z");
    const auto returnTime = utc("2026-08-14T06:00:00Z");
    MutableCareUtcClock clock{sessionTime};

    SceneConfig scene{};
    scene.name = "care_loop_integration";
    scene.gameState.water.temperatureC = 24.5f;
    scene.gameState.water.oxygen = 0.80f;
    scene.gameState.water.flow = 0.47f;
    scene.gameState.water.cleanliness = 0.92f;
    scene.gameState.progression.coins = 143;
    scene.gameState.progression.discovery = 9;
    scene.gameState.progression.careMilestone = 4;
    scene.gameState.progression.tankTier = 3;
    scene.gameState.creatures.push_back(makePrimaryCreature());
    scene.gameState.creatures.push_back(makeSecondaryCreature());
    const ProgressionState expectedProgression = scene.gameState.progression;
    const CreatureInstance expectedSecondary = scene.gameState.creatures[1];

    engine::game::CareRuntime sessionRuntime{clock};
    const std::array liveCreatures = {
        engine::game::LiveCreatureSlot{1001,
                                       engine::game::kStarterFishSpeciesId},
        engine::game::LiveCreatureSlot{2002, engine::game::kAxolotlSpeciesId},
    };
    const auto binding = sessionRuntime.synchronizePrimaryCreature(
        scene.gameState, liveCreatures);
    require(binding.liveBound() && binding.runtimeId == 1001 &&
                binding.creatureUuid == "care-loop-primary-0001" &&
                sessionRuntime.primaryCreatureRuntimeId() == 1001,
            "Offline care should begin with a stable persistent-to-live primary binding");

    CreatureInstance* primary = engine::game::findPrimaryCreature(scene.gameState);
    require(primary != nullptr, "Bound primary should be discoverable");
    const float hungerBeforeHeartbeat = primary->needs.hunger;
    const auto heartbeat =
        sessionRuntime.updateGameStateHeartbeat(scene.gameState, 5.0f, true);
    require(heartbeat.changed && heartbeat.waterChanged &&
                heartbeat.creatureChanged && heartbeat.primaryCreatureFound &&
                primary->needs.hunger > hungerBeforeHeartbeat,
            "An active bound session should advance habitat and primary care");

    engine::game::PrimaryCreatureFeedConfig feedConfig{};
    feedConfig.hungerReduction = 0.25f;
    feedConfig.bondGain = 0.05f;
    feedConfig.cooldownSeconds = 0.0f;
    feedConfig.feedbackSeconds = 0.0f;
    const float vitalityBeforeFeed = primary->vitality;
    const auto feed =
        sessionRuntime.feedPrimaryCreature(scene.gameState, feedConfig);
    const auto presentation =
        sessionRuntime.primaryCreaturePresentation(scene.gameState);
    const auto expectedPresentation =
        engine::game::creatureVitalityPresentation(primary->vitality);
    require(feed.fed() && feed.hungerAfter < feed.hungerBefore &&
                nearlyEqual(feed.bondAfter, feed.bondBefore + 0.05f) &&
                nearlyEqual(primary->bond, feed.bondAfter) &&
                !nearlyEqual(primary->vitality, vitalityBeforeFeed) &&
                nearlyEqual(primary->vitality,
                            engine::game::deriveCreatureVitality(*primary)) &&
                nearlyEqual(presentation.pathWobbleScale,
                            expectedPresentation.pathWobbleScale) &&
                nearlyEqual(presentation.bodyMotionScale,
                            expectedPresentation.bodyMotionScale) &&
                nearlyEqual(presentation.tailMotionScale,
                            expectedPresentation.tailMotionScale) &&
                (!nearlyEqual(presentation.pathWobbleScale, 1.0f) ||
                 !nearlyEqual(presentation.bodyMotionScale, 1.0f) ||
                 !nearlyEqual(presentation.tailMotionScale, 1.0f)),
            "Successful runtime feeding should persist hunger, bond, derived vitality, and its visible-response mapping");
    const float hungerAfterFeed = primary->needs.hunger;
    const float bondAfterFeed = primary->bond;
    const CreatureInstance expectedPrimaryAtFirstSave = *primary;
    const WaterState expectedWaterAtFirstSave = scene.gameState.water;

    sessionRuntime.stampLastPlayedUtc(scene.gameState);
    require(scene.gameState.lastPlayedUtc == "2026-08-13T12:00:00Z",
            "The first save should be stamped from the injected session clock");
    require(scene.saveToFile(firstSavePath),
            "The fed care state should save through the scene serializer");

    clock.set(returnTime);
    SceneConfig returnedScene{};
    require(returnedScene.loadFromFile(firstSavePath),
            "The fed care state should reload for the later session");
    const CreatureInstance* returnedPrimary =
        engine::game::findPrimaryCreature(returnedScene.gameState);
    require(returnedPrimary != nullptr &&
                sameCreature(*returnedPrimary, expectedPrimaryAtFirstSave) &&
                sameWater(returnedScene.gameState.water,
                          expectedWaterAtFirstSave) &&
                sameProgression(returnedScene.gameState.progression,
                                expectedProgression) &&
                returnedScene.gameState.creatures.size() == 2 &&
                sameCreature(returnedScene.gameState.creatures[1],
                             expectedSecondary),
            "The first persistence boundary should preserve the fed primary, habitat, progression, and secondary creature");

    engine::game::CareRuntime returnRuntime{clock};
    const auto offline =
        returnRuntime.reconcileOfflineCare(returnedScene.gameState);
    require(offline.timestampStatus ==
                engine::game::OfflineCareTimestampStatus::Applied &&
                offline.elapsedSeconds == 18 * 60 * 60 &&
                offline.appliedSeconds == offline.elapsedSeconds &&
                !offline.capped && offline.primaryCreatureFound &&
                offline.waterChanged && offline.creatureChanged &&
                returnRuntime.primaryCreatureRuntimeId() == 0,
            "The later load should apply one deterministic offline interval without requiring a live binding");

    primary = engine::game::findPrimaryCreature(returnedScene.gameState);
    require(primary != nullptr && primary->uuid == "care-loop-primary-0001" &&
                primary->speciesId == engine::game::kStarterFishSpeciesId &&
                primary->displayName == "Pip" && primary->primary &&
                nearlyEqual(primary->bond, bondAfterFeed) &&
                primary->needs.hunger > hungerAfterFeed &&
                sameProgression(returnedScene.gameState.progression,
                                expectedProgression) &&
                sameCreature(returnedScene.gameState.creatures[1],
                             expectedSecondary),
            "Offline catch-up should preserve primary identity, bond, progression, and every secondary value");
    requireFiniteCozyCareState(returnedScene.gameState);

    returnRuntime.stampLastPlayedUtc(returnedScene.gameState);
    require(returnedScene.gameState.lastPlayedUtc ==
                "2026-08-14T06:00:00Z" &&
                returnedScene.saveToFile(secondSavePath),
            "The consumed offline state should stamp and save at the return clock");

    SceneConfig finalScene{};
    require(finalScene.loadFromFile(secondSavePath),
            "The consumed offline state should survive its second reload");
    const GameState beforeRepeatedReconcile = finalScene.gameState;
    engine::game::CareRuntime finalRuntime{clock};
    const auto repeated = finalRuntime.reconcileOfflineCare(finalScene.gameState);
    require(repeated.timestampStatus ==
                engine::game::OfflineCareTimestampStatus::Current &&
                repeated.elapsedSeconds == 0 && repeated.appliedSeconds == 0 &&
                !repeated.stateChanged() && !repeated.timestampChanged &&
                finalScene.gameState.lastPlayedUtc ==
                    beforeRepeatedReconcile.lastPlayedUtc &&
                sameWater(finalScene.gameState.water,
                          beforeRepeatedReconcile.water),
            "Save, reload, and same-time reconcile should consume offline care exactly once");

    primary = engine::game::findPrimaryCreature(finalScene.gameState);
    const CreatureInstance* previousPrimary =
        engine::game::findPrimaryCreature(beforeRepeatedReconcile);
    require(primary != nullptr && previousPrimary != nullptr &&
                sameCreature(*primary, *previousPrimary) &&
                sameProgression(finalScene.gameState.progression,
                                expectedProgression) &&
                finalScene.gameState.creatures.size() == 2 &&
                sameCreature(finalScene.gameState.creatures[1],
                             expectedSecondary),
            "Idempotent reconciliation should preserve the complete persisted care journey");
    requireFiniteCozyCareState(finalScene.gameState);

    const std::array rebuiltLiveCreatures = {
        engine::game::LiveCreatureSlot{3003,
                                       engine::game::kStarterFishSpeciesId},
    };
    const auto rebuiltBinding = finalRuntime.synchronizePrimaryCreature(
        finalScene.gameState, rebuiltLiveCreatures);
    const auto rebuiltPresentation =
        finalRuntime.primaryCreaturePresentation(finalScene.gameState);
    const auto expectedRebuiltPresentation =
        engine::game::creatureVitalityPresentation(primary->vitality);
    require(rebuiltBinding.liveBound() && rebuiltBinding.runtimeId == 3003 &&
                rebuiltBinding.creatureUuid == "care-loop-primary-0001" &&
                finalRuntime.primaryCreatureRuntimeId() == 3003 &&
                nearlyEqual(rebuiltPresentation.pathWobbleScale,
                            expectedRebuiltPresentation.pathWobbleScale) &&
                nearlyEqual(rebuiltPresentation.bodyMotionScale,
                            expectedRebuiltPresentation.bodyMotionScale) &&
                nearlyEqual(rebuiltPresentation.tailMotionScale,
                            expectedRebuiltPresentation.tailMotionScale) &&
                (!nearlyEqual(rebuiltPresentation.pathWobbleScale, 1.0f) ||
                 !nearlyEqual(rebuiltPresentation.bodyMotionScale, 1.0f) ||
                 !nearlyEqual(rebuiltPresentation.tailMotionScale, 1.0f)),
            "A fresh disposable runtime id should rebind to the persisted primary and recover its vitality presentation");
}

} // namespace

int main()
{
    try
    {
        testPersistedCareJourney();
        std::cout << "Care loop integration tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Care loop integration test failure: " << e.what() << "\n";
        return 1;
    }
}
