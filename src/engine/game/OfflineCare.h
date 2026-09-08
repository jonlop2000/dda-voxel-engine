#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "engine/game/GameState.h"

namespace engine::game
{

using CareUtcTimePoint = std::chrono::sys_seconds;

class CareUtcClock
{
public:
    virtual ~CareUtcClock() = default;
    virtual CareUtcTimePoint now() const = 0;
};

const CareUtcClock& systemCareUtcClock();

enum class OfflineCareTimestampStatus
{
    Missing,
    Invalid,
    Future,
    Current,
    Applied,
};

struct OfflineCareConfig
{
    static constexpr int64_t kSecondsPerDay = 24 * 60 * 60;

    int64_t maxElapsedSeconds = kSecondsPerDay;
    float hungerIncreasePerSecond = 0.45f / static_cast<float>(kSecondsPerDay);
    float hungerCeiling = 0.85f;
    float waterCleanlinessDecayPerSecond =
        0.30f / static_cast<float>(kSecondsPerDay);
    float waterCleanlinessFloor = 0.60f;
    float waterOxygenDecayPerSecond =
        0.12f / static_cast<float>(kSecondsPerDay);
    float waterOxygenFloor = 0.55f;
    float cleanlinessResponsePerSecond = 0.000008f;
    float happinessResponsePerSecond = 0.000006f;
    float healthResponsePerSecond = 0.000003f;
    float creatureCleanlinessFloor = 0.15f;
    float creatureHappinessFloor = 0.20f;
    float creatureHealthFloor = 0.30f;
};

struct OfflineCareResult
{
    OfflineCareTimestampStatus timestampStatus =
        OfflineCareTimestampStatus::Missing;
    int64_t elapsedSeconds = 0;
    int64_t appliedSeconds = 0;
    bool capped = false;
    uint32_t cleanupCrewCount = 0;
    float cleanlinessDecayReduction = 0.0f;
    bool primaryCreatureFound = false;
    bool waterChanged = false;
    bool creatureChanged = false;
    bool timestampChanged = false;

    bool stateChanged() const { return waterChanged || creatureChanged; }
};

std::optional<CareUtcTimePoint> parseCareUtcTimestamp(std::string_view timestamp);
std::string formatCareUtcTimestamp(CareUtcTimePoint timePoint);

void stampLastPlayedUtc(GameState& gameState, CareUtcTimePoint now);
void stampLastPlayedUtc(GameState& gameState, const CareUtcClock& clock);

OfflineCareResult reconcileOfflineCare(
    GameState& gameState, CareUtcTimePoint now,
    const OfflineCareConfig& config = {});
OfflineCareResult reconcileOfflineCare(
    GameState& gameState, const CareUtcClock& clock,
    const OfflineCareConfig& config = {});

} // namespace engine::game
