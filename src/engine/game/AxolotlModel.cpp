#include "engine/game/AxolotlModel.h"

#include <algorithm>
#include <cmath>

#include "engine/voxel/VoxelBuilder.h"

namespace engine::game
{

namespace
{

// hand-authored per-column body profile (x=0 rear/tail joint -> x=15 nose).
// the head (x 12-15) flares wide in z and stays blunt all the way to the nose
// so the face is a broad flat front; the rear stays slim for the tail joint.
constexpr float kBodyYRadius[16] = {1.3f, 1.7f, 2.0f, 2.2f, 2.3f, 2.3f, 2.3f, 2.2f,
                                    2.2f, 2.1f, 2.0f, 1.9f, 1.9f, 1.8f, 1.8f, 1.7f};
constexpr float kBodyZRadius[16] = {1.2f, 1.6f, 2.0f, 2.3f, 2.4f, 2.4f, 2.4f, 2.3f,
                                    2.2f, 2.1f, 2.2f, 2.7f, 3.1f, 3.1f, 3.0f, 2.9f};
constexpr float kBodyCenterY[16] = {4.6f, 4.5f, 4.4f, 4.4f, 4.4f, 4.4f, 4.4f, 4.4f,
                                    4.4f, 4.4f, 4.3f, 4.2f, 4.2f, 4.2f, 4.2f, 4.2f};

constexpr float kBodyCenterZ = 5.0f;

// trunk keeps a soft rounded-square section; the head columns use a boxier
// exponent so the face reads as a blunt, wide axolotl mug instead of a snout.
float sectionExponent(int x)
{
    return x >= 12 ? 2.4f : 1.8f;
}

bool insideBodySection(int x, int y, int z)
{
    const float dy = (static_cast<float>(y) + 0.5f - kBodyCenterY[x]) / kBodyYRadius[x];
    const float dz = (static_cast<float>(z) + 0.5f - kBodyCenterZ) / kBodyZRadius[x];
    const float exponent = sectionExponent(x);
    return std::pow(std::abs(dy), exponent) + std::pow(std::abs(dz), exponent) <= 1.0f;
}

// deterministic, z-mirrored hash so freckles and dither stay symmetric.
float symmetricHash01(int x, int zMirrored)
{
    const float v = std::sin(static_cast<float>(x) * 12.9898f +
                             static_cast<float>(zMirrored) * 78.233f) *
                    43758.5453f;
    return v - std::floor(v);
}

} // namespace

std::vector<uint8_t> buildAxolotlBodyVoxelData(const AxolotlPaletteBandIds& palette)
{
    engine::VoxelBuilder builder(kAxolotlBodyVolumeDims);

    for (int x = 0; x < kAxolotlBodyVolumeDims.x; ++x)
    {
        for (int y = 0; y < kAxolotlBodyVolumeDims.y; ++y)
        {
            for (int z = 0; z < kAxolotlBodyVolumeDims.z; ++z)
            {
                if (!insideBodySection(x, y, z))
                {
                    continue;
                }

                const float dy =
                    (static_cast<float>(y) + 0.5f - kBodyCenterY[x]) / kBodyYRadius[x];
                const int zm = std::min(z, kAxolotlBodyVolumeDims.z - 1 - z);
                // the face zone (x>=14) stays clean flat pink so the eyes and
                // smile pop; freckles and dither only texture the body behind it.
                const bool faceZone = x >= 14;
                const bool checker = !faceZone && ((x + zm) & 1) != 0;

                uint8_t id = palette.body[2]; // main pink
                if (dy > 0.42f)
                {
                    if (!faceZone && symmetricHash01(x, zm) < 0.22f)
                    {
                        id = palette.detail[0]; // rosy freckle on the back/head top
                    }
                    else if (dy < 0.58f && checker)
                    {
                        id = palette.body[2]; // dithered highlight edge
                    }
                    else
                    {
                        id = palette.body[3]; // top highlight
                    }
                }
                else if (dy < -0.62f)
                {
                    id = palette.detail[2]; // cream belly
                }
                else if (dy < -0.34f)
                {
                    id = palette.body[1]; // lower flank
                }
                else if (dy < -0.16f)
                {
                    id = checker ? palette.body[1] : palette.body[2]; // dithered flank edge
                }
                builder.setVoxel(x, y, z, id);
            }
        }
    }

    // dorsal fin ridge along the spine (the membrane crest that runs into the
    // tail fin). sits one voxel above the back, following its curve.
    for (int x = 2; x <= 8; ++x)
    {
        int topY = -1;
        for (int y = kAxolotlBodyVolumeDims.y - 1; y >= 0; --y)
        {
            if (insideBodySection(x, y, 4))
            {
                topY = y;
                break;
            }
        }
        if (topY >= 0 && topY + 1 < kAxolotlBodyVolumeDims.y)
        {
            const uint8_t ridgeId = palette.fin[(x & 1) != 0 ? 1 : 0];
            builder.setVoxel(x, topY + 1, 4, ridgeId);
            builder.setVoxel(x, topY + 1, 5, ridgeId);
        }
    }

    // external gill fans: three long stalks per side sprouting from the head's
    // rear-top corners, flaring up/out/back to bright tips (the rearmost stalk
    // sweeps furthest back and gets a doubled tip).
    for (const int side : {0, 1})
    {
        const int baseZ = side == 0 ? 2 : 7;
        const int midZ = side == 0 ? 1 : 8;
        const int tipZ = side == 0 ? 0 : 9;

        builder.setVoxel(14, 5, baseZ, palette.gill[0]);
        builder.setVoxel(14, 6, midZ, palette.gill[0]);
        builder.setVoxel(14, 7, tipZ, palette.gill[1]);

        builder.setVoxel(13, 5, baseZ, palette.gill[0]);
        builder.setVoxel(12, 6, midZ, palette.gill[0]);
        builder.setVoxel(12, 7, tipZ, palette.gill[1]);

        builder.setVoxel(12, 5, baseZ, palette.gill[0]);
        builder.setVoxel(11, 6, midZ, palette.gill[0]);
        builder.setVoxel(10, 6, midZ, palette.gill[1]);
        builder.setVoxel(10, 7, tipZ, palette.gill[1]);
    }

    // the face is on the front of the head (x=15). keep the dark eye columns
    // one step in from the outer corners so they stay on the flat face instead
    // of reading as side/top markings when the axolotl pitches or yaws. each
    // eye is a chunky 2x2 patch so it remains black after voxel pixelation.
    for (const int eyeZ0 : {2, 6})
    {
        for (int y = 4; y <= 5; ++y)
        {
            builder.setVoxel(15, y, eyeZ0, palette.eye);
            builder.setVoxel(15, y, eyeZ0 + 1, palette.eye);
        }
    }

    // small dark nostril dots centered between the eyes make the flat front
    // read as a face instead of a plain pink block.
    builder.setVoxel(15, 4, 4, palette.detail[3]);
    builder.setVoxel(15, 4, 5, palette.detail[3]);

    // dark-rose smile with raised corners on the face; the straight lower span
    // keeps it readable at voxel scale, while the corners make it softer.
    for (int z = 2; z <= 7; ++z)
    {
        if (builder.getVoxel(15, 3, z) != 0)
        {
            builder.setVoxel(15, 3, z, palette.detail[3]);
        }
    }

    for (int z = 3; z <= 6; ++z)
    {
        if (builder.getVoxel(15, 2, z) != 0)
        {
            builder.setVoxel(15, 2, z, palette.detail[3]);
        }
    }

    builder.setVoxel(14, 3, 3, palette.detail[3]);
    builder.setVoxel(14, 3, 6, palette.detail[3]);

    // blush dots on each cheek side, just behind the mouth corners.
    builder.setVoxel(14, 4, 2, palette.detail[1]);
    builder.setVoxel(14, 4, 7, palette.detail[1]);
    builder.setVoxel(13, 4, 2, palette.detail[1]);
    builder.setVoxel(13, 4, 7, palette.detail[1]);

    return builder.data();
}

std::vector<uint8_t> buildAxolotlLimbVoxelData(const AxolotlPaletteBandIds& palette)
{
    engine::VoxelBuilder builder(kAxolotlLimbVolumeDims);

    // a small separate paddle: pink shoulder/hip cap, cream mitten foot, and
    // splayed toes. the cap lets the animated limb visibly plug into the body.
    builder.setVoxel(0, 4, 0, palette.body[2]);
    builder.setVoxel(0, 4, 1, palette.body[2]);
    builder.setVoxel(0, 4, 2, palette.body[2]);
    builder.setVoxel(1, 4, 1, palette.body[1]);
    builder.setVoxel(1, 3, 0, palette.body[1]);
    builder.setVoxel(1, 3, 1, palette.body[1]);
    builder.setVoxel(1, 3, 2, palette.body[1]);
    builder.setVoxel(1, 2, 1, palette.body[1]);

    builder.setVoxel(1, 1, 1, palette.detail[2]);
    builder.setVoxel(2, 1, 1, palette.detail[2]);
    builder.setVoxel(3, 1, 1, palette.detail[2]);
    builder.setVoxel(2, 1, 0, palette.detail[2]);
    builder.setVoxel(2, 1, 2, palette.detail[2]);

    return builder.data();
}

std::vector<uint8_t> buildAxolotlTailVoxelData(const AxolotlPaletteBandIds& palette)
{
    engine::VoxelBuilder builder(kAxolotlTailVolumeDims);
    const float centerY = 5.0f;

    for (int x = 0; x < kAxolotlTailVolumeDims.x; ++x)
    {
        // rootT: 1 at the body joint (x=7), 0 at the tip (x=0).
        const float rootT = static_cast<float>(x) /
                            static_cast<float>(kAxolotlTailVolumeDims.x - 1);
        const float coreHalf = 0.6f + 1.1f * rootT;
        const float membraneHalf = 1.8f + 2.0f * rootT;

        for (int y = 0; y < kAxolotlTailVolumeDims.y; ++y)
        {
            const float dy = static_cast<float>(y) + 0.5f - centerY;
            const float absDy = std::abs(dy);

            // the blade itself is two voxels thick (z=2,3 around center z=3).
            for (int z = 2; z <= 3; ++z)
            {
                if (absDy <= coreHalf)
                {
                    builder.setVoxel(x, y, z, palette.body[dy < 0.0f ? 1 : 2]);
                }
                else if (absDy <= membraneHalf)
                {
                    const bool brightEdge = absDy > membraneHalf - 1.0f;
                    builder.setVoxel(x, y, z, palette.fin[brightEdge ? 1 : 0]);
                }
            }

            // chunky muscle root where the tail meets the body, thinning as the
            // blade takes over.
            if (x >= 5 && absDy <= 0.4f + 1.8f * rootT)
            {
                builder.setVoxel(x, y, 1, palette.body[1]);
                builder.setVoxel(x, y, 4, palette.body[1]);
            }
        }
    }

    return builder.data();
}

} // namespace engine::game
