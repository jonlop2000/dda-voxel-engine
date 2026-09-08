#include "UI/Panels/ProfilerOverlay.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include <imgui.h>

#include "engine/render/GpuProfiler.h"
#include "UI/EngineFacade.h"
#include "UI/Editor.h"

namespace ProfilerOverlay
{

namespace
{
float historyPeak(const std::vector<float>& history, float current)
{
    float peak = current;
    for (float sample : history)
    {
        peak = std::max(peak, sample);
    }
    return std::max(16.6f, peak * 1.15f);
}

void plotHistory(const char* label, const std::vector<float>& history, float currentMs)
{
    if (history.empty())
    {
        ImGui::TextDisabled("%s history is still warming up.", label);
        return;
    }

    const float maxMs = historyPeak(history, currentMs);
    ImGui::PlotLines(label, history.data(), static_cast<int>(history.size()), 0, nullptr, 0.0f,
                     maxMs, ImVec2(-1.0f, 56.0f));
}
} // namespace

void Draw(EngineFacade& engine)
{
    EditorState& editorState = engine.editorState();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec4 viewportRect = editorState.enabled
                                    ? Editor::GetViewportRect(editorState)
                                    : ImVec4(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);

    const float overlayWidth = editorState.profilerGraphVisible ? 360.0f : 300.0f;
    const ImVec2 overlayPos(viewportRect.x + viewportRect.z - overlayWidth - 12.0f,
                            viewportRect.y + 12.0f);

    ImGui::SetNextWindowPos(overlayPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2(overlayWidth, editorState.profilerGraphVisible ? 360.0f : 220.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 140.0f), ImVec2(640.0f, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.86f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing;

    if (!ImGui::Begin("Profiler Overlay", &editorState.profilerOverlayVisible, flags))
    {
        ImGui::End();
        return;
    }

    bool enableGpuProfiler = engine.gpuProfilerEnabled();
    if (ImGui::Checkbox("GPU", &enableGpuProfiler))
    {
        engine.setGpuProfilerEnabled(enableGpuProfiler);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Graphs", &editorState.profilerGraphVisible);
    ImGui::SameLine();
    ImGui::Checkbox("Passes", &editorState.profilerOverlayShowPasses);

    const float fps = engine.cpuFrameTimeMs() > 0.0f ? 1000.0f / engine.cpuFrameTimeMs() : 0.0f;
    ImGui::Text("CPU %.2f ms  |  %.1f FPS", engine.cpuFrameTimeMs(), fps);

    if (engine.gpuProfiler().isSupported())
    {
        if (engine.gpuProfiler().isEnabled())
        {
            ImGui::Text("GPU %.2f ms", engine.gpuFrameTimeMs());
        }
        else
        {
            ImGui::TextDisabled("GPU profiler disabled");
        }
    }
    else
    {
        ImGui::TextDisabled("GPU timestamps unsupported");
    }

    const float effectiveCap = engine.effectiveFpsLimit();
    if (effectiveCap > 0.0f)
    {
        ImGui::Text("%s  |  Cap %.0f FPS  |  %s", engine.activePresentModeLabel(), effectiveCap,
                    engine.isBackgroundThrottleActive() ? "Background throttle"
                                                        : (engine.isWindowFocused() ? "Focused"
                                                                                    : "Unfocused"));
    }
    else
    {
        ImGui::Text("%s  |  Uncapped  |  %s", engine.activePresentModeLabel(),
                    engine.isBackgroundThrottleActive() ? "Background throttle"
                                                        : (engine.isWindowFocused() ? "Focused"
                                                                                    : "Unfocused"));
    }

    if (editorState.profilerGraphVisible)
    {
        ImGui::Separator();
        plotHistory("CPU Frame ms", engine.cpuFrameHistoryMs(), engine.cpuFrameTimeMs());

        if (engine.gpuProfiler().isEnabled())
        {
            plotHistory("GPU Frame ms", engine.gpuFrameHistoryMs(), engine.gpuFrameTimeMs());
        }
    }

    if (editorState.profilerOverlayShowPasses && engine.gpuProfiler().isEnabled())
    {
        const auto& samples = engine.gpuProfiler().samples();
        if (!samples.empty())
        {
            std::vector<GpuProfilerSample> topSamples{};
            topSamples.reserve(samples.size());
            for (const auto& sample : samples)
            {
                if (sample.label != "Frame")
                {
                    topSamples.push_back(sample);
                }
            }

            std::sort(topSamples.begin(), topSamples.end(),
                      [](const GpuProfilerSample& lhs, const GpuProfilerSample& rhs) {
                          return lhs.ms > rhs.ms;
                      });

            if (!topSamples.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Top GPU Passes");
                const size_t count = std::min<size_t>(topSamples.size(), 6);
                for (size_t i = 0; i < count; ++i)
                {
                    ImGui::Text("%s", topSamples[i].label.c_str());
                    ImGui::SameLine(230.0f);
                    ImGui::Text("%.2f ms", topSamples[i].ms);
                }
            }
        }
    }

    ImGui::End();
}

} // namespace ProfilerOverlay
