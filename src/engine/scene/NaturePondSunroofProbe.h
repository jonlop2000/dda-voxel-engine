#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "engine/scene/NaturePondGenerator.h"

struct SceneConfig;

struct NaturePondSunroofProbeLayout
{
    glm::vec2 centerXZ{0.0f};
    glm::vec2 islandRadii{1.0f};
    float islandSurfaceHeight = 0.0f;
    float pavilionInnerRadius = 0.0f;
    float pavilionOuterRadius = 0.0f;
    float pavilionColumnRadius = 0.0f;
    float pavilionColumnShaftRadius = 0.0f;
    float pavilionColumnBottom = 0.0f;
    float pavilionColumnTop = 0.0f;
    float pavilionRoofBottom = 0.0f;
    float pavilionRoofTop = 0.0f;
    uint32_t columnCount = 0;
};

struct NaturePondSunroofGlassLayout
{
    uint32_t entranceBayIndex = 0;
    float paneWidth = 0.0f;
    float paneBottom = 0.0f;
    float paneTop = 0.0f;
    float paneThickness = 0.0f;
};

[[nodiscard]] const NaturePondSunroofProbeLayout&
naturePondSunroofProbeLayout();

[[nodiscard]] const NaturePondSunroofGlassLayout&
naturePondSunroofGlassLayout();

// returns thin cube-mesh transforms for the refractive glass pass. eleven
// tangent panes connect adjacent pavilion columns above the pond waterline;
// the camera-facing twelfth bay remains open as the entrance.
[[nodiscard]] std::vector<glm::mat4>
naturePondSunroofGlassPaneModels(const SceneConfig& sceneConfig);

// keeps procedural fish in the west water lane so their analytic paths do not
// cross the solid central island.
[[nodiscard]] const engine::game::FishHabitat&
naturePondSunroofProbeFishHabitat();

// applies the scene-specific composition without changing the frozen six-volume
// nature pond generator baseline. the decorator moves the existing hero tree to
// the raised island, clears the island footprint from legacy ecology, gives the
// canopy a lavender flowering treatment, and appends one coarse opaque volume
// containing the island plus circular pavilion.
[[nodiscard]] bool decorateNaturePondSunroofProbe(NaturePondBuild& build);
