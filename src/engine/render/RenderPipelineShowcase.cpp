#include "engine/render/RenderPipelineShowcase.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

using engine::render::RenderPipelineShowcaseStage;

constexpr int kCompositeFinal = 0;
constexpr int kCompositeAlbedo = 1;
constexpr int kCompositeNormal = 2;
constexpr int kCompositeMaterial = 3;
constexpr int kCompositeBloomExtract = 6;
constexpr int kCompositeBloomBlur = 7;
constexpr int kCompositeBloomCombined = 8;
constexpr int kCompositeWaterDistance = 13;
constexpr int kCompositePrePostScene = 15;
constexpr int kCompositeTemporalResolve = 16;
constexpr int kCompositeDirectLightingDebug = 17;

constexpr std::array<RenderPipelineShowcaseStage, 18> kStages{{
    {"G-BUFFER / ALBEDO", "Base surface colors before lighting", kCompositeAlbedo, 0, 0},
    {"G-BUFFER / NORMALS", "Surface directions used by lighting", kCompositeNormal, 0, 0},
    {"G-BUFFER / MATERIALS", "Roughness, metalness, and material data",
     kCompositeMaterial, 0, 0},
    {"WATER VOLUME PATH", "Distance traveled through water volumes",
     kCompositeWaterDistance, 0, 0},
    {"DDA SUN SHADOW / RAW", "Fresh per-pixel voxel shadow rays before stabilization",
     kCompositeDirectLightingDebug, 0, 0, 1},
    {"DDA SUN SHADOW / RESOLVED", "Voxel shadows after temporal resolve and denoising",
     kCompositeDirectLightingDebug, 0, 0, 2},
    {"AMBIENT OCCLUSION / RAW", "Fresh short-range voxel occlusion rays",
     kCompositeDirectLightingDebug, 4, 0},
    {"AMBIENT OCCLUSION / RESOLVED", "Temporally stabilized ambient occlusion",
     kCompositeDirectLightingDebug, 5, 0},
    {"DIFFUSE LIGHTING", "Light scattered by matte surfaces", kCompositePrePostScene, 1, 0},
    {"SPECULAR LIGHTING", "Highlights reflected by shiny surfaces", kCompositePrePostScene, 2, 0},
    {"ENVIRONMENT LIGHTING", "Ambient light arriving from the environment", kCompositePrePostScene, 3, 0},
    {"WATER CAUSTICS", "Focused moving light below the water", kCompositePrePostScene, 6, 0},
    {"LIT SCENE + TRANSPARENCY",
     "Lighting, atmosphere, water, and glass before post effects",
     kCompositePrePostScene, 0, 0},
    {"BLOOM / BRIGHT EXTRACT", "Bright pixels selected for glow", kCompositeBloomExtract, 0, 1},
    {"BLOOM / BLUR", "The extracted glow after filtering", kCompositeBloomBlur, 0, 2},
    {"BLOOM / COMBINED", "The lit scene with bloom energy added", kCompositeBloomCombined, 0, 3},
    {"TEMPORAL RESOLVE", "Current and trustworthy previous-frame samples combined",
     kCompositeTemporalResolve, 0, 0},
    {"FINAL COMPOSITE", "Tone mapping, presentation effects, and native-resolution UI",
     kCompositeFinal, 0, 0},
}};

constexpr double kMinimumSecondsPerStage = 0.25;
constexpr double kMaximumSecondsPerStage = 60.0;

} // namespace

namespace engine::render
{

std::span<const RenderPipelineShowcaseStage> renderPipelineShowcaseStages()
{
    return kStages;
}

void RenderPipelineShowcase::configure(bool enabled, double secondsPerStage,
                                       bool loop, bool startPaused)
{
    enabled_ = enabled;
    loop_ = loop;
    paused_ = enabled && startPaused;
    completed_ = false;
    stageIndex_ = 0;
    stageElapsedSeconds_ = 0.0;
    secondsPerStage_ = std::isfinite(secondsPerStage)
                           ? std::clamp(secondsPerStage, kMinimumSecondsPerStage,
                                        kMaximumSecondsPerStage)
                           : 3.0;
}

bool RenderPipelineShowcase::update(double dtSeconds)
{
    if (!enabled_ || paused_ || completed_ || !std::isfinite(dtSeconds) ||
        dtSeconds <= 0.0)
    {
        return false;
    }

    stageElapsedSeconds_ += std::min(dtSeconds, kMaximumSecondsPerStage);
    bool changed = false;
    while (stageElapsedSeconds_ >= secondsPerStage_)
    {
        stageElapsedSeconds_ -= secondsPerStage_;
        if (stageIndex_ + 1 < kStages.size())
        {
            ++stageIndex_;
            changed = true;
            continue;
        }
        if (loop_)
        {
            stageIndex_ = 0;
            changed = true;
            continue;
        }

        stageElapsedSeconds_ = secondsPerStage_;
        completed_ = true;
        break;
    }
    return changed;
}

bool RenderPipelineShowcase::next()
{
    if (!enabled_ || stageIndex_ + 1 >= kStages.size())
    {
        return false;
    }
    ++stageIndex_;
    stageElapsedSeconds_ = 0.0;
    completed_ = false;
    return true;
}

bool RenderPipelineShowcase::previous()
{
    if (!enabled_ || stageIndex_ == 0)
    {
        return false;
    }
    --stageIndex_;
    stageElapsedSeconds_ = 0.0;
    completed_ = false;
    return true;
}

void RenderPipelineShowcase::togglePaused()
{
    if (enabled_)
    {
        paused_ = !paused_;
    }
}

size_t RenderPipelineShowcase::stageCount() const
{
    return kStages.size();
}

double RenderPipelineShowcase::secondsRemaining() const
{
    if (completed_)
    {
        return 0.0;
    }
    return std::max(0.0, secondsPerStage_ - stageElapsedSeconds_);
}

float RenderPipelineShowcase::stageProgress() const
{
    if (completed_)
    {
        return 1.0f;
    }
    return static_cast<float>(
        std::clamp(stageElapsedSeconds_ / secondsPerStage_, 0.0, 1.0));
}

const RenderPipelineShowcaseStage& RenderPipelineShowcase::currentStage() const
{
    return kStages[std::min(stageIndex_, kStages.size() - 1)];
}

} // namespace engine::render
