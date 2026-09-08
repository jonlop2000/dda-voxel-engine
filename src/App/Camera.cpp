#include "App/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 Camera::viewMatrix() const
{
    const float cosPitch = std::cos(pitch);
    const float sinPitch = std::sin(pitch);
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);

    glm::vec3 forward{};
    forward.x = cosPitch * sinYaw;
    forward.y = sinPitch;
    forward.z = -cosPitch * cosYaw;

    const glm::vec3 target = position + glm::normalize(forward);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::projMatrix(float aspect) const
{
    const float safeAspect = std::max(aspect, 0.001f);
    glm::mat4 proj(1.0f);
    if (projectionMode == CameraProjectionMode::Orthographic)
    {
        const float halfHeight = std::max(orthoHeight, 0.1f) * 0.5f;
        const float halfWidth = halfHeight * safeAspect;
        proj = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearZ, farZ);
    }
    else
    {
        proj = glm::perspective(fovY, safeAspect, nearZ, farZ);
    }
    proj[1][1] *= -1.0f;
    return proj;
}
