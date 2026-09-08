#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "engine/game/CreatureRuntimeBinding.h"
#include "engine/game/GameState.h"
#include "engine/game/SpeciesIds.h"

namespace engine::game
{

inline constexpr std::string_view kDefaultStarterCreatureUuid =
    "creature-primary-starter-fish-0001";
inline constexpr std::string_view kDefaultAxolotlCreatureUuid =
    "creature-primary-axolotl-0001";

struct PrimaryCreatureDefaults
{
    std::string_view speciesId = kStarterFishSpeciesId;
    std::string_view displayName = "Fish";
    std::string_view uuid = kDefaultStarterCreatureUuid;
};

struct CreatureRosterNormalizationResult
{
    bool changed = false;
    bool createdPrimary = false;
    bool selectedFallbackPrimary = false;
    size_t clearedExtraPrimaryFlags = 0;
    size_t repairedDuplicateUuids = 0;
    std::string primaryUuid{};
};

struct PrimaryCreatureSyncResult
{
    CreatureRosterNormalizationResult normalization{};
    CreatureRuntimeBindingResult rosterBindings{};
    std::string creatureUuid{};
    std::string speciesId{};
    uint64_t runtimeId = 0;

    bool hasPersistentCreature() const { return !creatureUuid.empty(); }
    bool liveBound() const { return runtimeId != 0; }
};

struct CreatureFeedConfig
{
    float hungerReduction = 0.30f;
    float minimumHunger = 0.01f;
    float bondGain = 0.02f;
};

struct CreatureFeedResult
{
    bool fed = false;
    float hungerBefore = 0.0f;
    float hungerAfter = 0.0f;
    float bondBefore = 0.0f;
    float bondAfter = 0.0f;
};

const CreatureInstance* findPrimaryCreature(const GameState& gameState);
CreatureInstance* findPrimaryCreature(GameState& gameState);

CreatureRosterNormalizationResult normalizeCreatureRoster(
    GameState& gameState, bool createPrimaryIfMissing = false,
    const PrimaryCreatureDefaults& defaults = {});

PrimaryCreatureSyncResult synchronizePrimaryCreature(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures,
    const PrimaryCreatureDefaults& defaults = {});

PrimaryCreatureSyncResult synchronizeCreatureRoster(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures,
    const PrimaryCreatureDefaults& defaults = {});

CreatureFeedResult feedCreature(CreatureInstance& creature,
                                const CreatureFeedConfig& config = {});

} // namespace engine::game
