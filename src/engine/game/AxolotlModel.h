#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine::game
{

// voxel sculpt for a leucistic pink axolotl. the sculpt stamps the palette
// ids supplied by the caller, which maps the body, gill, fin, and detail bands.
// geometry generation has no dependency on the aquarium palette registry.
// the same shape can use different palettes.
struct AxolotlPaletteBandIds
{
    std::array<uint8_t, 4> body{};   // belly shadow -> lower flank -> main pink -> top highlight
    std::array<uint8_t, 2> gill{};   // gill stalk -> bright gill tip
    std::array<uint8_t, 2> fin{};    // inner membrane -> bright membrane edge
    std::array<uint8_t, 4> detail{}; // freckle rose, cheek blush, belly cream, dark-rose mouth
    uint8_t eye = 0;
};

// the axolotl deliberately reuses the procedural-fish volume dimensions and
// anchor conventions (nose at +x, rear at x=0, tail volume tip at x=0) so it
// rides the existing body+tail swim rig and FishCelebration hooks unchanged.
constexpr glm::ivec3 kAxolotlBodyVolumeDims{16, 10, 10};
constexpr glm::ivec3 kAxolotlTailVolumeDims{8, 10, 6};
constexpr glm::ivec3 kAxolotlLimbVolumeDims{4, 5, 3};
constexpr glm::vec3 kAxolotlLimbAnchorLocal{1.0f, 3.8f, 1.5f};
constexpr int kAxolotlLimbCount = 4;

// body: blunt wide face with inset front-facing eyes and a dark-rose smile,
// three long gill stalks fanning up/out/back per side, a dorsal fin ridge along
// the spine, dithered flank shading with rosy freckles, and a cream belly.
// mirror-symmetric across z.
std::vector<uint8_t> buildAxolotlBodyVoxelData(const AxolotlPaletteBandIds& palette);

// tail: a laterally compressed fin blade — muscle core wrapped in a membrane
// that is tallest and thickest at the root and tapers toward the tip.
std::vector<uint8_t> buildAxolotlTailVoxelData(const AxolotlPaletteBandIds& palette);

// limb: one animated arm/leg paddle. the app places four copies at separate
// shoulder/hip anchors so they can swing independently from the body.
std::vector<uint8_t> buildAxolotlLimbVoxelData(const AxolotlPaletteBandIds& palette);

} // namespace engine::game
