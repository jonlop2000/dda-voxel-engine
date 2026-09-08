#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CreatureNeeds
{
    // hunger is pressure (0 = fed, 1 = hungry). the remaining needs are condition
    // (0 = poor, 1 = healthy). cleanliness is creature-specific; habitat water
    // cleanliness is owned separately by WaterState.
    float hunger = 0.0f;
    float cleanliness = 1.0f;
    float happiness = 1.0f;
    // authoritative recoverable wellbeing. vitality is its presentation cache.
    float health = 1.0f;
};

struct CreatureInstance
{
    std::string uuid;
    std::string speciesId = "starter_fish";
    std::string displayName = "Fish";
    bool primary = false;
    float bond = 0.0f;
    // derived presentation value; care simulation recomputes it from authoritative
    // needs. it remains persisted for backward compatibility and immediate display.
    float vitality = 1.0f;
    CreatureNeeds needs{};
};

struct WaterState
{
    float temperatureC = 24.0f;
    float oxygen = 0.82f;
    float flow = 0.45f;
    float cleanliness = 1.0f;
};

struct ProgressionState
{
    uint32_t coins = 0;
    uint32_t discovery = 0;
    uint32_t careMilestone = 0;
    uint32_t tankTier = 1;
};

struct GameState
{
    std::string lastPlayedUtc;
    WaterState water{};
    ProgressionState progression{};
    std::vector<CreatureInstance> creatures{};
};
