#pragma once

#include <glm/glm.hpp>

namespace engine::game
{

struct FishHabitat
{
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    glm::vec3 schoolCenter{0.0f};

    bool isValid() const
    {
        const glm::bvec3 positiveExtent = glm::greaterThan(boundsMax, boundsMin);
        const glm::bvec3 centerAboveMin = glm::greaterThanEqual(schoolCenter, boundsMin);
        const glm::bvec3 centerBelowMax = glm::lessThanEqual(schoolCenter, boundsMax);
        return glm::all(positiveExtent) && glm::all(centerAboveMin) && glm::all(centerBelowMax);
    }
};

} // namespace engine::game
