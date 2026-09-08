#include "engine/game/CreatureCareState.h"

#include <array>
#include <cmath>
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
    creature.primary = primary;
    return creature;
}

void testEmptyRosterStaysEmptyWithoutLiveCreature()
{
    GameState state{};
    const auto result = engine::game::normalizeCreatureRoster(state);
    require(!result.changed && state.creatures.empty(),
            "An empty roster must stay empty when no live creature exists");
}

void testLiveStarterCreatesDeterministicPrimary()
{
    GameState state{};
    const std::array live = {
        engine::game::LiveCreatureSlot{41, engine::game::kStarterFishSpeciesId},
        engine::game::LiveCreatureSlot{99, engine::game::kAxolotlSpeciesId},
    };
    const auto sync = engine::game::synchronizePrimaryCreature(state, live);

    require(sync.normalization.createdPrimary && sync.liveBound(),
            "The first live creature should create and bind a primary care record");
    require(sync.runtimeId == 41 &&
                sync.creatureUuid == engine::game::kDefaultStarterCreatureUuid,
            "The starter fish binding should use deterministic persistent identity");
    require(state.creatures.size() == 1 && state.creatures.front().primary,
            "Exactly one primary creature should be created");
}

void testRebuildChangesOnlyDisposableRuntimeBinding()
{
    GameState state{};
    const std::array firstLive = {
        engine::game::LiveCreatureSlot{10, engine::game::kStarterFishSpeciesId},
    };
    const auto first = engine::game::synchronizePrimaryCreature(state, firstLive);
    state.creatures.front().bond = 0.65f;
    state.creatures.front().needs.hunger = 0.35f;

    const std::array rebuiltLive = {
        engine::game::LiveCreatureSlot{500, engine::game::kStarterFishSpeciesId},
    };
    const auto rebuilt = engine::game::synchronizePrimaryCreature(state, rebuiltLive);

    require(first.creatureUuid == rebuilt.creatureUuid && rebuilt.runtimeId == 500,
            "A rebuild should rebind the same persistent UUID to the new runtime id");
    require(state.creatures.front().bond == 0.65f &&
                state.creatures.front().needs.hunger == 0.35f,
            "A rebuild must not replace authoritative care state");
}

void testPrimaryFlagsAndDuplicateUuidsRepairDeterministically()
{
    GameState state{};
    state.creatures.push_back(makeCreature("creature-a", "starter_fish", true));
    state.creatures.push_back(makeCreature("creature-a", "starter_fish", true));
    state.creatures.push_back(makeCreature("creature-c", "axolotl", true));

    const auto result = engine::game::normalizeCreatureRoster(state);
    require(result.changed && result.clearedExtraPrimaryFlags == 2 &&
                result.repairedDuplicateUuids == 1,
            "Roster normalization should repair duplicate identity and extra primaries");
    require(state.creatures[0].primary && !state.creatures[1].primary &&
                !state.creatures[2].primary,
            "The first explicit valid primary should win deterministically");
    require(state.creatures[1].uuid == "creature-a-duplicate-2",
            "Duplicate UUID repair should be stable and readable");

    const auto second = engine::game::normalizeCreatureRoster(state);
    require(!second.changed,
            "A normalized creature roster should be idempotent on the next pass");
}

void testSavedSpeciesControlsLiveBinding()
{
    GameState state{};
    state.creatures.push_back(makeCreature("creature-axo-1", "axolotl", true));
    const std::array live = {
        engine::game::LiveCreatureSlot{7, engine::game::kStarterFishSpeciesId},
        engine::game::LiveCreatureSlot{8, engine::game::kAxolotlSpeciesId},
    };

    const auto sync = engine::game::synchronizePrimaryCreature(state, live);
    require(sync.creatureUuid == "creature-axo-1" && sync.runtimeId == 8,
            "A saved primary must bind by species instead of current list position");
}

void testFeedCreatureIsBoundedAndRejectsNoOp()
{
    CreatureInstance creature{};
    creature.needs.hunger = 0.72f;
    engine::game::CreatureFeedConfig config{};
    config.hungerReduction = 0.30f;
    config.minimumHunger = 0.01f;
    config.bondGain = 0.02f;

    const auto fed = engine::game::feedCreature(creature, config);
    require(fed.fed && std::abs(fed.hungerBefore - 0.72f) < 0.0001f &&
                std::abs(fed.hungerAfter - 0.42f) < 0.0001f &&
                std::abs(creature.needs.hunger - 0.42f) < 0.0001f &&
                std::abs(fed.bondBefore) < 0.0001f &&
                std::abs(fed.bondAfter - 0.02f) < 0.0001f,
            "Feeding should reduce hunger and grant bounded bond progress");

    creature.needs.hunger = 0.005f;
    const auto notHungry = engine::game::feedCreature(creature, config);
    require(!notHungry.fed && creature.needs.hunger == 0.005f &&
                creature.bond == 0.02f,
            "Rejected feeding should not grant bond progress");

    creature.needs.hunger = 0.10f;
    config.hungerReduction = 2.0f;
    const auto clamped = engine::game::feedCreature(creature, config);
    require(clamped.fed && clamped.hungerAfter == 0.0f &&
                creature.needs.hunger == 0.0f,
            "Feeding must clamp hunger instead of underflowing it");

    creature.needs.hunger = 0.50f;
    creature.bond = 0.995f;
    const auto cappedBond = engine::game::feedCreature(creature, config);
    require(cappedBond.fed && creature.bond == 1.0f &&
                cappedBond.bondAfter == 1.0f,
            "Successful care actions must cap persistent bond at one");
}

} // namespace

int main()
{
    try
    {
        testEmptyRosterStaysEmptyWithoutLiveCreature();
        testLiveStarterCreatesDeterministicPrimary();
        testRebuildChangesOnlyDisposableRuntimeBinding();
        testPrimaryFlagsAndDuplicateUuidsRepairDeterministically();
        testSavedSpeciesControlsLiveBinding();
        testFeedCreatureIsBoundedAndRejectsNoOp();
        std::cout << "Creature care state tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Creature care state test failure: " << e.what() << "\n";
        return 1;
    }
}
