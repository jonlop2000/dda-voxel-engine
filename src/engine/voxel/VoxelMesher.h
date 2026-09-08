#pragma once

#include <array>
#include <vector>

#include "Resources/Mesh.h"
#include "engine/voxel/Chunk.h"
#include "engine/voxel/VoxelTypes.h"

void meshChunk(const Chunk& chunk, std::array<std::vector<Vertex>, kBlockTypeCount>& out);
