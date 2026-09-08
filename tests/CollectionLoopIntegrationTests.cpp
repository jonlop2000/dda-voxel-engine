#include "engine/game/CareRuntime.h"
#include "engine/game/CollectionCodex.h"
#include "engine/game/CreatureRuntimeBinding.h"
#include "engine/game/Placeables.h"
#include "engine/game/SpeciesIds.h"
#include "engine/game/SpeciesRegistry.h"
#include "engine/scene/SceneConfig.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

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

bool nearlyEqual(float lhs, float rhs, float epsilon = 1e-6f)
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

bool sameGameState(const GameState& lhs, const GameState& rhs)
{
    if (lhs.lastPlayedUtc != rhs.lastPlayedUtc ||
        !nearlyEqual(lhs.water.temperatureC, rhs.water.temperatureC) ||
        !nearlyEqual(lhs.water.oxygen, rhs.water.oxygen) ||
        !nearlyEqual(lhs.water.flow, rhs.water.flow) ||
        !nearlyEqual(lhs.water.cleanliness, rhs.water.cleanliness) ||
        lhs.progression.coins != rhs.progression.coins ||
        lhs.progression.discovery != rhs.progression.discovery ||
        lhs.progression.careMilestone != rhs.progression.careMilestone ||
        lhs.progression.tankTier != rhs.progression.tankTier ||
        lhs.creatures.size() != rhs.creatures.size())
    {
        return false;
    }
    for (size_t index = 0; index < lhs.creatures.size(); ++index)
    {
        if (!sameCreature(lhs.creatures[index], rhs.creatures[index]))
        {
            return false;
        }
    }
    return true;
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto seed = std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count();
        for (uint32_t attempt = 0; attempt < 64; ++attempt)
        {
            const fs::path candidate =
                fs::temp_directory_path() /
                ("dda-voxel-collection-loop-" + std::to_string(seed) + "-" +
                 std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error))
            {
                path_ = candidate;
                return;
            }
            if (error)
            {
                throw std::runtime_error(
                    "Failed to create collection-loop temporary directory: " +
                    error.message());
            }
        }
        throw std::runtime_error(
            "Could not reserve a unique collection-loop temporary directory");
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

CreatureInstance makeCreature(std::string uuid, std::string speciesId,
                              std::string displayName, bool primary = false)
{
    CreatureInstance creature{};
    creature.uuid = std::move(uuid);
    creature.speciesId = std::move(speciesId);
    creature.displayName = std::move(displayName);
    creature.primary = primary;
    creature.bond = primary ? 0.42f : 0.18f;
    creature.needs.hunger = primary ? 0.25f : 0.12f;
    creature.needs.cleanliness = 0.90f;
    creature.needs.happiness = 0.88f;
    creature.needs.health = 0.92f;
    engine::game::refreshCreatureVitality(creature);
    return creature;
}

const engine::game::CollectionCodexEntry* findCodexEntry(
    const engine::game::CollectionCodexView& view, std::string_view speciesId)
{
    for (const auto& entry : view.entries)
    {
        if (entry.speciesId == speciesId)
        {
            return &entry;
        }
    }
    return nullptr;
}

SceneConfig makeCollectionScene()
{
    SceneConfig scene{};
    scene.name = "collection_loop_integration";
    // twenty-two degrees is the shared boundary of the starter fish and axolotl
    // bands; every built-in species accepts the remaining readings below.
    scene.gameState.water = {22.0f, 0.82f, 0.30f, 1.0f};
    scene.gameState.progression.coins = 321;
    scene.gameState.progression.discovery = 17;
    scene.gameState.progression.careMilestone = 6;
    scene.gameState.progression.tankTier = 3;
    scene.gameState.lastPlayedUtc = "2026-08-17T12:00:00Z";
    scene.gameState.creatures = {
        makeCreature("collection-starter-1",
                     std::string(engine::game::kStarterFishSpeciesId), "Pip",
                     true),
        makeCreature("collection-axolotl-1",
                     std::string(engine::game::kAxolotlSpeciesId), "Mochi"),
        makeCreature("collection-snail-1",
                     std::string(engine::game::kRamshornSnailSpeciesId),
                     "Pebble"),
        makeCreature("collection-snail-2",
                     std::string(engine::game::kRamshornSnailSpeciesId),
                     "Ripple"),
    };
    return scene;
}

void requireCompleteCodex(
    const engine::game::CollectionCodexView& view)
{
    require(view.registeredSpeciesCount == 3 &&
                view.totalOwnedCreatures == 4 && view.entries.size() == 3 &&
                view.knownOwnedSpeciesCount == 3 &&
                view.unknownOwnedSpeciesCount == 0,
            "The persisted collection should expose every supported species");
    require(view.entries[0].speciesId == engine::game::kStarterFishSpeciesId &&
                view.entries[1].speciesId == engine::game::kAxolotlSpeciesId &&
                view.entries[2].speciesId ==
                    engine::game::kRamshornSnailSpeciesId &&
                view.entries[2].ownedCount == 2,
            "The Codex should preserve registry order and aggregate owned instances");
    for (const auto& entry : view.entries)
    {
        require(entry.knownSpecies() && entry.habitat.compatible() &&
                    entry.habitat.unsatisfiedRequirementCount == 0,
                "Every supported species should expose a complete queryable habitat");
    }
    const auto* snail =
        findCodexEntry(view, engine::game::kRamshornSnailSpeciesId);
    require(snail != nullptr &&
                engine::game::hasSpeciesRole(
                    snail->roles, engine::game::SpeciesRole::CleanupCrew) &&
                view.cleanupCrew.creatureCount == 2 &&
                nearlyEqual(view.cleanupCrew.cleanlinessDecayReduction, 0.40f) &&
                nearlyEqual(view.cleanupCrew.cleanlinessDecayMultiplier, 0.60f),
            "The Codex should expose the owned cleanup role and its measurable benefit");
}

void testPersistedCollectionJourney()
{
    require(engine::game::validateSpeciesRegistry(
                engine::game::registeredSpecies()).empty(),
            "The collection loop requires the complete built-in registry to validate");
    for (const auto& definition : engine::game::registeredSpecies())
    {
        require(engine::game::findSpecies(definition.id) == &definition,
                "Every supported species should be available through exact lookup");
    }

    SceneConfig scene = makeCollectionScene();
    const std::array decor = {
        engine::game::HabitatDecorContribution{
            PlaceableMaterialCategory::Rock, 1},
    };
    requireCompleteCodex(engine::game::buildCollectionCodexView(
        scene.gameState, decor));

    const GameState beforeBinding = scene.gameState;
    const std::array firstLiveSlots = {
        engine::game::LiveCreatureSlot{
            2202, engine::game::kAxolotlSpeciesId, 0},
        engine::game::LiveCreatureSlot{
            1101, engine::game::kStarterFishSpeciesId, 0},
    };
    const auto firstBindings = engine::game::bindCreatureRosterToLiveSlots(
        scene.gameState, firstLiveSlots);
    require(firstBindings.bindings.size() == 2 &&
                firstBindings.unmatchedPersistentCount == 2 &&
                firstBindings.unmatchedLiveCount == 0 &&
                firstBindings.findByCreatureUuid("collection-starter-1") !=
                    nullptr &&
                firstBindings.findByCreatureUuid("collection-starter-1")
                        ->runtimeId == 1101 &&
                firstBindings.findByCreatureUuid("collection-axolotl-1") !=
                    nullptr &&
                firstBindings.findByCreatureUuid("collection-axolotl-1")
                        ->runtimeId == 2202 &&
                sameGameState(scene.gameState, beforeBinding),
            "Renderable species should bind by persistent identity while collection-only snails remain safely unmatched");

    engine::game::CareRuntime collectionRuntime{};
    const auto primaryBinding = collectionRuntime.synchronizeCreatureRoster(
        scene.gameState, firstLiveSlots);
    require(primaryBinding.liveBound() && primaryBinding.runtimeId == 1101 &&
                primaryBinding.creatureUuid == "collection-starter-1" &&
                collectionRuntime.creatureRuntimeBindings().size() == 2,
            "The collection runtime should retain the complete renderable binding set");

    GameState noCleanupControl = scene.gameState;
    noCleanupControl.creatures.erase(noCleanupControl.creatures.begin() + 2,
                                     noCleanupControl.creatures.end());
    engine::game::CareRuntime controlRuntime{};
    controlRuntime.synchronizeCreatureRoster(noCleanupControl, firstLiveSlots);

    const auto collectionHeartbeat = collectionRuntime.updateGameStateHeartbeat(
        scene.gameState, 5.0f, true);
    const auto controlHeartbeat = controlRuntime.updateGameStateHeartbeat(
        noCleanupControl, 5.0f, true);
    require(collectionHeartbeat.changed && collectionHeartbeat.waterChanged &&
                collectionHeartbeat.cleanupCrewCount == 2 &&
                nearlyEqual(collectionHeartbeat.cleanlinessDecayReduction,
                            0.40f) &&
                controlHeartbeat.cleanupCrewCount == 0 &&
                nearlyEqual(scene.gameState.water.cleanliness, 0.994f) &&
                nearlyEqual(noCleanupControl.water.cleanliness, 0.990f) &&
                nearlyEqual(scene.gameState.water.oxygen,
                            noCleanupControl.water.oxygen),
            "Two cleanup creatures should reduce five-second cleanliness loss by exactly 40% without changing oxygen decay");

    const GameState beforeAdvisory = scene.gameState;
    GameState unsuitableHabitat = scene.gameState;
    unsuitableHabitat.water = {32.0f, 0.20f, 0.95f, 0.20f};
    const GameState unsuitableBefore = unsuitableHabitat;
    const auto advisory = engine::game::buildCollectionCodexView(unsuitableHabitat);
    const auto* starter =
        findCodexEntry(advisory, engine::game::kStarterFishSpeciesId);
    const auto unsuitableBindings =
        engine::game::bindCreatureRosterToLiveSlots(unsuitableHabitat,
                                                     firstLiveSlots);
    require(starter != nullptr && !starter->habitat.compatible() &&
                starter->habitat.unsatisfiedRequirementCount > 0 &&
                unsuitableBindings.bindings.size() == 2 &&
                sameGameState(unsuitableHabitat, unsuitableBefore) &&
                sameGameState(scene.gameState, beforeAdvisory),
            "Unsatisfied habitat advice must not mutate, reject, acquire, or prevent identity binding for any creature");

    TemporaryDirectory tempDirectory;
    const fs::path savePath = tempDirectory.path() / "collection_roundtrip.json";
    const GameState expectedSavedState = scene.gameState;
    require(scene.saveToFile(savePath),
            "The complete collection should serialize through schema v7");

    SceneConfig reloaded{};
    require(reloaded.loadFromFile(savePath) &&
                sameGameState(reloaded.gameState, expectedSavedState),
            "Collection identity, care, water, progression, roles-by-ID, and timestamp should survive reload");
    requireCompleteCodex(engine::game::buildCollectionCodexView(
        reloaded.gameState, decor));

    const GameState beforeRebind = reloaded.gameState;
    const std::array rebuiltLiveSlots = {
        engine::game::LiveCreatureSlot{
            9101, engine::game::kStarterFishSpeciesId, 0},
        engine::game::LiveCreatureSlot{
            9202, engine::game::kAxolotlSpeciesId, 0},
    };
    const auto rebuilt = engine::game::bindCreatureRosterToLiveSlots(
        reloaded.gameState, rebuiltLiveSlots);
    require(rebuilt.bindings.size() == 2 &&
                rebuilt.unmatchedPersistentCount == 2 &&
                rebuilt.findByCreatureUuid("collection-starter-1") != nullptr &&
                rebuilt.findByCreatureUuid("collection-starter-1")->runtimeId ==
                    9101 &&
                rebuilt.findByCreatureUuid("collection-axolotl-1") != nullptr &&
                rebuilt.findByCreatureUuid("collection-axolotl-1")->runtimeId ==
                    9202 &&
                sameGameState(reloaded.gameState, beforeRebind),
            "Reload should preserve durable UUIDs while allowing disposable runtime ids to rebuild without roster mutation");
}

} // namespace

int main()
{
    try
    {
        testPersistedCollectionJourney();
        std::cout << "Collection loop integration tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Collection loop integration test failure: " << error.what()
                  << '\n';
        return 1;
    }
}
