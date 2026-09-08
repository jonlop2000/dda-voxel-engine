#include "engine/game/CareRuntime.h"
#include "engine/game/CreatureRuntimeBinding.h"
#include "engine/game/SpeciesIds.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

CreatureInstance makeCreature(std::string uuid, std::string species, bool primary)
{
    CreatureInstance creature{};
    creature.uuid = std::move(uuid);
    creature.speciesId = std::move(species);
    creature.displayName = creature.uuid;
    creature.primary = primary;
    return creature;
}

void testMultipleSameSpeciesBindByStableOrdinal()
{
    GameState state{};
    state.creatures.push_back(makeCreature("starter-a", "starter_fish", true));
    state.creatures.push_back(makeCreature("starter-b", "starter_fish", false));
    state.creatures.push_back(makeCreature("axo-a", "axolotl", false));

    const std::array live = {
        engine::game::LiveCreatureSlot{90, engine::game::kStarterFishSpeciesId, 1},
        engine::game::LiveCreatureSlot{70, engine::game::kAxolotlSpeciesId, 0},
        engine::game::LiveCreatureSlot{80, engine::game::kStarterFishSpeciesId, 0},
    };
    const auto bindings =
        engine::game::bindCreatureRosterToLiveSlots(state, live);

    require(bindings.bindings.size() == 3 &&
                bindings.unmatchedPersistentCount == 0 &&
                bindings.unmatchedLiveCount == 0,
            "Every saved creature should bind one-to-one when a matching live slot exists");
    require(bindings.findByCreatureUuid("starter-a")->runtimeId == 80 &&
                bindings.findByCreatureUuid("starter-b")->runtimeId == 90 &&
                bindings.findByCreatureUuid("axo-a")->runtimeId == 70,
            "Same-species saved identities should bind by stable live ordinal, not discovery order");
    require(bindings.findByRuntimeId(90)->creatureUuid == "starter-b" &&
                bindings.contains("axo-a", 70),
            "The collection binding should support UUID and runtime-id lookup");
}

void testRebuildReplacesOnlyDisposableRuntimeIds()
{
    GameState state{};
    state.creatures.push_back(makeCreature("starter-a", "starter_fish", true));
    state.creatures.push_back(makeCreature("starter-b", "starter_fish", false));
    state.creatures.push_back(makeCreature("axo-a", "axolotl", false));

    const std::array firstLive = {
        engine::game::LiveCreatureSlot{10, engine::game::kStarterFishSpeciesId, 0},
        engine::game::LiveCreatureSlot{20, engine::game::kStarterFishSpeciesId, 1},
        engine::game::LiveCreatureSlot{30, engine::game::kAxolotlSpeciesId, 0},
    };
    const std::array rebuiltLive = {
        engine::game::LiveCreatureSlot{300, engine::game::kAxolotlSpeciesId, 0},
        engine::game::LiveCreatureSlot{200, engine::game::kStarterFishSpeciesId, 1},
        engine::game::LiveCreatureSlot{100, engine::game::kStarterFishSpeciesId, 0},
    };

    const auto first =
        engine::game::bindCreatureRosterToLiveSlots(state, firstLive);
    const auto rebuilt =
        engine::game::bindCreatureRosterToLiveSlots(state, rebuiltLive);

    require(first.findByCreatureUuid("starter-a")->runtimeId == 10 &&
                rebuilt.findByCreatureUuid("starter-a")->runtimeId == 100 &&
                first.findByCreatureUuid("starter-b")->runtimeId == 20 &&
                rebuilt.findByCreatureUuid("starter-b")->runtimeId == 200 &&
                first.findByCreatureUuid("axo-a")->runtimeId == 30 &&
                rebuilt.findByCreatureUuid("axo-a")->runtimeId == 300,
            "A rebuild should preserve every persistent UUID-to-slot relationship while replacing runtime ids");
    require(state.creatures.size() == 3 && state.creatures[0].uuid == "starter-a" &&
                state.creatures[1].uuid == "starter-b" &&
                state.creatures[2].uuid == "axo-a",
            "Pure runtime binding must not mutate collection identity or roster order");
}

void testUnmatchedLiveSlotsDoNotAcquireCreatures()
{
    GameState state{};
    state.creatures.push_back(makeCreature("owned-a", "starter_fish", true));
    const std::array live = {
        engine::game::LiveCreatureSlot{1, engine::game::kStarterFishSpeciesId, 0},
        engine::game::LiveCreatureSlot{2, engine::game::kStarterFishSpeciesId, 1},
        engine::game::LiveCreatureSlot{3, engine::game::kAxolotlSpeciesId, 0},
    };

    const auto bindings =
        engine::game::bindCreatureRosterToLiveSlots(state, live);
    require(bindings.bindings.size() == 1 && bindings.unmatchedLiveCount == 2 &&
                state.creatures.size() == 1,
            "Creature binding must report unmatched visuals without inventing an acquisition or mutating the roster");
}

void testInvalidAndAmbiguousLiveSlotsAreDiagnosed()
{
    GameState state{};
    state.creatures.push_back(makeCreature("starter-a", "starter_fish", true));
    state.creatures.push_back(makeCreature("starter-b", "starter_fish", false));
    const std::array live = {
        engine::game::LiveCreatureSlot{10, engine::game::kStarterFishSpeciesId, 0},
        engine::game::LiveCreatureSlot{10, engine::game::kAxolotlSpeciesId, 0},
        engine::game::LiveCreatureSlot{0, engine::game::kStarterFishSpeciesId, 2},
        engine::game::LiveCreatureSlot{11, engine::game::kStarterFishSpeciesId, 0},
    };

    const auto bindings =
        engine::game::bindCreatureRosterToLiveSlots(state, live);
    require(bindings.bindings.empty() &&
                bindings.duplicateRuntimeIdCount == 1 &&
                bindings.invalidLiveSlotCount == 1 &&
                bindings.duplicateStableSlotCount == 1 &&
                bindings.unmatchedPersistentCount == 2 &&
                bindings.unmatchedLiveCount == 2,
            "Invalid runtime ids and duplicate handles should be diagnosed, while ambiguous stable slots fail closed as unmatched");
}

void testCareHeartbeatRequiresCurrentPrimaryUuidBinding()
{
    GameState state{};
    state.creatures.push_back(makeCreature("primary-a", "starter_fish", true));
    state.creatures.push_back(makeCreature("primary-b", "starter_fish", false));
    state.creatures[0].needs.hunger = 0.10f;
    state.creatures[1].needs.hunger = 0.20f;
    const std::array live = {
        engine::game::LiveCreatureSlot{101, engine::game::kStarterFishSpeciesId, 0},
        engine::game::LiveCreatureSlot{202, engine::game::kStarterFishSpeciesId, 1},
    };

    engine::game::CareRuntime runtime{};
    const auto initial = runtime.synchronizeCreatureRoster(state, live);
    require(initial.runtimeId == 101 &&
                runtime.creatureRuntimeId("primary-a") == 101 &&
                runtime.creatureRuntimeId("primary-b") == 202 &&
                runtime.creatureUuidForRuntimeId(202) == "primary-b",
            "CareRuntime should retain the complete roster binding, not only the primary handle");

    state.creatures[0].primary = false;
    state.creatures[1].primary = true;
    const float stalePrimaryHunger = state.creatures[1].needs.hunger;
    const auto stale = runtime.updateGameStateHeartbeat(state, 5.0f, true);
    require(stale.waterChanged && !stale.creatureChanged &&
                !stale.primaryCreatureFound &&
                state.creatures[1].needs.hunger == stalePrimaryHunger,
            "A primary switch must pause creature simulation until UUID and runtime binding are synchronized");

    const auto rebound = runtime.synchronizeCreatureRoster(state, live);
    const auto active = runtime.updateGameStateHeartbeat(state, 5.0f, true);
    require(rebound.creatureUuid == "primary-b" && rebound.runtimeId == 202 &&
                active.creatureChanged && active.primaryCreatureFound &&
                state.creatures[1].needs.hunger > stalePrimaryHunger,
            "After synchronization, the newly selected primary should tick through its own exact binding");

    const float speciesMismatchHunger = state.creatures[1].needs.hunger;
    state.creatures[1].speciesId = "axolotl";
    const auto speciesMismatch = runtime.updateGameStateHeartbeat(state, 5.0f, true);
    require(speciesMismatch.waterChanged && !speciesMismatch.creatureChanged &&
                !speciesMismatch.primaryCreatureFound &&
                state.creatures[1].needs.hunger == speciesMismatchHunger,
            "A saved species change must invalidate a stale UUID/runtime binding until resynchronization");
}

} // namespace

int main()
{
    try
    {
        testMultipleSameSpeciesBindByStableOrdinal();
        testRebuildReplacesOnlyDisposableRuntimeIds();
        testUnmatchedLiveSlotsDoNotAcquireCreatures();
        testInvalidAndAmbiguousLiveSlotsAreDiagnosed();
        testCareHeartbeatRequiresCurrentPrimaryUuidBinding();
        std::cout << "Creature runtime binding tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Creature runtime binding test failure: " << error.what()
                  << "\n";
        return 1;
    }
}
