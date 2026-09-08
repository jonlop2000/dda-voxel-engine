#include "engine/scene/LightingParityProbe.h"

#include "Core/Logger.h"
#include "engine/scene/NaturePondGenerator.h"
#include "engine/scene/SceneConfig.h"

namespace engine::scene
{
namespace
{

std::array<engine::render::EditableAreaLight, 1> makeAreaLights()
{
    engine::render::EditableAreaLight fire{};
    fire.shape = LightShape::Sphere;
    fire.castsShadows = true;
    fire.position = NaturePondLightingParity::FireLightPosition;
    fire.influenceRadius = 6.0f;
    fire.color = glm::vec3(1.0f, 0.24f, 0.055f);
    fire.intensity = 8.0f;
    fire.sourceRadius = 0.22f;
    return {fire};
}

} // namespace

const std::array<engine::render::EditableAreaLight, 1>&
lightingParityProbeAreaLights()
{
    static const std::array<engine::render::EditableAreaLight, 1> kLights =
        makeAreaLights();
    return kLights;
}

bool applyLightingParityProbeAreaLights(
    const SceneConfig& sceneConfig, engine::render::LightingSettings& lighting)
{
    if (!isNaturePondLightingParityProbeName(sceneConfig.name))
    {
        return false;
    }

    const auto& authoredLights = lightingParityProbeAreaLights();
    lighting.areaLights_.assign(authoredLights.begin(), authoredLights.end());
    lighting.lightCount_ = static_cast<int>(authoredLights.size());
    logInfo(
        "LightingParityProbe",
        makeLogMessage("Applied authored fire light for scene '", sceneConfig.name,
                       "': enabled=", sceneConfig.enablePointLights ? 1 : 0,
                       " count=", authoredLights.size(), " position=(",
                       authoredLights.front().position.x, ",",
                       authoredLights.front().position.y, ",",
                       authoredLights.front().position.z, ")."));
    return true;
}

} // namespace engine::scene
