#include "engine/game/HabitatCompatibility.h"
#include "engine/game/SpeciesIds.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{

using engine::game::HabitatCompatibilityStatus;
using engine::game::HabitatEnvironmentSnapshot;
using engine::game::HabitatRequirementStatus;
using engine::game::HabitatTankmateRule;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

CreatureInstance makeCreature(std::string uuid, std::string speciesId)
{
    CreatureInstance creature{};
    creature.uuid = std::move(uuid);
    creature.speciesId = std::move(speciesId);
    return creature;
}

void testBuiltInSpeciesCanReportACompleteHabitat()
{
    const std::array creatures = {
        makeCreature("starter-a", std::string(engine::game::kStarterFishSpeciesId)),
    };
    const std::array decor = {
        engine::game::HabitatDecorContribution{
            PlaceableMaterialCategory::Rock, 1},
    };
    const HabitatEnvironmentSnapshot environment{{24.0f, 0.82f, 0.45f, 1.0f},
                                                  creatures, decor};

    const auto result = engine::game::evaluateHabitatCompatibility(
        engine::game::kStarterFishSpeciesId, environment);
    require(result.knownSpecies() && result.compatible() &&
                result.status == HabitatCompatibilityStatus::Compatible &&
                result.unsatisfiedRequirementCount == 0,
            "A complete starter-fish habitat should report compatible");
    require(result.water.size() == 4 && result.decor.size() == 1 &&
                result.tankmates.size() == 1,
            "The result should retain every water, decor, and group requirement");
    require(result.decor[0].observedCount == 1 &&
                result.decor[0].status == HabitatRequirementStatus::Satisfied &&
                result.tankmates[0].rule == HabitatTankmateRule::MinimumGroupSize &&
                result.tankmates[0].observedCount == 1,
            "Satisfied requirement diagnostics should remain readable to future UI");
}

void testWaterAndDecorFailuresRemainSpecificAndAdvisory()
{
    GameState state{};
    state.water = {30.0f, 0.30f, 0.90f, 0.20f};
    state.creatures.push_back(
        makeCreature("starter-a", std::string(engine::game::kStarterFishSpeciesId)));
    const GameState before = state;
    const std::array decor = {
        engine::game::HabitatDecorContribution{
            PlaceableMaterialCategory::Substrate, 8},
    };
    const HabitatEnvironmentSnapshot environment{state.water, state.creatures, decor};

    const auto result = engine::game::evaluateHabitatCompatibility(
        engine::game::kStarterFishSpeciesId, environment);
    require(result.status == HabitatCompatibilityStatus::NeedsAttention &&
                !result.compatible() && result.unsatisfiedRequirementCount == 5,
            "Four invalid water readings plus missing shelter should remain five distinct advisories");
    for (const auto& water : result.water)
    {
        require(water.status == HabitatRequirementStatus::Unsatisfied,
                "Every out-of-band water parameter should be identified");
    }
    require(result.decor[0].observedCount == 0 &&
                result.decor[0].status == HabitatRequirementStatus::Unsatisfied,
            "Substrate contributions must not masquerade as rock or decoration shelter");
    require(state.water.temperatureC == before.water.temperatureC &&
                state.water.oxygen == before.water.oxygen &&
                state.water.flow == before.water.flow &&
                state.water.cleanliness == before.water.cleanliness &&
                state.creatures.size() == before.creatures.size() &&
                state.creatures[0].uuid == before.creatures[0].uuid,
            "Advisory evaluation must not mutate habitat or roster state");
}

void testDecorContributionsAggregateBySemanticCategory()
{
    const std::array creatures = {
        makeCreature("starter-a", std::string(engine::game::kStarterFishSpeciesId)),
    };
    const std::array decor = {
        engine::game::HabitatDecorContribution{
            PlaceableMaterialCategory::Substrate, 50},
        engine::game::HabitatDecorContribution{
            PlaceableMaterialCategory::Decoration, 1},
        engine::game::HabitatDecorContribution{
            PlaceableMaterialCategory::Rock, 2},
    };
    const HabitatEnvironmentSnapshot environment{{24.0f, 0.82f, 0.45f, 1.0f},
                                                  creatures, decor};

    const auto result = engine::game::evaluateHabitatCompatibility(
        engine::game::kStarterFishSpeciesId, environment);
    require(result.decor[0].observedCount == 3 && result.compatible(),
            "Only semantic rock/decoration contributions should satisfy shelter");
}

void testTankmateRulesReportGroupRequiredAndIncompatibleSpecies()
{
    const std::array requiredSpecies = {engine::game::kAxolotlSpeciesId};
    const std::array incompatibleSpecies = {std::string_view("territorial_fish")};
    engine::game::SpeciesDefinition definition =
        *engine::game::findSpecies(engine::game::kStarterFishSpeciesId);
    definition.habitat.decor = {};
    definition.habitat.tankmates = {3, requiredSpecies, incompatibleSpecies};

    const std::array creatures = {
        makeCreature("starter-a", std::string(engine::game::kStarterFishSpeciesId)),
        makeCreature("starter-b", std::string(engine::game::kStarterFishSpeciesId)),
        makeCreature("territorial-a", "territorial_fish"),
    };
    const HabitatEnvironmentSnapshot environment{{24.0f, 0.82f, 0.45f, 1.0f},
                                                  creatures, {}};

    const auto result =
        engine::game::evaluateHabitatCompatibility(definition, environment);
    require(result.tankmates.size() == 3 &&
                result.unsatisfiedRequirementCount == 3,
            "Group size, required species, and incompatible species should report independently");
    require(result.tankmates[0].rule == HabitatTankmateRule::MinimumGroupSize &&
                result.tankmates[0].observedCount == 2 &&
                result.tankmates[0].requiredCount == 3,
            "The group-size diagnostic should expose observed and required counts");
    require(result.tankmates[1].rule == HabitatTankmateRule::RequiredSpeciesPresent &&
                result.tankmates[1].speciesId == engine::game::kAxolotlSpeciesId &&
                result.tankmates[1].observedCount == 0,
            "Missing required species should remain an explicit advisory");
    require(result.tankmates[2].rule ==
                    HabitatTankmateRule::IncompatibleSpeciesAbsent &&
                result.tankmates[2].speciesId == "territorial_fish" &&
                result.tankmates[2].observedCount == 1,
            "Present incompatible species should remain an explicit advisory");
}

void testUnknownAndNonFiniteInputsFailClosed()
{
    const HabitatEnvironmentSnapshot empty{};
    const auto unknown =
        engine::game::evaluateHabitatCompatibility("missing_species", empty);
    require(!unknown.knownSpecies() && !unknown.compatible() &&
                unknown.status == HabitatCompatibilityStatus::UnknownSpecies &&
                unknown.speciesId == "missing_species" && unknown.water.empty() &&
                unknown.decor.empty() && unknown.tankmates.empty(),
            "Unknown IDs should preserve diagnostics without guessing a species");

    const std::array creatures = {
        makeCreature("starter-a", std::string(engine::game::kStarterFishSpeciesId)),
    };
    HabitatEnvironmentSnapshot invalid{{24.0f, 0.82f, 0.45f, 1.0f}, creatures, {}};
    invalid.water.oxygen = std::numeric_limits<float>::quiet_NaN();
    const auto result = engine::game::evaluateHabitatCompatibility(
        engine::game::kStarterFishSpeciesId, invalid);
    require(result.water[1].status == HabitatRequirementStatus::Unsatisfied &&
                std::isnan(result.water[1].observed),
            "Non-finite readings should fail closed while retaining the observed diagnostic");
}

} // namespace

int main()
{
    try
    {
        testBuiltInSpeciesCanReportACompleteHabitat();
        testWaterAndDecorFailuresRemainSpecificAndAdvisory();
        testDecorContributionsAggregateBySemanticCategory();
        testTankmateRulesReportGroupRequiredAndIncompatibleSpecies();
        testUnknownAndNonFiniteInputsFailClosed();
        std::cout << "Habitat compatibility tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Habitat compatibility test failure: " << error.what()
                  << "\n";
        return 1;
    }
}
