#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "engine/render/GpuProfiler.h"

namespace engine::performance
{

struct FrameTimeStatistics
{
    double averageMs = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
};

struct AutomationProfileSampleResult
{
    bool readinessReached = false;
    bool warmupCompleted = false;
    bool samplingStarted = false;
    bool sampleAccepted = false;
    bool samplingCompleted = false;
};

struct AutomationProfileSummary
{
    FrameTimeStatistics cpu{};
    FrameTimeStatistics gpu{};
    std::vector<GpuProfilerSample> averagePasses{};
    uint64_t warmupFrames = 0;
    uint64_t sampledFrames = 0;
};

class AutomationProfileSession
{
public:
    void configure(uint64_t warmupFrames, uint64_t sampleFrames);
    void reset();

    AutomationProfileSampleResult consume(
        bool sceneReady,
        double cpuFrameMs,
        double gpuFrameMs,
        std::span<const GpuProfilerSample> passSamples);

    uint64_t requestedWarmupFrames() const { return requestedWarmupFrames_; }
    uint64_t requestedSampleFrames() const { return requestedSampleFrames_; }
    uint64_t consumedWarmupFrames() const { return consumedWarmupFrames_; }
    uint64_t sampledFrameCount() const { return cpuFrameMs_.size(); }
    bool readinessReached() const { return readinessReached_; }
    bool samplingComplete() const;

    AutomationProfileSummary summary() const;

private:
    uint64_t requestedWarmupFrames_ = 0;
    uint64_t requestedSampleFrames_ = 0;
    uint64_t consumedWarmupFrames_ = 0;
    bool readinessReached_ = false;
    bool samplingStarted_ = false;
    std::vector<double> cpuFrameMs_{};
    std::vector<double> gpuFrameMs_{};
    std::vector<GpuProfilerSample> passTotals_{};
};

FrameTimeStatistics calculateFrameTimeStatistics(std::span<const double> samples);
[[nodiscard]] std::optional<double> parseFixedSceneTimeSeconds(std::string_view value);

}  // namespace engine::performance
