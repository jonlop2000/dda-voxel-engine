#include "engine/game/FishHabitatResolver.h"

#include <algorithm>

#include "engine/scene/AquariumScene.h"
#include "engine/scene/AquariumSceneVariant.h"
#include "engine/scene/NaturePondScene.h"
#include "engine/scene/NaturePondSunroofProbe.h"
#include "engine/scene/SceneConfig.h"

namespace
{
bool isSunroofAquarium(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest &&
           engine::scene::isAquariumSunroofSceneName(sceneConfig.name);
}

bool isFishbowlAquarium(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest &&
           (sceneConfig.name == "fishbowl_perf_probe" || sceneConfig.name == "fishbowl");
}
} // namespace

namespace engine::game
{

bool sceneSupportsFishHabitat(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest || isNaturePondScene(sceneConfig);
}

std::optional<FishHabitat> resolveFishHabitat(const SceneConfig& sceneConfig)
{
    if (isNaturePondScene(sceneConfig))
    {
        if (isNaturePondSunroofProbeName(sceneConfig.name))
        {
            return naturePondSunroofProbeFishHabitat();
        }
        return NaturePondScene::fishHabitat();
    }
    if (!sceneConfig.loadAquariumTest)
    {
        return std::nullopt;
    }

    glm::vec3 waterMin(0.0f);
    glm::vec3 waterMax(0.0f);
    if (!AquariumScene::getWaterBounds(waterMin, waterMax))
    {
        const auto& volumeInfos = AquariumScene::getVolumeInfos();
        if (volumeInfos.size() <= AquariumScene::VOLUME_TANK_COMPOSITE)
        {
            return std::nullopt;
        }

        const AquariumVolumeInfo& tankInfo = volumeInfos[AquariumScene::VOLUME_TANK_COMPOSITE];
        const glm::ivec3 dims = tankInfo.dimensions;
        if (dims.x <= 4 || dims.y <= 4 || dims.z <= 4)
        {
            return std::nullopt;
        }

        constexpr int kWall = 2;
        const int xMin = kWall;
        const int yMin = kWall;
        const int zMin = kWall;
        const int xMax = dims.x - 1 - kWall;
        const int zMax = dims.z - 1 - kWall;
        const int topY = isSunroofAquarium(sceneConfig)
                             ? std::max(yMin, dims.y - kWall - 1)
                             : std::max(yMin, dims.y - 3);
        if (xMax < xMin || zMax < zMin || topY < yMin)
        {
            return std::nullopt;
        }

        waterMin = tankInfo.worldPosition +
                   glm::vec3(static_cast<float>(xMin), static_cast<float>(yMin),
                             static_cast<float>(zMin));
        waterMax = tankInfo.worldPosition +
                   glm::vec3(static_cast<float>(xMax + 1), static_cast<float>(topY + 1),
                             static_cast<float>(zMax + 1));
    }

    FishHabitat habitat{};
    habitat.boundsMin = waterMin;
    habitat.boundsMax = waterMax;
    habitat.schoolCenter = (waterMin + waterMax) * 0.5f;
    const glm::vec3 halfExtent = (waterMax - waterMin) * 0.5f;
    if (isFishbowlAquarium(sceneConfig))
    {
        habitat.schoolCenter.y += halfExtent.y * 0.04f;
    }
    else if (isSunroofAquarium(sceneConfig))
    {
        habitat.schoolCenter.y += halfExtent.y * 0.18f;
    }
    return habitat.isValid() ? std::optional<FishHabitat>(habitat) : std::nullopt;
}

} // namespace engine::game
