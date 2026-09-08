#include "engine/game/CleanupCrew.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "engine/game/SpeciesIds.h"

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

void testEmptyAndNonCleanupRostersHaveNoEffect()
{
    GameState state{};
    state.creatures.push_back(
        creature("starter", std::string(engine::game::kStarterFishSpeciesId)));
    state.creatures.push_back(
        creature("axolotl", std::string(engine::game::kAxolotlSpeciesId)));
    state.creatures.push_back(creature("unknown", "future_species"));
    state.creatures.push_back(
        creature("", std::string(engine::game::kRamshornSnailSpeciesId)));

    const auto effect = engine::game::evaluateCleanupCrewEffect(state);
    require(effect.creatureCount == 0 &&
                nearlyEqual(effect.cleanlinessDecayReduction, 0.0f) &&
                nearlyEqual(effect.cleanlinessDecayMultiplier, 1.0f),
            "Only complete saved entries with a registered cleanup role may affect decay");
}

void testOwnedCleanupCrewStacksToACap()
{
    GameState state{};
    for (int index = 0; index < 4; ++index)
    {
        state.creatures.push_back(creature(
            "snail-" + std::to_string(index),
            std::string(engine::game::kRamshornSnailSpeciesId)));
        const auto effect = engine::game::evaluateCleanupCrewEffect(state);
        const uint32_t expectedCount = static_cast<uint32_t>(index + 1);
        const float expectedReduction =
            std::min(0.60f, 0.20f * static_cast<float>(expectedCount));
        require(effect.creatureCount == expectedCount &&
                    nearlyEqual(effect.cleanlinessDecayReduction,
                                expectedReduction) &&
                    nearlyEqual(effect.cleanlinessDecayMultiplier,
                                1.0f - expectedReduction),
                "Cleanup effects should stack linearly and stop at the cozy cap");
    }
}

void testConfigurationIsSanitizedAndEvaluationIsPure()
{
    GameState state{};
    state.water.cleanliness = 0.73f;
    state.creatures.push_back(creature(
        "snail", std::string(engine::game::kRamshornSnailSpeciesId)));
    const GameState before = state;

    engine::game::CleanupCrewConfig config{};
    config.cleanlinessDecayReductionPerCreature =
        std::numeric_limits<float>::quiet_NaN();
    config.maximumCleanlinessDecayReduction = 2.0f;
    const auto fallback = engine::game::evaluateCleanupCrewEffect(state, config);
    require(fallback.creatureCount == 1 &&
                nearlyEqual(fallback.cleanlinessDecayReduction, 0.20f),
            "Non-finite tuning should fall back to the documented 20 percent role effect");

    config.cleanlinessDecayReductionPerCreature = -1.0f;
    config.maximumCleanlinessDecayReduction = 0.60f;
    const auto disabled = engine::game::evaluateCleanupCrewEffect(state, config);
    require(nearlyEqual(disabled.cleanlinessDecayReduction, 0.0f) &&
                nearlyEqual(disabled.cleanlinessDecayMultiplier, 1.0f),
            "Negative tuning must fail closed without reversing cleanliness decay");
    require(nearlyEqual(state.water.cleanliness, before.water.cleanliness) &&
                state.creatures.size() == before.creatures.size() &&
                state.creatures.front().uuid == before.creatures.front().uuid &&
                state.creatures.front().speciesId ==
                    before.creatures.front().speciesId,
            "Cleanup evaluation must remain a pure query over persistent state");
}

} // namespace

int main()
{
    try
    {
        testEmptyAndNonCleanupRostersHaveNoEffect();
        testOwnedCleanupCrewStacksToACap();
        testConfigurationIsSanitizedAndEvaluationIsPure();
        std::cout << "Cleanup crew tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Cleanup crew test failure: " << error.what() << "\n";
        return 1;
    }
}
