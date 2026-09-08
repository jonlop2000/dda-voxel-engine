#include "UI/Panels/FishPanel.h"

#include <cinttypes>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "UI/EngineFacade.h"
#include "UI/EditorWidgets.h"

namespace FishPanel
{

void Init()
{
    // nothing to initialize
}

void Shutdown()
{
    // nothing to clean up
}

void Draw(EngineFacade& engine)
{
    EditorState& state = engine.editorState();
    if (!state.fishPanelVisible)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(300.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 360.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Fish", &state.fishPanelVisible))
    {
        const std::vector<EngineFacade::FishHandle> fish = engine.fishList();
        const uint64_t selectedId = engine.selectedFishId();

        EditorWidgets::SectionHeader("Fish Focus");
        ImGui::Text("Live fish: %d", static_cast<int>(fish.size()));
        if (ImGui::Button("Clear Selection"))
        {
            engine.clearFishSelection();
        }
        ImGui::Separator();

        const EngineFacade::FishHandle* selectedHandle = nullptr;
        for (const EngineFacade::FishHandle& handle : fish)
        {
            const bool isSelected = handle.id == selectedId;
            if (isSelected)
            {
                selectedHandle = &handle;
            }

            char label[80];
            std::snprintf(label, sizeof(label), "Fish %" PRIu64 " (palette %d)##fish_%" PRIu64,
                          handle.id, static_cast<int>(handle.paletteIndex), handle.id);
            if (ImGui::Selectable(label, isSelected))
            {
                if (isSelected)
                {
                    engine.clearFishSelection();
                }
                else
                {
                    engine.selectFish(handle.id);
                }
            }
        }
        if (fish.empty())
        {
            ImGui::TextDisabled("No procedural fish in the current scene.");
        }

        ImGui::Separator();
        if (selectedHandle != nullptr)
        {
            ImGui::Text("Selected: Fish %" PRIu64, selectedHandle->id);
            ImGui::Text("World pos: %.2f, %.2f, %.2f", selectedHandle->worldPos.x,
                        selectedHandle->worldPos.y, selectedHandle->worldPos.z);
            ImGui::Text("Scale: %.2f", selectedHandle->scale);
        }
        else
        {
            ImGui::TextDisabled("No fish selected.");
        }

        EditorWidgets::SectionHeader("Cinematic Focus");
        const bool focusActive = engine.isFishFocusActive();
        if (focusActive)
        {
            if (engine.isFishFocusReturning())
            {
                ImGui::Text("Returning to free camera...");
            }
            else
            {
                ImGui::Text("Focused: Fish %" PRIu64, engine.focusedFishId());
            }
            if (ImGui::Button("Release Focus (Esc)"))
            {
                engine.releaseFishFocus();
            }
        }
        else if (selectedHandle != nullptr)
        {
            if (ImGui::Button("Focus Selected Fish"))
            {
                engine.focusOnFish(selectedHandle->id);
            }
        }
        else
        {
            ImGui::TextDisabled("Select a fish to enable focus.");
        }

        bool celebrationEnabled = engine.fishCelebrationEnabled();
        if (ImGui::Checkbox("Viva Mexico on focus", &celebrationEnabled))
        {
            engine.setFishCelebrationEnabled(celebrationEnabled);
        }

        bool bellyFloatEnabled = engine.axolotlBellyFloatEnabled();
        if (ImGui::Checkbox("Axolotl belly float on focus", &bellyFloatEnabled))
        {
            engine.setAxolotlBellyFloatEnabled(bellyFloatEnabled);
        }

        bool axolotlEnabled = engine.axolotlEnabled();
        if (ImGui::Checkbox("Axolotl", &axolotlEnabled))
        {
            engine.setAxolotlEnabled(axolotlEnabled);
        }

        EditorWidgets::SectionHeader("Framing");
        float orbitDistance = engine.fishFocusOrbitDistance();
        if (ImGui::SliderFloat("Distance", &orbitDistance, 1.0f, 20.0f, "%.1f"))
        {
            engine.setFishFocusOrbitDistance(orbitDistance);
        }
        float heightOffset = engine.fishFocusHeightOffset();
        if (ImGui::SliderFloat("Height", &heightOffset, -5.0f, 8.0f, "%.1f"))
        {
            engine.setFishFocusHeightOffset(heightOffset);
        }
        float positionDamping = engine.fishFocusPositionDamping();
        if (ImGui::SliderFloat("Move Damping", &positionDamping, 0.5f, 12.0f, "%.1f"))
        {
            engine.setFishFocusPositionDamping(positionDamping);
        }
        float rotationDamping = engine.fishFocusRotationDamping();
        if (ImGui::SliderFloat("Look Damping", &rotationDamping, 0.5f, 16.0f, "%.1f"))
        {
            engine.setFishFocusRotationDamping(rotationDamping);
        }

        EditorWidgets::SectionHeader("Handheld Shake");
        bool shakeEnabled = engine.fishShakeEnabled();
        if (ImGui::Checkbox("Enable Shake", &shakeEnabled))
        {
            engine.setFishShakeEnabled(shakeEnabled);
        }
        float shakePosAmplitude = engine.fishShakePositionAmplitude();
        if (ImGui::SliderFloat("Sway Amount", &shakePosAmplitude, 0.0f, 0.3f, "%.3f"))
        {
            engine.setFishShakePositionAmplitude(shakePosAmplitude);
        }
        float shakeRotDegrees = engine.fishShakeRotationAmplitude() * 57.2957795f;
        if (ImGui::SliderFloat("Wobble (deg)", &shakeRotDegrees, 0.0f, 2.0f, "%.2f"))
        {
            engine.setFishShakeRotationAmplitude(shakeRotDegrees / 57.2957795f);
        }
        float shakeFrequency = engine.fishShakeFrequency();
        if (ImGui::SliderFloat("Frequency (Hz)", &shakeFrequency, 0.1f, 3.0f, "%.2f"))
        {
            engine.setFishShakeFrequency(shakeFrequency);
        }
        float shakeSettleDecay = engine.fishShakeSettleDecay();
        if (ImGui::SliderFloat("Settle Decay", &shakeSettleDecay, 0.0f, 3.0f, "%.2f"))
        {
            engine.setFishShakeSettleDecay(shakeSettleDecay);
        }
    }
    ImGui::End();
}

} // namespace FishPanel
