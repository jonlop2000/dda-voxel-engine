#include "App/App.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "Core/Logger.h"

namespace
{

bool hasShowcaseArg(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

std::string showcaseArgValue(int argc, char** argv, const char* name)
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::strcmp(argv[i], name) == 0)
        {
            return argv[i + 1];
        }
    }
    return {};
}

std::optional<double> parseShowcaseSeconds(const std::string& value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    try
    {
        size_t consumed = 0;
        const double seconds = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(seconds) || seconds <= 0.0)
        {
            return std::nullopt;
        }
        return seconds;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

void logShowcaseStage(const engine::render::RenderPipelineShowcase& showcase,
                      const char* reason)
{
    const auto& stage = showcase.currentStage();
    logInfo("RenderShowcase",
            makeLogMessage("Stage ", showcase.stageIndex() + 1, "/",
                           showcase.stageCount(), " ", stage.title,
                           " reason=", reason != nullptr ? reason : "unknown", "."));
}

} // namespace

void App::configureRenderPipelineShowcase(int argc, char** argv)
{
    if (!hasShowcaseArg(argc, argv, "--render-pipeline-showcase"))
    {
        return;
    }

    constexpr double kDefaultSecondsPerStage = 3.0;
    double secondsPerStage = kDefaultSecondsPerStage;
    const std::string secondsArg =
        showcaseArgValue(argc, argv, "--render-showcase-seconds");
    if (!secondsArg.empty())
    {
        const std::optional<double> parsed = parseShowcaseSeconds(secondsArg);
        if (parsed.has_value())
        {
            secondsPerStage = *parsed;
        }
        else
        {
            logWarning("RenderShowcase",
                       makeLogMessage("Ignoring invalid --render-showcase-seconds value: ",
                                      secondsArg, "; using ",
                                      kDefaultSecondsPerStage, " seconds."));
        }
    }

    renderPipelineShowcase_.configure(
        true, secondsPerStage,
        hasShowcaseArg(argc, argv, "--render-showcase-loop"),
        hasShowcaseArg(argc, argv, "--render-showcase-start-paused"));
    setAppMode(AppMode::Game, "--render-pipeline-showcase");
    automationHideUi_ = true;
    automationSkipLastUsedSave_ = true;

    logInfo("RenderShowcase",
            makeLogMessage("Enabled stages=", renderPipelineShowcase_.stageCount(),
                           " seconds_per_stage=",
                           renderPipelineShowcase_.secondsPerStage(),
                           " loop=", renderPipelineShowcase_.looping() ? 1 : 0,
                           " start_paused=", renderPipelineShowcase_.paused() ? 1 : 0,
                           ". Controls: SPACE pause/resume, LEFT/RIGHT step."));
    logInfo("RenderShowcase",
            "Normal runtime/editor UI hidden and scene persistence disabled for capture.");
#if !VOXEL_WITH_RUNTIME_UI
    logWarning("RenderShowcase",
               "VOXEL_WITH_RUNTIME_UI=OFF: stage views work, but the capture label is unavailable.");
#endif
    logShowcaseStage(renderPipelineShowcase_, "startup");
}

void App::startRenderPipelineShowcaseFromEditor(double secondsPerStage, bool loop,
                                                bool startPaused)
{
#if VOXEL_WITH_EDITOR
    if (appMode_ != AppMode::Editor)
    {
        logWarning("RenderShowcase",
                   "The editor showcase can only start while editor mode is active.");
        return;
    }

    enterRuntimeUiPlayPreview("Render Pipeline Showcase");
    if (appMode_ != AppMode::PlayPreview)
    {
        return;
    }
    renderPipelineShowcase_.configure(true, secondsPerStage, loop, startPaused);
    renderPipelineShowcaseStartedFromEditor_ = true;
    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;
    logInfo("RenderShowcase",
            makeLogMessage("Started from editor stages=",
                           renderPipelineShowcase_.stageCount(),
                           " seconds_per_stage=",
                           renderPipelineShowcase_.secondsPerStage(), " loop=",
                           renderPipelineShowcase_.looping() ? 1 : 0,
                           " start_paused=",
                           renderPipelineShowcase_.paused() ? 1 : 0, "."));
    logInfo("RenderShowcase",
            "Using the current scene, camera, and live editor graphics settings. Press F5 to return to the editor.");
    logShowcaseStage(renderPipelineShowcase_, "editor-start");
#else
    (void)secondsPerStage;
    (void)loop;
    (void)startPaused;
    logWarning("RenderShowcase",
               "Editor showcase controls are unavailable because VOXEL_WITH_EDITOR=OFF.");
#endif
}

void App::stopRenderPipelineShowcaseFromEditor()
{
#if VOXEL_WITH_EDITOR
    if (!renderPipelineShowcaseStartedFromEditor_)
    {
        return;
    }
    renderPipelineShowcase_.configure(false);
    renderPipelineShowcaseStartedFromEditor_ = false;
    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;
    logInfo("RenderShowcase",
            "Stopped editor showcase; live scene and graphics settings were preserved.");
#endif
}

void App::updateRenderPipelineShowcase(double dtSeconds)
{
    const bool previousRequested = input_.consumeRenderShowcasePrevious();
    const bool nextRequested = input_.consumeRenderShowcaseNext();
    const bool pauseRequested = input_.consumeRenderShowcasePauseToggle();
    if (!renderPipelineShowcase_.enabled())
    {
        return;
    }

    bool changed = false;
    const char* reason = "timer";
    if (previousRequested)
    {
        changed = renderPipelineShowcase_.previous();
        reason = "previous-key";
    }
    if (nextRequested)
    {
        changed = renderPipelineShowcase_.next() || changed;
        reason = "next-key";
    }
    if (pauseRequested)
    {
        renderPipelineShowcase_.togglePaused();
        logInfo("RenderShowcase",
                renderPipelineShowcase_.paused() ? "Paused." : "Resumed.");
    }

    const bool wasCompleted = renderPipelineShowcase_.completed();
    // loading happens before the first rendered frame, so do not charge that
    // startup time against the opening showcase stage.
    const double showcaseDtSeconds = renderedFrameCount_ == 0 ? 0.0 : dtSeconds;
    if (renderPipelineShowcase_.update(showcaseDtSeconds))
    {
        changed = true;
        reason = "timer";
    }
    if (changed)
    {
        postFxSettings_.taaResetHistory_ = true;
        logShowcaseStage(renderPipelineShowcase_, reason);
    }
    if (!wasCompleted && renderPipelineShowcase_.completed())
    {
        logInfo("RenderShowcase", "Sequence complete; holding on the final composite.");
    }
}

#if VOXEL_WITH_RUNTIME_UI
void App::appendRenderPipelineShowcaseOverlay(VkExtent2D extent)
{
    if (!renderPipelineShowcase_.enabled() || extent.width == 0 || extent.height == 0)
    {
        return;
    }

    const float width = static_cast<float>(extent.width);
    const float height = static_cast<float>(extent.height);
    const float margin = std::clamp(width * 0.025f, 16.0f, 48.0f);
    const float panelWidth = std::min(840.0f, width - margin * 2.0f);
    const float panelHeight = std::min(174.0f, height - margin * 2.0f);
    if (panelWidth <= 0.0f || panelHeight <= 0.0f)
    {
        return;
    }

    const float panelX = margin;
    const float panelY = std::max(margin, height - margin - panelHeight);
    const ui::UiScissor fullScissor{0, 0, extent.width, extent.height};
    const ui::UiRect panel{panelX, panelY, panelWidth, panelHeight};
    runtimeUiDrawList_.addSolidRect(panel, ui::packColor(0.015f, 0.025f, 0.045f, 0.86f),
                                    fullScissor);

    const float sequenceProgress =
        (static_cast<float>(renderPipelineShowcase_.stageIndex()) +
         renderPipelineShowcase_.stageProgress()) /
        static_cast<float>(std::max<size_t>(renderPipelineShowcase_.stageCount(), 1));
    runtimeUiDrawList_.addSolidRect(
        ui::UiRect{panelX, panelY, panelWidth, 5.0f},
        ui::packColor(0.10f, 0.16f, 0.24f, 1.0f), fullScissor);
    runtimeUiDrawList_.addSolidRect(
        ui::UiRect{panelX, panelY, panelWidth * sequenceProgress, 5.0f},
        ui::packColor(0.20f, 0.76f, 1.0f, 1.0f), fullScissor);

    const auto& stage = renderPipelineShowcase_.currentStage();
    std::ostringstream header;
    header << "RENDER PIPELINE  " << std::setw(2) << std::setfill('0')
           << (renderPipelineShowcase_.stageIndex() + 1) << " / "
           << renderPipelineShowcase_.stageCount();
    ui::addDebugText(runtimeUiDebugFont_, runtimeUiDrawList_, header.str(),
                     panelX + 20.0f, panelY + 15.0f, 17.0f,
                     ui::packColor(0.45f, 0.82f, 1.0f, 1.0f), fullScissor);
    ui::addDebugText(runtimeUiDebugFont_, runtimeUiDrawList_, stage.title,
                     panelX + 20.0f, panelY + 39.0f, 30.0f,
                     ui::packColor(1.0f, 1.0f, 1.0f, 1.0f), fullScissor);
    ui::addDebugText(runtimeUiDebugFont_, runtimeUiDrawList_, stage.description,
                     panelX + 20.0f, panelY + 78.0f, 17.0f,
                     ui::packColor(0.78f, 0.84f, 0.92f, 1.0f), fullScissor);

    std::ostringstream settings;
    settings << "LIVE SETTINGS  DDA SHADOWS "
             << (canUseDdaShadows() ? "ON" : "OFF") << "  BLOOM "
             << (postFxSettings_.bloomEnabled_ ? "ON" : "OFF")
             << "  COLOR GRADE ";
    if (postFxSettings_.colorGradeEnabled_ &&
        postFxSettings_.colorGradeStrength_ > 0.0f)
    {
        settings << "ON  SAT " << std::fixed << std::setprecision(2)
                 << postFxSettings_.colorGradeSaturation_;
    }
    else
    {
        settings << "OFF";
    }
    ui::addDebugText(runtimeUiDebugFont_, runtimeUiDrawList_, settings.str(),
                     panelX + 20.0f, panelY + 105.0f, 14.0f,
                     ui::packColor(0.42f, 0.78f, 0.68f, 1.0f), fullScissor);

    std::ostringstream footer;
    if (renderPipelineShowcase_.completed())
    {
        footer << "COMPLETE - LEFT / RIGHT TO REVIEW";
    }
    else if (renderPipelineShowcase_.paused())
    {
        footer << "PAUSED - SPACE TO RESUME - LEFT / RIGHT TO STEP";
    }
    else
    {
        footer << std::fixed << std::setprecision(1)
               << renderPipelineShowcase_.secondsRemaining()
               << "s - SPACE TO PAUSE - LEFT / RIGHT TO STEP";
    }
    if (renderPipelineShowcaseStartedFromEditor_)
    {
        footer << " - F5 EDITOR";
    }
    ui::addDebugText(runtimeUiDebugFont_, runtimeUiDrawList_, footer.str(),
                     panelX + 20.0f, panelY + 132.0f, 14.0f,
                     ui::packColor(0.58f, 0.66f, 0.76f, 1.0f), fullScissor);
}
#endif
