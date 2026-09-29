#include "engine/scene/PhysicsSandboxVisuals.h"

#include <cmath>
#include <utility>
#include "engine/voxel/VoxelBuilder.h"

namespace engine::physics {

BallVisualTransform makeBallVisualTransform(const Ball& ball)
{
    const glm::quat rotation = glm::normalize(glm::quat(ball.orientation));
    const glm::vec3 scale{static_cast<float>(ball.radius / ballVoxelRadius)};
    const glm::vec3 localCenter{ballVoxelSize * 0.5f};
    // compensate for the voxel volume's corner origin after rotation.
    const glm::vec3 origin = glm::vec3(ball.position) - rotation * (localCenter * scale);
    return {origin, rotation, scale};
}

std::vector<uint8_t> buildMarkedBallVoxels(uint8_t material)
{
    engine::VoxelBuilder builder(glm::ivec3{ballVoxelSize});
    const glm::vec3 center{ballVoxelSize * 0.5f};
    builder.fillSphere(center, ballVoxelRadius, material);
    const glm::vec3 markerDirection = glm::normalize(glm::vec3{1.0f, 1.0f, 1.0f});
    for (int z = 0; z < ballVoxelSize; ++z)
        for (int y = 0; y < ballVoxelSize; ++y)
            for (int x = 0; x < ballVoxelSize; ++x)
            {
                if (builder.getVoxel(x, y, z) == 0) continue;
                const glm::vec3 offset = glm::vec3{x, y, z} + 0.5f - center;
                uint8_t color = material;
                if (std::abs(offset.y) <= 1.5f) color = ballStripeMaterial;
                if (glm::dot(offset, markerDirection) > 7.5f) color = ballMarkerMaterial;
                builder.setVoxel(x, y, z, color);
            }
    return std::move(builder.data());
}

} // namespace engine::physics
