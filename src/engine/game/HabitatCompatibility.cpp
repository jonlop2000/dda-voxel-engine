#include "engine/game/HabitatCompatibility.h"

#include <limits>

namespace
{

using engine::game::HabitatDecorContribution;
using engine::game::HabitatEnvironmentSnapshot;
using engine::game::HabitatRequirementStatus;

uint32_t saturatingAdd(uint32_t lhs, uint32_t rhs)
{
    if (rhs > std::numeric_limits<uint32_t>::max() - lhs)
    {
        return std::numeric_limits<uint32_t>::max();
    }
    return lhs + rhs;
}

uint32_t countSpecies(const HabitatEnvironmentSnapshot& environment,
                      std::string_view speciesId)
{
    uint32_t count = 0;
    for (const CreatureInstance& creature : environment.creatures)
    {
        if (creature.speciesId == speciesId)
        {
            count = saturatingAdd(count, 1);
        }
    }
    return count;
}

uint32_t countMatchingDecor(const HabitatEnvironmentSnapshot& environment,
                            uint32_t acceptedMaterialCategories)
{
    uint32_t count = 0;
    for (const HabitatDecorContribution& contribution : environment.decor)
    {
        if ((contribution.materialCategories & acceptedMaterialCategories) != 0)
        {
            count = saturatingAdd(count, contribution.count);
        }
    }
    return count;
}

HabitatRequirementStatus requirementStatus(bool satisfied)
{
    return satisfied ? HabitatRequirementStatus::Satisfied
                     : HabitatRequirementStatus::Unsatisfied;
}

} // namespace

namespace engine::game
{

HabitatCompatibilityResult evaluateHabitatCompatibility(
    const SpeciesDefinition& species,
    const HabitatEnvironmentSnapshot& environment)
{
    HabitatCompatibilityResult result{};
    result.status = HabitatCompatibilityStatus::Compatible;
    result.speciesId = species.id;
    result.water.reserve(4);

    const auto addWater = [&](HabitatWaterParameter parameter, float observed,
                              const SpeciesParameterBand& required) {
        const bool satisfied = speciesBandContains(required, observed);
        result.water.push_back(
            {parameter, observed, required, requirementStatus(satisfied)});
        result.unsatisfiedRequirementCount += satisfied ? 0u : 1u;
    };

    addWater(HabitatWaterParameter::TemperatureC, environment.water.temperatureC,
             species.water.temperatureC);
    addWater(HabitatWaterParameter::Oxygen, environment.water.oxygen,
             species.water.oxygen);
    addWater(HabitatWaterParameter::Flow, environment.water.flow,
             species.water.flow);
    addWater(HabitatWaterParameter::Cleanliness, environment.water.cleanliness,
             species.water.cleanliness);

    result.decor.reserve(species.habitat.decor.size());
    for (const SpeciesDecorRequirement& requirement : species.habitat.decor)
    {
        const uint32_t observed = countMatchingDecor(
            environment, requirement.acceptedMaterialCategories);
        const bool satisfied = observed >= requirement.minimumCount;
        result.decor.push_back({std::string(requirement.id),
                                std::string(requirement.displayName),
                                requirement.acceptedMaterialCategories, observed,
                                requirement.minimumCount,
                                requirementStatus(satisfied)});
        result.unsatisfiedRequirementCount += satisfied ? 0u : 1u;
    }

    const SpeciesTankmateRequirements& requirements = species.habitat.tankmates;
    result.tankmates.reserve(1 + requirements.requiredSpeciesIds.size() +
                             requirements.incompatibleSpeciesIds.size());

    const uint32_t sameSpeciesCount = countSpecies(environment, species.id);
    const bool groupSatisfied = sameSpeciesCount >= requirements.minimumGroupSize;
    result.tankmates.push_back({HabitatTankmateRule::MinimumGroupSize,
                                std::string(species.id), sameSpeciesCount,
                                requirements.minimumGroupSize,
                                requirementStatus(groupSatisfied)});
    result.unsatisfiedRequirementCount += groupSatisfied ? 0u : 1u;

    for (std::string_view requiredSpeciesId : requirements.requiredSpeciesIds)
    {
        const uint32_t observed = countSpecies(environment, requiredSpeciesId);
        const bool satisfied = observed > 0;
        result.tankmates.push_back({HabitatTankmateRule::RequiredSpeciesPresent,
                                    std::string(requiredSpeciesId), observed, 1,
                                    requirementStatus(satisfied)});
        result.unsatisfiedRequirementCount += satisfied ? 0u : 1u;
    }

    for (std::string_view incompatibleSpeciesId :
         requirements.incompatibleSpeciesIds)
    {
        const uint32_t observed = countSpecies(environment, incompatibleSpeciesId);
        const bool satisfied = observed == 0;
        result.tankmates.push_back({HabitatTankmateRule::IncompatibleSpeciesAbsent,
                                    std::string(incompatibleSpeciesId), observed, 0,
                                    requirementStatus(satisfied)});
        result.unsatisfiedRequirementCount += satisfied ? 0u : 1u;
    }

    if (result.unsatisfiedRequirementCount != 0)
    {
        result.status = HabitatCompatibilityStatus::NeedsAttention;
    }
    return result;
}

HabitatCompatibilityResult evaluateHabitatCompatibility(
    std::string_view speciesId,
    const HabitatEnvironmentSnapshot& environment)
{
    const SpeciesDefinition* species = findSpecies(speciesId);
    if (species != nullptr)
    {
        return evaluateHabitatCompatibility(*species, environment);
    }

    HabitatCompatibilityResult result{};
    result.speciesId = speciesId;
    return result;
}

} // namespace engine::game
