#pragma once

#include <optional>

#include <glm/glm.hpp>

#include "engine/scene/TankGlassBuilder.h"

struct SceneConfig;

struct NaturePondGlassDomePlacement
{
    TankGlassBuilder::FishbowlGlassSpec shellSpec{};
    glm::mat4 model{1.0f};
    glm::vec3 center{0.0f};
    float rimHeight = 0.0f;
    float apexHeight = 0.0f;
    glm::vec2 openingRadii{0.0f};
};

// returns the deterministic, renderer-independent glass-dome contract for the
// authored nature-pond scene. the shell is an inverted fishbowl: its broad open
// rim rests on the bank while the original bowl base becomes the top foot.
std::optional<NaturePondGlassDomePlacement> naturePondGlassDomePlacement(
    const SceneConfig& sceneConfig);
