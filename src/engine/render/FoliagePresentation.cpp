#include "engine/render/FoliagePresentation.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
namespace
{
constexpr uint32_t kFlowerRole = 2u;
constexpr uint32_t kGroundCoverRole = 3u;
constexpr glm::vec3 kLuminanceWeights(0.2126f, 0.7152f, 0.0722f);
constexpr float kLuminancePivot = 0.52f;

[[nodiscard]] float paletteRoleStrength(uint32_t primitiveRole,
                                        float strength,
                                        float meadowVolumeStrength) noexcept
{
    float roleScale = 0.78f;
    if (primitiveRole == kFlowerRole)
    {
        roleScale = 0.20f;
    }
    else if (primitiveRole == kGroundCoverRole)
    {
        roleScale = 0.55f;
    }
    else if (primitiveRole == 0u)
    {
        roleScale = 0.70f;
    }
    const float meadow = std::clamp(meadowVolumeStrength, 0.0f, 1.0f);
    if (meadow > 0.0f && primitiveRole != kFlowerRole)
    {
        const float meadowRoleScale =
            primitiveRole == kGroundCoverRole ? 0.82f
                                              : primitiveRole == 0u ? 0.80f
                                                                    : 0.88f;
        roleScale = glm::mix(roleScale, meadowRoleScale, meadow);
    }
    return std::clamp(strength, 0.0f, 1.0f) * roleScale;
}

[[nodiscard]] float paletteRoleValueScale(uint32_t primitiveRole,
                                          float meadowVolumeStrength,
                                          float softMaterialStrength) noexcept
{
    if (primitiveRole == kFlowerRole)
    {
        return 1.0f;
    }
    const float meadow = std::clamp(meadowVolumeStrength, 0.0f, 1.0f);
    float valueScale = 1.0f;
    if (primitiveRole == kGroundCoverRole)
    {
        valueScale = glm::mix(0.90f, 0.82f, meadow);
    }
    else
    {
        valueScale = primitiveRole == 0u ? glm::mix(0.86f, 0.78f, meadow)
                                         : glm::mix(0.88f, 0.91f, meadow);
    }
    const float pastel = std::clamp(softMaterialStrength, 0.0f, 1.0f);
    const float pastelScale = primitiveRole == kGroundCoverRole
                                  ? 0.96f
                                  : primitiveRole == 0u ? 0.92f : 0.98f;
    return glm::mix(valueScale, pastelScale, pastel);
}

[[nodiscard]] float normalRoleStrength(uint32_t primitiveRole,
                                       float strength) noexcept
{
    const float roleScale = primitiveRole == kFlowerRole
                                ? 0.25f
                                : primitiveRole == kGroundCoverRole ? 0.90f
                                                                     : 1.0f;
    return std::clamp(strength, 0.0f, 1.0f) * roleScale;
}

[[nodiscard]] float smoothUnit(float value) noexcept
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

[[nodiscard]] glm::vec3 botanicalVariantTint(uint32_t materialId) noexcept
{
    switch (materialId % 3u)
    {
    case 0u:
        return glm::vec3(0.84f, 1.02f, 0.98f);
    case 1u:
        return glm::vec3(0.91f, 1.00f, 0.87f);
    default:
        return glm::vec3(0.98f, 1.03f, 0.79f);
    }
}

[[nodiscard]] glm::vec3 meadowVariantTint(uint32_t materialId) noexcept
{
    switch (materialId % 3u)
    {
    case 0u:
        return glm::vec3(0.88f, 1.04f, 0.72f);
    case 1u:
        return glm::vec3(0.80f, 1.01f, 0.78f);
    default:
        return glm::vec3(0.94f, 1.03f, 0.68f);
    }
}

[[nodiscard]] glm::vec3 pastelVariantTint(uint32_t materialId) noexcept
{
    switch (materialId % 8u)
    {
    case 0u:
        return glm::vec3(0.90f, 1.03f, 0.82f);
    case 1u:
        return glm::vec3(0.82f, 1.01f, 0.94f);
    case 2u:
        return glm::vec3(1.04f, 1.07f, 0.74f);
    case 3u:
        return glm::vec3(0.82f, 0.94f, 0.70f);
    case 4u:
        return glm::vec3(0.92f, 1.08f, 0.78f);
    case 5u:
        return glm::vec3(0.76f, 0.98f, 0.88f);
    case 6u:
        return glm::vec3(0.98f, 1.01f, 0.70f);
    default:
        return glm::vec3(0.80f, 1.05f, 0.98f);
    }
}

[[nodiscard]] float botanicalRoleValue(uint32_t primitiveRole) noexcept
{
    if (primitiveRole == kGroundCoverRole)
    {
        return 0.92f;
    }
    return primitiveRole == 0u ? 0.86f : 1.0f;
}

[[nodiscard]] float meadowRoleValue(uint32_t primitiveRole) noexcept
{
    if (primitiveRole == kGroundCoverRole)
    {
        return 0.88f;
    }
    return primitiveRole == 0u ? 0.80f : 1.0f;
}

[[nodiscard]] float botanicalFaceValue(
    const glm::vec3& faceNormal, float contrast) noexcept
{
    const float length = glm::length(faceNormal);
    if (length <= 1e-6f)
    {
        return 1.0f;
    }
    const float normalY = (faceNormal / length).y;
    const float top = std::max(normalY, 0.0f);
    const float bottom = std::max(-normalY, 0.0f);
    const float side = 1.0f - std::abs(normalY);
    return 1.0f + std::clamp(contrast, 0.0f, 1.0f) *
                      (0.04f * top - 0.10f * bottom - 0.06f * side);
}
} // namespace

FoliagePaletteResponse parseFoliagePaletteResponse(std::string_view value)
{
    if (value == "raw")
    {
        return FoliagePaletteResponse::Raw;
    }
    if (value == "soft-value-v1")
    {
        return FoliagePaletteResponse::SoftValueV1;
    }
    if (value == "botanical-depth-v2")
    {
        return FoliagePaletteResponse::BotanicalDepthV2;
    }
    if (value == "meadow-volume-v3")
    {
        return FoliagePaletteResponse::MeadowVolumeV3;
    }
    return FoliagePaletteResponse::PastelLightV4;
}

std::string_view foliagePaletteResponseName(FoliagePaletteResponse response)
{
    switch (response)
    {
    case FoliagePaletteResponse::Raw:
        return "raw";
    case FoliagePaletteResponse::SoftValueV1:
        return "soft-value-v1";
    case FoliagePaletteResponse::BotanicalDepthV2:
        return "botanical-depth-v2";
    case FoliagePaletteResponse::MeadowVolumeV3:
        return "meadow-volume-v3";
    case FoliagePaletteResponse::PastelLightV4:
        return "pastel-light-v4";
    }
    return "pastel-light-v4";
}

FoliagePresentationSettings makeFoliagePresentationSettings(
    FoliagePaletteResponse response)
{
    if (response == FoliagePaletteResponse::Raw)
    {
        return {};
    }
    if (response == FoliagePaletteResponse::SoftValueV1)
    {
        return FoliagePresentationSettings{1.0f, 0.84f, 0.96f, 0.15f};
    }
    if (response == FoliagePaletteResponse::BotanicalDepthV2)
    {
        return FoliagePresentationSettings{
            1.0f, 0.90f, 1.0f, 0.12f,
            0.88f, 0.82f, 0.85f, 0.0f};
    }
    if (response == FoliagePaletteResponse::MeadowVolumeV3)
    {
        return FoliagePresentationSettings{
            1.0f, 0.96f, 1.0f, 0.10f,
            0.92f, 0.84f, 0.72f, 1.0f, 0.0f};
    }
    return FoliagePresentationSettings{
        1.0f, 0.42f, 0.78f, 0.52f,
        0.72f, 0.96f, 0.10f, 1.0f, 0.86f};
}

glm::vec3 applyFoliagePaletteResponse(
    const glm::vec3& baseColor, uint32_t primitiveRole,
    const FoliagePresentationSettings& settings, uint32_t materialId,
    float motionHeight, const glm::vec3& faceNormal)
{
    const float strength = paletteRoleStrength(
        primitiveRole, settings.strength, settings.meadowVolumeStrength);
    if (strength <= 0.0f)
    {
        return baseColor;
    }

    const float luminance = glm::dot(baseColor, kLuminanceWeights);
    const float compressedLuminance =
        kLuminancePivot +
        (luminance - kLuminancePivot) *
            std::clamp(settings.luminanceContrast, 0.0f, 1.0f);
    const glm::vec3 chroma = baseColor - glm::vec3(luminance);
    const glm::vec3 softened =
        glm::vec3(compressedLuminance) +
        chroma * std::clamp(settings.saturationRetention, 0.0f, 1.0f);
    glm::vec3 separated =
        softened * paletteRoleValueScale(
            primitiveRole, settings.meadowVolumeStrength,
            settings.softMaterialStrength);
    const float identityStrength =
        primitiveRole == kFlowerRole
            ? 0.0f
            : std::clamp(settings.botanicalIdentityStrength, 0.0f, 1.0f);
    if (identityStrength > 0.0f)
    {
        const float meadow =
            std::clamp(settings.meadowVolumeStrength, 0.0f, 1.0f);
        const float smoothHeight = smoothUnit(motionHeight);
        float heightValue = glm::mix(
            glm::mix(0.76f, 1.04f, smoothHeight),
            glm::mix(0.68f, 1.03f, smoothHeight), meadow);
        const float pastel =
            std::clamp(settings.softMaterialStrength, 0.0f, 1.0f);
        heightValue = glm::mix(
            heightValue, glm::mix(0.88f, 1.05f, smoothHeight), pastel);
        glm::vec3 variantTint = glm::mix(
            botanicalVariantTint(materialId), meadowVariantTint(materialId),
            meadow);
        variantTint = glm::mix(variantTint, pastelVariantTint(materialId),
                               pastel);
        const float roleValue = glm::mix(
            botanicalRoleValue(primitiveRole),
            meadowRoleValue(primitiveRole), meadow);
        const glm::vec3 offset = glm::mix(
            glm::vec3(-0.010f, 0.0f, 0.012f),
            glm::vec3(0.006f, 0.0f, -0.012f), meadow);
        const glm::vec3 botanical =
            separated * variantTint * roleValue * heightValue *
                botanicalFaceValue(faceNormal, settings.faceValueContrast) +
            offset;
        separated = glm::mix(separated, botanical, identityStrength);
        separated += glm::vec3(0.050f, 0.070f, 0.025f) * pastel;
    }
    return glm::clamp(glm::mix(baseColor, separated, strength),
                      glm::vec3(0.0f), glm::vec3(1.0f));
}

float applyFoliageRoughnessResponse(
    float baseRoughness, uint32_t primitiveRole,
    const FoliagePresentationSettings& settings)
{
    const float identityStrength =
        std::clamp(settings.botanicalIdentityStrength, 0.0f, 1.0f);
    if (identityStrength <= 0.0f)
    {
        return baseRoughness;
    }
    float target = 0.74f;
    if (primitiveRole == kFlowerRole)
    {
        target = 0.80f;
    }
    else if (primitiveRole == kGroundCoverRole)
    {
        target = 0.78f;
    }
    else if (primitiveRole == 0u)
    {
        target = 0.84f;
    }
    const float blend = identityStrength *
                        std::clamp(settings.roughnessSeparation, 0.0f, 1.0f);
    return std::clamp(glm::mix(baseRoughness, target, blend), 0.04f, 1.0f);
}

glm::vec3 softenFoliageFaceNormal(
    const glm::vec3& faceNormal, uint32_t primitiveRole,
    const FoliagePresentationSettings& settings)
{
    const float length = glm::length(faceNormal);
    if (length <= 1e-6f)
    {
        return faceNormal;
    }
    const glm::vec3 normalized = faceNormal / length;
    const float strength = normalRoleStrength(primitiveRole, settings.strength);
    if (strength <= 0.0f || std::abs(normalized.y) >= 0.5f)
    {
        return normalized;
    }
    return glm::normalize(
        normalized +
        glm::vec3(0.0f, std::max(settings.sideNormalUpBias, 0.0f) * strength,
                  0.0f));
}

float softenFoliageShadowVisibility(
    float shadowVisibility, const FoliagePresentationSettings& settings)
{
    const float strength =
        std::clamp(settings.softMaterialStrength, 0.0f, 1.0f);
    const float shadow = std::clamp(shadowVisibility, 0.0f, 1.0f);
    const float lifted = 0.42f + shadow * 0.58f;
    return glm::mix(shadow, lifted, strength * 0.78f);
}

float evaluateFoliageWrappedDiffuse(
    float rawNdotL, const FoliagePresentationSettings& settings)
{
    const float strength =
        std::clamp(settings.softMaterialStrength, 0.0f, 1.0f);
    const float hard = std::clamp(rawNdotL, 0.0f, 1.0f);
    const float wrapped = std::clamp((rawNdotL + 0.85f) / 1.85f, 0.0f, 1.0f);
    return glm::mix(hard, wrapped, strength * 0.74f);
}

} // namespace engine::render
