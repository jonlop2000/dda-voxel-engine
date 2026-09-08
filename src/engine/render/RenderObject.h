#pragma once

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>

#include "Assets/Material.h"
#include "Resources/Mesh.h"

struct RenderObject
{
    glm::mat4 model{1.0f};
    MeshGpu* mesh = nullptr;
    Material* material = nullptr;
};
