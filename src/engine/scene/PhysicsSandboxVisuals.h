#pragma once

#include <cstdint>
#include <vector>
#include "engine/physics/BallPhysics.h"

namespace engine::physics {

inline constexpr int ballVoxelSize = 24;
inline constexpr float ballVoxelRadius = 10.0f;
inline constexpr uint8_t ballStripeMaterial = 17;
inline constexpr uint8_t ballMarkerMaterial = 18;

struct BallVisualTransform
{
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

// map a valid ball to a voxel transform that rotates about its center.
BallVisualTransform makeBallVisualTransform(const Ball& ball);

// recolor occupied voxels with a stripe and an asymmetric surface marker.
std::vector<uint8_t> buildMarkedBallVoxels(uint8_t material);

} // namespace engine::physics
