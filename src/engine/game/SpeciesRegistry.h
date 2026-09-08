#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "engine/game/Placeables.h"
#include "engine/game/SpeciesIds.h"

namespace engine::game
{

enum class SpeciesRole : uint32_t
{
    None = 0,
    Producer = 1u << 0,
    CleanupCrew = 1u << 1,
    FilterFeeder = 1u << 2,
    Grazer = 1u << 3,
    Predator = 1u << 4,
    Symbiont = 1u << 5,
    Showpiece = 1u << 6,
};

using SpeciesRoleMask = uint32_t;

constexpr SpeciesRoleMask speciesRoleMask(SpeciesRole role)
{
    return static_cast<SpeciesRoleMask>(role);
}

constexpr SpeciesRoleMask operator|(SpeciesRole lhs, SpeciesRole rhs)
{
    return speciesRoleMask(lhs) | speciesRoleMask(rhs);
}

constexpr SpeciesRoleMask operator|(SpeciesRoleMask lhs, SpeciesRole rhs)
{
    return lhs | speciesRoleMask(rhs);
}

constexpr SpeciesRoleMask operator|(SpeciesRole lhs, SpeciesRoleMask rhs)
{
    return speciesRoleMask(lhs) | rhs;
}

constexpr bool hasSpeciesRole(SpeciesRoleMask roles, SpeciesRole role)
{
    return (roles & speciesRoleMask(role)) != 0;
}

struct SpeciesParameterBand
{
    float minimum = 0.0f;
    float maximum = 1.0f;
};

struct SpeciesWaterRequirements
{
    SpeciesParameterBand temperatureC{20.0f, 28.0f};
    SpeciesParameterBand oxygen{0.0f, 1.0f};
    SpeciesParameterBand flow{0.0f, 1.0f};
    SpeciesParameterBand cleanliness{0.0f, 1.0f};
};

// this uses the same Substrate/Rock/Decoration category vocabulary as
// PlacementRules. it is descriptive species data; no acquisition or
// placement path is allowed to treat it as a gate yet.
struct SpeciesDecorRequirement
{
    std::string_view id{};
    std::string_view displayName{};
    uint32_t acceptedMaterialCategories = 0;
    uint32_t minimumCount = 0;
};

struct SpeciesTankmateRequirements
{
    uint32_t minimumGroupSize = 1;
    std::span<const std::string_view> requiredSpeciesIds{};
    std::span<const std::string_view> incompatibleSpeciesIds{};
};

struct SpeciesHabitatRequirements
{
    std::span<const SpeciesDecorRequirement> decor{};
    SpeciesTankmateRequirements tankmates{};
};

struct SpeciesDefinition
{
    std::string_view id{};
    std::string_view displayName{};
    std::string_view description{};
    SpeciesRoleMask roles = speciesRoleMask(SpeciesRole::None);
    SpeciesWaterRequirements water{};
    SpeciesHabitatRequirements habitat{};
};

enum class SpeciesRegistryIssueCode
{
    EmptyId,
    EmptyDisplayName,
    DuplicateId,
    InvalidRoleMask,
    InvalidTemperatureBand,
    InvalidOxygenBand,
    InvalidFlowBand,
    InvalidCleanlinessBand,
    InvalidDecorRequirement,
    DuplicateDecorRequirement,
    InvalidMinimumGroupSize,
    EmptyTankmateReference,
    SelfTankmateReference,
    UnknownTankmateReference,
};

struct SpeciesRegistryIssue
{
    SpeciesRegistryIssueCode code = SpeciesRegistryIssueCode::EmptyId;
    size_t definitionIndex = 0;
    std::string_view speciesId{};
    std::string_view detailId{};
};

std::span<const SpeciesDefinition> registeredSpecies();
const SpeciesDefinition* findSpecies(std::string_view speciesId);

bool speciesBandContains(const SpeciesParameterBand& band, float value);
std::string_view speciesRoleDisplayName(SpeciesRole role);

std::vector<SpeciesRegistryIssue> validateSpeciesRegistry(
    std::span<const SpeciesDefinition> definitions);

} // namespace engine::game
