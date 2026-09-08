#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/game/GameState.h"

namespace engine::game
{

inline constexpr uint32_t kUnspecifiedLiveCreatureOrdinal =
    std::numeric_limits<uint32_t>::max();

// runtime ids are disposable. stableSpeciesOrdinal identifies a deterministic
// procedural slot within one species and is never persisted as creature identity.
struct LiveCreatureSlot
{
    uint64_t runtimeId = 0;
    std::string_view speciesId{};
    uint32_t stableSpeciesOrdinal = kUnspecifiedLiveCreatureOrdinal;
};

struct CreatureRuntimeBinding
{
    std::string creatureUuid{};
    std::string speciesId{};
    uint64_t runtimeId = 0;
    uint32_t stableSpeciesOrdinal = kUnspecifiedLiveCreatureOrdinal;
    size_t persistentIndex = 0;
    size_t liveIndex = 0;
    bool primary = false;
};

struct CreatureRuntimeBindingResult
{
    std::vector<CreatureRuntimeBinding> bindings{};
    size_t persistentCreatureCount = 0;
    size_t liveCreatureCount = 0;
    size_t unmatchedPersistentCount = 0;
    size_t unmatchedLiveCount = 0;
    size_t invalidLiveSlotCount = 0;
    size_t duplicateRuntimeIdCount = 0;
    size_t duplicateStableSlotCount = 0;

    const CreatureRuntimeBinding* findByCreatureUuid(
        std::string_view creatureUuid) const;
    const CreatureRuntimeBinding* findByRuntimeId(uint64_t runtimeId) const;
    bool contains(std::string_view creatureUuid, uint64_t runtimeId) const;
};

// binds valid saved creatures to valid live slots one-to-one. saved roster order
// supplies the stable same-species identity order; explicit live ordinals make the
// result independent of discovery order. ambiguous ordinal slots remain unmatched.
// the function never creates, removes, or otherwise mutates persistent creatures.
CreatureRuntimeBindingResult bindCreatureRosterToLiveSlots(
    const GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures);

} // namespace engine::game
