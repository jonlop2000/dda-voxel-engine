#pragma once

namespace engine::render
{

[[nodiscard]] constexpr bool isDdaShadowsAvailable(bool shadowRayPassAvailable,
                                                   bool shadowTemporalResolveAvailable)
{
    return shadowRayPassAvailable && shadowTemporalResolveAvailable;
}

[[nodiscard]] constexpr bool canUseDdaShadows(bool useDdaShadows,
                                              bool shadowRayPassAvailable,
                                              bool shadowTemporalResolveAvailable)
{
    return useDdaShadows &&
           isDdaShadowsAvailable(shadowRayPassAvailable, shadowTemporalResolveAvailable);
}

[[nodiscard]] constexpr bool isAmbientOcclusionAvailable(bool aoPassAvailable,
                                                         bool aoTemporalResolveAvailable)
{
    return aoPassAvailable && aoTemporalResolveAvailable;
}

[[nodiscard]] constexpr bool canUseAmbientOcclusion(bool ambientOcclusionEnabled,
                                                    bool aoPassAvailable,
                                                    bool aoTemporalResolveAvailable)
{
    return ambientOcclusionEnabled &&
           isAmbientOcclusionAvailable(aoPassAvailable, aoTemporalResolveAvailable);
}

[[nodiscard]] constexpr bool isLocalLightShadowsAvailable(bool localLightShadowPassAvailable,
                                                          bool localLightShadowResolveAvailable)
{
    return localLightShadowPassAvailable && localLightShadowResolveAvailable;
}

[[nodiscard]] constexpr bool canUseLocalLightShadows(bool localLightShadowsEnabled,
                                                     bool localLightShadowPassAvailable,
                                                     bool localLightShadowResolveAvailable)
{
    return localLightShadowsEnabled &&
           isLocalLightShadowsAvailable(localLightShadowPassAvailable,
                                        localLightShadowResolveAvailable);
}

[[nodiscard]] constexpr bool isLocalShadowBlurAvailable(bool localShadowBlurAvailable,
                                                        bool localLightShadowPassAvailable,
                                                        bool localLightShadowResolveAvailable)
{
    return localShadowBlurAvailable &&
           isLocalLightShadowsAvailable(localLightShadowPassAvailable,
                                        localLightShadowResolveAvailable);
}

[[nodiscard]] constexpr bool canUseLocalShadowBlur(bool localShadowBlurEnabled,
                                                   bool localShadowBlurAvailable,
                                                   bool localLightShadowPassAvailable,
                                                   bool localLightShadowResolveAvailable)
{
    return localShadowBlurEnabled &&
           isLocalShadowBlurAvailable(localShadowBlurAvailable, localLightShadowPassAvailable,
                                      localLightShadowResolveAvailable);
}

}  // namespace engine::render
