#include "engine/game/OfflineCare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "engine/game/CleanupCrew.h"
#include "engine/game/CreatureCareState.h"
#include "engine/game/CreatureVitality.h"
#include "engine/game/GameStateSimulation.h"

namespace engine::game
{
namespace
{

class SystemCareUtcClock final : public CareUtcClock
{
public:
    CareUtcTimePoint now() const override
    {
        return std::chrono::floor<std::chrono::seconds>(
            std::chrono::system_clock::now());
    }
};

bool parseDigits(std::string_view text, size_t offset, size_t count, int& value)
{
    value = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const char digit = text[offset + i];
        if (digit < '0' || digit > '9')
        {
            return false;
        }
        value = value * 10 + static_cast<int>(digit - '0');
    }
    return true;
}

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

float clamp01(float value, float fallback)
{
    return std::clamp(finiteOr(value, fallback), 0.0f, 1.0f);
}

bool assignIfChanged(float& target, float value)
{
    if (std::isfinite(target) && std::fabs(target - value) <= 1e-6f)
    {
        return false;
    }
    target = value;
    return true;
}

bool isNormalized(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool creatureCareChanged(const CreatureInstance& before,
                         const CreatureInstance& after)
{
    return std::fabs(before.needs.hunger - after.needs.hunger) > 1e-6f ||
           std::fabs(before.needs.cleanliness - after.needs.cleanliness) > 1e-6f ||
           std::fabs(before.needs.happiness - after.needs.happiness) > 1e-6f ||
           std::fabs(before.needs.health - after.needs.health) > 1e-6f ||
           !std::isfinite(before.needs.hunger) ||
           !std::isfinite(before.needs.cleanliness) ||
           !std::isfinite(before.needs.happiness) ||
           !std::isfinite(before.needs.health) ||
           !std::isfinite(before.vitality) ||
           std::fabs(before.vitality - after.vitality) > 1e-6f;
}

bool updateOfflineWater(WaterState& water, float elapsedSeconds,
                        float cleanlinessDecayMultiplier,
                        const OfflineCareConfig& config)
{
    const float cleanlinessFloor = clamp01(config.waterCleanlinessFloor, 0.60f);
    const float oxygenFloor = clamp01(config.waterOxygenFloor, 0.55f);
    const float cleanliness = clamp01(water.cleanliness, 1.0f);
    const float oxygen = clamp01(water.oxygen, 0.82f);
    const float cleanlinessDecay =
        std::max(0.0f, finiteOr(config.waterCleanlinessDecayPerSecond, 0.0f));
    const float cleanlinessMultiplier =
        std::clamp(finiteOr(cleanlinessDecayMultiplier, 1.0f), 0.0f, 1.0f);
    const float oxygenDecay =
        std::max(0.0f, finiteOr(config.waterOxygenDecayPerSecond, 0.0f));

    bool changed = false;
    changed |= assignIfChanged(
        water.cleanliness,
        std::clamp(cleanliness - cleanlinessDecay * cleanlinessMultiplier *
                                     elapsedSeconds,
                   std::min(cleanliness, cleanlinessFloor), 1.0f));
    changed |= assignIfChanged(
        water.oxygen,
        std::clamp(oxygen - oxygenDecay * elapsedSeconds,
                   std::min(oxygen, oxygenFloor), 1.0f));
    return changed;
}

} // namespace

const CareUtcClock& systemCareUtcClock()
{
    static const SystemCareUtcClock clock{};
    return clock;
}

std::optional<CareUtcTimePoint> parseCareUtcTimestamp(std::string_view timestamp)
{
    if (timestamp.size() != 20 || timestamp[4] != '-' ||
        timestamp[7] != '-' || timestamp[10] != 'T' ||
        timestamp[13] != ':' || timestamp[16] != ':' ||
        timestamp[19] != 'Z')
    {
        return std::nullopt;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parseDigits(timestamp, 0, 4, year) ||
        !parseDigits(timestamp, 5, 2, month) ||
        !parseDigits(timestamp, 8, 2, day) ||
        !parseDigits(timestamp, 11, 2, hour) ||
        !parseDigits(timestamp, 14, 2, minute) ||
        !parseDigits(timestamp, 17, 2, second) || year < 1 || year > 9999 ||
        hour > 23 || minute > 59 || second > 59)
    {
        return std::nullopt;
    }

    const std::chrono::year_month_day calendarDate{
        std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    if (!calendarDate.ok())
    {
        return std::nullopt;
    }

    return CareUtcTimePoint{
        std::chrono::sys_days{calendarDate}.time_since_epoch() +
        std::chrono::hours{hour} + std::chrono::minutes{minute} +
        std::chrono::seconds{second}};
}

std::string formatCareUtcTimestamp(CareUtcTimePoint timePoint)
{
    const std::chrono::sys_days dayPoint =
        std::chrono::floor<std::chrono::days>(timePoint);
    const std::chrono::year_month_day calendarDate{dayPoint};
    const std::chrono::hh_mm_ss timeOfDay{timePoint - dayPoint};
    char buffer[21]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02lld:%02lld:%02lldZ",
                  static_cast<int>(calendarDate.year()),
                  static_cast<unsigned>(calendarDate.month()),
                  static_cast<unsigned>(calendarDate.day()),
                  static_cast<long long>(timeOfDay.hours().count()),
                  static_cast<long long>(timeOfDay.minutes().count()),
                  static_cast<long long>(timeOfDay.seconds().count()));
    return buffer;
}

void stampLastPlayedUtc(GameState& gameState, CareUtcTimePoint now)
{
    gameState.lastPlayedUtc = formatCareUtcTimestamp(now);
}

void stampLastPlayedUtc(GameState& gameState, const CareUtcClock& clock)
{
    stampLastPlayedUtc(gameState, clock.now());
}

OfflineCareResult reconcileOfflineCare(GameState& gameState,
                                       CareUtcTimePoint now,
                                       const OfflineCareConfig& config)
{
    OfflineCareResult result{};
    const CleanupCrewEffect cleanupCrew = evaluateCleanupCrewEffect(gameState);
    result.cleanupCrewCount = cleanupCrew.creatureCount;
    result.cleanlinessDecayReduction =
        cleanupCrew.cleanlinessDecayReduction;
    const std::string canonicalNow = formatCareUtcTimestamp(now);
    const std::string previousTimestamp = gameState.lastPlayedUtc;
    result.timestampChanged = previousTimestamp != canonicalNow;

    if (previousTimestamp.empty())
    {
        result.timestampStatus = OfflineCareTimestampStatus::Missing;
        gameState.lastPlayedUtc = canonicalNow;
        return result;
    }

    const std::optional<CareUtcTimePoint> previous =
        parseCareUtcTimestamp(previousTimestamp);
    if (!previous)
    {
        result.timestampStatus = OfflineCareTimestampStatus::Invalid;
        gameState.lastPlayedUtc = canonicalNow;
        return result;
    }
    if (*previous > now)
    {
        result.timestampStatus = OfflineCareTimestampStatus::Future;
        gameState.lastPlayedUtc = canonicalNow;
        return result;
    }
    if (*previous == now)
    {
        result.timestampStatus = OfflineCareTimestampStatus::Current;
        gameState.lastPlayedUtc = canonicalNow;
        return result;
    }

    result.timestampStatus = OfflineCareTimestampStatus::Applied;
    result.elapsedSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(now - *previous).count();
    const int64_t maxElapsedSeconds = std::max<int64_t>(0, config.maxElapsedSeconds);
    result.appliedSeconds = std::min(result.elapsedSeconds, maxElapsedSeconds);
    result.capped = result.elapsedSeconds > result.appliedSeconds;
    CreatureInstance* primary = findPrimaryCreature(gameState);
    result.primaryCreatureFound = primary != nullptr;
    if (result.appliedSeconds <= 0)
    {
        gameState.lastPlayedUtc = canonicalNow;
        return result;
    }

    const float elapsedSeconds = static_cast<float>(std::min<int64_t>(
        result.appliedSeconds,
        static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
    result.waterChanged =
        updateOfflineWater(gameState.water, elapsedSeconds,
                           cleanupCrew.cleanlinessDecayMultiplier, config);

    CreatureNeedsHeartbeatConfig creatureConfig{};
    creatureConfig.maxStepSeconds = elapsedSeconds;
    creatureConfig.hungerIncreasePerSecond = config.hungerIncreasePerSecond;
    creatureConfig.hungerCeiling = config.hungerCeiling;
    creatureConfig.cleanlinessResponsePerSecond =
        config.cleanlinessResponsePerSecond;
    creatureConfig.happinessResponsePerSecond = config.happinessResponsePerSecond;
    creatureConfig.healthResponsePerSecond = config.healthResponsePerSecond;
    creatureConfig.cleanlinessFloor = config.creatureCleanlinessFloor;
    creatureConfig.happinessFloor = config.creatureHappinessFloor;
    creatureConfig.healthFloor = config.creatureHealthFloor;
    if (primary != nullptr)
    {
        const CreatureInstance before = *primary;
        updateCreatureNeeds(*primary, gameState.water, elapsedSeconds,
                            creatureConfig);
        if (isNormalized(before.needs.hunger))
        {
            primary->needs.hunger =
                std::max(before.needs.hunger, primary->needs.hunger);
        }
        if (isNormalized(before.needs.cleanliness))
        {
            primary->needs.cleanliness =
                std::min(before.needs.cleanliness,
                         primary->needs.cleanliness);
        }
        if (isNormalized(before.needs.happiness))
        {
            primary->needs.happiness =
                std::min(before.needs.happiness, primary->needs.happiness);
        }
        if (isNormalized(before.needs.health))
        {
            primary->needs.health =
                std::min(before.needs.health, primary->needs.health);
        }
        refreshCreatureVitality(*primary);
        result.creatureChanged = creatureCareChanged(before, *primary);
    }
    gameState.lastPlayedUtc = canonicalNow;
    return result;
}

OfflineCareResult reconcileOfflineCare(GameState& gameState,
                                       const CareUtcClock& clock,
                                       const OfflineCareConfig& config)
{
    return reconcileOfflineCare(gameState, clock.now(), config);
}

} // namespace engine::game
