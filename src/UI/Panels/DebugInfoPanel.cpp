#include "UI/Panels/DebugInfoPanel.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "Water/WaterPresets.h"
#include "UI/Panels/SunroofLivingWaterControls.h"
#include "engine/render/GpuProfiler.h"
#include "engine/render/passes/OBBPass.h"
#include "engine/scene/AquariumScene.h"
#include "engine/voxel/ProceduralWorldSettings.h"
#include "engine/voxel/VoxelWorld.h"
#include "engine/voxel/WaterVolume.h"

namespace DebugInfoPanel
{

SceneConfigurationRequests DrawSceneConfiguration(
    const SceneConfig& config,
    const std::filesystem::path& currentScenePath,
    const std::vector<SceneCatalogEntry>& availableScenes)
{
    SceneConfigurationRequests requests{};
    if (!ImGui::CollapsingHeader("Scene Configuration"))
    {
        return requests;
    }
    ImGui::Indent();

    ImGui::Text("Current Scene: %s", config.name.c_str());
    if (!config.description.empty())
    {
        ImGui::TextDisabled("%s", config.description.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Refresh Scene Catalog"))
    {
        requests.refreshCatalog = true;
    }

    const std::string currentSceneLabel =
        currentScenePath.empty() ? config.name : currentScenePath.stem().string();
    if (ImGui::BeginCombo("Authored Scene", currentSceneLabel.c_str()))
    {
        const std::filesystem::path activeScenePath = currentScenePath.lexically_normal();
        for (const SceneCatalogEntry& scene : availableScenes)
        {
            const bool isCurrent =
                !activeScenePath.empty() && scene.path.lexically_normal() == activeScenePath;
            if (scene.valid)
            {
                if (ImGui::Selectable(scene.displayName.c_str(), isCurrent))
                {
                    requests.loadScenePath = scene.path;
                }
            }
            else
            {
                const std::string invalidLabel = scene.displayName + " (invalid)";
                ImGui::BeginDisabled();
                ImGui::Selectable(invalidLabel.c_str(), false);
                ImGui::EndDisabled();
            }

            if (ImGui::IsItemHovered())
            {
                if (!scene.valid && !scene.errorMessage.empty())
                {
                    ImGui::SetTooltip("%s", scene.errorMessage.c_str());
                }
                else if (!scene.description.empty())
                {
                    ImGui::SetTooltip("%s", scene.description.c_str());
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Text("Content Flags:");

    // use temp copy to detect changes
    SceneConfig tempConfig = config;
    bool changed = false;

    changed |= ImGui::Checkbox("Test Floor", &tempConfig.loadTestFloor);
    changed |= ImGui::Checkbox("Voxel World (Greedy Mesh)", &tempConfig.loadVoxelWorld);
    ImGui::Checkbox("OBB Volumes", &tempConfig.loadOBBVolumes);
    ImGui::SameLine();
    ImGui::TextDisabled("(restart required)");
    changed |= ImGui::Checkbox("Glass Panel", &tempConfig.loadGlassPanel);
    if (tempConfig.loadAquariumTest)
    {
        changed |= ImGui::Checkbox("Use Mesh Tank Glass", &tempConfig.useMeshTankGlass);
    }

    if (changed)
    {
        requests.reloadConfig = tempConfig;
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Current Scene"))
    {
        requests.saveCurrentScene = true;
    }

    ImGui::Unindent();
    return requests;
}

WaterPanelRequests DrawWater(engine::render::WaterSettings& water,
                             engine::WaterVolumeManager& volumes,
                             const WaterPanelState& state)
{
    WaterPanelRequests requests{};
    if (!ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return requests;
    }
    ImGui::Indent();

    bool waterEnabled = state.waterEnabled;
    if (ImGui::Checkbox("Enable Water (F6)", &waterEnabled))
    {
        requests.waterEnabled = waterEnabled;
    }
    bool useWaterV2 = water.useWaterV2_;
    if (ImGui::Checkbox("Use Water V2", &useWaterV2))
    {
        requests.useWaterV2 = useWaterV2;
    }
    ImGui::TextDisabled("A/B toggle between legacy WaterPass and Water V2 surface path.");
    ImGui::Text("Active Path: %s", useWaterV2 ? "Water V2" : "Legacy Water");

    if (ImGui::SliderFloat("Water Level", &water.waterLevel_, 0.0f, 40.0f))
    {
        auto& waterVolumes = volumes.volumes();
        if (!waterVolumes.empty())
        {
            waterVolumes[0].surfaceHeight = water.waterLevel_;
            volumes.markDirty();
        }
    }
    if (water.useWaterV2_ && !volumes.volumes().empty())
    {
        auto& waterVolumes = volumes.volumes();
        const float bakedVoxelTop = waterVolumes[0].boundsMax.y;
        ImGui::TextDisabled("Voxel water top: %.2f", bakedVoxelTop);
        if (ImGui::Button("Match Water Level To Voxel Top"))
        {
            water.waterLevel_ = bakedVoxelTop;
            waterVolumes[0].surfaceHeight = water.waterLevel_;
            volumes.markDirty();
            requests.markParametersDirty = true;
        }
        if (std::abs(water.waterLevel_ - bakedVoxelTop) > 0.25f)
        {
            ImGui::TextWrapped(
                "Delta debug compares analytic water against baked voxel water. "
                "Waterline color is expected when Water Level differs from voxel top.");
        }
    }
    bool waterParametersChanged = false;
    ImGui::Checkbox("Stylized Water", &water.waterStylizedMode_);
    waterParametersChanged |=
        ImGui::SliderFloat("Band Hardness", &water.waterBandHardness_, 0.0f, 1.0f,
                           "%.2f (0=smooth, 1=cel)");
    waterParametersChanged |=
        ImGui::SliderFloat("Reflection Strength", &water.waterReflectionStrength_, 0.0f, 1.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Fresnel Bias", &water.waterFresnelBias_, 0.0f, 0.5f);
    waterParametersChanged |=
        ImGui::SliderFloat("Refraction Strength", &water.waterRefract_, 0.0f, 0.06f);
    waterParametersChanged |=
        ImGui::SliderFloat("Distortion Depth Scale", &water.waterDistortionDepthScale_, 0.0f, 0.6f);
    waterParametersChanged |=
        ImGui::SliderFloat("Wave Scale", &water.waterWaveScale_, 0.02f, 0.30f);
    waterParametersChanged |=
        ImGui::SliderFloat("Wave Amplitude", &water.waterWaveAmp_, 0.0f, 1.2f);
    waterParametersChanged |=
        ImGui::Checkbox("Enable Crest Highlights", &water.waterCrestHighlightsEnabled_);
    waterParametersChanged |=
        ImGui::SliderFloat("Crest Threshold", &water.waterCrestThreshold_, 0.0f, 1.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Crest Softness", &water.waterCrestSoftness_, 0.01f, 0.35f);
    waterParametersChanged |=
        ImGui::SliderFloat("Crest Intensity", &water.waterCrestIntensity_, 0.0f, 1.5f);
    ImGui::TextDisabled("Stylized toon crest bands near wave peaks (non-foam).");
    waterParametersChanged |=
        ImGui::SliderFloat("Specular Power", &water.waterSpecPower_, 64.0f, 640.0f, "%.0f");
    waterParametersChanged |=
        ImGui::SliderFloat("Spec Intensity", &water.waterSpecIntensity_, 0.1f, 4.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Edge Fade Depth", &water.waterEdgeFadeDepth_, 0.05f, 2.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Gradient Strength", &water.waterGradientStrength_, 0.0f, 1.0f);

    ImGui::Separator();
    waterParametersChanged |=
        ImGui::Checkbox("Enable Caustics", &water.waterCausticsEnabled_);
    waterParametersChanged |=
        ImGui::SliderFloat("Caustics Intensity", &water.waterCausticsIntensity_, 0.0f, 4.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Caustics Scale", &water.waterCausticsScale_, 0.02f, 1.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Caustics Speed", &water.waterCausticsSpeed_, 0.0f, 3.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Caustics Banding", &water.waterCausticsBanding_, 0.0f, 1.0f);
    waterParametersChanged |=
        ImGui::SliderFloat("Caustics Depth Fade", &water.waterCausticsDepthFade_, 1.0f, 40.0f);
    ImGui::TextDisabled(
        "Caustics work in both stylized and non-stylized water. Lower banding for a smoother look.");
    ImGui::TextDisabled("Tip: use Lighting Debug -> Caustics to isolate this contribution.");

    if (ImGui::TreeNode("Voxel Water VFX"))
    {
        ImGui::TextWrapped(
            "First slice: sparse voxel-like suspended motes inside Water V2 volumes.");
        waterParametersChanged |=
            ImGui::Checkbox("Enable Voxel Particles", &water.waterParticlesPlanned_);
        waterParametersChanged |=
            ImGui::SliderFloat("Particle Density", &water.waterParticlesPlannedDensity_, 0.0f, 1.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Particle Drift", &water.waterParticlesPlannedDrift_, 0.0f, 1.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Particle Scale", &water.waterParticlesPlannedScale_, 0.0f, 1.0f);
        ImGui::SeparatorText("Localized Foam Emitter");
        ImGui::TextWrapped(
            "Slice 2 prototype: authored voxel foam/bubble cluster around one emitter.");
        waterParametersChanged |=
            ImGui::Checkbox("Enable Foam Emitter", &water.waterFoamEmitterEnabled_);
        waterParametersChanged |=
            ImGui::SliderFloat("Foam Intensity", &water.waterFoamEmitterIntensity_, 0.0f, 1.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Foam Radius", &water.waterFoamEmitterRadius_, 0.25f, 12.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Foam Offset X", &water.waterFoamEmitterOffsetX_, -1.0f, 1.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Foam Offset Z", &water.waterFoamEmitterOffsetZ_, -1.0f, 1.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Foam Voxel Scale", &water.waterFoamEmitterScale_, 0.0f, 1.0f);
        waterParametersChanged |=
            ImGui::SliderFloat("Foam Spread", &water.waterFoamEmitterSpread_, 0.0f, 1.0f);
        ImGui::TextDisabled(
            "Shader-side VFX layers; use Water Debug -> Voxel VFX or Voxel Foam to isolate them.");
        ImGui::TreePop();
    }

    if (state.isSunroofScene && ImGui::TreeNode("Sunroof Living Water VFX"))
    {
        waterParametersChanged |=
            ui::panels::DrawSunroofLivingWaterVfxControls(water);
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Checkbox("Enable Planar Reflection (Spike)", &water.waterPlanarReflectionEnabled_);
    ImGui::Checkbox("Use Oblique Clip Plane", &water.waterPlanarObliqueClipEnabled_);
    waterParametersChanged |=
        ImGui::SliderFloat("Planar Reflection Strength", &water.waterPlanarStrength_, 0.0f, 1.0f);
    ImGui::TextWrapped("Test toggle: when disabled, planar reflection uses the mirrored camera without the oblique water-plane clip.");
    if (waterParametersChanged)
    {
        requests.markParametersDirty = true;
    }

    requests.waterDebugMode = DrawWaterDebugMode(state.waterDebugMode);

    ImGui::Separator();
    if (ImGui::TreeNode("Underwater Absorption (Day 18)"))
    {
        auto& waterVolumes = volumes.volumes();
        ImGui::Text("Active volumes: %u", volumes.count());
        if (water.waterStylizedMode_)
        {
            ImGui::TextDisabled(
                "Absorption RGB/Deep Color are ignored in Stylized mode (depth bands drive color).");
        }

        if (waterVolumes.empty())
        {
            if (ImGui::Button("Create Default Water Volume"))
            {
                requests.createDefaultVolume = true;
            }
        }

        for (size_t i = 0; i < waterVolumes.size(); ++i)
        {
            auto& vol = waterVolumes[i];
            bool changed = false;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Volume %zu", i);
            changed |= ImGui::DragFloat3("Bounds Min", &vol.boundsMin.x, 0.25f);
            changed |= ImGui::DragFloat3("Bounds Max", &vol.boundsMax.x, 0.25f);
            changed |= ImGui::DragFloat("Surface Height", &vol.surfaceHeight, 0.05f);
            if (state.isSunroofScene)
            {
                float clarity =
                    estimateWaterPresetBlend(vol, kSunroofBaseWaterPreset,
                                             kSunroofAeroWaterPreset);
                if (ImGui::SliderFloat("Water Clarity", &clarity, 0.0f, 1.0f, "%.2f"))
                {
                    applyWaterPreset(vol,
                                     blendWaterPreset(kSunroofBaseWaterPreset,
                                                      kSunroofAeroWaterPreset, clarity));
                    volumes.markDirty();
                }
                ImGui::TextDisabled("0 = deeper tint, 1 = airy aero-clear water.");
            }
            changed |=
                ImGui::SliderFloat3("Absorption RGB", &vol.absorptionCoeff.x, 0.0f, 1.0f);
            changed |= ImGui::ColorEdit3("Deep Color", &vol.deepColor.x);
            changed |= ImGui::SliderFloat("Fog Density", &vol.fogDensity, 0.01f, 0.3f);

            if (changed)
            {
                volumes.markDirty();
            }

            if (ImGui::BeginCombo("Preset", "Apply..."))
            {
                for (const auto& preset : kWaterPresets)
                {
                    if (ImGui::Selectable(preset.name))
                    {
                        vol.absorptionCoeff = preset.absorption;
                        vol.deepColor = preset.deepColor;
                        vol.fogDensity = preset.fogDensity;
                        volumes.markDirty();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }

        ImGui::TreePop();
    }

    ImGui::Unindent();
    return requests;
}

namespace
{

void drawVolumeVisibilityRows(engine::VoxelWorld& world, const char* fallbackPrefix)
{
    for (size_t i = 0; i < world.instances().size(); ++i)
    {
        auto& inst = world.instances()[i];
        const glm::ivec3 dims = inst.volume.dimensions();
        const std::string& name = inst.volume.debugName();
        std::string label = name.empty()
                                ? (std::string(fallbackPrefix) + " " + std::to_string(i))
                                : name;
        bool visible = inst.visible;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Checkbox(label.c_str(), &visible))
        {
            world.setVolumeVisible(static_cast<uint32_t>(i), visible);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%dx%dx%d)", dims.x, dims.y, dims.z);
        ImGui::PopID();
    }
}

} // namespace

VoxelWorldPanelRequests DrawVoxelWorld(
    engine::render::VoxelDebugSettings& voxelDebug,
    engine::render::DiagnosticsSettings& diagnostics,
    engine::VoxelWorld& world,
    const engine::DDAMetrics& metrics,
    const std::vector<DdaMetricsSamplePoint>& metricsHistory,
    const VoxelWorldPanelState& state)
{
    VoxelWorldPanelRequests requests{};
    if (!ImGui::CollapsingHeader("Voxel World", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return requests;
    }
    ImGui::Indent();

    bool voxelWorldEnabled = state.voxelWorldEnabled;
    if (ImGui::Checkbox("Show Voxel World (F1)", &voxelWorldEnabled))
    {
        requests.toggleVoxelWorld = true;
    }
    bool fishEnabled = voxelDebug.proceduralFishEnabled_;
    if (ImGui::Checkbox("Enable Procedural Fish", &fishEnabled))
    {
        requests.proceduralFishEnabled = fishEnabled;
    }
    int fishCount = voxelDebug.proceduralFishCount_;
    if (!fishEnabled)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderInt("Fish Count", &fishCount, 0, state.maxFishCount))
    {
        requests.proceduralFishCount = fishCount;
    }
    if (!fishEnabled)
    {
        ImGui::EndDisabled();
    }

    if (ImGui::Button("Regenerate World (F2)"))
    {
        requests.regenerateWorld = true;
    }

    bool chunkBounds = state.chunkBoundsEnabled;
    if (ImGui::Checkbox("Show Chunk Bounds (F3)", &chunkBounds))
    {
        requests.toggleChunkBounds = true;
    }
    bool volumeBounds = state.volumeBoundsEnabled;
    if (ImGui::Checkbox("Show Volume Bounds (F12)", &volumeBounds))
    {
        requests.toggleVolumeBounds = true;
    }

    bool meshingFrozen = state.meshingFrozen;
    if (ImGui::Checkbox("Freeze Meshing (F4)", &meshingFrozen))
    {
        requests.toggleMeshingFrozen = true;
    }

    bool editMode = state.editModeEnabled;
    if (ImGui::Checkbox("Edit Mode (F5)", &editMode))
    {
        requests.toggleEditMode = true;
    }

    if (editMode)
    {
        ImGui::Indent();
        if (state.placeableEditHints)
        {
            ImGui::TextDisabled("Left Click: Remove foliage");
            ImGui::TextDisabled("Right Click: Place foliage");
        }
        else
        {
            ImGui::TextDisabled("Left Click: Remove block");
            ImGui::TextDisabled("Right Click: Place block");
            ImGui::TextDisabled("1-6: Select block type");
        }
        ImGui::Unindent();
    }

    if (state.showVolumeBSlider && world.instances().size() > 1)
    {
        glm::vec3 volumeBPos = world.instancePosition(1);
        if (ImGui::SliderFloat3("Volume B Position", &volumeBPos.x, -64.0f, 64.0f))
        {
            world.setInstancePosition(1, volumeBPos);
        }
    }

    DrawDdaDebugModes(voxelDebug);
    if (state.showSkipControls && !world.instances().empty())
    {
        ImGui::Checkbox("Enable Empty Skip", &voxelDebug.voxelDdaSkipEnabled_);

        const glm::ivec3 dims = world.instances()[0].volume.dimensions();
        int maxMip = 0;
        int maxDim = std::max(dims.x, std::max(dims.y, dims.z));
        while ((1 << (maxMip + 1)) <= maxDim)
        {
            ++maxMip;
        }
        voxelDebug.voxelDdaSkipMip_ = std::clamp(voxelDebug.voxelDdaSkipMip_, 0, maxMip);
        ImGui::SliderInt("Skip Mip", &voxelDebug.voxelDdaSkipMip_, 0, maxMip);
    }

    // material-aware bevel normal blend.
    ImGui::Separator();
    ImGui::Text("Material Edge Bevel");
    ImGui::SliderFloat("Edge Smoothing", &voxelDebug.voxelNormalEdgeSmoothing_, 0.0f, 0.5f, "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("0.00 = hard edges\n0.10-0.15 = subtle material-aware bevel\n0.25+ = rounded debug range");
    }
    if (ImGui::Button("Reset Edge Smoothing"))
    {
        voxelDebug.voxelNormalEdgeSmoothing_ = 0.12f;
    }
    ImGui::SliderFloat("Voxel Pixel Detail", &voxelDebug.voxelPixelEdgeShadowStrength_, 0.0f, 1.0f,
                       "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Controls stable voxel face pixels and edge/contact darkening.\n"
                          "Cast-shadow pixelization is handled by Post FX pixel presentation.");
    }
    if (ImGui::Button("Reset Voxel Pixel Detail"))
    {
        voxelDebug.voxelPixelEdgeShadowStrength_ = 0.35f;
    }
    ImGui::SliderFloat("Cavity Strength", &voxelDebug.voxelCavityStrength_, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Painted Material", &voxelDebug.voxelPaintedMaterialStrength_, 0.0f,
                       1.0f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Voxel Cell Variation");
    auto& variation = voxelDebug.voxelCellVariation_;
    ImGui::SliderFloat("Variation Strength", &variation.masterStrength, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Generic Variation", &variation.genericAmplitude, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Gravel Variation", &variation.gravelAmplitude, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Plant Variation", &variation.plantAmplitude, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Stone Variation", &variation.stoneAmplitude, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Wood Variation", &variation.woodAmplitude, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Hue Spread", &variation.hueSpread, 0.0f, 0.25f, "%.3f");
    ImGui::SliderFloat("Saturation Spread", &variation.saturationSpread, 0.0f, 1.0f,
                       "%.2f");
    ImGui::SliderFloat("Value Spread", &variation.valueSpread, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Palette Families", &variation.paletteFamilyStrength, 0.0f, 1.0f,
                       "%.2f");
    if (ImGui::Button("Painted Outdoor v0##CellVariation"))
    {
        variation = engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::PaintedOutdoorV0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Soft Outdoor v0##CellVariation"))
    {
        variation = engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable##CellVariation"))
    {
        variation = {};
    }

    ImGui::Separator();
    ImGui::Text("Debug Freeze");
    bool freezeDebug =
        voxelDebug.voxelFreezeCamera_ && voxelDebug.voxelFreezeTime_ && voxelDebug.voxelDisableJitter_;
    if (ImGui::Checkbox("Freeze Debug", &freezeDebug))
    {
        voxelDebug.voxelFreezeCamera_ = freezeDebug;
        voxelDebug.voxelFreezeTime_ = freezeDebug;
        if (freezeDebug)
        {
            voxelDebug.voxelDisableJitter_ = true;
            requests.resetTemporalHistory = true;
        }
    }
    ImGui::Checkbox("Freeze Camera", &voxelDebug.voxelFreezeCamera_);
    ImGui::Checkbox("Freeze Time", &voxelDebug.voxelFreezeTime_);
    if (ImGui::Checkbox("Disable Jitter", &voxelDebug.voxelDisableJitter_))
    {
        requests.resetTemporalHistory = true;
    }
    voxelDebug.voxelFreezeDebug_ =
        voxelDebug.voxelFreezeCamera_ && voxelDebug.voxelFreezeTime_ && voxelDebug.voxelDisableJitter_;

    // heatmap controls (only show when step heatmap mode is selected)
    if (voxelDebug.voxelDdaDebugMode_ == 2)
    {
        ImGui::Separator();
        ImGui::Text("Heatmap Settings");
        const char* heatmapModeItems = "Iterations\0Skip Jumps\0";
        ImGui::Combo("Heatmap Mode", &voxelDebug.voxelHeatmapMode_, heatmapModeItems);
        ImGui::SliderFloat("Heatmap Max", &voxelDebug.voxelHeatmapMax_, 16.0f, 512.0f, "%.0f");
        ImGui::SliderFloat("Heatmap Gamma", &voxelDebug.voxelHeatmapGamma_, 0.25f, 4.0f, "%.2f");
    }

    // day 8.5: dda metrics controls and display
    ImGui::Separator();
    ImGui::Checkbox("Enable DDA Metrics", &diagnostics.voxelMetricsEnabled_);
    if (diagnostics.voxelMetricsEnabled_)
    {
        ImGui::SliderInt("Metrics Sample Stride", &diagnostics.voxelMetricsSampleStride_, 1, 16);
        const char* samplePatternItems = "Fixed\0Cycling\0Random\0";
        int samplePattern = state.metricsSamplePattern;
        if (ImGui::Combo("Sample Pattern", &samplePattern, samplePatternItems))
        {
            requests.metricsSamplePattern = samplePattern;
        }
        ImGui::Text("Sample Offset: (%d, %d)", state.metricsSampleOffset.x,
                    state.metricsSampleOffset.y);
        ImGui::SliderInt("History Length", &diagnostics.voxelMetricsHistoryLength_, 1, 240);
        bool writeJson = state.metricsWriteJson;
        if (ImGui::Checkbox("Write Capture JSON", &writeJson))
        {
            requests.metricsWriteJson = writeJson;
        }
        if (ImGui::Button("Dump Metrics Capture (F11)"))
        {
            requests.dumpMetricsCapture = true;
        }

        // display computed metrics
        if (metrics.sampleCount > 0)
        {
            float avgIters = static_cast<float>(metrics.sumIters) /
                             static_cast<float>(metrics.sampleCount);
            float avgSkipJumps = static_cast<float>(metrics.sumSkipJumps) /
                                 static_cast<float>(metrics.sampleCount);
            float avgHierarchyDescents =
                static_cast<float>(metrics.sumHierarchyDescents) /
                static_cast<float>(metrics.sampleCount);
            float avgFineIters = static_cast<float>(metrics.sumFineIters) /
                                 static_cast<float>(metrics.sampleCount);
            float avgHierarchyProbes =
                static_cast<float>(metrics.sumHierarchyProbes) /
                static_cast<float>(metrics.sampleCount);
            uint32_t totalHitMiss = metrics.hitCount + metrics.missCount;
            float hitPercent = totalHitMiss > 0
                                   ? 100.0f * static_cast<float>(metrics.hitCount) /
                                         static_cast<float>(totalHitMiss)
                                   : 0.0f;

            ImGui::Text("Samples: %u", metrics.sampleCount);
            ImGui::Text("Avg Iterations: %.1f", avgIters);
            ImGui::Text("Avg Skip Jumps: %.1f", avgSkipJumps);
            ImGui::Text("Avg Hierarchy Descents: %.1f", avgHierarchyDescents);
            ImGui::Text("Avg Fine Iterations: %.1f", avgFineIters);
            ImGui::Text("Avg Hierarchy Probes: %.1f", avgHierarchyProbes);
            ImGui::Text("Hit Rate: %.1f%% (%u/%u)", hitPercent, metrics.hitCount,
                        totalHitMiss);
            if (!metricsHistory.empty())
            {
                struct Stats
                {
                    float mean = 0.0f;
                    float min = 0.0f;
                    float max = 0.0f;
                    float stddev = 0.0f;
                };
                auto computeStats = [](const std::vector<DdaMetricsSamplePoint>& samples,
                                       auto getter) -> Stats
                {
                    Stats out{};
                    if (samples.empty())
                    {
                        return out;
                    }
                    out.min = getter(samples[0]);
                    out.max = out.min;
                    double sum = 0.0;
                    for (const auto& s : samples)
                    {
                        const float v = getter(s);
                        sum += v;
                        out.min = std::min(out.min, v);
                        out.max = std::max(out.max, v);
                    }
                    out.mean = static_cast<float>(sum / samples.size());
                    double variance = 0.0;
                    for (const auto& s : samples)
                    {
                        const double v = getter(s);
                        const double diff = v - out.mean;
                        variance += diff * diff;
                    }
                    out.stddev =
                        static_cast<float>(std::sqrt(variance / samples.size()));
                    return out;
                };

                const Stats iterStats =
                    computeStats(metricsHistory,
                                 [](const DdaMetricsSamplePoint& s) { return s.avgIterations; });
                const Stats skipStats =
                    computeStats(metricsHistory,
                                 [](const DdaMetricsSamplePoint& s) { return s.avgSkipJumps; });
                const Stats hitStats =
                    computeStats(metricsHistory,
                                 [](const DdaMetricsSamplePoint& s) { return s.hitRate; });
                const Stats sampleStats =
                    computeStats(metricsHistory,
                                 [](const DdaMetricsSamplePoint& s) { return s.sampleCount; });

                ImGui::Separator();
                ImGui::Text("History (%zu frames)", metricsHistory.size());
                ImGui::Text("Avg Iterations: %.2f ± %.2f (min %.2f / max %.2f)",
                            iterStats.mean, iterStats.stddev, iterStats.min, iterStats.max);
                ImGui::Text("Avg Skip Jumps: %.2f ± %.2f (min %.2f / max %.2f)",
                            skipStats.mean, skipStats.stddev, skipStats.min, skipStats.max);
                ImGui::Text("Hit Rate: %.2f%% ± %.2f%% (min %.2f%% / max %.2f%%)",
                            hitStats.mean * 100.0f, hitStats.stddev * 100.0f,
                            hitStats.min * 100.0f, hitStats.max * 100.0f);
                ImGui::Text("Samples: %.0f ± %.0f (min %.0f / max %.0f)",
                            sampleStats.mean, sampleStats.stddev, sampleStats.min,
                            sampleStats.max);
            }
        }
        else
        {
            ImGui::Text("No samples collected");
        }
    }

    ImGui::Unindent();
    return requests;
}

void DrawObbVolumes(engine::VoxelWorld& world)
{
    if (!ImGui::CollapsingHeader("OBB Volumes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    ImGui::Indent();
    drawVolumeVisibilityRows(world, "Volume");
    ImGui::Unindent();
}

ProceduralWorldPanelRequests DrawProceduralWorld(
    engine::ProceduralWorldSettings& settings, engine::VoxelWorld& world)
{
    ProceduralWorldPanelRequests requests{};
    if (!ImGui::CollapsingHeader("Procedural World", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return requests;
    }
    ImGui::Indent();
    ImGui::Text("Volumes: %zu", world.instances().size());
    ImGui::Text("Seed: %u", settings.proceduralSeed_);

    if (ImGui::Button("Reseed"))
    {
        requests.reseed = true;
    }

    bool changed = false;

    int gridX = settings.proceduralGridDims_.x;
    int gridY = settings.proceduralGridDims_.y;
    int gridZ = settings.proceduralGridDims_.z;
    bool gridChanged = false;
    gridChanged |= ImGui::SliderInt("Chunks X", &gridX, 1, 16);
    gridChanged |= ImGui::SliderInt("Chunks Y", &gridY, 1, 4);
    gridChanged |= ImGui::SliderInt("Chunks Z", &gridZ, 1, 16);
    if (gridChanged)
    {
        settings.proceduralGridDims_ = glm::ivec3(gridX, gridY, gridZ);
        changed = true;
    }

    int chunkSize = settings.proceduralChunkDims_.x;
    if (ImGui::SliderInt("Chunk Size", &chunkSize, 16, 96))
    {
        settings.proceduralChunkDims_ = glm::ivec3(chunkSize, chunkSize, chunkSize);
        changed = true;
    }

    const glm::ivec3 worldDimsVoxels(
        settings.proceduralGridDims_.x * settings.proceduralChunkDims_.x,
        settings.proceduralGridDims_.y * settings.proceduralChunkDims_.y,
        settings.proceduralGridDims_.z * settings.proceduralChunkDims_.z);
    ImGui::Text("World Voxels: %dx%dx%d", worldDimsVoxels.x, worldDimsVoxels.y,
                worldDimsVoxels.z);

    const int maxWorldY = std::max(1, worldDimsVoxels.y - 1);

    ImGui::Separator();
    changed |= ImGui::SliderFloat("Noise Scale", &settings.proceduralNoiseScale_, 8.0f, 64.0f);
    changed |= ImGui::SliderFloat("Base Height", &settings.proceduralBaseHeight_, 0.0f,
                                  static_cast<float>(maxWorldY));
    changed |=
        ImGui::SliderFloat("Height Amplitude", &settings.proceduralHeightAmplitude_, 0.0f, 40.0f);
    changed |= ImGui::SliderInt("Dirt Depth", &settings.proceduralDirtDepth_, 1, 10);

    ImGui::Separator();
    changed |= ImGui::Checkbox("Enable Caves", &settings.proceduralCavesEnabled_);
    if (settings.proceduralCavesEnabled_)
    {
        changed |= ImGui::SliderFloat("Cave Noise Scale", &settings.proceduralCaveNoiseScale_, 8.0f,
                                      64.0f);
        changed |= ImGui::SliderFloat("Cave Threshold", &settings.proceduralCaveThreshold_, 0.2f,
                                      0.9f);
        changed |= ImGui::SliderInt("Cave Min Y", &settings.proceduralCaveMinY_, 0, maxWorldY);
        changed |= ImGui::SliderInt("Cave Max Y", &settings.proceduralCaveMaxY_, 0, maxWorldY);
    }

    ImGui::Separator();
    changed |= ImGui::Checkbox("Enable Trees", &settings.proceduralTreesEnabled_);
    if (settings.proceduralTreesEnabled_)
    {
        changed |= ImGui::SliderInt("Trees / Chunk", &settings.proceduralTreesPerChunk_, 0, 6);
        changed |= ImGui::SliderInt("Trunk Min H", &settings.proceduralTrunkMinH_, 1, 12);
        changed |= ImGui::SliderInt("Trunk Max H", &settings.proceduralTrunkMaxH_, 1, 20);
        changed |= ImGui::SliderInt("Leaf Radius", &settings.proceduralLeafRadius_, 1, 8);
    }

    ImGui::Separator();
    changed |= ImGui::Checkbox("Enable Rocks", &settings.proceduralRocksEnabled_);
    if (settings.proceduralRocksEnabled_)
    {
        changed |= ImGui::SliderInt("Rocks / Chunk", &settings.proceduralRocksPerChunk_, 0, 6);
        changed |= ImGui::SliderInt("Rock Min R", &settings.proceduralRockMinR_, 1, 8);
        changed |= ImGui::SliderInt("Rock Max R", &settings.proceduralRockMaxR_, 1, 12);
    }

    if (changed)
    {
        settings.proceduralBaseHeight_ =
            std::clamp(settings.proceduralBaseHeight_, 0.0f, static_cast<float>(maxWorldY));
        settings.proceduralCaveMinY_ = std::clamp(settings.proceduralCaveMinY_, 0, maxWorldY);
        settings.proceduralCaveMaxY_ = std::clamp(settings.proceduralCaveMaxY_, 0, maxWorldY);
        settings.proceduralCaveMaxY_ =
            std::max(settings.proceduralCaveMinY_, settings.proceduralCaveMaxY_);
        settings.proceduralTrunkMinH_ = std::max(1, settings.proceduralTrunkMinH_);
        settings.proceduralTrunkMaxH_ =
            std::max(settings.proceduralTrunkMinH_, settings.proceduralTrunkMaxH_);
        settings.proceduralRockMinR_ = std::max(1, settings.proceduralRockMinR_);
        settings.proceduralRockMaxR_ =
            std::max(settings.proceduralRockMinR_, settings.proceduralRockMaxR_);
        settings.proceduralDirty_ = true;
    }

    if (settings.proceduralDirty_)
    {
        if (ImGui::Button("Rebuild Procedural World"))
        {
            requests.rebuild = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Pending changes");
    }

    if (!world.instances().empty())
    {
        ImGui::Separator();
        ImGui::Text("Chunk Visibility");
        drawVolumeVisibilityRows(world, "Chunk");
    }

    ImGui::Unindent();
    return requests;
}

VoxelImportPanelRequests DrawVoxelImport(engine::VoxelWorld& world,
                                         const VoxelImportPanelState& state)
{
    VoxelImportPanelRequests requests{};
    if (!ImGui::CollapsingHeader("Voxel Import", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return requests;
    }
    ImGui::Indent();
    if (state.meshPath.empty())
    {
        ImGui::TextDisabled("Mesh: (none found)");
    }
    else
    {
        ImGui::TextWrapped("Mesh: %s", state.meshPath.string().c_str());
    }

    if (ImGui::Button("Refresh Mesh List"))
    {
        requests.refreshMeshList = true;
    }

    int resIndex = state.resolution == 128 ? 1 : 0;
    const char* resItems = "64^3\0"
                           "128^3\0";
    if (ImGui::Combo("Resolution", &resIndex, resItems))
    {
        requests.resolution = resIndex == 0 ? 64 : 128;
    }

    bool splitChunks = state.splitChunks;
    if (ImGui::Checkbox("Split Into Chunks", &splitChunks))
    {
        requests.splitChunks = splitChunks;
    }

    if (ImGui::Button("Voxelize Mesh"))
    {
        requests.voxelize = true;
    }
    const bool pendingChanges = state.dirty || requests.refreshMeshList ||
                                requests.resolution.has_value() ||
                                requests.splitChunks.has_value();
    if (pendingChanges)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Pending changes");
    }

    ImGui::Text("Triangles: %u", state.triangleCount);
    ImGui::Text("Filled Voxels: %u", state.filledCount);
    ImGui::Text("Voxelize Time: %.2f ms", state.voxelizeMs);

    if (!world.instances().empty())
    {
        ImGui::Separator();
        ImGui::Text("Volume Visibility");
        drawVolumeVisibilityRows(world, "Volume");
    }

    ImGui::Unindent();
    return requests;
}

AquariumVolumesPanelRequests DrawAquariumVolumes(
    engine::VoxelWorld& world, const PlaceablePreview& preview, bool& previewEnabled,
    int& selectedSpecies, const char* const* speciesLabels, int speciesCount,
    const AquariumVolumesPanelState& state)
{
    AquariumVolumesPanelRequests requests{};
    if (!ImGui::CollapsingHeader("Aquarium Volumes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return requests;
    }
    ImGui::Indent();
    const auto& volumeInfos = AquariumScene::getVolumeInfos();
    auto& instances = world.instances();

    for (size_t i = 0; i < instances.size() && i < volumeInfos.size(); ++i)
    {
        const auto& info = volumeInfos[i];
        bool visible = instances[i].visible;
        if (ImGui::Checkbox(info.name.c_str(), &visible))
        {
            world.setVolumeVisible(static_cast<uint32_t>(i), visible);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(%dx%dx%d)", info.dimensions.x, info.dimensions.y,
                            info.dimensions.z);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Position: (%.1f, %.1f, %.1f)", info.worldPosition.x,
                        info.worldPosition.y, info.worldPosition.z);
            ImGui::Text("Flags: 0x%02X", info.flags);
            if (info.flags & engine::VoxelVolume::FLAG_GLASS)
            {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "  GLASS");
            }
            if (info.flags & engine::VoxelVolume::FLAG_WATER)
            {
                ImGui::TextColored(ImVec4(0.3f, 0.5f, 1.0f, 1.0f), "  WATER");
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Placeable Preview");
    ImGui::Checkbox("Enable Preview", &previewEnabled);
    selectedSpecies = std::clamp(selectedSpecies, 0, std::max(0, speciesCount - 1));
    ImGui::Combo("Species", &selectedSpecies, speciesLabels, speciesCount);
    ImGui::Text("Edit Mode: %s", state.editModeEnabled ? "on" : "off");
    ImGui::Text("Saved Placeables: %zu", state.savedPlaceableCount);
    ImGui::SameLine();
    ImGui::TextDisabled(state.useDefaultPlaceables ? "(default fallback)"
                                                   : "(explicit)");
    if (preview.active)
    {
        if (preview.evaluation.hasHit)
        {
            const glm::vec3& pos = preview.evaluation.hit.position;
            ImGui::Text("Hit: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            ImGui::Text("Water Depth: %.2f", preview.evaluation.hit.waterDepth);
            ImGui::Text("Footprint Clear: %s",
                        preview.evaluation.footprintClear ? "yes" : "no");
        }
        const ImVec4 statusColor = preview.evaluation.valid
                                       ? ImVec4(0.2f, 0.9f, 0.45f, 1.0f)
                                       : ImVec4(1.0f, 0.35f, 0.25f, 1.0f);
        ImGui::TextColored(statusColor, "%s",
                           preview.evaluation.valid ? "Valid" : "Invalid");
        if (!preview.evaluation.rejectReason.empty())
        {
            ImGui::TextWrapped("%s", preview.evaluation.rejectReason.c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("Inactive");
    }
    const bool canPlacePreview = preview.active && preview.evaluation.valid;
    if (!canPlacePreview)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Place Preview"))
    {
        requests.placePreview = true;
    }
    if (!canPlacePreview)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!state.canRemovePreview)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Remove Under Cursor"))
    {
        requests.removeUnderCursor = true;
    }
    if (!state.canRemovePreview)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!state.hasPlaceableUndo)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Undo Placeable"))
    {
        requests.undoPlaceable = true;
    }
    if (!state.hasPlaceableUndo)
    {
        ImGui::EndDisabled();
    }
    if (state.canRemovePreview)
    {
        ImGui::TextDisabled("Hover: %s", state.hoveredPrototypeSlug.c_str());
    }
    if (state.placeableSceneDirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                           "Unsaved placeable edits");
    }
    if (!state.placeableEditStatus.empty())
    {
        ImGui::TextWrapped("%s", state.placeableEditStatus.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Rebuild Aquarium"))
    {
        requests.rebuildAquarium = true;
    }

    ImGui::Unindent();
    return requests;
}

void DrawTemporalStatus(const TemporalStatusInfo& status)
{
    if (ImGui::CollapsingHeader("Temporal Status", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        ImGui::Text("Frame Index: %u", status.frameIndex);
        ImGui::Text("Depth History Index: %d", status.depthHistoryIndex);
        ImGui::Text("Use DDA Shadows: %s",
                    status.ddaShadowsAvailable
                        ? (status.ddaShadowsEnabled ? "yes" : "no")
                        : "unavailable");
        ImGui::Text("AO Enabled: %s",
                    status.ambientOcclusionAvailable
                        ? (status.ambientOcclusionEnabled ? "yes" : "no")
                        : "unavailable");
        if (status.shadowResetPending)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                               "Shadow Reset Pending: YES");
        }
        else
        {
            ImGui::Text("Shadow Reset Pending: no");
        }
        if (status.ambientOcclusionResetPending)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                               "AO Reset Pending: YES");
        }
        else
        {
            ImGui::Text("AO Reset Pending: no");
        }
        ImGui::Unindent();
        ImGui::Separator();
    }
}

std::optional<int> DrawViewMode(int currentMode)
{
    ImGui::Text("View Mode:");

    std::optional<int> selectedMode;
    const auto drawMode = [&](const char* label, int mode, const char* shortcut) {
        if (ImGui::RadioButton(label, currentMode == mode))
        {
            selectedMode = mode;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shortcut);
    };

    drawMode("Final Composite", 0, "(0)");
    drawMode("G-Buffer: Albedo", 1, "(1)");
    drawMode("G-Buffer: Normal", 2, "(2)");
    drawMode("G-Buffer: Material", 3, "(3)");
    drawMode("Shadow Map", 4, "(4)");
    drawMode("Shadow Factor", 5, "(5)");
    drawMode("Water Distance", 6, "(6)");
    drawMode("Water Distance Delta", 7, "(7)");

    return selectedMode;
}

bool DrawGpuProfiler(GpuProfiler& profiler, bool& enabled)
{
    bool changed = false;
    if (ImGui::CollapsingHeader("GPU Profiler"))
    {
        if (!profiler.isSupported())
        {
            ImGui::TextDisabled("GPU timestamp queries not supported on this device.");
        }
        else
        {
            changed = ImGui::Checkbox("Enable GPU Profiler", &enabled);

            const bool showSamples = changed ? enabled : profiler.isEnabled();
            const auto& samples = profiler.samples();
            if (showSamples)
            {
                ImGui::Text("GPU Total: %.2f ms", profiler.totalMs());
                ImGui::Separator();
                if (samples.empty())
                {
                    ImGui::TextDisabled("No data yet.");
                }
                else
                {
                    for (const auto& sample : samples)
                    {
                        ImGui::Text("%s: %.2f ms", sample.label.c_str(), sample.ms);
                    }
                }
            }
        }
    }
    return changed;
}

void DrawDay16Checklist(engine::render::DiagnosticsSettings& diagnostics)
{
    if (ImGui::CollapsingHeader("Day 16 Checklist"))
    {
        ImGui::Indent();
        ImGui::Checkbox("Shadow raw/resolved debug verified",
                        &diagnostics.day16ShadowDebugVerified_);
        ImGui::Checkbox("AO raw/resolved debug verified",
                        &diagnostics.day16AoDebugVerified_);
        ImGui::Checkbox("Depth reject mask looks reasonable",
                        &diagnostics.day16DepthRejectMaskVerified_);
        ImGui::Checkbox("AO stable with Ray Count = 2",
                        &diagnostics.day16AoStableRayCount2_);
        ImGui::Checkbox("GPU profiler baseline recorded",
                        &diagnostics.day16GpuBaselineRecorded_);
        ImGui::Checkbox("Parameters tuned for scene",
                        &diagnostics.day16ParamsTuned_);
        ImGui::Checkbox("Dead code verified inactive",
                        &diagnostics.day16DeadCodeChecked_);
        ImGui::Checkbox("Next step chosen (Day 17/18/Water)",
                        &diagnostics.day16NextStepChosen_);
        ImGui::Unindent();
    }
}

void DrawRenderingPipelinePasses()
{
    if (ImGui::CollapsingHeader("Rendering Pipeline Passes",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        ImGui::Text("1. Shadow Pass");
        ImGui::BulletText("Renders depth from light perspective");
        ImGui::BulletText("3 cascaded shadow maps (2048x2048)");
        ImGui::BulletText("Outputs: Depth texture");

        ImGui::Spacing();
        ImGui::Text("2. G-Buffer Pass");
        ImGui::BulletText("Renders scene geometry to MRT");
        ImGui::BulletText("Outputs:");
        ImGui::Indent();
        ImGui::BulletText("Albedo (RGB)");
        ImGui::BulletText("World-space Normal");
        ImGui::BulletText("Material params (Roughness/Metallic/AO)");
        ImGui::BulletText("Velocity");
        ImGui::BulletText("Water distance (R16F)");
        ImGui::BulletText("Depth buffer");
        ImGui::Unindent();

        ImGui::Spacing();
        ImGui::Text("3. Lighting Pass");
        ImGui::BulletText("Deferred shading using G-buffer");
        ImGui::BulletText("Reads shadow maps for shadows");
        ImGui::BulletText("Supports up to 64 point lights");
        ImGui::BulletText("Outputs: HDR lighting (R16G16B16A16F)");

        ImGui::Spacing();
        ImGui::Text("4. Water Pass");
        ImGui::BulletText("Forward water surface using scene color + depth");
        ImGui::BulletText("Outputs: HDR scene with water (R16G16B16A16F)");

        ImGui::Spacing();
        ImGui::Text("5. Glass Pass");
        ImGui::BulletText("Backface depth pass + thickness shading");
        ImGui::BulletText("Outputs: HDR scene with glass (R16G16B16A16F)");

        ImGui::Spacing();
        ImGui::Text("6. Bloom Pass");
        ImGui::BulletText("Extract + blur bright HDR highlights");
        ImGui::BulletText("Outputs: Half-res HDR bloom chain");

        ImGui::Spacing();
        ImGui::Text("7. TAA Resolve (Teardown-style)");
        ImGui::BulletText("Plus-pattern sampling + similarity-based history blending");
        ImGui::BulletText("Outputs: Temporally resolved HDR scene");

        ImGui::Spacing();
        ImGui::Text("8. Composite Pass");
        ImGui::BulletText("Final composition to swapchain");
        ImGui::BulletText("Applies tonemapping + FXAA");
        ImGui::BulletText("View mode switching");

        ImGui::Unindent();
    }
}

LightingShadowRequests DrawLightingAndShadows(
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadow,
    engine::render::DiagnosticsSettings& diagnostics,
    const LightingShadowState& state)
{
    LightingShadowRequests requests{};
    if (ImGui::CollapsingHeader("Lighting Controls", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        bool lightsEnabled = state.lightsEnabled;
        if (ImGui::Checkbox("Enable Point Lights (L)", &lightsEnabled))
        {
            requests.toggleLights = true;
        }

        int lightCount = state.lightCount;
        if (ImGui::SliderInt("Active Lights", &lightCount, 0, 64))
        {
            requests.lightCount = lightCount;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("([/])");

        bool heatmap = state.heatmap;
        if (ImGui::Checkbox("Light Heatmap (H)", &heatmap))
        {
            requests.toggleHeatmap = true;
        }

        bool cascadeDebug = state.cascadeDebug;
        if (ImGui::Checkbox("Cascade Debug (C)", &cascadeDebug))
        {
            requests.toggleCascadeDebug = true;
        }

        int lightingDebug = lighting.lightingDebugMode_;
        const char* lightingDebugItems =
            "Full\0Diffuse Only\0Specular Only\0Env Only\0AO Raw\0AO Resolved\0Caustics\0"
            "Local Shadow R\0Local Shadow G\0Local Shadow B\0Local Shadow A\0"
            "Material Roughness\0Material Metallic\0Material Emissive\0Material Category\0"
            "Hemisphere Ambient\0AO Applied\0";
        if (ImGui::Combo("Lighting Debug", &lightingDebug, lightingDebugItems))
        {
            lighting.lightingDebugMode_ = lightingDebug;
            diagnostics.edgeFlickerDebugMode_ = 0;
        }

        if (ImGui::CollapsingHeader("Hemisphere Ambient"))
        {
            auto& ambient = lighting.hemisphereAmbient_;
            ImGui::SliderFloat("Strength##LegacyHemisphere", &ambient.strength, 0.0f, 1.0f);
            ImGui::ColorEdit3(
                "Sky Tint##LegacyHemisphere", &ambient.skyTint.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            ImGui::ColorEdit3(
                "Ground Tint##LegacyHemisphere", &ambient.groundTint.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            if (ImGui::Button("Warm/Cool Outdoor v0##LegacyHemisphere"))
            {
                ambient = engine::render::makeHemisphereAmbientSettings(
                    engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
            }
            ImGui::SameLine();
            if (ImGui::Button("Compatibility Off##LegacyHemisphere"))
            {
                ambient = {};
            }
            ImGui::TextWrapped(
                "Strength 0 preserves the previous flat ambient term exactly.");
        }

        if (ImGui::CollapsingHeader("Edge Flicker Debug (Day 12)"))
        {
            const char* edgeDebugItems = "Off\0"
                                         "20: NdotL (RGB)\0"
                                         "21: NdotL Grayscale\0"
                                         "22: Normal Edges\0"
                                         "23: Flicker Risk\0";
            if (ImGui::Combo("Edge Debug Mode", &diagnostics.edgeFlickerDebugMode_, edgeDebugItems))
            {
                if (diagnostics.edgeFlickerDebugMode_ > 0)
                {
                    lighting.lightingDebugMode_ = 0;
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "20: Red=back-facing, Green=lit, Yellow=terminator\n"
                    "21: Grayscale NdotL (see flickering easily)\n"
                    "22: Normal discontinuities (edges)\n"
                    "23: Red=high sensitivity to normal changes");
            }
        }

        if (ImGui::CollapsingHeader("Pixel Inspector (Day 12 Debug)"))
        {
            ImGui::Checkbox("Enable Pixel Inspect", &diagnostics.pixelInspectEnabled_);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "When enabled, middle-click samples the pixel under cursor.\n"
                    "Requires DDA debug mode 10, 11, or 12 to capture data.");
            }

            if (diagnostics.pixelInspectEnabled_ && !ImGui::GetIO().WantCaptureMouse &&
                state.pixelInspect.capture.middleMousePressed)
            {
                diagnostics.inspectPixel_ = state.pixelInspect.capture.cursor;
            }

            if (diagnostics.inspectPixel_.x >= 0)
            {
                ImGui::Text("Inspecting: (%d, %d)", diagnostics.inspectPixel_.x,
                            diagnostics.inspectPixel_.y);

                ImGui::Text("voxelFrac: (%.4f, %.4f, %.4f)",
                            state.pixelInspect.voxelFrac.x,
                            state.pixelInspect.voxelFrac.y,
                            state.pixelInspect.voxelFrac.z);
                const char* axisLabel = (state.pixelInspect.hitAxis == 1)
                                            ? "X"
                                            : (state.pixelInspect.hitAxis == 2)
                                                  ? "Y"
                                                  : (state.pixelInspect.hitAxis == 3) ? "Z" : "-";
                ImGui::Text("hitAxis: %s", axisLabel);
                ImGui::Text("NdotL: %.4f", diagnostics.lastInspectNdotL_);
                ImGui::Text("Normal: (%.3f, %.3f, %.3f)",
                            diagnostics.lastInspectNormal_.x,
                            diagnostics.lastInspectNormal_.y,
                            diagnostics.lastInspectNormal_.z);
                ImGui::Text("Depth: %.6f", diagnostics.lastInspectDepth_);
                if (diagnostics.lastInspectHasWorld_)
                {
                    ImGui::Text("World Pos: (%.3f, %.3f, %.3f)",
                                diagnostics.lastInspectWorldPos_.x,
                                diagnostics.lastInspectWorldPos_.y,
                                diagnostics.lastInspectWorldPos_.z);
                    if (state.pixelInspect.volumeIndex >= 0)
                    {
                        ImGui::Text("Volume: %d  Voxel: (%d, %d, %d)",
                                    state.pixelInspect.volumeIndex,
                                    state.pixelInspect.voxel.x,
                                    state.pixelInspect.voxel.y,
                                    state.pixelInspect.voxel.z);
                    }
                    else
                    {
                        ImGui::Text("Volume: -  Voxel: (-, -, -)");
                    }
                }
                else
                {
                    ImGui::TextDisabled("World Pos: (n/a)");
                    ImGui::TextDisabled("Volume/Voxel: (n/a)");
                }

                ImGui::Separator();
                ImGui::Text("Frame Delta:");
                ImGui::Text("  Normal change: %.6f", state.pixelInspect.normalDelta);
                ImGui::Text("  NdotL change: %.6f", state.pixelInspect.ndotlDelta);
                if (state.pixelInspect.axisChanged)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       "  Axis changed: YES!");
                }
                else
                {
                    ImGui::Text("  Axis changed: no");
                }

                if (ImGui::Button("Clear Pixel"))
                {
                    diagnostics.inspectPixel_ = glm::ivec2(-1, -1);
                }
            }
            else
            {
                ImGui::TextDisabled("Middle-click to sample a pixel");
            }

            ImGui::TextWrapped(
                "Note: Use DDA debug mode 11 (Voxel Position) to see voxelFrac "
                "values encoded in normal output. Mode 12 shows hitAxis.");
        }

        ImGui::Separator();
        ImGui::Text("Shadow Settings (Day 11/12)");

        ImGui::Checkbox("Enable CSM Shadows", &shadow.csmEnabled_);

        const bool wasDdaEnabled = shadow.useDDAShadows_;
        if (!state.ddaShadowsAvailable)
        {
            ImGui::TextDisabled("DDA shadow tracing unavailable in this run.");
        }
        ImGui::BeginDisabled(!state.ddaShadowsAvailable);
        if (ImGui::Checkbox("Use DDA Shadows (Stochastic)", &shadow.useDDAShadows_))
        {
            if (shadow.useDDAShadows_ && !wasDdaEnabled)
            {
                shadow.shadowResetHistory_ = true;
                shadow.localShadowResetHistory_ = true;
            }
        }
        ImGui::EndDisabled();

        if (shadow.useDDAShadows_ && state.ddaShadowsAvailable)
        {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "Shadows converge over several frames (Day 12)");

            bool sunDistributionEditFinished = false;
            ImGui::SliderFloat("Sun Angular Radius", &shadow.sunAngularRadius_, 0.0f, 0.1f);
            sunDistributionEditFinished |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SliderInt("Sun Samples", &shadow.shadowDdaSunSampleCount_, 1, 12);
            sunDistributionEditFinished |= ImGui::IsItemDeactivatedAfterEdit();
            bool ddaTracingChanged = false;
            ddaTracingChanged |=
                ImGui::SliderFloat("Foliage Shadow Opacity", &shadow.foliageShadowOpacity_, 0.0f, 2.0f);
                ImGui::TextDisabled("Canopy 100%% / meadow shadow proxy 45%%");
            ddaTracingChanged |=
                ImGui::SliderFloat("Max Shadow Distance", &shadow.maxShadowDist_, 10.0f, 500.0f);
            ddaTracingChanged |=
                ImGui::SliderFloat("Normal Bias", &shadow.shadowNormalBias_, 0.0f, 0.2f);
            ddaTracingChanged |=
                ImGui::SliderInt("Max Steps Per Volume", &shadow.maxShadowSteps_, 32, 256);
            if (sunDistributionEditFinished)
            {
                shadow.shadowResetHistory_ = true;
            }
            if (ddaTracingChanged)
            {
                shadow.shadowResetHistory_ = true;
                shadow.localShadowResetHistory_ = true;
            }
            ImGui::Unindent();
        }

        if (shadow.useDDAShadows_ && state.ddaShadowsAvailable)
        {
            ImGui::Indent();
            ImGui::Text("Shadow Temporal (Day 12)");
            bool shadowTemporalChanged = false;
            shadowTemporalChanged |=
                ImGui::SliderFloat("Blend Alpha##ShadowTemporal", &shadow.shadowBlendAlpha_, 0.01f,
                                   0.5f);
            shadowTemporalChanged |=
                ImGui::SliderFloat("Depth Reject Thresh", &shadow.shadowDepthReject_, 0.0001f, 0.01f,
                                   "%.4f");
            shadowTemporalChanged |=
                ImGui::SliderFloat("Normal Reject Dot##ShadowTemporal",
                                   &shadow.shadowNormalRejectDot_, 0.5f, 0.999f, "%.3f");
            shadowTemporalChanged |=
                ImGui::SliderFloat("Clamp Sharpness##ShadowTemporal",
                                   &shadow.shadowClampSharpness_, 0.5f, 1.5f, "%.2f");
            shadowTemporalChanged |=
                ImGui::SliderInt("Denoise Radius##ShadowTemporal",
                                 &shadow.shadowSpatialFilterRadius_, 0, 2);
            shadowTemporalChanged |=
                ImGui::SliderFloat("Denoise Depth Sigma##ShadowTemporal",
                                   &shadow.shadowSpatialDepthSigma_, 0.0005f, 0.02f, "%.4f");
            shadowTemporalChanged |=
                ImGui::SliderFloat("Denoise Value Sigma##ShadowTemporal",
                                   &shadow.shadowSpatialValueSigma_, 0.05f, 1.0f, "%.2f");
            shadowTemporalChanged |=
                ImGui::SliderFloat("Denoise Normal Power##ShadowTemporal",
                                   &shadow.shadowSpatialNormalPower_, 1.0f, 96.0f, "%.0f");
            if (shadowTemporalChanged)
            {
                shadow.shadowResetHistory_ = true;
                shadow.localShadowResetHistory_ = true;
            }
            ImGui::SeparatorText("Post Denoise");
            ImGui::SliderInt("Post Radius##ShadowTemporal", &shadow.shadowPostDenoiseRadius_, 0, 2);
            ImGui::SliderFloat("Post Depth Sigma##ShadowTemporal",
                               &shadow.shadowPostDenoiseDepthSigma_, 0.0005f, 0.02f, "%.4f");
            ImGui::SliderFloat("Post Value Sigma##ShadowTemporal",
                               &shadow.shadowPostDenoiseValueSigma_, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Post Normal Power##ShadowTemporal",
                               &shadow.shadowPostDenoiseNormalPower_, 1.0f, 96.0f, "%.0f");
            if (ImGui::Button("Reset Shadow History"))
            {
                shadow.shadowResetHistory_ = true;
                shadow.localShadowResetHistory_ = true;
            }
            ImGui::Unindent();
        }

        ImGui::Checkbox("CSM Dither (TAA only)", &shadow.csmDitherEnabled_);
        if (!state.taaEnabled)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("TAA off");
        }

        const char* shadowDebugModes[] = {"Off", "Raw DDA Shadow", "Resolved Shadow",
                                          "CSM Shadow", "Depth Reject Mask"};
        ImGui::Combo("Shadow Debug", &shadow.shadowDebugMode_, shadowDebugModes, 5);

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Shadow Terminator (Day 12C)"))
        {
            const char* terminatorModes[] = {"Off (Hard Edge)", "Wrap", "Smoothstep"};
            ImGui::Combo("Terminator Mode", &shadow.terminatorMode_, terminatorModes, 3);

            if (shadow.terminatorMode_ != 0)
            {
                ImGui::SliderFloat("Softness", &shadow.terminatorSoftness_, 0.0f, 0.5f, "%.2f");
                ImGui::Text("Recommended: 0.10 - 0.20");

                if (ImGui::Button("Reset to Default"))
                {
                    shadow.terminatorSoftness_ = 0.15f;
                    shadow.terminatorMode_ = 1;
                }
            }
        }

        ImGui::Unindent();
    }
    return requests;
}

void DrawAmbientOcclusion(engine::render::AmbientOcclusionSettings& ao,
                          bool available)
{
    if (ImGui::CollapsingHeader("Ambient Occlusion (Day 15)"))
    {
        ImGui::Indent();
        if (!available)
        {
            ImGui::TextDisabled("Ambient occlusion unavailable in this run.");
        }
        ImGui::BeginDisabled(!available);
        bool aoEnabled = ao.aoEnabled_;
        bool aoChanged = false;
        if (ImGui::Checkbox("Enable AO", &aoEnabled))
        {
            ao.aoEnabled_ = aoEnabled;
            aoChanged = true;
        }

        const char* distanceModes[] = {"Fixed World Reach",
                                       "Screen-Scaled Reach (Performance)"};
        int distanceMode = static_cast<int>(ao.aoDistanceMode_);
        if (ImGui::Combo("Distance Mode", &distanceMode, distanceModes, 2))
        {
            ao.aoDistanceMode_ = static_cast<
                engine::render::AmbientOcclusionDistanceMode>(distanceMode);
            aoChanged = true;
        }
        if (ao.aoDistanceMode_ ==
            engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius)
        {
            aoChanged |= ImGui::SliderFloat("Projected Radius",
                                            &ao.aoProjectedRadiusPixels_, 64.0f,
                                            1024.0f, "%.0f px");
            aoChanged |= ImGui::SliderFloat("Minimum World Distance",
                                            &ao.aoProjectedMinDistance_, 0.0f,
                                            8.0f, "%.1f m");
            ImGui::TextDisabled("512 px / 2 m: validated performance tier");
        }
        const char* reachLabel =
            ao.aoDistanceMode_ ==
                    engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius
                ? "Maximum Reach Cap"
                : "Trace Reach";
        aoChanged |= ImGui::SliderFloat(reachLabel, &ao.aoMaxDistance_, 0.25f,
                                        20.0f, "%.2f m",
                                        ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled("Short = contacts; long = broad cavities");
        aoChanged |= ImGui::SliderFloat("Sample Step (Quality)", &ao.aoStepSize_,
                                        0.05f, 0.5f, "%.2f m");
        aoChanged |= ImGui::SliderFloat("Intensity", &ao.aoIntensity_, 0.0f, 2.0f, "%.2f");
        aoChanged |= ImGui::SliderFloat("Ambient Strength", &ao.aoContribution_,
                                        0.0f, 0.5f, "%.2f");
        aoChanged |= ImGui::SliderFloat("Surface Bias", &ao.aoBias_, 0.01f, 0.2f, "%.3f");
        aoChanged |= ImGui::SliderInt("Authored Samples / Pixel", &ao.aoRayCount_, 1, 4);
        ImGui::SameLine();
        ImGui::TextDisabled("Half tier doubles this, capped at 4");

        ImGui::Separator();
        ImGui::Text("AO Temporal");
        aoChanged |= ImGui::SliderFloat("Blend Alpha##AOTemporal", &ao.aoBlendAlpha_, 0.01f,
                                        0.5f, "%.3f");
        ImGui::SameLine();
        ImGui::TextDisabled("Half: 0.5x + bilateral filter");
        aoChanged |= ImGui::SliderFloat("Depth Reject", &ao.aoDepthReject_, 0.005f, 0.1f, "%.3f");
        aoChanged |= ImGui::SliderFloat("Normal Reject Dot", &ao.aoNormalRejectDot_, 0.5f, 0.99f,
                                        "%.2f");
        ImGui::TextDisabled("Temporal controls tune stability, not AO darkness");

        if (aoChanged)
        {
            ao.aoResetHistory_ = true;
        }

        if (ImGui::Button("Reset AO History"))
        {
            ao.aoResetHistory_ = true;
        }
        ImGui::EndDisabled();
        ImGui::Unindent();
    }
}

PostProcessingRequests DrawPostProcessing(
    engine::render::PostFxSettings& postFx,
    const PostProcessingState& state)
{
    PostProcessingRequests requests{};
    if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        bool tonemapEnabled = state.tonemapEnabled;
        if (ImGui::Checkbox("Enable Tonemapping (T)", &tonemapEnabled))
        {
            requests.toggleTonemap = true;
        }

        float exposure = state.exposure;
        if (ImGui::SliderFloat("Exposure", &exposure, 0.05f, 8.0f))
        {
            requests.exposure = exposure;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(-/=)");
        ImGui::SliderFloat("Highlight Recovery", &postFx.highlightRecovery_, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Compresses bright HDR highlights before and after tonemap so pale voxel\n"
                "materials retain more shape under pixel presentation.");
        }

        bool bloomEnabled = state.bloomEnabled;
        if (ImGui::Checkbox("Enable Bloom (B)", &bloomEnabled))
        {
            requests.bloomEnabled = bloomEnabled;
        }

        ImGui::SliderFloat("Bloom Threshold", &postFx.bloomThreshold_, 0.5f, 4.0f);
        ImGui::SliderFloat("Bloom Knee", &postFx.bloomKnee_, 0.0f, 1.0f);
        ImGui::SliderFloat("Bloom Intensity", &postFx.bloomIntensity_, 0.0f, 0.25f);
        ImGui::SliderFloat("Bloom Blur Sigma", &postFx.bloomSigma_, 0.5f, 6.0f);

        ImGui::SeparatorText("Color Grade");
        ImGui::Checkbox("Enable Color Grade", &postFx.colorGradeEnabled_);
        if (postFx.colorGradeEnabled_)
        {
            ImGui::SliderFloat("Grade Strength", &postFx.colorGradeStrength_, 0.0f, 1.0f);
            ImGui::SliderFloat("Saturation", &postFx.colorGradeSaturation_, 0.0f, 2.0f);
            ImGui::SliderFloat("Contrast", &postFx.colorGradeContrast_, 0.25f, 2.0f);
            ImGui::SliderFloat("Temperature", &postFx.colorGradeTemperature_, -1.0f, 1.0f);
        }

        ImGui::SeparatorText("Pixel Presentation");
        ImGui::Checkbox("Pixelize Final Scene", &postFx.postPixelizationEnabled_);
        if (postFx.postPixelizationEnabled_)
        {
            ImGui::SliderFloat("Pixel Block Size", &postFx.postPixelizationBlockSize_, 1.0f, 12.0f,
                               "%.0f px");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Snaps final-scene sampling to display-pixel blocks before ImGui.\n"
                    "Try 3-5 px for the current aquarium reference pass.");
            }
            ImGui::SliderFloat("Pixel Blend", &postFx.postPixelizationStrength_, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Blends native-resolution color back into the pixel grid.\n"
                    "Lower values keep the image cleaner while retaining stepped silhouettes.");
            }
            ImGui::SliderFloat("Edge Focus", &postFx.postPixelizationEdgeFocus_, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Keeps stronger pixelization on depth, normal, and color edges while\n"
                    "relaxing it across smooth water, glass, sky, and broad lit faces.");
            }
        }
        ImGui::SliderFloat("Material Detail", &postFx.postMaterialDetailStrength_, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Reintroduces voxel material albedo detail after tonemapping.\n"
                "Glass and water are skipped; gravel darkening is capped to avoid muddy floors.");
        }

        ImGui::SliderFloat("Vignette", &postFx.vignetteStrength_, 0.0f, 0.3f);
        ImGui::SliderFloat("Film Grain", &postFx.grainStrength_, 0.0f, 0.1f);

        int postDebug = state.postDebugMode;
        const char* postDebugItems =
            "Off\0Bloom Extract\0Bloom Blur\0Bloom Combined\0Luminance Heatmap\0"
            "Blue Noise (Raw)\0Stochastic Noise\0Noise Hemisphere\0";
        if (ImGui::Combo("Post Debug (F10)", &postDebug, postDebugItems))
        {
            requests.postDebugMode = postDebug;
        }

        ImGui::Unindent();
    }
    return requests;
}

TaaRequests DrawTaa(engine::render::PostFxSettings& postFx,
                    bool jitterSuppressedByDebug)
{
    TaaRequests requests{};
    if (ImGui::CollapsingHeader("TAA Settings (Teardown-style)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        if (ImGui::Checkbox("Enable TAA", &postFx.taaEnabled_))
        {
            postFx.taaResetHistory_ = true;
            requests.taaEnabledChanged = true;
        }

        if (postFx.taaEnabled_)
        {
            if (ImGui::Checkbox("Enable Jitter", &postFx.jitterEnabled_))
            {
                postFx.taaResetHistory_ = true;
                requests.jitterChanged = true;
            }

            if (jitterSuppressedByDebug)
            {
                ImGui::TextDisabled("Jitter is suppressed by Voxel Debug settings.");
            }
            else if (!postFx.jitterEnabled_)
            {
                ImGui::TextDisabled("Tip: TAA converges best with profile jitter enabled.");
            }

            ImGui::Separator();
            ImGui::Text("Similarity-Based Trust");
            ImGui::SliderFloat("Similarity Threshold", &postFx.taaSimilarityThreshold_, 0.01f, 0.5f,
                               "%.2f (lower = more aggressive rejection)");
            ImGui::SliderFloat("Velocity Scale", &postFx.taaVelocityScale_, 1.0f, 50.0f,
                               "%.1f (higher = more rejection on motion)");

            ImGui::Separator();
            ImGui::Text("Blend Factors");
            ImGui::SliderFloat("Blend Min", &postFx.taaBlendMin_, 0.01f, 0.2f,
                               "%.2f (stable areas)");
            ImGui::SliderFloat("Blend Max", &postFx.taaBlendMax_, 0.1f, 0.5f,
                               "%.2f (high motion/reactive areas)");
            ImGui::SliderFloat("Soft Edge Reconstruction", &postFx.taaSoftEdgeStrength_,
                               0.0f, 1.0f,
                               "%.2f (edge-local, 0 = historical resolve)");

            ImGui::Separator();
            ImGui::Text("Silhouette Protection (Day 12E)");
            ImGui::SliderFloat("Depth Edge Threshold", &postFx.taaDepthEdgeThreshold_, 0.0f, 0.5f,
                               "%.2f (0 = off, higher = more edge rejection)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Rejects TAA history at depth discontinuities (silhouettes).\n"
                    "Prevents sky from bleeding onto voxel edges.\n"
                    "Recommended: 0.05 - 0.15");
            }

            ImGui::SliderFloat("Cross-Frame Depth", &postFx.taaCrossFrameDepthThreshold_, 0.0f, 0.1f,
                               "%.3f (0 = off, detects disocclusion)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Rejects TAA history when depth changes significantly between frames.\n"
                    "Detects disocclusion (newly revealed content) that spatial edge detection "
                    "misses.\n"
                    "Lower = more aggressive rejection, may cause flickering.\n"
                    "Recommended: 0.01 - 0.05");
            }

            ImGui::SliderFloat("Color Variance", &postFx.taaColorVarianceThreshold_, 0.0f, 0.5f,
                               "%.2f (0 = off, rejects at shadow edges)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Rejects TAA history where neighborhood color variance is high.\n"
                    "Targets shadow boundary fringing caused by stochastic DDA shadow jitter.\n"
                    "Higher = more aggressive rejection, may reduce AA quality on stable edges.\n"
                    "Recommended: 0.10 - 0.25");
            }

            ImGui::Separator();
            if (ImGui::Button("Reset TAA History"))
            {
                postFx.taaResetHistory_ = true;
            }

            const char* taaDebugModes[] = {"Off", "Show t Value", "Show Reactive Mask",
                                           "Show Velocity", "Show Depth Edges",
                                           "Show Normal Edges", "Show Cross-Frame Depth",
                                           "Show Color Variance"};
            ImGui::Combo("TAA Debug", &postFx.taaDebugMode_, taaDebugModes, 8);
        }

        ImGui::Separator();
        ImGui::Checkbox("Enable FXAA (after TAA)", &postFx.fxaaEnabled_);
        ImGui::SliderFloat("Sharpen", &postFx.taaSharpen_, 0.0f, 1.0f);

        ImGui::Unindent();
    }
    return requests;
}

GlassRequests DrawGlass(engine::render::GlassSettings& glass,
                        const GlassState& state)
{
    GlassRequests requests{};
    if (ImGui::CollapsingHeader("Glass", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        bool glassEnabled = state.enabled;
        if (ImGui::Checkbox("Enable Glass (F8)", &glassEnabled))
        {
            requests.enabled = glassEnabled;
        }

        if (state.presetName != nullptr && ImGui::Button(state.presetName))
        {
            requests.applyPreset = true;
        }

        ImGui::ColorEdit3("Glass Tint", &glass.glassTint_.x);
        ImGui::SliderFloat("Absorption##Glass", &glass.glassAbsorption_, 0.0f, 3.0f);
        ImGui::SliderFloat("Thickness Scale", &glass.glassThicknessScale_, 0.1f, 20.0f);
        ImGui::SliderFloat("Refract Strength", &glass.glassRefract_, 0.0f, 0.05f);
        ImGui::SliderFloat("IOR", &glass.glassIor_, 1.0f, 2.5f,
                           "%.2f (1.0=air, 1.5=glass, 2.4=diamond)");
        ImGui::SliderFloat("Thickness Debug Scale", &glass.glassThicknessDebugScale_, 0.05f, 5.0f);
        ImGui::SliderFloat("Refract Debug Scale", &glass.glassRefractDebugScale_, 0.001f, 0.1f);
        ImGui::ColorEdit3("Reflection Color", &glass.glassReflection_.x);

        int glassDebug = state.debugMode;
        const char* glassDebugItems =
            "Off\0Thickness\0Refraction\0Fresnel\0Scene Color\0Transmittance\0Reflection\0"
            "Waterline/Rim\0Bubbles\0Iridescence\0";
        if (ImGui::Combo("Glass Debug (F9)", &glassDebug, glassDebugItems))
        {
            requests.debugMode = glassDebug;
        }

        ImGui::Unindent();
    }
    return requests;
}

std::optional<int> DrawWaterDebugMode(int currentMode)
{
    int waterDebug = currentMode;
    const char* debugItems =
        "Off\0Water Path\0Fresnel\0Reflection Mix\0Alpha\0Refraction Src\0"
        "Path+Contact+Sparkle\0Reflection+Planar\0Reflection Color\0"
        "Refraction Color\0Planar Mask\0Body Tint\0Surface Normal\0"
        "Raw Planar Projected\0Fallback Reflection\0Raw Planar Screen UV\0"
        "Planar Projected UV\0Raw Planar No Distort\0Voxel VFX\0Voxel Foam\0"
        "Body Detail\0Water Body Path\0Water Body Density\0Water Body Composite\0";
    if (ImGui::Combo("Water Debug (F7)", &waterDebug, debugItems))
    {
        return waterDebug;
    }
    return std::nullopt;
}

void DrawDdaDebugModes(engine::render::VoxelDebugSettings& voxelDebug)
{
    const char* ddaDebugItems = "Off\0Hit Axis\0Step Heatmap\0HitId\0Occ Mip View\0";
    ImGui::Combo("DDA Debug", &voxelDebug.voxelDdaDebugMode_, ddaDebugItems);

    const char* ddaAdvancedItems =
        "Off\0Edge Diagnostic\0Voxel Position\0Hit Axis (RGB)\0Closest Face (Hard Normal)\0"
        "Closest Face Diff (Axis vs Closest)\0Depth Validity (Clip)\0Clamp Diagnostic\0"
        "Transmissive Layers\0Voxel Water Distance\0Bevel Mask\0Material Atlas Mask\0"
        "Material Atlas Only\0Cavity Mask\0Pixel Edge Shadow Mask\0"
        "Cell Variation Signal\0";
    if (ImGui::Combo("DDA Edge Debug", &voxelDebug.voxelDdaAdvancedDebug_, ddaAdvancedItems))
    {
        if (voxelDebug.voxelDdaAdvancedDebug_ > 0)
        {
            voxelDebug.voxelDdaDebugMode_ = 0;
        }
    }
    if (ImGui::IsItemHovered() && voxelDebug.voxelDdaAdvancedDebug_ > 0)
    {
        ImGui::SetTooltip(
            "1: voxelFrac.xy in RG, hitAxis in B\n"
            "2: Position within voxel (RGB)\n"
            "3: R=X, G=Y, B=Z axis hit\n"
            "4: Closest face from voxelFrac (encoded normal)\n"
            "5: Magenta where hitAxis != closest face\n"
            "6: Depth validity (magenta=clip.w<=0, red<0, yellow>1, green=valid)\n"
            "7: Clamp diagnostic (R=entry, G=exit, B=p clamped)\n"
            "8: Transmissive layers passed (glass/water)\n"
            "9: Voxel water distance heatmap\n"
            "10: Material-aware bevel mask\n"
            "11: Material atlas category mask\n"
            "12: Material atlas signal only\n"
            "13: Material cavity/contact mask\n"
            "14: Pixel edge shadow mask\n"
            "15: Integer-cell hue/saturation/value variation signal");
    }
}

bool DrawCamera(const CameraStatus& status)
{
    bool resetRequested = false;
    if (ImGui::CollapsingHeader("Camera"))
    {
        ImGui::Indent();
        ImGui::Text("Position: (%.1f, %.1f, %.1f)",
                    status.position.x, status.position.y, status.position.z);
        ImGui::Text("Yaw: %.2f, Pitch: %.2f", status.yaw, status.pitch);

        resetRequested = ImGui::Button("Reset Camera (R)");

        ImGui::Spacing();
        ImGui::TextDisabled("WASD: Move");
        ImGui::TextDisabled("QE: Up/Down");
        ImGui::TextDisabled("Shift: Sprint");
        ImGui::TextDisabled("M: Toggle mouse capture");
        ImGui::TextDisabled("Right Mouse: Look (when not captured)");

        ImGui::Unindent();
    }
    return resetRequested;
}

std::optional<SwapPresentMode> DrawPerformanceStats(
    SwapPresentMode presentMode,
    engine::render::FramePacingSettings& framePacing,
    const PerformanceStatus& status)
{
    ImGui::Text("Performance:");
    std::optional<SwapPresentMode> requestedPresentMode;
    const char* presentModeLabels[] = {"FIFO", "MAILBOX", "IMMEDIATE"};
    int selectedPresentMode = static_cast<int>(presentMode);
    if (ImGui::Combo("Present Mode", &selectedPresentMode, presentModeLabels,
                     IM_ARRAYSIZE(presentModeLabels)))
    {
        requestedPresentMode = static_cast<SwapPresentMode>(selectedPresentMode);
    }
    ImGui::SliderFloat("Max FPS", &framePacing.maxFpsLimit_, 0.0f, 240.0f, "%.0f");
    if (framePacing.maxFpsLimit_ <= 0.0f)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Uncapped");
    }
    ImGui::Checkbox("Focused Idle Throttle", &framePacing.focusedIdleThrottleEnabled_);
    ImGui::BeginDisabled(!framePacing.focusedIdleThrottleEnabled_);
    ImGui::SliderFloat("Focused Idle FPS", &framePacing.focusedIdleFpsLimit_, 5.0f, 60.0f, "%.0f");
    ImGui::SliderFloat("Focused Idle Delay", &framePacing.focusedIdleDelaySeconds_, 0.10f, 2.0f, "%.2f s");
    ImGui::EndDisabled();
    ImGui::Checkbox("Background Throttle", &framePacing.backgroundThrottleEnabled_);
    ImGui::BeginDisabled(!framePacing.backgroundThrottleEnabled_);
    ImGui::SliderFloat("Background FPS", &framePacing.backgroundFpsLimit_, 15.0f, 60.0f, "%.0f");
    ImGui::EndDisabled();
    const char* throttleLabel =
        status.backgroundThrottleActive ? "Background"
                                        : (status.focusedIdleThrottleActive ? "Focused Idle" : "Foreground");
    ImGui::Text("Throttle: %s", throttleLabel);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

    return requestedPresentMode;
}

} // namespace DebugInfoPanel
