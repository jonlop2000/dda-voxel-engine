#pragma once

#include <glm/vec3.hpp>

struct GlassMaterialPreset
{
    const char* name = "";
    glm::vec3 tint{0.8f, 0.95f, 0.9f};
    glm::vec3 reflection{0.09f, 0.29f, 0.88f};
    float absorption = 0.6f;
    float thicknessScale = 1.0f;
    float refractStrength = 0.01f;
    float ior = 1.52f;
    float bubbleScale = 12.0f;
    float bubbleIntensity = 0.0f;
    float bubbleThicknessGate = 0.1f;
    float bubbleChromaticSplit = 0.0f;
    float iridescentStrength = 0.0f;
    float iridescentFilmThickness = 1.5f;
    float iridescentFrequency = 2.0f;
    bool voxelGlassRefractEnabled = false;
};

inline const GlassMaterialPreset kRoundFishbowlGlassPreset{
    "Round Fishbowl",
    glm::vec3(240.0f / 255.0f, 251.0f / 255.0f, 1.0f),
    glm::vec3(0.50f, 0.68f, 0.82f),
    0.810f,
    0.100f,
    0.000f,
    1.50f,
    18.0f,
    0.05f,
    0.18f,
    0.010f,
    0.62f,
    3.1f,
    1.6f,
    false,
};

template <typename Target>
void applyGlassMaterialPreset(Target& target, const GlassMaterialPreset& preset)
{
    target.glassTint_ = preset.tint;
    target.glassReflection_ = preset.reflection;
    target.glassAbsorption_ = preset.absorption;
    target.glassThicknessScale_ = preset.thicknessScale;
    target.glassRefract_ = preset.refractStrength;
    target.glassIor_ = preset.ior;
    target.glassBubbleScale_ = preset.bubbleScale;
    target.glassBubbleIntensity_ = preset.bubbleIntensity;
    target.glassBubbleThicknessGate_ = preset.bubbleThicknessGate;
    target.glassBubbleChromaticSplit_ = preset.bubbleChromaticSplit;
    target.glassIridescentStrength_ = preset.iridescentStrength;
    target.glassIridescentFilmThickness_ = preset.iridescentFilmThickness;
    target.glassIridescentFrequency_ = preset.iridescentFrequency;
    target.voxelGlassRefractEnabled_ = preset.voxelGlassRefractEnabled;
}
