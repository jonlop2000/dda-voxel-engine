#include "engine/scene/NaturePondGlassDome.h"

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/scene/SceneConfig.h"

std::optional<NaturePondGlassDomePlacement> naturePondGlassDomePlacement(
    const SceneConfig& sceneConfig)
{
    if (sceneConfig.name != "nature_pond_probe" || !sceneConfig.enableGlass)
    {
        return std::nullopt;
    }

    NaturePondGlassDomePlacement placement{};
    placement.center = glm::vec3(0.0f);
    placement.rimHeight = 4.05f;

    auto& spec = placement.shellSpec;
    spec.volumeDims = glm::ivec3(21, 13, 21);
    spec.volumeWorldPos =
        glm::vec3(placement.center.x - 10.5f, 0.0f, placement.center.z - 10.5f);
    spec.paneThickness = 0.10f;
    spec.baseRadiusFactor = 0.06f;
    spec.rimRadiusGrowthFactor = 0.257f;
    spec.bulbRadiusFactor = 0.0f;
    spec.baseFootRadiusScale = 0.78f;
    spec.baseFootHeight = 0.38f;
    spec.profileShape = TankGlassBuilder::FishbowlProfileShape::RoundedDome;
    spec.radialSegments = 96;
    spec.verticalBands = 24;
    spec.includeBase = true;
    spec.includeRim = true;
    spec.includeBaseFoot = true;

    constexpr float kOriginalBottomY = 3.0f;
    constexpr float kHeightScale = 0.80f;
    constexpr float kDepthScale = 0.90f;
    const float originalTopY = static_cast<float>(spec.volumeDims.y) - 4.0f;
    placement.model =
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(placement.center.x, placement.rimHeight, placement.center.z)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, kHeightScale, kDepthScale)) *
        glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(-placement.center.x, -originalTopY, -placement.center.z));
    placement.apexHeight =
        placement.rimHeight +
        kHeightScale * (originalTopY - (kOriginalBottomY - spec.baseFootHeight));
    const float openingRadius =
        static_cast<float>(std::min(spec.volumeDims.x, spec.volumeDims.z)) *
        (spec.baseRadiusFactor + spec.rimRadiusGrowthFactor);
    placement.openingRadii = glm::vec2(openingRadius, openingRadius * kDepthScale);
    return placement;
}
