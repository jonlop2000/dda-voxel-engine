#pragma once

#include <cmath>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>

enum class CameraProjectionMode
{
    Perspective,
    Orthographic,
};

struct Camera
{
    glm::vec3 position{0.0f, 1.5f, 5.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;
    float fovY = glm::radians(60.0f);
    float orthoHeight = 40.0f;
    float nearZ = 0.1f;
    float farZ = 500.0f;

    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;
};

inline glm::vec3 cameraForward(const Camera& camera)
{
    const float cosPitch = std::cos(camera.pitch);
    const float sinPitch = std::sin(camera.pitch);
    const float cosYaw = std::cos(camera.yaw);
    const float sinYaw = std::sin(camera.yaw);
    glm::vec3 forward{};
    forward.x = cosPitch * sinYaw;
    forward.y = sinPitch;
    forward.z = -cosPitch * cosYaw;
    return glm::normalize(forward);
}
