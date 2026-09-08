#include "engine/game/CollectionCodex.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "engine/game/SpeciesIds.h"
#include "engine/game/Placeables.h"

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

CreatureInstance creature(std::string uuid, std::string speciesId)
{
    CreatureInstance instance{};
    instance.uuid = std::move(uuid);
    instance.speciesId = std::move(speciesId);
    return instance;
}

void testOwnedSpeciesAreAggregatedInStableRegistryOrder()
{
    GameState state{};
    state.water.temperatureC = 24.0f;
    state.water.oxygen = 0.82f;
    state.water.flow = 0.45f;
    state.water.cleanliness = 1.0f;
    state.creatures = {
        creature("starter-a", std::string(engine::game::kStarterFishSpeciesId)),
        creature("unknown-b", "future_species_b"),
        creature("snail-a", std::string(engine::game::kRamshornSnailSpeciesId)),
        creature("starter-b", std::string(engine::game::kStarterFishSpeciesId)),
        creature("unknown-a", "future_species_a"),
        creature("", std::string(engine::game::kAxolotlSpeciesId)),
        creature("incomplete", ""),
    };
    const GameState before = state;
    const engine::game::HabitatDecorContribution shelter{
        PlaceableMaterialCategory::Rock, 1};

    const auto view = engine::game::buildCollectionCodexView(state, {&shelter, 1});
    require(view.totalOwnedCreatures == 5 && view.entries.size() == 4,
            "The Codex should count complete instances and aggregate them by species");
    require(view.knownOwnedSpeciesCount == 2 && view.unknownOwnedSpeciesCount == 2,
            "Known and unknown persisted species should remain explicitly distinguishable");
    require(view.entries[0].speciesId == engine::game::kStarterFishSpeciesId &&
                view.entries[0].ownedCount == 2 &&
                view.entries[1].speciesId == engine::game::kRamshornSnailSpeciesId,
            "Known owned species should retain deterministic registry order");
    require(view.entries[2].speciesId == "future_species_a" &&
                view.entries[3].speciesId == "future_species_b",
            "Unknown saved species should follow in deterministic lexical order");
    require(view.entries[0].habitat.compatible() &&
                view.entries[1].habitat.compatible(),
            "The Codex should expose current advisory habitat evaluations");
    require(view.cleanupCrew.creatureCount == 1 &&
                nearlyEqual(view.cleanupCrew.cleanlinessDecayReduction, 0.20f),
            "The Codex should summarize the functional cleanup benefit");
    require(state.creatures.size() == before.creatures.size() &&
                state.creatures.front().uuid == before.creatures.front().uuid &&
                nearlyEqual(state.water.cleanliness, before.water.cleanliness),
            "Building the Codex must remain a pure read of persistent state");
}

void testEmptyRosterAndMissingDecorRemainAdvisory()
{
    const auto empty = engine::game::buildCollectionCodexView(GameState{});
    require(empty.entries.empty() && empty.totalOwnedCreatures == 0 &&
                empty.registeredSpeciesCount == 3,
            "An empty collection should produce an explicit empty Codex view");

    GameState state{};
    state.water.temperatureC = 24.0f;
    state.water.oxygen = 0.82f;
    state.water.flow = 0.45f;
    state.water.cleanliness = 1.0f;
    state.creatures.push_back(
        creature("starter", std::string(engine::game::kStarterFishSpeciesId)));
    const auto view = engine::game::buildCollectionCodexView(state);
    require(view.entries.size() == 1 &&
                view.entries.front().habitat.status ==
                    engine::game::HabitatCompatibilityStatus::NeedsAttention &&
                view.entries.front().habitat.unsatisfiedRequirementCount == 1,
            "Absent semantic decor should be reported as advice, never used as a gate");
    require(state.creatures.size() == 1,
            "An unsatisfied advisory requirement must not add or remove creatures");
}

} // namespace

int main()
{
    try
    {
        testOwnedSpeciesAreAggregatedInStableRegistryOrder();
        testEmptyRosterAndMissingDecorRemainAdvisory();
        std::cout << "Collection Codex tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Collection Codex tests failed: " << error.what() << '\n';
        return 1;
    }
}
