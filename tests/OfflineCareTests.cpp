#include "engine/game/OfflineCare.h"

#include <cmath>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "engine/game/CreatureVitality.h"
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

bool nearlyEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

engine::game::CareUtcTimePoint utc(std::string_view timestamp)
{
    const auto parsed = engine::game::parseCareUtcTimestamp(timestamp);
    require(parsed.has_value(), "Test timestamp should be canonical UTC");
    return *parsed;
}

CreatureInstance makeCreature(std::string uuid, bool primary)
{
    CreatureInstance creature{};
    creature.uuid = std::move(uuid);
    creature.speciesId = primary ? "starter_fish" : "axolotl";
    creature.displayName = primary ? "Pip" : "Mochi";
    creature.primary = primary;
    creature.bond = primary ? 0.35f : 0.72f;
    creature.needs.hunger = primary ? 0.10f : 0.42f;
    creature.needs.cleanliness = primary ? 1.0f : 0.68f;
    creature.needs.happiness = primary ? 1.0f : 0.74f;
    creature.needs.health = primary ? 1.0f : 0.81f;
    creature.vitality = 0.01f;
    return creature;
}

GameState makeGameState()
{
    GameState state{};
    state.water.temperatureC = 24.5f;
    state.water.flow = 0.47f;
    state.water.cleanliness = 1.0f;
    state.water.oxygen = 0.82f;
    state.progression.coins = 91;
    state.progression.discovery = 7;
    state.progression.careMilestone = 3;
    state.progression.tankTier = 2;
    state.creatures.push_back(makeCreature("primary-care", true));
    state.creatures.push_back(makeCreature("secondary-care", false));
    return state;
}

bool sameNeeds(const CreatureNeeds& lhs, const CreatureNeeds& rhs)
{
    return nearlyEqual(lhs.hunger, rhs.hunger) &&
           nearlyEqual(lhs.cleanliness, rhs.cleanliness) &&
           nearlyEqual(lhs.happiness, rhs.happiness) &&
           nearlyEqual(lhs.health, rhs.health);
}

bool sameWater(const WaterState& lhs, const WaterState& rhs)
{
    return nearlyEqual(lhs.temperatureC, rhs.temperatureC) &&
           nearlyEqual(lhs.oxygen, rhs.oxygen) &&
           nearlyEqual(lhs.flow, rhs.flow) &&
           nearlyEqual(lhs.cleanliness, rhs.cleanliness);
}

void testCanonicalUtcParsingAndFormatting()
{
    const auto leapDay = utc("2024-02-29T23:59:59Z");
    require(engine::game::formatCareUtcTimestamp(leapDay) ==
                "2024-02-29T23:59:59Z",
            "Canonical leap-day UTC should round-trip at second precision");
    require(!engine::game::parseCareUtcTimestamp("2023-02-29T12:00:00Z") &&
                !engine::game::parseCareUtcTimestamp("2026-13-01T00:00:00Z") &&
                !engine::game::parseCareUtcTimestamp("2026-08-13 12:00:00Z") &&
                !engine::game::parseCareUtcTimestamp("2026-08-13T12:00:00.0Z") &&
                !engine::game::parseCareUtcTimestamp("2026-08-13T12:00:00+00:00"),
            "Offline care should reject impossible or non-canonical timestamps");
    const auto beforeYear = utc("2025-12-31T23:59:59Z");
    const auto afterYear = utc("2026-01-01T00:00:01Z");
    require(std::chrono::duration_cast<std::chrono::seconds>(afterYear - beforeYear)
                    .count() == 2,
            "UTC calendar conversion should preserve exact cross-year deltas");
}

void testExactOneHourCatchUpIsDeterministic()
{
    GameState first = makeGameState();
    first.lastPlayedUtc = "2026-08-13T11:00:00Z";
    GameState second = first;
    const auto now = utc("2026-08-13T12:00:00Z");

    const auto firstResult = engine::game::reconcileOfflineCare(first, now);
    const auto secondResult = engine::game::reconcileOfflineCare(second, now);
    require(firstResult.timestampStatus ==
                engine::game::OfflineCareTimestampStatus::Applied &&
                firstResult.elapsedSeconds == 3600 &&
                firstResult.appliedSeconds == 3600 && !firstResult.capped &&
                secondResult.appliedSeconds == firstResult.appliedSeconds &&
                firstResult.primaryCreatureFound && firstResult.waterChanged &&
                firstResult.creatureChanged,
            "A one-hour valid gap should reconcile water and the saved primary");
    require(nearlyEqual(first.water.cleanliness, 0.9875f) &&
                nearlyEqual(first.water.oxygen, 0.815f) &&
                nearlyEqual(first.creatures[0].needs.hunger, 0.11875f),
            "One-hour offline deltas should match the forgiving offline-care rates");
    require(sameWater(first.water, second.water) &&
                sameNeeds(first.creatures[0].needs, second.creatures[0].needs) &&
                nearlyEqual(first.creatures[0].vitality,
                            second.creatures[0].vitality),
            "Identical state, UTC, and config should reconcile deterministically");
    require(nearlyEqual(first.water.temperatureC, 24.5f) &&
                nearlyEqual(first.water.flow, 0.47f),
            "Offline care must leave temperature and flow unchanged");
}

void testDayCapAndCozyLimits()
{
    GameState oneDay = makeGameState();
    oneDay.lastPlayedUtc = "2026-08-12T12:00:00Z";
    oneDay.water.cleanliness = 0.61f;
    oneDay.water.oxygen = 0.56f;
    oneDay.creatures[0].needs.hunger = 0.80f;
    oneDay.creatures[0].needs.cleanliness = 0.16f;
    oneDay.creatures[0].needs.happiness = 0.21f;
    oneDay.creatures[0].needs.health = 0.31f;
    GameState twoDays = oneDay;
    twoDays.lastPlayedUtc = "2026-08-11T12:00:00Z";
    const auto now = utc("2026-08-13T12:00:00Z");

    const auto oneDayResult = engine::game::reconcileOfflineCare(oneDay, now);
    const auto twoDayResult = engine::game::reconcileOfflineCare(twoDays, now);
    require(!oneDayResult.capped && twoDayResult.capped &&
                twoDayResult.elapsedSeconds == 172800 &&
                twoDayResult.appliedSeconds == 86400,
            "Elapsed absence should be discarded beyond the exact 24-hour cap");
    require(sameWater(oneDay.water, twoDays.water) &&
                sameNeeds(oneDay.creatures[0].needs,
                          twoDays.creatures[0].needs),
            "One day and longer absences should converge to the same capped state");
    require(nearlyEqual(twoDays.water.cleanliness, 0.60f) &&
                nearlyEqual(twoDays.water.oxygen, 0.55f) &&
                twoDays.creatures[0].needs.hunger <= 0.85f &&
                twoDays.creatures[0].needs.cleanliness >= 0.15f &&
                twoDays.creatures[0].needs.happiness >= 0.20f &&
                twoDays.creatures[0].needs.health >= 0.30f,
            "Offline catch-up should retain recoverable water and creature floors");
}

void testCleanupCrewReducesOfflineCleanlinessDecayOnly()
{
    GameState control = makeGameState();
    control.lastPlayedUtc = "2026-08-12T12:00:00Z";
    GameState protectedState = control;
    CreatureInstance snail = makeCreature("cleanup-snail", false);
    snail.speciesId = std::string(engine::game::kRamshornSnailSpeciesId);
    snail.displayName = "Pebble";
    protectedState.creatures.push_back(snail);
    const auto now = utc("2026-08-13T12:00:00Z");

    const auto controlResult =
        engine::game::reconcileOfflineCare(control, now);
    const auto protectedResult =
        engine::game::reconcileOfflineCare(protectedState, now);

    require(controlResult.cleanupCrewCount == 0 &&
                nearlyEqual(controlResult.cleanlinessDecayReduction, 0.0f) &&
                protectedResult.cleanupCrewCount == 1 &&
                nearlyEqual(protectedResult.cleanlinessDecayReduction, 0.20f),
            "Offline diagnostics should report the saved cleanup roster effect");
    require(nearlyEqual(control.water.cleanliness, 0.70f) &&
                nearlyEqual(protectedState.water.cleanliness, 0.76f) &&
                nearlyEqual(control.water.oxygen,
                            protectedState.water.oxygen),
            "One cleanup creature should reduce only offline cleanliness decay by 20 percent");
    require(protectedState.creatures.size() == 3 &&
                protectedState.creatures.back().uuid == "cleanup-snail" &&
                sameNeeds(protectedState.creatures.back().needs, snail.needs),
            "A cleanup role must not mutate the secondary creature that supplies it");
}

void testOfflineLimitsNeverImproveAlreadyPoorValidState()
{
    GameState state = makeGameState();
    state.lastPlayedUtc = "2026-08-12T12:00:00Z";
    state.water.cleanliness = 0.20f;
    state.water.oxygen = 0.40f;
    CreatureInstance& primary = state.creatures.front();
    primary.needs.hunger = 0.95f;
    primary.needs.cleanliness = 0.10f;
    primary.needs.happiness = 0.10f;
    primary.needs.health = 0.20f;
    engine::game::refreshCreatureVitality(primary);
    const GameState before = state;

    const auto result = engine::game::reconcileOfflineCare(
        state, utc("2026-08-13T12:00:00Z"));

    require(result.timestampStatus ==
                engine::game::OfflineCareTimestampStatus::Applied &&
                result.appliedSeconds == 86400 && !result.stateChanged(),
            "A capped gap beyond every cozy guard should consume time without "
            "changing care state");
    require(nearlyEqual(state.water.cleanliness,
                        before.water.cleanliness) &&
                nearlyEqual(state.water.oxygen, before.water.oxygen) &&
                nearlyEqual(state.creatures.front().needs.hunger,
                            before.creatures.front().needs.hunger) &&
                nearlyEqual(state.creatures.front().needs.cleanliness,
                            before.creatures.front().needs.cleanliness) &&
                nearlyEqual(state.creatures.front().needs.happiness,
                            before.creatures.front().needs.happiness) &&
                nearlyEqual(state.creatures.front().needs.health,
                            before.creatures.front().needs.health),
            "Offline floors and ceilings must stop additional neglect, never "
            "heal an already-poor valid state");
}

void testZeroCapConsumesTimestampWithoutSimulatingCare()
{
    GameState state = makeGameState();
    state.lastPlayedUtc = "2026-08-12T12:00:00Z";
    const GameState before = state;
    engine::game::OfflineCareConfig config{};
    config.maxElapsedSeconds = 0;

    const auto result = engine::game::reconcileOfflineCare(
        state, utc("2026-08-13T12:00:00Z"), config);

    require(result.timestampStatus ==
                engine::game::OfflineCareTimestampStatus::Applied &&
                result.elapsedSeconds == 86400 && result.appliedSeconds == 0 &&
                result.capped && result.primaryCreatureFound &&
                !result.stateChanged(),
            "A zero offline cap should consume the interval without simulating it");
    require(sameWater(state.water, before.water) &&
                sameNeeds(state.creatures.front().needs,
                          before.creatures.front().needs) &&
                nearlyEqual(state.creatures.front().vitality,
                            before.creatures.front().vitality),
            "A zero offline cap must not normalize or repair care values");
}

void testUnsafeTimestampsApplyNoCareMutation()
{
    const auto now = utc("2026-08-13T12:00:00Z");
    const std::string canonicalNow = engine::game::formatCareUtcTimestamp(now);
    const std::string cases[] = {"", "not-a-date", "2026-02-30T12:00:00Z",
                                 "2026-08-14T12:00:00Z"};
    const engine::game::OfflineCareTimestampStatus statuses[] = {
        engine::game::OfflineCareTimestampStatus::Missing,
        engine::game::OfflineCareTimestampStatus::Invalid,
        engine::game::OfflineCareTimestampStatus::Invalid,
        engine::game::OfflineCareTimestampStatus::Future};

    for (size_t i = 0; i < std::size(cases); ++i)
    {
        GameState state = makeGameState();
        state.lastPlayedUtc = cases[i];
        const GameState before = state;
        const auto result = engine::game::reconcileOfflineCare(state, now);
        require(result.timestampStatus == statuses[i] &&
                    result.appliedSeconds == 0 && !result.stateChanged() &&
                    state.lastPlayedUtc == canonicalNow &&
                    sameWater(state.water, before.water) &&
                    sameNeeds(state.creatures[0].needs,
                              before.creatures[0].needs),
                "Unsafe timestamps should repair to now without applying neglect");
    }
}

class FixedCareUtcClock final : public engine::game::CareUtcClock
{
public:
    explicit FixedCareUtcClock(engine::game::CareUtcTimePoint value)
        : value_(value)
    {
    }

    engine::game::CareUtcTimePoint now() const override { return value_; }

private:
    engine::game::CareUtcTimePoint value_;
};

void testInjectedClockAndConsumedTimestampAreIdempotent()
{
    GameState state = makeGameState();
    state.lastPlayedUtc = "2026-08-13T10:00:00Z";
    const FixedCareUtcClock clock{utc("2026-08-13T12:00:00Z")};
    const auto first = engine::game::reconcileOfflineCare(state, clock);
    const GameState consumed = state;
    const auto second = engine::game::reconcileOfflineCare(state, clock);

    require(first.appliedSeconds == 7200 &&
                second.timestampStatus ==
                    engine::game::OfflineCareTimestampStatus::Current &&
                second.appliedSeconds == 0 && !second.stateChanged() &&
                !second.timestampChanged && sameWater(state.water, consumed.water) &&
                sameNeeds(state.creatures[0].needs,
                          consumed.creatures[0].needs),
            "A fixed injected clock should consume a loaded interval exactly once");
}

void testOnlyPrimaryAndCareOwnedStateChange()
{
    GameState state = makeGameState();
    state.lastPlayedUtc = "2026-08-12T12:00:00Z";
    const CreatureInstance secondary = state.creatures[1];
    const ProgressionState progression = state.progression;
    const float bond = state.creatures[0].bond;
    const std::string uuid = state.creatures[0].uuid;
    const std::string species = state.creatures[0].speciesId;
    const std::string displayName = state.creatures[0].displayName;
    const auto result = engine::game::reconcileOfflineCare(
        state, utc("2026-08-13T12:00:00Z"));

    require(result.primaryCreatureFound && state.creatures.size() == 2 &&
                state.creatures[0].uuid == uuid &&
                state.creatures[0].speciesId == species &&
                state.creatures[0].displayName == displayName &&
                state.creatures[0].primary &&
                nearlyEqual(state.creatures[0].bond, bond),
            "Offline care must preserve primary identity, flags, and bond");
    require(state.creatures[1].uuid == secondary.uuid &&
                state.creatures[1].speciesId == secondary.speciesId &&
                state.creatures[1].displayName == secondary.displayName &&
                state.creatures[1].primary == secondary.primary &&
                nearlyEqual(state.creatures[1].bond, secondary.bond) &&
                nearlyEqual(state.creatures[1].vitality, secondary.vitality) &&
                sameNeeds(state.creatures[1].needs, secondary.needs),
            "Secondary collection creatures must remain byte-for-value semantic state");
    require(state.progression.coins == progression.coins &&
                state.progression.discovery == progression.discovery &&
                state.progression.careMilestone == progression.careMilestone &&
                state.progression.tankTier == progression.tankTier,
            "Offline care must not mutate progression");
    require(nearlyEqual(state.creatures[0].vitality,
                        engine::game::deriveCreatureVitality(state.creatures[0])),
            "Offline needs should refresh the derived vitality cache");

    GameState empty{};
    empty.lastPlayedUtc = "2026-08-12T12:00:00Z";
    const auto emptyResult = engine::game::reconcileOfflineCare(
        empty, utc("2026-08-13T12:00:00Z"));
    require(!emptyResult.primaryCreatureFound && emptyResult.waterChanged &&
                empty.creatures.empty(),
            "Scene-wide water should catch up even when the roster is empty");
}

void testInvalidSavedValuesRepairInsideOfflineLimits()
{
    GameState state = makeGameState();
    state.lastPlayedUtc = "2026-08-12T12:00:00Z";
    state.water.cleanliness = std::numeric_limits<float>::quiet_NaN();
    state.water.oxygen = std::numeric_limits<float>::infinity();
    state.creatures[0].needs.hunger = std::numeric_limits<float>::infinity();
    state.creatures[0].needs.cleanliness = -3.0f;
    state.creatures[0].needs.happiness = 4.0f;
    state.creatures[0].needs.health = std::numeric_limits<float>::quiet_NaN();

    engine::game::reconcileOfflineCare(state, utc("2026-08-13T12:00:00Z"));
    const CreatureNeeds& needs = state.creatures[0].needs;
    require(std::isfinite(state.water.cleanliness) &&
                std::isfinite(state.water.oxygen) &&
                state.water.cleanliness >= 0.60f && state.water.oxygen >= 0.55f &&
                std::isfinite(needs.hunger) && needs.hunger <= 0.85f &&
                std::isfinite(needs.cleanliness) && needs.cleanliness >= 0.15f &&
                std::isfinite(needs.happiness) && needs.happiness >= 0.20f &&
                std::isfinite(needs.health) && needs.health >= 0.30f,
            "Corrupt loaded care values should repair to finite recoverable ranges");
}

} // namespace

int main()
{
    try
    {
        testCanonicalUtcParsingAndFormatting();
        testExactOneHourCatchUpIsDeterministic();
        testDayCapAndCozyLimits();
        testCleanupCrewReducesOfflineCleanlinessDecayOnly();
        testOfflineLimitsNeverImproveAlreadyPoorValidState();
        testZeroCapConsumesTimestampWithoutSimulatingCare();
        testUnsafeTimestampsApplyNoCareMutation();
        testInjectedClockAndConsumedTimestampAreIdempotent();
        testOnlyPrimaryAndCareOwnedStateChange();
        testInvalidSavedValuesRepairInsideOfflineLimits();
        std::cout << "Offline care tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Offline care test failure: " << error.what() << "\n";
        return 1;
    }
}
