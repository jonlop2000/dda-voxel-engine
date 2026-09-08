#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

#include "engine/voxel/VoxelMaterialCategory.h"

namespace engine
{

// runtime controls for deterministic, cell-constant palette variation.
// zero is the compatibility configuration. ScenePresentationProfile v1 owns
// persistent scene values; this type remains the renderer-facing runtime bucket.
struct VoxelCellVariationSettings
{
    float masterStrength = 0.0f;
    float genericAmplitude = 0.0f;
    float gravelAmplitude = 0.0f;
    float plantAmplitude = 0.0f;
    float stoneAmplitude = 0.0f;
    float woodAmplitude = 0.0f;
    // historical SoftOutdoorV0 spread constants are explicit runtime values so
    // newer painted profiles can widen the palette without changing old looks.
    float hueSpread = 0.018f;
    float saturationSpread = 0.12f;
    float valueSpread = 0.10f;
    // blends fine cell noise toward a quantized 4x4x4-cell palette family.
    float paletteFamilyStrength = 0.0f;

    [[nodiscard]] bool enabled() const;
    bool operator==(const VoxelCellVariationSettings&) const = default;
};

enum class VoxelCellVariationPreset
{
    Disabled,
    SoftOutdoorV0,
    PaintedOutdoorV0,
};

struct VoxelCellVariationSignal
{
    // signed deterministic signals in [-1, 1].
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;

    bool operator==(const VoxelCellVariationSignal&) const = default;
};

[[nodiscard]] std::optional<VoxelCellVariationPreset>
voxelCellVariationPresetFromName(std::string_view name);
[[nodiscard]] std::string_view voxelCellVariationPresetName(VoxelCellVariationPreset preset);
[[nodiscard]] VoxelCellVariationSettings
makeVoxelCellVariationSettings(VoxelCellVariationPreset preset);

[[nodiscard]] float voxelCellVariationCategoryAmplitude(
    const VoxelCellVariationSettings& settings, VoxelMaterialCategory category);
[[nodiscard]] float voxelCellVariationEffectiveStrength(
    const VoxelCellVariationSettings& settings, VoxelMaterialCategory category);

// the key is volume-local integer cell + stable scene volume index. it contains
// no world transform, camera, time, or screen-space input.
[[nodiscard]] VoxelCellVariationSignal voxelCellVariationSignal(
    const glm::ivec3& voxel, uint32_t stableVolumeKey);

// cpu reference implementation used by focused contract tests. the obb shader
// mirrors this hsv nudge around the authored palette color.
[[nodiscard]] glm::vec3 applyVoxelCellAlbedoVariation(
    const glm::vec3& authoredColor, VoxelMaterialCategory category,
    const glm::ivec3& voxel, uint32_t stableVolumeKey,
    const VoxelCellVariationSettings& settings);

} // namespace engine
