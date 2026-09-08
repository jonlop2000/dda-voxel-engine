#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "engine/render/RenderObject.h"

struct SceneConfig;

void rebuildGlassObjectsForScene(std::vector<RenderObject>& out,
                                 MeshGpu* cubeMesh,
                                 MeshGpu* fishbowlMesh,
                                 const SceneConfig& sceneConfig,
                                 const glm::vec3& sunDirection);
