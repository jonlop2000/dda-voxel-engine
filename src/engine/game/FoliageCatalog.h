#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/game/Placeables.h"

struct FoliageMotionProfile
{
    float amplitude = 0.04f;
    float speed = 0.5f;
    float twistScale = 0.28f;
};

struct FoliagePrototype
{
    PlaceablePrototype placeable{};
    FoliageMotionProfile motion{};
};

namespace FoliageCatalog
{
const FoliagePrototype* findPrototype(std::string_view slug, uint32_t version);
std::span<const FoliagePrototype> prototypes();
const std::vector<PlaceableInstance>& defaultAquariumHeroFoliageInstances();
std::vector<PlaceableInstance> aquariumHeroFoliageInstances(
    const std::vector<PlaceableInstance>* scenePlaceables, bool useDefaultPlaceables);
std::string heroFoliageVolumeName(size_t index);
} // namespace FoliageCatalog
