#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/game/CleanupCrew.h"
#include "engine/game/HabitatCompatibility.h"

namespace engine::game
{

struct CollectionCodexEntry
{
    std::string speciesId{};
    std::string displayName{};
    std::string description{};
    uint32_t ownedCount = 0;
    SpeciesRoleMask roles = speciesRoleMask(SpeciesRole::None);
    HabitatCompatibilityResult habitat{};

    bool knownSpecies() const
    {
        return habitat.knownSpecies();
    }
};

struct CollectionCodexView
{
    uint32_t totalOwnedCreatures = 0;
    uint32_t registeredSpeciesCount = 0;
    uint32_t knownOwnedSpeciesCount = 0;
    uint32_t unknownOwnedSpeciesCount = 0;
    CleanupCrewEffect cleanupCrew{};
    std::vector<CollectionCodexEntry> entries{};
};

// builds a durable, deterministic read model for the runtime codex. only complete
// saved roster entries are considered owned. registered species follow registry
// order; unknown persisted ids follow in lexical order so they remain visible
// without inventing metadata. the query is advisory and never mutates the roster.
CollectionCodexView buildCollectionCodexView(
    const GameState& gameState,
    std::span<const HabitatDecorContribution> decorContributions = {});

} // namespace engine::game
