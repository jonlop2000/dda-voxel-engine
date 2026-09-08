#pragma once

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace engine::render
{

enum class FoliagePaletteResponse : uint32_t
{
    Raw = 0,
    SoftValueV1 = 1,
    BotanicalDepthV2 = 2,
    MeadowVolumeV3 = 3,
    PastelLightV4 = 4,
};

struct FoliagePresentationSettings
{
    float strength = 0.0f;
    float luminanceContrast = 1.0f;
    float saturationRetention = 1.0f;
    float sideNormalUpBias = 0.0f;
    float botanicalIdentityStrength = 0.0f;
    float roughnessSeparation = 0.0f;
    float faceValueContrast = 0.0f;
    float meadowVolumeStrength = 0.0f;
    float softMaterialStrength = 0.0f;

    [[nodiscard]] bool enabled() const { return strength > 0.0f; }
};

[[nodiscard]] FoliagePaletteResponse parseFoliagePaletteResponse(
    std::string_view value);
[[nodiscard]] std::string_view foliagePaletteResponseName(
    FoliagePaletteResponse response);
[[nodiscard]] FoliagePresentationSettings makeFoliagePresentationSettings(
    FoliagePaletteResponse response);

// vulkan-free references mirrored by foliage.frag. strength zero is an exact no-op.
[[nodiscard]] glm::vec3 applyFoliagePaletteResponse(
    const glm::vec3& baseColor, uint32_t primitiveRole,
    const FoliagePresentationSettings& settings, uint32_t materialId = 0u,
    float motionHeight = 0.5f,
    const glm::vec3& faceNormal = glm::vec3(0.0f, 1.0f, 0.0f));
[[nodiscard]] float applyFoliageRoughnessResponse(
    float baseRoughness, uint32_t primitiveRole,
    const FoliagePresentationSettings& settings);
[[nodiscard]] glm::vec3 softenFoliageFaceNormal(
    const glm::vec3& faceNormal, uint32_t primitiveRole,
    const FoliagePresentationSettings& settings);
[[nodiscard]] float softenFoliageShadowVisibility(
    float shadowVisibility, const FoliagePresentationSettings& settings);
[[nodiscard]] float evaluateFoliageWrappedDiffuse(
    float rawNdotL, const FoliagePresentationSettings& settings);

} // namespace engine::render
