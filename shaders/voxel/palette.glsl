#pragma once

#ifndef VOXEL_SET
#define VOXEL_SET 2
#endif

#ifndef VOXEL_BINDING_PALETTE
#define VOXEL_BINDING_PALETTE 0
#endif

// keep std430 alignment stable: use vec4 blocks.
struct PaletteEntry
{
    vec4 baseColor_alpha; // rgb + alpha
    vec4 pbr0;            // x=metallic, y=roughness, z=emissive, w=shadingModel
    vec4 extra;           // x=ior, y=transmission, z=absorption, w=materialCategory
};

const uint SHADING_OPAQUE = 0u;
const uint SHADING_GLASS = 1u;
const uint SHADING_WATER = 2u;

const uint MATERIAL_GENERIC = 0u;
const uint MATERIAL_FRAME = 1u;
const uint MATERIAL_GRAVEL = 2u;
const uint MATERIAL_PLANT = 3u;
const uint MATERIAL_FISH = 4u;
const uint MATERIAL_STONE = 5u;
const uint MATERIAL_WOOD = 6u;
const uint MATERIAL_CORAL = 7u;
const uint MATERIAL_GLASS = 8u;
const uint MATERIAL_WATER = 9u;
const uint MATERIAL_ROOM = 10u;

layout(std430, set = VOXEL_SET, binding = VOXEL_BINDING_PALETTE) readonly buffer PaletteBuffer
{
    PaletteEntry entries[];
} g_Palette;

PaletteEntry GetPaletteEntry(uint paletteId, uint voxelId)
{
    return g_Palette.entries[paletteId * 256u + voxelId];
}
