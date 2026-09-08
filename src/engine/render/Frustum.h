#pragma once

#include <array>

#include <glm/glm.hpp>

struct Frustum
{
    std::array<glm::vec4, 6> planes{};
};

Frustum makeFrustum(const glm::mat4& viewProj);
bool aabbInFrustum(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max);
