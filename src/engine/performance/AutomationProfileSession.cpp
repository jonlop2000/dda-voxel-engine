#include "engine/performance/AutomationProfileSession.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>
#include <string>
#include <utility>

namespace engine::performance
{
namespace
{

double percentile(const std::vector<double>& sortedSamples, double fraction)
{
    if (sortedSamples.empty())
    {
        return 0.0;
    }
    if (sortedSamples.size() == 1)
    {
        return sortedSamples.front();
    }

    const double position =
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(sortedSamples.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    const double blend = position - static_cast<double>(lower);
    return sortedSamples[lower] +
           (sortedSamples[upper] - sortedSamples[lower]) * blend;
}

GpuProfilerSample* findPassTotal(std::vector<GpuProfilerSample>& totals,
                                 const std::string& label)
{
    const auto it = std::find_if(
        totals.begin(), totals.end(),
        [&label](const GpuProfilerSample& sample) { return sample.label == label; });
    return it == totals.end() ? nullptr : &(*it);
}

}  // namespace

FrameTimeStatistics calculateFrameTimeStatistics(std::span<const double> samples)
{
    FrameTimeStatistics result{};
    if (samples.empty())
    {
        return result;
    }

    std::vector<double> sortedSamples(samples.begin(), samples.end());
    std::sort(sortedSamples.begin(), sortedSamples.end());

    result.averageMs =
        std::accumulate(sortedSamples.begin(), sortedSamples.end(), 0.0) /
        static_cast<double>(sortedSamples.size());
    result.medianMs = percentile(sortedSamples, 0.50);
    result.p95Ms = percentile(sortedSamples, 0.95);
    result.p99Ms = percentile(sortedSamples, 0.99);
    result.minMs = sortedSamples.front();
    result.maxMs = sortedSamples.back();
    return result;
}

std::optional<double> parseFixedSceneTimeSeconds(std::string_view value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    try
    {
        size_t consumed = 0;
        const double parsed = std::stod(std::string(value), &consumed);
        if (consumed != value.size() || !std::isfinite(parsed) || parsed < 0.0)
        {
            return std::nullopt;
        }
        return parsed;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void AutomationProfileSession::configure(uint64_t warmupFrames, uint64_t sampleFrames)
{
    requestedWarmupFrames_ = warmupFrames;
    requestedSampleFrames_ = sampleFrames;
    reset();
}

void AutomationProfileSession::reset()
{
    consumedWarmupFrames_ = 0;
    readinessReached_ = false;
    samplingStarted_ = false;
    cpuFrameMs_.clear();
    gpuFrameMs_.clear();
    passTotals_.clear();
}

AutomationProfileSampleResult AutomationProfileSession::consume(
    bool sceneReady,
    double cpuFrameMs,
    double gpuFrameMs,
    std::span<const GpuProfilerSample> passSamples)
{
    AutomationProfileSampleResult result{};
    if (!sceneReady || samplingComplete())
    {
        return result;
    }

    if (!readinessReached_)
    {
        readinessReached_ = true;
        result.readinessReached = true;
    }

    if (consumedWarmupFrames_ < requestedWarmupFrames_)
    {
        ++consumedWarmupFrames_;
        result.warmupCompleted = consumedWarmupFrames_ == requestedWarmupFrames_;
        return result;
    }

    if (!samplingStarted_)
    {
        samplingStarted_ = true;
        result.samplingStarted = true;
    }

    cpuFrameMs_.push_back(cpuFrameMs);
    gpuFrameMs_.push_back(gpuFrameMs);
    result.sampleAccepted = true;

    for (const GpuProfilerSample& sample : passSamples)
    {
        GpuProfilerSample* total = findPassTotal(passTotals_, sample.label);
        if (total != nullptr)
        {
            total->ms += sample.ms;
        }
        else
        {
            passTotals_.push_back(sample);
        }
    }

    result.samplingCompleted = samplingComplete();
    return result;
}

bool AutomationProfileSession::samplingComplete() const
{
    return requestedSampleFrames_ > 0 &&
           sampledFrameCount() >= requestedSampleFrames_;
}

AutomationProfileSummary AutomationProfileSession::summary() const
{
    AutomationProfileSummary result{};
    result.cpu = calculateFrameTimeStatistics(cpuFrameMs_);
    result.gpu = calculateFrameTimeStatistics(gpuFrameMs_);
    result.warmupFrames = consumedWarmupFrames_;
    result.sampledFrames = sampledFrameCount();
    result.averagePasses = passTotals_;

    if (result.sampledFrames > 0)
    {
        const double denominator = static_cast<double>(result.sampledFrames);
        for (GpuProfilerSample& sample : result.averagePasses)
        {
            sample.ms /= denominator;
        }
    }

    return result;
}

}  // namespace engine::performance
