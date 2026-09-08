#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace engine::render
{

struct RenderPipelineShowcaseStage
{
    std::string_view title;
    std::string_view description;
    int compositeMode = 0;
    int lightingDebugMode = 0;
    int postDebugMode = 0;
    int shadowDebugMode = 0;
};

[[nodiscard]] std::span<const RenderPipelineShowcaseStage>
renderPipelineShowcaseStages();

class RenderPipelineShowcase
{
public:
    void configure(bool enabled, double secondsPerStage = 3.0, bool loop = false,
                   bool startPaused = false);

    [[nodiscard]] bool update(double dtSeconds);
    [[nodiscard]] bool next();
    [[nodiscard]] bool previous();
    void togglePaused();

    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] bool completed() const { return completed_; }
    [[nodiscard]] bool looping() const { return loop_; }
    [[nodiscard]] size_t stageIndex() const { return stageIndex_; }
    [[nodiscard]] size_t stageCount() const;
    [[nodiscard]] double secondsPerStage() const { return secondsPerStage_; }
    [[nodiscard]] double secondsRemaining() const;
    [[nodiscard]] float stageProgress() const;
    [[nodiscard]] const RenderPipelineShowcaseStage& currentStage() const;

private:
    bool enabled_ = false;
    bool paused_ = false;
    bool completed_ = false;
    bool loop_ = false;
    size_t stageIndex_ = 0;
    double secondsPerStage_ = 3.0;
    double stageElapsedSeconds_ = 0.0;
};

} // namespace engine::render
