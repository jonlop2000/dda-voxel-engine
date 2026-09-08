#include "UI/EditorWidgets.h"

#include <cstdarg>
#include <cstdio>

namespace EditorWidgets
{

namespace
{
// theme colors
constexpr ImVec4 kCyanAccent = ImVec4(0.300f, 0.760f, 0.900f, 1.00f);
constexpr ImVec4 kCyanDim = ImVec4(0.220f, 0.620f, 0.760f, 1.00f);
constexpr ImVec4 kIndigoAccent = ImVec4(0.320f, 0.460f, 0.740f, 1.00f);
constexpr ImVec4 kWarningColor = ImVec4(1.000f, 0.700f, 0.200f, 1.00f);
constexpr ImVec4 kErrorColor = ImVec4(1.000f, 0.350f, 0.300f, 1.00f);
constexpr ImVec4 kSuccessColor = ImVec4(0.300f, 0.900f, 0.400f, 1.00f);
constexpr ImVec4 kDimText = ImVec4(0.500f, 0.530f, 0.570f, 1.00f);
constexpr ImVec4 kGroupBoxBg = ImVec4(0.070f, 0.085f, 0.120f, 0.62f);
} // namespace

void SectionHeader(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kCyanAccent);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
}

bool SectionHeaderCollapsible(const char* label, bool defaultOpen)
{
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.150f, 0.220f, 0.380f, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.220f, 0.320f, 0.530f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.300f, 0.430f, 0.700f, 1.00f));

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_AllowOverlap;
    if (defaultOpen)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(3);

    return open;
}

bool SliderWithReset(const char* label, float* value, float min, float max, float resetValue,
                     const char* format)
{
    bool changed = false;

    ImGui::PushID(label);

    // slider takes most of the width
    float buttonWidth = 50.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - buttonWidth - spacing);

    if (ImGui::SliderFloat("##slider", value, min, max, format))
    {
        changed = true;
    }

    ImGui::SameLine();

    // reset button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.150f, 0.220f, 0.380f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.220f, 0.320f, 0.530f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.300f, 0.430f, 0.700f, 1.00f));

    if (ImGui::Button("Reset", ImVec2(buttonWidth, 0)))
    {
        *value = resetValue;
        changed = true;
    }

    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::Text("%s", label);

    ImGui::PopID();

    return changed;
}

bool SliderIntWithReset(const char* label, int* value, int min, int max, int resetValue)
{
    bool changed = false;

    ImGui::PushID(label);

    float buttonWidth = 50.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - buttonWidth - spacing);

    if (ImGui::SliderInt("##slider", value, min, max))
    {
        changed = true;
    }

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.150f, 0.220f, 0.380f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.220f, 0.320f, 0.530f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.300f, 0.430f, 0.700f, 1.00f));

    if (ImGui::Button("Reset", ImVec2(buttonWidth, 0)))
    {
        *value = resetValue;
        changed = true;
    }

    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::Text("%s", label);

    ImGui::PopID();

    return changed;
}

void ValueLabel(const char* label, const char* format, ...)
{
    ImGui::Text("%s:", label);
    ImGui::SameLine();

    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ImGui::TextColored(kCyanAccent, "%s", buffer);
}

void ValueLabelWarning(const char* label, float value, float warningThreshold, const char* format)
{
    ImGui::Text("%s:", label);
    ImGui::SameLine();

    if (value > warningThreshold)
    {
        ImGui::TextColored(kWarningColor, format, value);
    }
    else
    {
        ImGui::TextColored(kCyanAccent, format, value);
    }
}

void HelpMarker(const char* description)
{
    ImGui::SameLine();
    ImGui::TextColored(kDimText, "(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void TextCyan(const char* text)
{
    ImGui::TextColored(kCyanAccent, "%s", text);
}

void TextWarning(const char* text)
{
    ImGui::TextColored(kWarningColor, "%s", text);
}

void TextError(const char* text)
{
    ImGui::TextColored(kErrorColor, "%s", text);
}

void TextSuccess(const char* text)
{
    ImGui::TextColored(kSuccessColor, "%s", text);
}

bool CompactButton(const char* label)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleVar();
    return pressed;
}

bool ToggleButton(const char* label, bool* active)
{
    bool changed = false;

    if (*active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.180f, 0.430f, 0.340f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.260f, 0.560f, 0.450f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.360f, 0.720f, 0.580f, 1.00f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.080f, 0.100f, 0.140f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.100f, 0.150f, 0.200f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.120f, 0.180f, 0.240f, 1.00f));
    }

    if (ImGui::Button(label))
    {
        *active = !*active;
        changed = true;
    }

    ImGui::PopStyleColor(3);

    return changed;
}

void Separator()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void SeparatorWithLabel(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kCyanDim);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
}

void Spacing(int lines)
{
    for (int i = 0; i < lines; ++i)
    {
        ImGui::Spacing();
    }
}

void BeginGroupBox(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kGroupBoxBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    if (label && label[0] != '\0')
    {
        ImGui::TextColored(kIndigoAccent, "%s", label);
    }

    ImGui::BeginChild(label, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
}

void EndGroupBox()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void KeyboardHint(const char* hint)
{
    ImGui::SameLine();
    ImGui::TextColored(kDimText, "(%s)", hint);
}

void PropertyLabel(const char* label, float labelWidth)
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
}

} // namespace EditorWidgets
