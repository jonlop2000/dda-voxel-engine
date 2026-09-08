#include "engine/game/GameStateSimulation.h"
#include "engine/game/CreatureVitality.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool nearlyEqual(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

CreatureInstance makePrimary()
{
    CreatureInstance creature{};
    creature.uuid = "care-primary";
    creature.speciesId = "starter_fish";
    creature.primary = true;
    return creature;
}

engine::game::CreatureNeedsHeartbeatConfig fastConfig()
{
    engine::game::CreatureNeedsHeartbeatConfig config{};
    config.maxStepSeconds = 100.0f;
    config.hungerIncreasePerSecond = 0.01f;
    config.cleanlinessResponsePerSecond = 0.5f;
    config.happinessResponsePerSecond = 0.5f;
    config.healthResponsePerSecond = 0.5f;
    return config;
}

void testAdverseWaterRaisesHungerAndLowersConditions()
{
    CreatureInstance creature = makePrimary();
    creature.needs.hunger = 0.20f;
    creature.needs.cleanliness = 0.80f;
    creature.needs.happiness = 0.80f;
    creature.needs.health = 0.80f;
    WaterState water{};
    water.cleanliness = 0.10f;
    water.oxygen = 0.20f;

    require(engine::game::updateCreatureNeeds(creature, water, 10.0f,
                                              fastConfig()),
            "Adverse care conditions should change creature needs");
    require(nearlyEqual(creature.needs.hunger, 0.30f),
            "Hunger pressure should rise at the configured linear rate");
    require(creature.needs.cleanliness < 0.80f &&
                creature.needs.happiness < 0.80f &&
                creature.needs.health < 0.80f,
            "Dirty, low-oxygen water plus hunger should lower creature conditions");
}

void testHealthyWaterCanRecoverPersonalConditions()
{
    CreatureInstance creature = makePrimary();
    creature.needs.hunger = 0.0f;
    creature.needs.cleanliness = 0.20f;
    creature.needs.happiness = 0.25f;
    creature.needs.health = 0.35f;
    WaterState water{};
    water.cleanliness = 1.0f;
    water.oxygen = 1.0f;
    auto config = fastConfig();
    config.hungerIncreasePerSecond = 0.0f;

    engine::game::updateCreatureNeeds(creature, water, 2.0f, config);
    require(creature.needs.cleanliness > 0.20f &&
                creature.needs.happiness > 0.25f &&
                creature.needs.health > 0.35f,
            "Good water and satiety should recover personal conditions");
}

void testLargeDeltaIsBoundedAndReported()
{
    GameState state{};
    state.creatures.push_back(makePrimary());
    engine::game::CreatureNeedsHeartbeatConfig config{};
    config.maxStepSeconds = 2.0f;
    config.hungerIncreasePerSecond = 0.10f;
    config.cleanlinessResponsePerSecond = 0.0f;
    config.happinessResponsePerSecond = 0.0f;
    config.healthResponsePerSecond = 0.0f;

    const auto result =
        engine::game::updatePrimaryCreatureNeeds(state, 100.0f, config);
    require(result.primaryFound && result.changed &&
                nearlyEqual(result.simulatedSeconds, 2.0f),
            "Primary heartbeat should report its capped simulation step");
    require(nearlyEqual(state.creatures.front().needs.hunger, 0.20f),
            "Large frame deltas must not bypass the configured step cap");
}

void testCozyFloorsPreventDeathState()
{
    CreatureInstance creature = makePrimary();
    creature.needs.hunger = 1.0f;
    creature.needs.cleanliness = 1.0f;
    creature.needs.happiness = 1.0f;
    creature.needs.health = 1.0f;
    WaterState water{};
    water.cleanliness = 0.0f;
    water.oxygen = 0.0f;
    auto config = fastConfig();
    config.cleanlinessFloor = 0.15f;
    config.happinessFloor = 0.20f;
    config.healthFloor = 0.30f;

    for (int i = 0; i < 10; ++i)
    {
        engine::game::updateCreatureNeeds(creature, water, 100.0f, config);
    }
    require(creature.needs.cleanliness >= 0.15f &&
                creature.needs.happiness >= 0.20f &&
                creature.needs.health >= 0.30f,
            "Neglect should stop at recoverable cozy-mode condition floors");
    require(creature.primary && creature.uuid == "care-primary",
            "Needs simulation must never delete or replace creature identity");
}

void testOnlyPrimaryCreatureChanges()
{
    GameState state{};
    state.creatures.push_back(makePrimary());
    CreatureInstance secondary{};
    secondary.uuid = "care-secondary";
    secondary.speciesId = "starter_fish";
    secondary.needs.hunger = 0.40f;
    state.creatures.push_back(secondary);
    const CreatureNeeds secondaryBefore = state.creatures[1].needs;

    engine::game::updatePrimaryCreatureNeeds(state, 10.0f, fastConfig());
    require(state.creatures[0].needs.hunger > 0.0f,
            "The primary creature should receive the care heartbeat");
    require(nearlyEqual(state.creatures[1].needs.hunger,
                        secondaryBefore.hunger) &&
                nearlyEqual(state.creatures[1].needs.cleanliness,
                            secondaryBefore.cleanliness) &&
                nearlyEqual(state.creatures[1].needs.happiness,
                            secondaryBefore.happiness) &&
                nearlyEqual(state.creatures[1].needs.health,
                            secondaryBefore.health),
            "The care heartbeat must not simulate secondary collection creatures");
}

void testNoPrimaryIsNoOpAndSimulationIsDeterministic()
{
    GameState empty{};
    const auto emptyResult =
        engine::game::updatePrimaryCreatureNeeds(empty, 10.0f, fastConfig());
    require(!emptyResult.primaryFound && !emptyResult.changed,
            "A scene without a primary creature should remain unchanged");

    GameState first{};
    first.water.cleanliness = 0.55f;
    first.water.oxygen = 0.63f;
    first.creatures.push_back(makePrimary());
    first.creatures.front().needs.hunger = 0.12f;
    GameState second = first;
    engine::game::updatePrimaryCreatureNeeds(first, 3.25f, fastConfig());
    engine::game::updatePrimaryCreatureNeeds(second, 3.25f, fastConfig());
    const CreatureNeeds& a = first.creatures.front().needs;
    const CreatureNeeds& b = second.creatures.front().needs;
    require(nearlyEqual(a.hunger, b.hunger) &&
                nearlyEqual(a.cleanliness, b.cleanliness) &&
                nearlyEqual(a.happiness, b.happiness) &&
                nearlyEqual(a.health, b.health),
            "Identical state and elapsed time must produce identical needs");
}

void testInvalidSavedNeedsClampToFiniteRanges()
{
    CreatureInstance creature = makePrimary();
    creature.needs.hunger = std::numeric_limits<float>::infinity();
    creature.needs.cleanliness = -4.0f;
    creature.needs.happiness = 7.0f;
    creature.needs.health = std::numeric_limits<float>::quiet_NaN();
    WaterState water{};

    require(engine::game::updateCreatureNeeds(creature, water, 0.0f,
                                              fastConfig()),
            "Invalid saved needs should be repaired even without elapsed time");
    require(std::isfinite(creature.needs.hunger) &&
                std::isfinite(creature.needs.cleanliness) &&
                std::isfinite(creature.needs.happiness) &&
                std::isfinite(creature.needs.health) &&
                creature.needs.hunger >= 0.0f && creature.needs.hunger <= 1.0f &&
                creature.needs.cleanliness >= 0.15f &&
                creature.needs.cleanliness <= 1.0f &&
                creature.needs.happiness >= 0.20f &&
                creature.needs.happiness <= 1.0f &&
                creature.needs.health >= 0.30f &&
                creature.needs.health <= 1.0f,
            "Needs repair must produce finite values inside the care contract");
}

void testVitalityIsDerivedAndPresentationStaysBounded()
{
    CreatureInstance thriving = makePrimary();
    thriving.bond = 1.0f;
    thriving.vitality = 0.0f;
    WaterState water{};
    auto config = fastConfig();
    config.hungerIncreasePerSecond = 0.0f;
    require(engine::game::updateCreatureNeeds(thriving, water, 0.0f, config) &&
                nearlyEqual(thriving.vitality, 1.0f),
            "The heartbeat should replace stale vitality with a needs-and-bond derivation");

    CreatureInstance neglected = makePrimary();
    neglected.bond = 0.0f;
    neglected.needs.hunger = 1.0f;
    neglected.needs.cleanliness = 0.15f;
    neglected.needs.happiness = 0.20f;
    neglected.needs.health = 0.30f;
    const float neglectedVitality =
        engine::game::deriveCreatureVitality(neglected);
    require(neglectedVitality < thriving.vitality && neglectedVitality > 0.0f,
            "Recoverable neglect should lower vitality without creating a death state");

    const auto lowMotion =
        engine::game::creatureVitalityPresentation(neglectedVitality);
    const auto highMotion =
        engine::game::creatureVitalityPresentation(thriving.vitality);
    require(lowMotion.pathWobbleScale < highMotion.pathWobbleScale &&
                lowMotion.bodyMotionScale < highMotion.bodyMotionScale &&
                lowMotion.tailMotionScale < highMotion.tailMotionScale &&
                lowMotion.pathWobbleScale >= 0.70f &&
                highMotion.tailMotionScale <= 1.14f,
            "Vitality presentation should be subtle, monotonic, and bounded");
}

} // namespace

int main()
{
    try
    {
        testAdverseWaterRaisesHungerAndLowersConditions();
        testHealthyWaterCanRecoverPersonalConditions();
        testLargeDeltaIsBoundedAndReported();
        testCozyFloorsPreventDeathState();
        testOnlyPrimaryCreatureChanges();
        testNoPrimaryIsNoOpAndSimulationIsDeterministic();
        testInvalidSavedNeedsClampToFiniteRanges();
        testVitalityIsDerivedAndPresentationStaysBounded();
        std::cout << "Creature needs simulation tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Creature needs simulation test failure: " << e.what()
                  << "\n";
        return 1;
    }
}
