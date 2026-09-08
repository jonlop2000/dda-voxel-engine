#include "UI/EditorTheme.h"

#include <imgui.h>

namespace EditorTheme
{

void Apply()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // =========================================================================
    // high contrast ocean theme
    // cohesive slate base with cyan/indigo/mint accents
    // =========================================================================

    // --- backgrounds (dark slate, less harsh than pure black) ---
    colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.040f, 0.048f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.040f, 0.046f, 0.058f, 0.92f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.050f, 0.056f, 0.070f, 0.98f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.028f, 0.033f, 0.042f, 1.00f);

    // --- text ---
    colors[ImGuiCol_Text] = ImVec4(0.940f, 0.970f, 1.000f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.550f, 0.600f, 0.680f, 1.00f);

    // --- borders ---
    colors[ImGuiCol_Border] = ImVec4(0.190f, 0.720f, 0.860f, 0.55f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // --- frame backgrounds ---
    colors[ImGuiCol_FrameBg] = ImVec4(0.090f, 0.105f, 0.135f, 0.85f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.120f, 0.145f, 0.185f, 0.95f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.150f, 0.185f, 0.235f, 1.00f);

    // --- title bars ---
    colors[ImGuiCol_TitleBg] = ImVec4(0.040f, 0.048f, 0.064f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.180f, 0.240f, 0.420f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.030f, 0.036f, 0.048f, 0.85f);

    // --- accent colors ---
    colors[ImGuiCol_CheckMark] = ImVec4(0.360f, 0.880f, 0.680f, 1.00f);       // mint
    colors[ImGuiCol_SliderGrab] = ImVec4(0.360f, 0.880f, 0.680f, 1.00f);      // mint
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.500f, 0.950f, 0.760f, 1.00f);

    // --- buttons ---
    colors[ImGuiCol_Button] = ImVec4(0.110f, 0.300f, 0.400f, 0.92f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.160f, 0.430f, 0.560f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.280f, 0.540f, 0.700f, 1.00f);

    // --- headers ---
    colors[ImGuiCol_Header] = ImVec4(0.150f, 0.220f, 0.380f, 0.78f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.220f, 0.320f, 0.530f, 0.92f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.300f, 0.430f, 0.700f, 1.00f);

    // --- scrollbar ---
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.030f, 0.036f, 0.048f, 0.85f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.180f, 0.280f, 0.460f, 0.95f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.390f, 0.620f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.340f, 0.520f, 0.760f, 1.00f);

    // --- tabs ---
    colors[ImGuiCol_Tab] = ImVec4(0.060f, 0.080f, 0.120f, 0.95f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.140f, 0.320f, 0.450f, 0.95f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.220f, 0.330f, 0.560f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.300f, 0.760f, 0.900f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.045f, 0.060f, 0.090f, 0.95f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.160f, 0.240f, 0.420f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.220f, 0.620f, 0.760f, 1.00f);

    // --- separators ---
    colors[ImGuiCol_Separator] = ImVec4(0.200f, 0.680f, 0.820f, 0.55f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.280f, 0.760f, 0.900f, 0.95f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.360f, 0.880f, 0.680f, 1.00f);

    // --- resize grips ---
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.250f, 0.450f, 0.700f, 0.45f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.340f, 0.580f, 0.840f, 0.80f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.460f, 0.700f, 0.950f, 1.00f);

    // --- selection ---
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.180f, 0.420f, 0.620f, 0.45f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.360f, 0.880f, 0.680f, 1.00f);
    colors[ImGuiCol_NavCursor] = ImVec4(0.300f, 0.760f, 0.900f, 1.00f);

    // --- tables ---
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.085f, 0.110f, 0.160f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.200f, 0.500f, 0.680f, 0.70f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.180f, 0.380f, 0.520f, 0.45f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.075f, 0.095f, 0.130f, 0.40f);

    // --- plots ---
    colors[ImGuiCol_PlotLines] = ImVec4(0.300f, 0.760f, 0.900f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.440f, 0.620f, 0.980f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.360f, 0.880f, 0.680f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.540f, 0.940f, 0.780f, 1.00f);

    // --- modal ---
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.72f);

    // =========================================================================
    // style parameters
    // =========================================================================

    // --- rounding (modern soft corners) ---
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    // --- padding (comfortable spacing) ---
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;

    // --- borders ---
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 1.0f;

    // --- scrollbar ---
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    // --- misc ---
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
}

void ApplyDark()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    // Apply some modern styling to the default dark theme
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 3.0f;

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
}

} // namespace EditorTheme
