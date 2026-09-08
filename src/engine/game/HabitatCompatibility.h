#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/game/GameState.h"
#include "engine/game/SpeciesRegistry.h"

namespace engine::game
{

enum class HabitatRequirementStatus
{
    Satisfied,
    Unsatisfied,
};

enum class HabitatCompatibilityStatus
{
    Compatible,
    NeedsAttention,
    UnknownSpecies,
};

enum class HabitatWaterParameter
{
    TemperatureC,
    Oxygen,
    Flow,
    Cleanliness,
};

enum class HabitatTankmateRule
{
    MinimumGroupSize,
    RequiredSpeciesPresent,
    IncompatibleSpeciesAbsent,
};

// a semantic contribution describes what an object provides to the habitat. it
// is deliberately separate from PlacementRules::allowedMaterialCategories,
// which describes the surfaces on which an object may be placed.
struct HabitatDecorContribution
{
    uint32_t materialCategories = 0;
    uint32_t count = 1;
};

// the spans are borrowed only for the duration of evaluation. keeping the
// environment explicit makes the pure evaluator useful to runtime ui, tests,
// and future previews without coupling it to an acquisition path.
struct HabitatEnvironmentSnapshot
{
    WaterState water{};
    std::span<const CreatureInstance> creatures{};
    std::span<const HabitatDecorContribution> decor{};
};

struct HabitatWaterEvaluation
{
    HabitatWaterParameter parameter = HabitatWaterParameter::TemperatureC;
    float observed = 0.0f;
    SpeciesParameterBand required{};
    HabitatRequirementStatus status = HabitatRequirementStatus::Unsatisfied;
};

struct HabitatDecorEvaluation
{
    std::string requirementId{};
    std::string displayName{};
    uint32_t acceptedMaterialCategories = 0;
    uint32_t observedCount = 0;
    uint32_t requiredCount = 0;
    HabitatRequirementStatus status = HabitatRequirementStatus::Unsatisfied;
};

struct HabitatTankmateEvaluation
{
    HabitatTankmateRule rule = HabitatTankmateRule::MinimumGroupSize;
    std::string speciesId{};
    uint32_t observedCount = 0;
    uint32_t requiredCount = 0;
    HabitatRequirementStatus status = HabitatRequirementStatus::Unsatisfied;
};

struct HabitatCompatibilityResult
{
    HabitatCompatibilityStatus status = HabitatCompatibilityStatus::UnknownSpecies;
    std::string speciesId{};
    std::vector<HabitatWaterEvaluation> water{};
    std::vector<HabitatDecorEvaluation> decor{};
    std::vector<HabitatTankmateEvaluation> tankmates{};
    size_t unsatisfiedRequirementCount = 0;

    bool knownSpecies() const
    {
        return status != HabitatCompatibilityStatus::UnknownSpecies;
    }

    bool compatible() const
    {
        return status == HabitatCompatibilityStatus::Compatible;
    }
};

// produces advisory information only. it never adds, removes, rejects, or
// mutates a creature/placeable and must not be used as an acquisition gate.
HabitatCompatibilityResult evaluateHabitatCompatibility(
    const SpeciesDefinition& species,
    const HabitatEnvironmentSnapshot& environment);

// unknown canonical ids fail closed as UnknownSpecies while still preserving
// the requested id in the result for diagnostics.
HabitatCompatibilityResult evaluateHabitatCompatibility(
    std::string_view speciesId,
    const HabitatEnvironmentSnapshot& environment);

} // namespace engine::game
