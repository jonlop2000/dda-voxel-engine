#include "engine/game/CollectionCodex.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>

namespace engine::game
{
namespace
{

uint32_t saturatingIncrement(uint32_t value)
{
    return value == std::numeric_limits<uint32_t>::max() ? value : value + 1u;
}

CollectionCodexEntry makeKnownEntry(
    const SpeciesDefinition& definition, uint32_t ownedCount,
    const HabitatEnvironmentSnapshot& environment)
{
    CollectionCodexEntry entry{};
    entry.speciesId = definition.id;
    entry.displayName = definition.displayName;
    entry.description = definition.description;
    entry.ownedCount = ownedCount;
    entry.roles = definition.roles;
    entry.habitat = evaluateHabitatCompatibility(definition, environment);
    return entry;
}

CollectionCodexEntry makeUnknownEntry(
    const std::string& speciesId, uint32_t ownedCount,
    const HabitatEnvironmentSnapshot& environment)
{
    CollectionCodexEntry entry{};
    entry.speciesId = speciesId;
    entry.displayName = "Unknown Species";
    entry.description = "No registered species metadata is available.";
    entry.ownedCount = ownedCount;
    entry.habitat = evaluateHabitatCompatibility(speciesId, environment);
    return entry;
}

} // namespace

CollectionCodexView buildCollectionCodexView(
    const GameState& gameState,
    std::span<const HabitatDecorContribution> decorContributions)
{
    CollectionCodexView view{};
    view.registeredSpeciesCount = static_cast<uint32_t>(std::min<size_t>(
        registeredSpecies().size(), std::numeric_limits<uint32_t>::max()));
    view.cleanupCrew = evaluateCleanupCrewEffect(gameState);

    std::unordered_map<std::string, uint32_t> ownedCounts;
    ownedCounts.reserve(gameState.creatures.size());
    std::vector<CreatureInstance> ownedCreatures;
    ownedCreatures.reserve(gameState.creatures.size());
    for (const CreatureInstance& creature : gameState.creatures)
    {
        if (creature.uuid.empty() || creature.speciesId.empty())
        {
            continue;
        }

        view.totalOwnedCreatures = saturatingIncrement(view.totalOwnedCreatures);
        uint32_t& count = ownedCounts[creature.speciesId];
        count = saturatingIncrement(count);
        ownedCreatures.push_back(creature);
    }

    const HabitatEnvironmentSnapshot environment{
        gameState.water, ownedCreatures, decorContributions};
    view.entries.reserve(ownedCounts.size());
    for (const SpeciesDefinition& definition : registeredSpecies())
    {
        const auto owned = ownedCounts.find(std::string(definition.id));
        if (owned == ownedCounts.end())
        {
            continue;
        }

        view.entries.push_back(makeKnownEntry(definition, owned->second, environment));
        view.knownOwnedSpeciesCount =
            saturatingIncrement(view.knownOwnedSpeciesCount);
        ownedCounts.erase(owned);
    }

    std::map<std::string, uint32_t> unknownCounts(ownedCounts.begin(),
                                                  ownedCounts.end());
    for (const auto& [speciesId, count] : unknownCounts)
    {
        view.entries.push_back(makeUnknownEntry(speciesId, count, environment));
        view.unknownOwnedSpeciesCount =
            saturatingIncrement(view.unknownOwnedSpeciesCount);
    }
    return view;
}

} // namespace engine::game
