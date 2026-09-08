#include "UI/Panels/StatusBar.h"

#include <imgui.h>

#include "engine/render/GpuProfiler.h"
#include "engine/scene/SceneConfig.h"
#include "UI/EngineFacade.h"
#include "UI/EditorWidgets.h"

namespace StatusBar
{

namespace
{
// colors
constexpr ImVec4 kStatusBarBg = ImVec4(0.035f, 0.040f, 0.055f, 1.00f);
constexpr ImVec4 kCyanAccent = ImVec4(0.000f, 0.900f, 1.000f, 1.00f);
constexpr ImVec4 kDimText = ImVec4(0.550f, 0.580f, 0.620f, 1.00f);
constexpr ImVec4 kWarningColor = ImVec4(1.000f, 0.700f, 0.200f, 1.00f);
} // namespace

void Init()
{
    // nothing to initialize
}

void Shutdown()
{
    // nothing to clean up
}

void Draw(EngineFacade& engine, ImVec2 position, ImVec2 size)
{
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, kStatusBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##StatusBar", nullptr, flags))
    {
        // get stats from app (we'll need to add accessor methods)
        // for now, use ImGui's internal framerate
        float fps = ImGui::GetIO().Framerate;
        float frameTimeMs = 1000.0f / fps;

        // fps display
        ImGui::TextColored(kDimText, "FPS:");
        ImGui::SameLine(0.0f, 4.0f);

        if (fps < 30.0f)
        {
            ImGui::TextColored(kWarningColor, "%.1f", fps);
        }
        else
        {
            ImGui::TextColored(kCyanAccent, "%.1f", fps);
        }

        // frame time
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::TextColored(kDimText, "Frame:");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(kCyanAccent, "%.2fms", frameTimeMs);

        // gpu time (if profiler enabled)
        if (engine.gpuProfiler().isEnabled())
        {
            ImGui::SameLine(0.0f, 20.0f);
            ImGui::TextColored(kDimText, "GPU:");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(kCyanAccent, "%.2fms", engine.gpuProfiler().totalMs());
        }

        // separator
        ImGui::SameLine(0.0f, 30.0f);
        ImGui::TextColored(ImVec4(0.3f, 0.4f, 0.5f, 1.0f), "|");

        // scene name from app
        ImGui::SameLine(0.0f, 30.0f);
        ImGui::TextColored(kDimText, "Scene:");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(kCyanAccent, "%s", engine.sceneConfig().name.c_str());

        // right-aligned items
        float rightItemsWidth = 150.0f;
        ImGui::SameLine(size.x - rightItemsWidth - 20.0f);
        ImGui::TextColored(kDimText, "Voxel Aquarium Engine");
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace StatusBar
