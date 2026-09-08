#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/vec3.hpp>

namespace engine::game
{

// stable handle to a procedural fish (or the axolotl) for selection/focus ui.
struct FishHandle
{
    uint64_t id = 0;           // stable for the fish's lifetime; 0 = invalid
    int index = -1;            // index into the live fish array this frame (debug aid)
    size_t paletteIndex = 0;
    float scale = 1.0f;
    bool isAxolotl = false;
    glm::vec3 worldPos{0.0f};  // body-volume aabb center; live after updateAnimatedObjects()
};

// fish-focus camera framing and handheld-shake tunables (ui-editable, like the
// other settings buckets). defaults baked from manual qa on 2026-06-09
// (fishbowl_perf_probe).
struct FishFocusSettings
{
    float fishFocusOrbitDistance_ = 12.3f;
    float fishFocusHeightOffset_ = 2.5f;
    float fishFocusPositionDamping_ = 4.8f;
    float fishFocusRotationDamping_ = 6.3f;

    bool fishShakeEnabled_ = true;
    float fishShakePositionAmplitude_ = 0.05f;  // world units
    float fishShakeRotationAmplitude_ = 0.005f; // radians (~0.29 deg)
    float fishShakeFrequency_ = 0.6f;           // hz of the lowest noise layer
    float fishShakeSettleDecay_ = 0.8f;         // 1/s decay toward the idle floor
};

} // namespace engine::game
