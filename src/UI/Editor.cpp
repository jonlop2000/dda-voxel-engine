#include "UI/Editor.h"

#include <algorithm>

#include <imgui.h>

#include "UI/EditorState.h"
#include "UI/EngineFacade.h"
#include "UI/EditorWidgets.h"
#include "UI/ViewportTransformTool.h"
#include "UI/Panels/StatusBar.h"
#include "UI/Panels/ScenePanel.h"
#include "UI/Panels/RenderingPanel.h"
#include "UI/Panels/MenuBar.h"
#include "UI/Panels/FishPanel.h"

namespace Editor
{

namespace
{
constexpr float kStatusBarHeight = 24.0f;
constexpr float kMenuBarHeight = 22.0f;
constexpr float kMinPanelWidth = 220.0f;
constexpr float kMinViewportWidth = 420.0f;
} // namespace

void Init()
{
    // initialize all panels
    MenuBar::Init();
    StatusBar::Init();
    ScenePanel::Init();
    RenderingPanel::Init();
    FishPanel::Init();
}

void Shutdown()
{
    // cleanup if needed
    FishPanel::Shutdown();
    RenderingPanel::Shutdown();
    ScenePanel::Shutdown();
    StatusBar::Shutdown();
    MenuBar::Shutdown();
}

void Draw(EngineFacade& engine)
{
    EditorState& state = engine.editorState();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;

    // calculate positions based on visibility
    float topY = kMenuBarHeight;
    float bottomY = state.statusBarVisible ? (displaySize.y - kStatusBarHeight) : displaySize.y;
    float contentHeight = bottomY - topY;

    // Draw menu bar (always at top)
    MenuBar::Draw(engine);

    // Draw status bar at bottom
    if (state.statusBarVisible)
    {
        StatusBar::Draw(engine, ImVec2(0.0f, bottomY), ImVec2(displaySize.x, kStatusBarHeight));
    }

    // Draw left panel (scene)
    if (state.leftPanelVisible)
    {
        const float maxLeftWidth = std::max(
            kMinPanelWidth,
            displaySize.x - (state.rightPanelVisible ? state.rightPanelWidth : 0.0f) -
                kMinViewportWidth);
        state.leftPanelWidth = std::clamp(state.leftPanelWidth, kMinPanelWidth, maxLeftWidth);
        const ImVec2 actualSize =
            ScenePanel::Draw(engine, ImVec2(0.0f, topY),
                             ImVec2(state.leftPanelWidth, contentHeight));
        state.leftPanelWidth = std::clamp(actualSize.x, kMinPanelWidth, maxLeftWidth);
    }

    // Draw right panel (rendering - tabbed)
    if (state.rightPanelVisible)
    {
        const float maxRightWidth = std::max(
            kMinPanelWidth,
            displaySize.x - (state.leftPanelVisible ? state.leftPanelWidth : 0.0f) -
                kMinViewportWidth);
        state.rightPanelWidth = std::clamp(state.rightPanelWidth, kMinPanelWidth, maxRightWidth);
        float rightX = displaySize.x - state.rightPanelWidth;
        const ImVec2 actualSize =
            RenderingPanel::Draw(engine, ImVec2(rightX, topY),
                                 ImVec2(state.rightPanelWidth, contentHeight));
        state.rightPanelWidth = std::clamp(actualSize.x, kMinPanelWidth, maxRightWidth);
    }

    // floating dev panel for the fish-focus camera feature (view > fish panel)
    FishPanel::Draw(engine);

    // visible, editor-owned selection and snapped placeable transform tool.
    ViewportTransformTool::Draw(engine, GetViewportRect(state));
}

ImVec4 GetViewportRect(const EditorState& state)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;

    float left = state.leftPanelVisible ? state.leftPanelWidth : 0.0f;
    float right = state.rightPanelVisible ? (displaySize.x - state.rightPanelWidth) : displaySize.x;
    float top = kMenuBarHeight;
    float bottom = state.statusBarVisible ? (displaySize.y - kStatusBarHeight) : displaySize.y;

    return ImVec4(left, top, right - left, bottom - top);
}

} // namespace Editor
