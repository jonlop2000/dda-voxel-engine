#include "engine/game/SpeciesRegistry.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void testBuiltInRegistryIsValidAndDeterministic()
{
    const auto species = engine::game::registeredSpecies();
    require(species.size() == 3,
            "The registry should add one collection-role species without changing acquisition");
    require(species[0].id == engine::game::kStarterFishSpeciesId &&
                species[1].id == engine::game::kAxolotlSpeciesId &&
                species[2].id == engine::game::kRamshornSnailSpeciesId,
            "Species iteration order must stay deterministic for future Codex views");
    require(engine::game::validateSpeciesRegistry(species).empty(),
            "The built-in registry must satisfy its complete data contract");
}

void testLookupIsExactAndStable()
{
    const auto* starter =
        engine::game::findSpecies(engine::game::kStarterFishSpeciesId);
    const auto* axolotl = engine::game::findSpecies(engine::game::kAxolotlSpeciesId);
    const auto* snail =
        engine::game::findSpecies(engine::game::kRamshornSnailSpeciesId);

    require(starter != nullptr && starter->displayName == "Starter Fish",
            "The starter fish must resolve to stable player-facing metadata");
    require(axolotl != nullptr && axolotl->displayName == "Axolotl",
            "The axolotl must resolve to stable player-facing metadata");
    require(snail != nullptr && snail->displayName == "Ramshorn Snail" &&
                engine::game::hasSpeciesRole(
                    snail->roles, engine::game::SpeciesRole::CleanupCrew) &&
                engine::game::hasSpeciesRole(snail->roles,
                                             engine::game::SpeciesRole::Grazer),
            "The first cleanup species must resolve to its functional roles");
    require(engine::game::findSpecies("AXOLOTL") == nullptr &&
                engine::game::findSpecies("unknown") == nullptr,
            "Species IDs are canonical, exact, and must not guess unknown content");
}

void testWaterAndDecorRequirementsAreDescriptive()
{
    const auto* starter =
        engine::game::findSpecies(engine::game::kStarterFishSpeciesId);
    const auto* axolotl = engine::game::findSpecies(engine::game::kAxolotlSpeciesId);
    const auto* snail =
        engine::game::findSpecies(engine::game::kRamshornSnailSpeciesId);
    require(starter != nullptr && axolotl != nullptr && snail != nullptr,
            "Water-band checks require all built-in definitions");

    require(engine::game::speciesBandContains(starter->water.temperatureC, 24.0f) &&
                !engine::game::speciesBandContains(axolotl->water.temperatureC,
                                                   24.0f),
            "Species must be able to describe distinct advisory water preferences");
    require(engine::game::speciesBandContains(starter->water.oxygen, 0.55f) &&
                engine::game::speciesBandContains(starter->water.oxygen, 1.0f),
            "Band boundaries should be inclusive");
    require(!engine::game::speciesBandContains(
                starter->water.oxygen,
                std::numeric_limits<float>::quiet_NaN()),
            "Non-finite habitat readings must never match a valid band");

    require(starter->habitat.decor.size() == 1 &&
                starter->habitat.decor[0].minimumCount == 1 &&
                (starter->habitat.decor[0].acceptedMaterialCategories &
                 PlaceableMaterialCategory::Rock) != 0,
            "Decor requirements should reuse the placeable material vocabulary");
    require(engine::game::hasSpeciesRole(starter->roles,
                                         engine::game::SpeciesRole::Showpiece) &&
                engine::game::speciesRoleDisplayName(
                    engine::game::SpeciesRole::CleanupCrew) == "Cleanup crew",
            "Functional roles must be typed and ready for later collection behavior");
    require(snail->habitat.decor.size() == 1 &&
                snail->habitat.decor[0].id == "grazing_surface" &&
                snail->water.flow.maximum == 0.60f,
            "Cleanup species should declare descriptive grazing and water preferences");

    const engine::game::SpeciesRoleMask ecosystemRoles =
        engine::game::SpeciesRole::CleanupCrew | engine::game::SpeciesRole::Grazer |
        engine::game::SpeciesRole::Showpiece;
    require(engine::game::hasSpeciesRole(ecosystemRoles,
                                         engine::game::SpeciesRole::CleanupCrew) &&
                engine::game::hasSpeciesRole(ecosystemRoles,
                                             engine::game::SpeciesRole::Grazer),
            "A species must be able to carry several functional roles");
}

void testValidationRejectsAmbiguousDefinitions()
{
    std::array definitions = {
        engine::game::registeredSpecies()[0],
        engine::game::registeredSpecies()[1],
    };
    definitions[1].id = definitions[0].id;
    definitions[1].water.flow = {0.8f, 0.2f};
    definitions[1].roles = engine::game::speciesRoleMask(
        engine::game::SpeciesRole::None);
    definitions[1].habitat.tankmates.minimumGroupSize = 0;
    const std::array<std::string_view, 1> missingTankmate = {"missing_species"};
    definitions[1].habitat.tankmates.requiredSpeciesIds = missingTankmate;

    const auto issues = engine::game::validateSpeciesRegistry(definitions);
    const auto hasIssue = [&](engine::game::SpeciesRegistryIssueCode code) {
        for (const auto& issue : issues)
        {
            if (issue.code == code)
            {
                return true;
            }
        }
        return false;
    };

    require(hasIssue(engine::game::SpeciesRegistryIssueCode::DuplicateId) &&
                hasIssue(engine::game::SpeciesRegistryIssueCode::InvalidFlowBand) &&
                hasIssue(engine::game::SpeciesRegistryIssueCode::InvalidRoleMask) &&
                hasIssue(
                    engine::game::SpeciesRegistryIssueCode::InvalidMinimumGroupSize) &&
                hasIssue(
                    engine::game::SpeciesRegistryIssueCode::UnknownTankmateReference),
            "Registry validation must reject duplicate, malformed, and dangling data");
}

} // namespace

int main()
{
    try
    {
        testBuiltInRegistryIsValidAndDeterministic();
        testLookupIsExactAndStable();
        testWaterAndDecorRequirementsAreDescriptive();
        testValidationRejectsAmbiguousDefinitions();
        std::cout << "Species registry tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Species registry test failure: " << e.what() << "\n";
        return 1;
    }
}
