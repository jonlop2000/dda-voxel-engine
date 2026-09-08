#ifndef VOXEL_HIERARCHY_GLSL
#define VOXEL_HIERARCHY_GLSL

// conservative material classes stored in the R8_UINT voxel hierarchy.
// parent levels bitwise-OR their children, so a zero mask proves that the
// entire brick is empty while nonzero bits preserve which ray/material
// classes may contribute below it.
const uint VOXEL_HIERARCHY_OPAQUE = 0x01u;
const uint VOXEL_HIERARCHY_GLASS = 0x02u;
const uint VOXEL_HIERARCHY_WATER = 0x04u;
const uint VOXEL_HIERARCHY_ANY =
    VOXEL_HIERARCHY_OPAQUE | VOXEL_HIERARCHY_GLASS | VOXEL_HIERARCHY_WATER;
// mip 1 stores the material mask in the low bits and, for empty leaves, the
// largest proven-empty ancestor mip in the remaining five bits.
const uint VOXEL_HIERARCHY_JUMP_MIP_SHIFT = 3u;

uint voxelHierarchyClassFromShadingModel(uint shadingModel)
{
    if (shadingModel == SHADING_GLASS)
    {
        return VOXEL_HIERARCHY_GLASS;
    }
    if (shadingModel == SHADING_WATER)
    {
        return VOXEL_HIERARCHY_WATER;
    }
    return VOXEL_HIERARCHY_OPAQUE;
}

#endif // VOXEL_HIERARCHY_GLSL
