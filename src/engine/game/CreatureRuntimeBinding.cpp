#include "engine/game/CreatureRuntimeBinding.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{

bool isValidPersistentCreature(const CreatureInstance& creature)
{
    return !creature.uuid.empty() && !creature.speciesId.empty();
}

struct LiveCandidate
{
    engine::game::LiveCreatureSlot slot{};
    size_t inputIndex = 0;
    bool claimed = false;
};

} // namespace

namespace engine::game
{

const CreatureRuntimeBinding* CreatureRuntimeBindingResult::findByCreatureUuid(
    std::string_view creatureUuid) const
{
    const auto found = std::find_if(
        bindings.begin(), bindings.end(),
        [creatureUuid](const CreatureRuntimeBinding& binding) {
            return binding.creatureUuid == creatureUuid;
        });
    return found != bindings.end() ? &*found : nullptr;
}

const CreatureRuntimeBinding* CreatureRuntimeBindingResult::findByRuntimeId(
    uint64_t runtimeId) const
{
    if (runtimeId == 0)
    {
        return nullptr;
    }
    const auto found = std::find_if(
        bindings.begin(), bindings.end(),
        [runtimeId](const CreatureRuntimeBinding& binding) {
            return binding.runtimeId == runtimeId;
        });
    return found != bindings.end() ? &*found : nullptr;
}

bool CreatureRuntimeBindingResult::contains(std::string_view creatureUuid,
                                            uint64_t runtimeId) const
{
    const CreatureRuntimeBinding* binding = findByCreatureUuid(creatureUuid);
    return binding != nullptr && binding->runtimeId == runtimeId;
}

CreatureRuntimeBindingResult bindCreatureRosterToLiveSlots(
    const GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures)
{
    CreatureRuntimeBindingResult result{};

    std::vector<LiveCandidate> candidates;
    candidates.reserve(liveCreatures.size());
    std::unordered_set<uint64_t> runtimeIds;
    for (size_t index = 0; index < liveCreatures.size(); ++index)
    {
        const LiveCreatureSlot& live = liveCreatures[index];
        if (live.runtimeId == 0 || live.speciesId.empty())
        {
            ++result.invalidLiveSlotCount;
            continue;
        }
        if (!runtimeIds.insert(live.runtimeId).second)
        {
            ++result.duplicateRuntimeIdCount;
            continue;
        }
        candidates.push_back({live, index, false});
    }

    std::unordered_map<std::string, std::unordered_map<uint32_t, size_t>>
        explicitOrdinalCounts;
    for (const LiveCandidate& candidate : candidates)
    {
        if (candidate.slot.stableSpeciesOrdinal !=
            kUnspecifiedLiveCreatureOrdinal)
        {
            ++explicitOrdinalCounts[std::string(candidate.slot.speciesId)]
                                    [candidate.slot.stableSpeciesOrdinal];
        }
    }
    for (const auto& speciesEntry : explicitOrdinalCounts)
    {
        for (const auto& ordinalEntry : speciesEntry.second)
        {
            const size_t count = ordinalEntry.second;
            if (count > 1)
            {
                result.duplicateStableSlotCount += count - 1;
            }
        }
    }

    result.liveCreatureCount = candidates.size();
    candidates.erase(
        std::remove_if(
            candidates.begin(), candidates.end(),
            [&explicitOrdinalCounts](const LiveCandidate& candidate) {
                if (candidate.slot.stableSpeciesOrdinal ==
                    kUnspecifiedLiveCreatureOrdinal)
                {
                    return false;
                }
                const auto species = explicitOrdinalCounts.find(
                    std::string(candidate.slot.speciesId));
                if (species == explicitOrdinalCounts.end())
                {
                    return false;
                }
                const auto ordinal =
                    species->second.find(candidate.slot.stableSpeciesOrdinal);
                return ordinal != species->second.end() && ordinal->second > 1;
            }),
        candidates.end());

    std::stable_sort(
        candidates.begin(), candidates.end(),
        [](const LiveCandidate& lhs, const LiveCandidate& rhs) {
            if (lhs.slot.speciesId != rhs.slot.speciesId)
            {
                return lhs.slot.speciesId < rhs.slot.speciesId;
            }
            const bool lhsExplicit =
                lhs.slot.stableSpeciesOrdinal != kUnspecifiedLiveCreatureOrdinal;
            const bool rhsExplicit =
                rhs.slot.stableSpeciesOrdinal != kUnspecifiedLiveCreatureOrdinal;
            if (lhsExplicit != rhsExplicit)
            {
                return lhsExplicit;
            }
            if (lhsExplicit &&
                lhs.slot.stableSpeciesOrdinal != rhs.slot.stableSpeciesOrdinal)
            {
                return lhs.slot.stableSpeciesOrdinal < rhs.slot.stableSpeciesOrdinal;
            }
            return lhs.inputIndex < rhs.inputIndex;
        });

    for (size_t persistentIndex = 0;
         persistentIndex < gameState.creatures.size(); ++persistentIndex)
    {
        const CreatureInstance& creature = gameState.creatures[persistentIndex];
        if (!isValidPersistentCreature(creature))
        {
            continue;
        }
        ++result.persistentCreatureCount;

        const auto found = std::find_if(
            candidates.begin(), candidates.end(),
            [&creature](const LiveCandidate& candidate) {
                return !candidate.claimed &&
                       candidate.slot.speciesId == creature.speciesId;
            });
        if (found == candidates.end())
        {
            continue;
        }

        found->claimed = true;
        CreatureRuntimeBinding binding{};
        binding.creatureUuid = creature.uuid;
        binding.speciesId = creature.speciesId;
        binding.runtimeId = found->slot.runtimeId;
        binding.stableSpeciesOrdinal = found->slot.stableSpeciesOrdinal;
        binding.persistentIndex = persistentIndex;
        binding.liveIndex = found->inputIndex;
        binding.primary = creature.primary;
        result.bindings.push_back(std::move(binding));
    }

    result.unmatchedPersistentCount =
        result.persistentCreatureCount - result.bindings.size();
    result.unmatchedLiveCount = result.liveCreatureCount - result.bindings.size();
    return result;
}

} // namespace engine::game
