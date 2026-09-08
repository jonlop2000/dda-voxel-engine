#pragma once

#include <imgui.h>

#include "engine/render/RenderSettings.h"

namespace ui::panels
{

// shared control body used by both editor water panels. the caller owns the
// surrounding scene-only header and propagates the returned dirty state.
inline bool DrawSunroofLivingWaterVfxControls(
    engine::render::WaterSettings& water)
{
    engine::render::SunroofLivingWaterVfxSettings& vfx =
        water.sunroofLivingWaterVfx_;
    bool changed = false;

    ImGui::PushID("SunroofLivingWaterVfx");
    changed |= ImGui::Checkbox("Enable Living Water VFX", &vfx.enabled);
    ImGui::SameLine();
    if (ImGui::Button("Reset V2.6 Defaults"))
    {
        vfx = {};
        changed = true;
    }

    ImGui::BeginDisabled(!vfx.enabled);

    ImGui::SeparatorText("Painted Sun Rays");
    changed |= ImGui::SliderFloat("Ray Intensity", &vfx.rayIntensity,
                                  0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Ray Softness", &vfx.raySoftness,
                                  0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Ray Warmth", &vfx.rayWarmth,
                                  0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Illuminated Motes",
                                  &vfx.illuminatedMoteIntensity,
                                  0.0f, 2.0f, "%.2f");

    ImGui::SeparatorText("Bubble Streams");
    changed |= ImGui::Checkbox("Enable Bubble Streams",
                               &vfx.bubbleStreamsEnabled);
    ImGui::BeginDisabled(!vfx.bubbleStreamsEnabled);
    changed |= ImGui::SliderFloat("Bubble Amount", &vfx.bubbleAmount,
                                  0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Bubble Size", &vfx.bubbleSize,
                                  0.5f, 1.75f, "%.2f");
    changed |= ImGui::SliderFloat("Bubble Rise Speed", &vfx.bubbleRiseSpeed,
                                  0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Bubble Drift", &vfx.bubbleDrift,
                                  0.0f, 2.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Hero Foliage Motion");
    changed |= ImGui::Checkbox("Enable Hero Foliage Motion",
                               &vfx.heroFoliageMotionEnabled);
    ImGui::BeginDisabled(!vfx.heroFoliageMotionEnabled);
    changed |= ImGui::SliderFloat("Hero Sway Strength",
                                  &vfx.heroFoliageSwayStrength,
                                  0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Hero Motion Speed",
                                  &vfx.heroFoliageMotionSpeed,
                                  0.25f, 2.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "Sunroof only. Bubble Amount selects within the validated 27-cube ceiling.");
    ImGui::PopID();
    return changed;
}

} // namespace ui::panels
