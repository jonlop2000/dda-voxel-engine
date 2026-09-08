#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "engine/game/FoliageArchetype.h"

namespace engine::scene
{

struct SunroofLivingFoliagePalette
{
    uint8_t stemBase = 0;
    uint8_t stemCount = 0;
    uint8_t secondaryBase = 0;
    uint8_t secondaryCount = 0;
    uint8_t flowerBase = 0;
    uint8_t flowerCount = 0;
};

struct SunroofBubbleMote
{
    glm::vec3 center{0.0f};
    float scale = 0.0f;
};

struct SunroofBubbleControls
{
    float amount = 1.0f;
    float size = 1.0f;
    float riseSpeed = 1.0f;
    float drift = 1.0f;
};

// a deliberately small animated overlay for the otherwise static, dense
// sunroof garden. the shared foliage pass expands these semantic anchors into
// root-pinned, swaying flowers/reeds without rebuilding the fine voxel volume.
[[nodiscard]] std::vector<engine::game::FoliageBladeInstance>
buildSunroofLivingFoliage(const glm::ivec3& dims, const glm::vec3& position,
                          const glm::vec3& scale,
                          const SunroofLivingFoliagePalette& palette,
                          uint32_t seed = 0x4c495645u);

// three bounded bubble streams. live amount selects a deterministic subset of
// the fixed 27-mote maximum; the ceiling and topology remain independent of
// camera coverage while the phase wraps through the water column.
[[nodiscard]] std::vector<SunroofBubbleMote> buildSunroofBubbleMotes(
    const glm::vec3& boundsMin, const glm::vec3& boundsMax,
    float surfaceHeight, float timeSeconds,
    const SunroofBubbleControls& controls = {},
    uint32_t seed = 0x4255424cu);

} // namespace engine::scene
