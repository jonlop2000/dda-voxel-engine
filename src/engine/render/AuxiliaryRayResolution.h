#pragma once

#include <array>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

namespace engine::render
{

inline constexpr float kMinimumAuxiliaryRayScale = 0.50f;
inline constexpr float kMaximumAuxiliaryRayScale = 1.00f;
inline constexpr float kQualityAuxiliaryRayScale = 0.75f;
inline constexpr float kHalfResolutionAuxiliaryRayScale = 0.50f;
// creator visual qa plus macOS and windows qualification promote coverage-
// adaptive auxiliary rays for new runtime settings. explicit fixed scales and
// quality presets remain supported controls.
inline constexpr bool kAdaptiveAuxiliaryRayResolutionProductDefault = true;

enum class AdaptiveAuxiliaryRayTier : uint8_t
{
    Native,
    Quality,
    Half,
};

enum class AdaptiveAuxiliaryRayReason : uint8_t
{
    Fixed,
    LowCoverage,
    HighCoverage,
    CameraInsideVolume,
};

struct AdaptiveAuxiliaryRayConfig
{
    bool enabled = true;
    float qualityCoverageThreshold = 0.42f;
    float halfCoverageThreshold = 0.68f;
    float coverageHysteresis = 0.08f;
};

// ao's full-resolution edge-aware reconstruction costs more than its ray
// saving at the intermediate tier. adaptive ao therefore stays Native until
// Half is profitable; explicit fixed Quality remains available to authors.
[[nodiscard]] AdaptiveAuxiliaryRayConfig ambientOcclusionAdaptiveRayConfig(
    bool enabled);

struct AdaptiveAuxiliaryRayState
{
    AdaptiveAuxiliaryRayTier tier = AdaptiveAuxiliaryRayTier::Native;
    float effectiveScale = 1.0f;
    VkExtent2D activeExtent{};
    bool initialized = false;
};

struct AdaptiveAuxiliaryRayDecision
{
    AdaptiveAuxiliaryRayTier tier = AdaptiveAuxiliaryRayTier::Native;
    AdaptiveAuxiliaryRayReason reason = AdaptiveAuxiliaryRayReason::Fixed;
    float effectiveScale = 1.0f;
    VkExtent2D activeExtent{};
    // active raw-ray extent/logging signal. the consumer decides whether its
    // full-resolution temporal history remains compatible with that change.
    bool changed = false;
};

// conservative 16x9 union mask for projected voxel bounds. it intentionally
// overestimates near-plane intersections so adaptive quality never oscillates
// because a camera is grazing or entering a volume.
class ProjectedCoverageAccumulator
{
  public:
    void includeBounds(const glm::mat4& clipFromLocal, const glm::vec3& boundsMin,
                       const glm::vec3& boundsMax, bool fullScreenCoverageRequired);
    [[nodiscard]] float coverage() const;
    [[nodiscard]] uint32_t coveredCellCount() const;

  private:
    static constexpr uint32_t kColumns = 16;
    static constexpr uint32_t kRows = 9;
    std::array<uint16_t, kRows> rowMasks_{};
};

[[nodiscard]] float sanitizeAuxiliaryRayScale(float scale);
[[nodiscard]] VkExtent2D auxiliaryRayExtent(VkExtent2D sceneExtent, float scale);
[[nodiscard]] const char* auxiliaryRayScaleName(float scale);
// soft sun shadows trade part of each reduced-resolution tier's pixel savings
// for additional per-pixel disc strata. Native preserves the sampling-policy
// result; Quality and Half apply 1.5x and 2x sampling, respectively. point-sun
// shadows remain unchanged because they have no stochastic penumbra variance.
[[nodiscard]] uint32_t effectiveSunShadowSampleCount(
    uint32_t baseSampleCount, float effectiveScale, float effectiveAngularRadius,
    bool compensateReducedResolution = true);
// a reconstructed stochastic penumbra must be allowed to accumulate beyond
// the current low-resolution 3x3 value range. Native and point-sun shadows keep
// the established neighborhood clamp; reduced soft shadows rely on the same
// depth/normal rejection and spatial filter without that variance reset.
[[nodiscard]] bool useSunShadowNeighborhoodClamp(
    bool reconstructCurrent, float effectiveAngularRadius);
// Half-resolution ao has one quarter as many source pixels. doubling its
// per-pixel ray count reduces reconstruction noise while retaining half the
// total ray work of the equivalent native-resolution configuration.
[[nodiscard]] uint32_t effectiveAmbientOcclusionRayCount(
    uint32_t configuredRayCount, float effectiveScale);
// Half-resolution ao retains history longer to suppress the extra variance
// introduced by reconstructing one source sample across multiple output pixels.
[[nodiscard]] float effectiveAmbientOcclusionTemporalBlendAlpha(
    float configuredBlendAlpha, float effectiveScale);
// the reconstruction-aware bilateral pass is reserved for the noisy Half
// tier; Native and Quality use the cheaper temporal reconstruction path.
[[nodiscard]] bool useAmbientOcclusionSpatialFilter(float effectiveScale);
[[nodiscard]] const char* adaptiveAuxiliaryRayTierName(AdaptiveAuxiliaryRayTier tier);
[[nodiscard]] const char* adaptiveAuxiliaryRayReasonName(AdaptiveAuxiliaryRayReason reason);
[[nodiscard]] AdaptiveAuxiliaryRayDecision resolveAdaptiveAuxiliaryRayResolution(
    VkExtent2D sceneExtent, float configuredScale, float projectedCoverage,
    bool cameraInsideVolume, const AdaptiveAuxiliaryRayConfig& config,
    AdaptiveAuxiliaryRayState& state);

}  // namespace engine::render
