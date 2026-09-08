#include "engine/voxel/VoxelCellVariation.h"

#include <algorithm>
#include <cmath>

namespace engine
{
namespace
{

uint32_t mixHash(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint32_t cellHash(const glm::ivec3& voxel, uint32_t stableVolumeKey, uint32_t salt)
{
    uint32_t hash = 0x9e3779b9u ^ (stableVolumeKey * 0x85ebca6bu);
    hash ^= static_cast<uint32_t>(voxel.x) * 0x8da6b343u;
    hash ^= static_cast<uint32_t>(voxel.y) * 0xd8163841u;
    hash ^= static_cast<uint32_t>(voxel.z) * 0xcb1ab31fu;
    hash ^= salt * 0x27d4eb2du;
    return mixHash(hash);
}

float signedHash(const glm::ivec3& voxel, uint32_t stableVolumeKey, uint32_t salt)
{
    constexpr float kInverse24Bit = 1.0f / 16777215.0f;
    const float unit =
        static_cast<float>(cellHash(voxel, stableVolumeKey, salt) & 0x00ffffffu) *
        kInverse24Bit;
    return unit * 2.0f - 1.0f;
}

glm::vec3 rgbToHsv(const glm::vec3& color)
{
    const float maxChannel = std::max(color.r, std::max(color.g, color.b));
    const float minChannel = std::min(color.r, std::min(color.g, color.b));
    const float delta = maxChannel - minChannel;

    float hue = 0.0f;
    if (delta > 1e-6f)
    {
        if (maxChannel == color.r)
        {
            hue = std::fmod((color.g - color.b) / delta, 6.0f);
        }
        else if (maxChannel == color.g)
        {
            hue = (color.b - color.r) / delta + 2.0f;
        }
        else
        {
            hue = (color.r - color.g) / delta + 4.0f;
        }
        hue /= 6.0f;
        if (hue < 0.0f)
        {
            hue += 1.0f;
        }
    }

    const float saturation = maxChannel > 1e-6f ? delta / maxChannel : 0.0f;
    return glm::vec3(hue, saturation, maxChannel);
}

glm::vec3 hsvToRgb(const glm::vec3& hsv)
{
    const float wrappedHue = hsv.x - std::floor(hsv.x);
    const float scaledHue = wrappedHue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaledHue)) % 6;
    const float fraction = scaledHue - std::floor(scaledHue);
    const float p = hsv.z * (1.0f - hsv.y);
    const float q = hsv.z * (1.0f - fraction * hsv.y);
    const float t = hsv.z * (1.0f - (1.0f - fraction) * hsv.y);

    switch (sector)
    {
    case 0:
        return glm::vec3(hsv.z, t, p);
    case 1:
        return glm::vec3(q, hsv.z, p);
    case 2:
        return glm::vec3(p, hsv.z, t);
    case 3:
        return glm::vec3(p, q, hsv.z);
    case 4:
        return glm::vec3(t, p, hsv.z);
    default:
        return glm::vec3(hsv.z, p, q);
    }
}

} // namespace

bool VoxelCellVariationSettings::enabled() const
{
    return masterStrength > 0.0f &&
           (genericAmplitude > 0.0f || gravelAmplitude > 0.0f ||
            plantAmplitude > 0.0f || stoneAmplitude > 0.0f ||
            woodAmplitude > 0.0f);
}

std::optional<VoxelCellVariationPreset>
voxelCellVariationPresetFromName(std::string_view name)
{
    if (name == "off")
    {
        return VoxelCellVariationPreset::Disabled;
    }
    if (name == "soft-v0")
    {
        return VoxelCellVariationPreset::SoftOutdoorV0;
    }
    if (name == "painted-v0")
    {
        return VoxelCellVariationPreset::PaintedOutdoorV0;
    }
    return std::nullopt;
}

std::string_view voxelCellVariationPresetName(VoxelCellVariationPreset preset)
{
    switch (preset)
    {
    case VoxelCellVariationPreset::Disabled:
        return "off";
    case VoxelCellVariationPreset::SoftOutdoorV0:
        return "soft-v0";
    case VoxelCellVariationPreset::PaintedOutdoorV0:
        return "painted-v0";
    }
    return "unknown";
}

VoxelCellVariationSettings makeVoxelCellVariationSettings(VoxelCellVariationPreset preset)
{
    VoxelCellVariationSettings settings{};
    if (preset == VoxelCellVariationPreset::SoftOutdoorV0)
    {
        settings.masterStrength = 1.0f;
        settings.genericAmplitude = 0.18f;
        settings.gravelAmplitude = 0.35f;
        settings.plantAmplitude = 0.55f;
        settings.stoneAmplitude = 0.28f;
        settings.woodAmplitude = 0.38f;
    }
    else if (preset == VoxelCellVariationPreset::PaintedOutdoorV0)
    {
        settings.masterStrength = 1.0f;
        settings.genericAmplitude = 0.78f;
        settings.gravelAmplitude = 0.70f;
        settings.plantAmplitude = 0.92f;
        settings.stoneAmplitude = 0.62f;
        settings.woodAmplitude = 0.72f;
        settings.hueSpread = 0.078f;
        settings.saturationSpread = 0.40f;
        settings.valueSpread = 0.30f;
        settings.paletteFamilyStrength = 0.76f;
    }
    return settings;
}

float voxelCellVariationCategoryAmplitude(
    const VoxelCellVariationSettings& settings, VoxelMaterialCategory category)
{
    switch (category)
    {
    case VoxelMaterialCategory::Generic:
        return settings.genericAmplitude;
    case VoxelMaterialCategory::Gravel:
        return settings.gravelAmplitude;
    case VoxelMaterialCategory::Plant:
        return settings.plantAmplitude;
    case VoxelMaterialCategory::Stone:
        return settings.stoneAmplitude;
    case VoxelMaterialCategory::Wood:
        return settings.woodAmplitude;
    default:
        return 0.0f;
    }
}

float voxelCellVariationEffectiveStrength(
    const VoxelCellVariationSettings& settings, VoxelMaterialCategory category)
{
    return std::clamp(settings.masterStrength, 0.0f, 1.0f) *
           std::clamp(voxelCellVariationCategoryAmplitude(settings, category),
                      0.0f, 1.0f);
}

VoxelCellVariationSignal voxelCellVariationSignal(
    const glm::ivec3& voxel, uint32_t stableVolumeKey)
{
    return VoxelCellVariationSignal{
        signedHash(voxel, stableVolumeKey, 0x68bc21ebu),
        signedHash(voxel, stableVolumeKey, 0x02e5be93u),
        signedHash(voxel, stableVolumeKey, 0x967a889bu)};
}

glm::vec3 applyVoxelCellAlbedoVariation(
    const glm::vec3& authoredColor, VoxelMaterialCategory category,
    const glm::ivec3& voxel, uint32_t stableVolumeKey,
    const VoxelCellVariationSettings& settings)
{
    const float strength =
        voxelCellVariationEffectiveStrength(settings, category);
    if (strength <= 0.0f)
    {
        return authoredColor;
    }

    const VoxelCellVariationSignal signal =
        voxelCellVariationSignal(voxel, stableVolumeKey);
    const glm::ivec3 familyCell = glm::ivec3(glm::floor(glm::vec3(voxel) / 4.0f));
    VoxelCellVariationSignal family =
        voxelCellVariationSignal(familyCell, stableVolumeKey ^ 0x6a09e667u);
    family.hue = std::round(family.hue * 2.0f) * 0.5f;
    family.saturation = std::round(family.saturation * 2.0f) * 0.5f;
    family.value = std::round(family.value * 2.0f) * 0.5f;
    const glm::ivec3 fineFamilyCell =
        glm::ivec3(glm::floor(glm::vec3(voxel) / 2.0f));
    VoxelCellVariationSignal fineFamily =
        voxelCellVariationSignal(fineFamilyCell, stableVolumeKey ^ 0xbb67ae85u);
    fineFamily.hue = std::round(fineFamily.hue * 4.0f) * 0.25f;
    fineFamily.saturation =
        std::round(fineFamily.saturation * 4.0f) * 0.25f;
    fineFamily.value = std::round(fineFamily.value * 4.0f) * 0.25f;
    const VoxelCellVariationSignal layeredFamily{
        std::lerp(family.hue, fineFamily.hue, 0.38f),
        std::lerp(family.saturation, fineFamily.saturation, 0.38f),
        std::lerp(family.value, fineFamily.value, 0.38f)};
    const float familyStrength =
        std::clamp(settings.paletteFamilyStrength, 0.0f, 1.0f);
    const VoxelCellVariationSignal paintedSignal{
        std::lerp(signal.hue, layeredFamily.hue, familyStrength),
        std::lerp(signal.saturation, layeredFamily.saturation, familyStrength),
        std::lerp(signal.value, layeredFamily.value, familyStrength)};
    glm::vec3 hsv = rgbToHsv(authoredColor);
    hsv.x = hsv.x + paintedSignal.hue *
                        std::clamp(settings.hueSpread, 0.0f, 0.25f) * strength;
    hsv.x -= std::floor(hsv.x);
    hsv.y = std::clamp(
        hsv.y * (1.0f + paintedSignal.saturation *
                            std::clamp(settings.saturationSpread, 0.0f, 1.0f) *
                            strength),
        0.0f, 1.0f);
    hsv.z = std::max(
        0.0f, hsv.z * (1.0f + paintedSignal.value *
                                  std::clamp(settings.valueSpread, 0.0f, 1.0f) *
                                  strength));
    return glm::clamp(hsvToRgb(hsv), glm::vec3(0.0f), glm::vec3(1.35f));
}

} // namespace engine
