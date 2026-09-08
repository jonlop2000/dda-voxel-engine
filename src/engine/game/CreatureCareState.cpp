#include "engine/game/CreatureCareState.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

#include "engine/game/CreatureVitality.h"

namespace
{

bool isValidCreature(const CreatureInstance& creature)
{
    return !creature.uuid.empty() && !creature.speciesId.empty();
}

std::string uniqueDuplicateUuid(std::string_view baseUuid, size_t firstSuffix,
                                const std::unordered_set<std::string>& usedUuids)
{
    size_t suffix = firstSuffix;
    std::string candidate;
    do
    {
        candidate = std::string(baseUuid) + "-duplicate-" + std::to_string(suffix++);
    } while (usedUuids.contains(candidate));
    return candidate;
}

} // namespace

namespace engine::game
{

const CreatureInstance* findPrimaryCreature(const GameState& gameState)
{
    for (const CreatureInstance& creature : gameState.creatures)
    {
        if (creature.primary && isValidCreature(creature))
        {
            return &creature;
        }
    }
    return nullptr;
}

CreatureInstance* findPrimaryCreature(GameState& gameState)
{
    for (CreatureInstance& creature : gameState.creatures)
    {
        if (creature.primary && isValidCreature(creature))
        {
            return &creature;
        }
    }
    return nullptr;
}

CreatureRosterNormalizationResult normalizeCreatureRoster(
    GameState& gameState, bool createPrimaryIfMissing,
    const PrimaryCreatureDefaults& defaults)
{
    CreatureRosterNormalizationResult result{};

    std::unordered_set<std::string> usedUuids;
    for (CreatureInstance& creature : gameState.creatures)
    {
        if (!isValidCreature(creature))
        {
            if (creature.primary)
            {
                creature.primary = false;
                result.changed = true;
                ++result.clearedExtraPrimaryFlags;
            }
            continue;
        }

        if (!usedUuids.insert(creature.uuid).second)
        {
            creature.uuid = uniqueDuplicateUuid(creature.uuid, 2, usedUuids);
            usedUuids.insert(creature.uuid);
            result.changed = true;
            ++result.repairedDuplicateUuids;
        }
    }

    CreatureInstance* primary = nullptr;
    for (CreatureInstance& creature : gameState.creatures)
    {
        if (!creature.primary || !isValidCreature(creature))
        {
            continue;
        }
        if (primary == nullptr)
        {
            primary = &creature;
        }
        else
        {
            creature.primary = false;
            result.changed = true;
            ++result.clearedExtraPrimaryFlags;
        }
    }

    if (primary == nullptr)
    {
        for (CreatureInstance& creature : gameState.creatures)
        {
            if (isValidCreature(creature))
            {
                creature.primary = true;
                primary = &creature;
                result.changed = true;
                result.selectedFallbackPrimary = true;
                break;
            }
        }
    }

    if (primary == nullptr && createPrimaryIfMissing)
    {
        CreatureInstance creature{};
        creature.uuid = defaults.uuid.empty() ? std::string(kDefaultStarterCreatureUuid)
                                              : std::string(defaults.uuid);
        creature.speciesId = defaults.speciesId.empty()
                                 ? std::string(kStarterFishSpeciesId)
                                 : std::string(defaults.speciesId);
        creature.displayName =
            defaults.displayName.empty() ? "Fish" : std::string(defaults.displayName);
        creature.primary = true;
        gameState.creatures.push_back(std::move(creature));
        primary = &gameState.creatures.back();
        result.changed = true;
        result.createdPrimary = true;
    }

    if (primary != nullptr)
    {
        result.primaryUuid = primary->uuid;
    }
    return result;
}

PrimaryCreatureSyncResult synchronizePrimaryCreature(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures,
    const PrimaryCreatureDefaults& defaults)
{
    return synchronizeCreatureRoster(gameState, liveCreatures, defaults);
}

PrimaryCreatureSyncResult synchronizeCreatureRoster(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures,
    const PrimaryCreatureDefaults& defaults)
{
    PrimaryCreatureDefaults resolvedDefaults = defaults;
    const auto firstValidLive = std::find_if(
        liveCreatures.begin(), liveCreatures.end(), [](const LiveCreatureSlot& live) {
            return live.runtimeId != 0 && !live.speciesId.empty();
        });
    if (firstValidLive != liveCreatures.end())
    {
        resolvedDefaults.speciesId = firstValidLive->speciesId;
        if (resolvedDefaults.speciesId == kAxolotlSpeciesId)
        {
            resolvedDefaults.displayName = "Axolotl";
            resolvedDefaults.uuid = kDefaultAxolotlCreatureUuid;
        }
    }

    PrimaryCreatureSyncResult result{};
    result.normalization =
        normalizeCreatureRoster(gameState, firstValidLive != liveCreatures.end(),
                                resolvedDefaults);
    result.rosterBindings = bindCreatureRosterToLiveSlots(gameState, liveCreatures);

    const CreatureInstance* primary = findPrimaryCreature(gameState);
    if (primary == nullptr)
    {
        return result;
    }

    result.creatureUuid = primary->uuid;
    result.speciesId = primary->speciesId;
    if (const CreatureRuntimeBinding* binding =
            result.rosterBindings.findByCreatureUuid(primary->uuid))
    {
        result.runtimeId = binding->runtimeId;
    }
    return result;
}

CreatureFeedResult feedCreature(CreatureInstance& creature,
                                const CreatureFeedConfig& config)
{
    CreatureFeedResult result{};
    const float bond = normalizedCreatureBond(creature.bond);
    creature.bond = bond;
    result.bondBefore = bond;
    result.bondAfter = bond;
    const float hunger = std::isfinite(creature.needs.hunger)
                             ? std::clamp(creature.needs.hunger, 0.0f, 1.0f)
                             : 0.0f;
    creature.needs.hunger = hunger;
    result.hungerBefore = hunger;
    result.hungerAfter = hunger;

    const float minimumHunger = std::clamp(config.minimumHunger, 0.0f, 1.0f);
    const float reduction = std::clamp(config.hungerReduction, 0.0f, 1.0f);
    if (hunger <= minimumHunger || reduction <= 0.0f)
    {
        return result;
    }

    creature.needs.hunger = std::max(0.0f, hunger - reduction);
    result.fed = creature.needs.hunger < hunger;
    result.hungerAfter = creature.needs.hunger;
    const float bondGain = std::clamp(
        std::isfinite(config.bondGain) ? config.bondGain : 0.0f, 0.0f, 1.0f);
    creature.bond = std::clamp(bond + bondGain, 0.0f, 1.0f);
    result.bondAfter = creature.bond;
    refreshCreatureVitality(creature);
    return result;
}

} // namespace engine::game
