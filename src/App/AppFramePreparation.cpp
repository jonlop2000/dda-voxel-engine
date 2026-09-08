#include "App/App.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Core/Logger.h"

uint32_t App::prepareCompletedFrameResources()
{
    std::string frameFenceError;
    if (!renderer_.waitForCurrentFrameFence(&frameFenceError))
    {
        logAndExit("Vulkan", frameFenceError);
    }

    const uint32_t currentFrame = renderer_.currentFrameIndex();
    meshUploadQueue_.releaseFrame(ctx_, currentFrame);

    updatePixelInspectReadback(currentFrame);
#if VOXEL_WITH_RUNTIME_UI
    updateRuntimeUiPixelInspectReadback(currentFrame);
#endif
    gpuProfiler_.resolve(ctx_, currentFrame);
    return currentFrame;
}

void App::readCompletedDdaMetrics(engine::render::RendererPassResources& passes,
                                  uint32_t frameIndex)
{
    cachedDdaMetrics_ = passes.obbPass.readMetrics(ctx_, frameIndex);
    if (cachedDdaMetrics_.sampleCount == 0)
    {
        return;
    }

    DdaMetricsSample sample{};
    sample.sampleCount = static_cast<float>(cachedDdaMetrics_.sampleCount);
    sample.avgIterations =
        static_cast<float>(cachedDdaMetrics_.sumIters) / sample.sampleCount;
    sample.avgSkipJumps =
        static_cast<float>(cachedDdaMetrics_.sumSkipJumps) / sample.sampleCount;

    const uint32_t totalHitMiss = cachedDdaMetrics_.hitCount + cachedDdaMetrics_.missCount;
    sample.hitRate = totalHitMiss > 0
                         ? static_cast<float>(cachedDdaMetrics_.hitCount) /
                               static_cast<float>(totalHitMiss)
                         : 0.0f;

    ddaMetricsHistory_.push_back(sample);
    const size_t historyLimit =
        static_cast<size_t>(std::max(1, diagnosticsSettings_.voxelMetricsHistoryLength_));
    if (ddaMetricsHistory_.size() > historyLimit)
    {
        const auto overflow =
            static_cast<std::vector<DdaMetricsSample>::difference_type>(
                ddaMetricsHistory_.size() - historyLimit);
        ddaMetricsHistory_.erase(ddaMetricsHistory_.begin(),
                                 ddaMetricsHistory_.begin() + overflow);
    }
}
