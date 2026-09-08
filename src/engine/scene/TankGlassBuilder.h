#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Resources/Mesh.h"

namespace TankGlassBuilder
{

enum class FishbowlProfileShape
{
    Classic,
    RoundedDome,
};

struct RectangularTankSpec
{
    glm::ivec3 volumeDims{0, 0, 0};
    glm::vec3 volumeWorldPos{0.0f, 0.0f, 0.0f};
    int wallThickness = 2;
    float paneThickness = 0.15f;
    bool includeBottom = false;
};

struct FishbowlGlassSpec
{
    glm::ivec3 volumeDims{0, 0, 0};
    glm::vec3 volumeWorldPos{0.0f, 0.0f, 0.0f};
    float paneThickness = 0.12f;
    float baseRadiusFactor = 0.150f;
    float rimRadiusGrowthFactor = 0.160f;
    float bulbRadiusFactor = 0.160f;
    float baseFootRadiusScale = 0.78f;
    float baseFootHeight = 0.36f;
    FishbowlProfileShape profileShape = FishbowlProfileShape::Classic;
    int radialSegments = 32;
    int verticalBands = 11;
    bool includeBase = true;
    bool includeRim = true;
    bool includeBaseFoot = false;
};

// builds model matrices for axis-aligned pane boxes that fit the aquarium wall apertures.
std::vector<glm::mat4> buildRectangularTankGlassPaneModels(const RectangularTankSpec& spec);
std::vector<glm::mat4> buildFishbowlGlassPaneModels(const FishbowlGlassSpec& spec);
std::vector<Vertex> buildFishbowlGlassShellVertices(const FishbowlGlassSpec& spec);

} // namespace TankGlassBuilder
