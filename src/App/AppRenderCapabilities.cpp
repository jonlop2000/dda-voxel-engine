#include "App/App.h"

engine::render::Renderer::CapabilityState App::rendererCapabilityState() const
{
    const auto& availability = renderPasses().availability;
    return engine::render::Renderer::CapabilityState{
        shadowSettings_.useDDAShadows_,
        aoSettings_.aoEnabled_,
        shadowSettings_.localLightShadowsEnabled_,
        shadowSettings_.localShadowBlurEnabled_,
        availability.shadowRayPass,
        availability.shadowTemporalResolve,
        availability.aoPass,
        availability.aoTemporalResolve,
        availability.localLightShadowPass,
        availability.localLightShadowResolve,
        availability.localShadowBlur,
    };
}

bool App::isDdaShadowsAvailable() const
{
    return renderer_.isDdaShadowsAvailable(rendererCapabilityState());
}

bool App::isAmbientOcclusionAvailable() const
{
    return renderer_.isAmbientOcclusionAvailable(rendererCapabilityState());
}

bool App::isLocalLightShadowsAvailable() const
{
    return renderer_.isLocalLightShadowsAvailable(rendererCapabilityState());
}

bool App::isLocalShadowBlurAvailable() const
{
    return renderer_.isLocalShadowBlurAvailable(rendererCapabilityState());
}

bool App::canUseDdaShadows() const
{
    return renderer_.canUseDdaShadows(rendererCapabilityState());
}

bool App::canUseAmbientOcclusion() const
{
    return renderer_.canUseAmbientOcclusion(rendererCapabilityState());
}

bool App::canUseLocalLightShadowTracing() const
{
    return renderer_.canUseLocalLightShadows(rendererCapabilityState());
}

bool App::canUseLocalShadowBlur() const
{
    return renderer_.canUseLocalShadowBlur(rendererCapabilityState());
}
