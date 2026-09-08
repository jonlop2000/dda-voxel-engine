#include "engine/game/SpeciesRegistry.h"

#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace
{

using engine::game::SpeciesDecorRequirement;
using engine::game::SpeciesDefinition;
using engine::game::SpeciesParameterBand;
using engine::game::SpeciesRegistryIssue;
using engine::game::SpeciesRegistryIssueCode;
using engine::game::SpeciesRole;

constexpr uint32_t kKnownMaterialCategories =
    PlaceableMaterialCategory::Substrate | PlaceableMaterialCategory::Rock |
    PlaceableMaterialCategory::Decoration;

constexpr engine::game::SpeciesRoleMask kKnownRoles =
    engine::game::speciesRoleMask(SpeciesRole::Producer) |
    engine::game::speciesRoleMask(SpeciesRole::CleanupCrew) |
    engine::game::speciesRoleMask(SpeciesRole::FilterFeeder) |
    engine::game::speciesRoleMask(SpeciesRole::Grazer) |
    engine::game::speciesRoleMask(SpeciesRole::Predator) |
    engine::game::speciesRoleMask(SpeciesRole::Symbiont) |
    engine::game::speciesRoleMask(SpeciesRole::Showpiece);

constexpr std::array kStarterFishDecor = {
    SpeciesDecorRequirement{"shelter", "Shelter",
                            PlaceableMaterialCategory::Rock |
                                PlaceableMaterialCategory::Decoration,
                            1},
};

constexpr std::array kAxolotlDecor = {
    SpeciesDecorRequirement{"resting_cover", "Resting cover",
                            PlaceableMaterialCategory::Rock |
                                PlaceableMaterialCategory::Decoration,
                            1},
};

constexpr std::array kRamshornSnailDecor = {
    SpeciesDecorRequirement{"grazing_surface", "Grazing surface",
                            PlaceableMaterialCategory::Rock |
                                PlaceableMaterialCategory::Decoration,
                            1},
};

const std::array kSpecies = {
    SpeciesDefinition{
        engine::game::kStarterFishSpeciesId,
        "Starter Fish",
        "A resilient first companion that prefers clean, gently moving water.",
        engine::game::speciesRoleMask(SpeciesRole::Showpiece),
        {{22.0f, 28.0f}, {0.55f, 1.0f}, {0.10f, 0.70f}, {0.55f, 1.0f}},
        {kStarterFishDecor, {1, {}, {}}},
    },
    SpeciesDefinition{
        engine::game::kAxolotlSpeciesId,
        "Axolotl",
        "A calm cold-water companion that favors gentle flow and cover.",
        engine::game::speciesRoleMask(SpeciesRole::Showpiece),
        {{16.0f, 22.0f}, {0.65f, 1.0f}, {0.05f, 0.45f}, {0.65f, 1.0f}},
        {kAxolotlDecor, {1, {}, {}}},
    },
    SpeciesDefinition{
        engine::game::kRamshornSnailSpeciesId,
        "Ramshorn Snail",
        "A peaceful grazer that reduces the aquarium's routine cleaning load.",
        SpeciesRole::CleanupCrew | SpeciesRole::Grazer,
        {{18.0f, 28.0f}, {0.45f, 1.0f}, {0.0f, 0.60f}, {0.35f, 1.0f}},
        {kRamshornSnailDecor, {1, {}, {}}},
    },
};

bool validBand(const SpeciesParameterBand& band)
{
    return std::isfinite(band.minimum) && std::isfinite(band.maximum) &&
           band.minimum <= band.maximum;
}

bool validNormalizedBand(const SpeciesParameterBand& band)
{
    return validBand(band) && band.minimum >= 0.0f && band.maximum <= 1.0f;
}

void addIssue(std::vector<SpeciesRegistryIssue>& issues,
              SpeciesRegistryIssueCode code, size_t definitionIndex,
              const SpeciesDefinition& definition, std::string_view detailId = {})
{
    issues.push_back({code, definitionIndex, definition.id, detailId});
}

} // namespace

namespace engine::game
{

std::span<const SpeciesDefinition> registeredSpecies()
{
    return kSpecies;
}

const SpeciesDefinition* findSpecies(std::string_view speciesId)
{
    for (const SpeciesDefinition& definition : kSpecies)
    {
        if (definition.id == speciesId)
        {
            return &definition;
        }
    }
    return nullptr;
}

bool speciesBandContains(const SpeciesParameterBand& band, float value)
{
    return validBand(band) && std::isfinite(value) && value >= band.minimum &&
           value <= band.maximum;
}

std::string_view speciesRoleDisplayName(SpeciesRole role)
{
    switch (role)
    {
    case SpeciesRole::Producer:
        return "Producer";
    case SpeciesRole::CleanupCrew:
        return "Cleanup crew";
    case SpeciesRole::FilterFeeder:
        return "Filter feeder";
    case SpeciesRole::Grazer:
        return "Grazer";
    case SpeciesRole::Predator:
        return "Predator";
    case SpeciesRole::Symbiont:
        return "Symbiont";
    case SpeciesRole::Showpiece:
        return "Showpiece";
    case SpeciesRole::None:
        return "None";
    }
    return "Unknown";
}

std::vector<SpeciesRegistryIssue> validateSpeciesRegistry(
    std::span<const SpeciesDefinition> definitions)
{
    std::vector<SpeciesRegistryIssue> issues;
    std::unordered_map<std::string_view, size_t> speciesById;
    speciesById.reserve(definitions.size());

    for (size_t index = 0; index < definitions.size(); ++index)
    {
        const SpeciesDefinition& definition = definitions[index];
        if (definition.id.empty())
        {
            addIssue(issues, SpeciesRegistryIssueCode::EmptyId, index, definition);
        }
        else if (!speciesById.emplace(definition.id, index).second)
        {
            addIssue(issues, SpeciesRegistryIssueCode::DuplicateId, index, definition);
        }

        if (definition.displayName.empty())
        {
            addIssue(issues, SpeciesRegistryIssueCode::EmptyDisplayName, index,
                     definition);
        }
        if (definition.roles == speciesRoleMask(SpeciesRole::None) ||
            (definition.roles & ~kKnownRoles) != 0)
        {
            addIssue(issues, SpeciesRegistryIssueCode::InvalidRoleMask, index,
                     definition);
        }
        if (!validBand(definition.water.temperatureC))
        {
            addIssue(issues, SpeciesRegistryIssueCode::InvalidTemperatureBand, index,
                     definition);
        }
        if (!validNormalizedBand(definition.water.oxygen))
        {
            addIssue(issues, SpeciesRegistryIssueCode::InvalidOxygenBand, index,
                     definition);
        }
        if (!validNormalizedBand(definition.water.flow))
        {
            addIssue(issues, SpeciesRegistryIssueCode::InvalidFlowBand, index,
                     definition);
        }
        if (!validNormalizedBand(definition.water.cleanliness))
        {
            addIssue(issues, SpeciesRegistryIssueCode::InvalidCleanlinessBand, index,
                     definition);
        }

        std::unordered_set<std::string_view> decorIds;
        for (const SpeciesDecorRequirement& requirement : definition.habitat.decor)
        {
            const bool validRequirement =
                !requirement.id.empty() && !requirement.displayName.empty() &&
                requirement.minimumCount > 0 &&
                requirement.acceptedMaterialCategories != 0 &&
                (requirement.acceptedMaterialCategories & ~kKnownMaterialCategories) ==
                    0;
            if (!validRequirement)
            {
                addIssue(issues, SpeciesRegistryIssueCode::InvalidDecorRequirement,
                         index, definition, requirement.id);
            }
            if (!requirement.id.empty() && !decorIds.insert(requirement.id).second)
            {
                addIssue(issues, SpeciesRegistryIssueCode::DuplicateDecorRequirement,
                         index, definition, requirement.id);
            }
        }

        if (definition.habitat.tankmates.minimumGroupSize == 0)
        {
            addIssue(issues, SpeciesRegistryIssueCode::InvalidMinimumGroupSize, index,
                     definition);
        }
    }

    for (size_t index = 0; index < definitions.size(); ++index)
    {
        const SpeciesDefinition& definition = definitions[index];
        const auto validateReferences = [&](std::span<const std::string_view> ids) {
            for (std::string_view referencedId : ids)
            {
                if (referencedId.empty())
                {
                    addIssue(issues, SpeciesRegistryIssueCode::EmptyTankmateReference,
                             index, definition);
                }
                else if (referencedId == definition.id)
                {
                    addIssue(issues, SpeciesRegistryIssueCode::SelfTankmateReference,
                             index, definition, referencedId);
                }
                else if (!speciesById.contains(referencedId))
                {
                    addIssue(issues, SpeciesRegistryIssueCode::UnknownTankmateReference,
                             index, definition, referencedId);
                }
            }
        };
        validateReferences(definition.habitat.tankmates.requiredSpeciesIds);
        validateReferences(definition.habitat.tankmates.incompatibleSpeciesIds);
    }

    return issues;
}

} // namespace engine::game
