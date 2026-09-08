#include "UI/Panels/RenderingPanel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>

#include <imgui.h>

#include "App/Camera.h"
#include "App/GlassPresets.h"
#include "UI/EngineFacade.h"
#include "UI/EditorWidgets.h"
#include "UI/Panels/SunroofLivingWaterControls.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/GpuProfiler.h"
#include "engine/render/RenderQualityPreset.h"
#include "engine/render/passes/OBBPass.h"
#include "engine/voxel/WaterVolume.h"

namespace RenderingPanel
{

namespace
{
// tab drawing functions (forward declarations)
void DrawLightingTab(EngineFacade::RenderingPanelAccess& app);
void DrawPostFXTab(EngineFacade::RenderingPanelAccess& app);
void DrawMaterialsTab(EngineFacade::RenderingPanelAccess& app);
void DrawVoxelsTab(EngineFacade::RenderingPanelAccess& app);
void DrawDebugTab(EngineFacade::RenderingPanelAccess& app);

int AuxiliaryRayTier(float scale)
{
    if (scale < 0.625f)
    {
        return 2;
    }
    return scale < 0.875f ? 1 : 0;
}

float AuxiliaryRayScaleForTier(int tier)
{
    constexpr float kScales[] = {
        1.0f,
        engine::render::kQualityAuxiliaryRayScale,
        engine::render::kHalfResolutionAuxiliaryRayScale,
    };
    return kScales[std::clamp(tier, 0, 2)];
}
} // namespace

void Init()
{
    // nothing to initialize
}

void Shutdown()
{
    // nothing to clean up
}

ImVec2 Draw(EngineFacade& engine, ImVec2 position, ImVec2 size)
{
    auto app = engine.renderingPanel();
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, size.y), ImVec2(FLT_MAX, size.y));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Rendering", nullptr, flags))
    {
        if (ImGui::BeginTabBar("RenderingTabs"))
        {
            if (ImGui::BeginTabItem("Lighting"))
            {
                DrawLightingTab(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Post FX"))
            {
                DrawPostFXTab(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Materials"))
            {
                DrawMaterialsTab(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Voxels"))
            {
                DrawVoxelsTab(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Debug"))
            {
                DrawDebugTab(app);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    const ImVec2 finalSize = ImGui::GetWindowSize();
    ImGui::End();
    return finalSize;
}

namespace
{

void DrawLightingTab(EngineFacade::RenderingPanelAccess& app)
{
    auto& lighting = app.lighting;
    auto& shadows = app.shadows;
    auto& ao = app.ao;

    // =========================================================================
    // area lights
    // =========================================================================
    EditorWidgets::SectionHeader("Area Lights");

    ImGui::Checkbox("Enable Local Lights (L)", &lighting.pointLightsEnabled_);
    const int authoredLightCount = static_cast<int>(lighting.areaLights_.size());
    ImGui::Text("Authored Lights: %d", authoredLightCount);
    const int maxActiveLights = std::max(0, authoredLightCount);
    lighting.lightCount_ = std::clamp(lighting.lightCount_, 0, maxActiveLights);
    ImGui::SliderInt("Active Lights", &lighting.lightCount_, 0, maxActiveLights);
    ImGui::Checkbox("Light Heatmap (H)", &lighting.lightHeatmap_);
    ImGui::Checkbox("Cascade Debug (C)", &lighting.cascadeDebug_);

    if (ImGui::Button("Reset Scene Preset"))
    {
        app.resetAreaLightsToScenePreset();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Sphere"))
    {
        EngineFacade::RenderingPanelAccess::EditableAreaLight light{};
        light.shape = LightShape::Sphere;
        light.sourceRadius = 0.45f;
        light.position = app.camera().position + glm::vec3(0.0f, 2.5f, 0.0f);
        light.capsuleEndA = light.position + glm::vec3(-1.5f, 0.0f, 0.0f);
        light.capsuleEndB = light.position + glm::vec3(1.5f, 0.0f, 0.0f);
        lighting.areaLights_.push_back(light);
        lighting.lightCount_ = static_cast<int>(lighting.areaLights_.size());
        shadows.localShadowResetHistory_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Rect"))
    {
        EngineFacade::RenderingPanelAccess::EditableAreaLight light{};
        light.shape = LightShape::Rectangle;
        light.position = app.camera().position + glm::vec3(0.0f, 3.0f, 0.0f);
        light.edge1 = glm::vec3(2.0f, 0.0f, 0.0f);
        light.edge2 = glm::vec3(0.0f, 0.0f, 1.5f);
        light.capsuleEndA = light.position + glm::vec3(-1.5f, 0.0f, 0.0f);
        light.capsuleEndB = light.position + glm::vec3(1.5f, 0.0f, 0.0f);
        lighting.areaLights_.push_back(light);
        lighting.lightCount_ = static_cast<int>(lighting.areaLights_.size());
        shadows.localShadowResetHistory_ = true;
    }
    if (ImGui::Button("+ Disc"))
    {
        EngineFacade::RenderingPanelAccess::EditableAreaLight light{};
        light.shape = LightShape::Disc;
        light.sourceRadius = 0.8f;
        light.position = app.camera().position + glm::vec3(0.0f, 2.0f, 0.0f);
        light.discNormal = glm::vec3(0.0f, -1.0f, 0.0f);
        light.capsuleEndA = light.position + glm::vec3(-1.5f, 0.0f, 0.0f);
        light.capsuleEndB = light.position + glm::vec3(1.5f, 0.0f, 0.0f);
        lighting.areaLights_.push_back(light);
        lighting.lightCount_ = static_cast<int>(lighting.areaLights_.size());
        shadows.localShadowResetHistory_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Capsule"))
    {
        EngineFacade::RenderingPanelAccess::EditableAreaLight light{};
        light.shape = LightShape::Capsule;
        light.sourceRadius = 0.2f;
        light.position = app.camera().position + glm::vec3(0.0f, 2.5f, 0.0f);
        light.capsuleEndA = light.position + glm::vec3(-2.0f, 0.0f, 0.0f);
        light.capsuleEndB = light.position + glm::vec3(2.0f, 0.0f, 0.0f);
        lighting.areaLights_.push_back(light);
        lighting.lightCount_ = static_cast<int>(lighting.areaLights_.size());
        shadows.localShadowResetHistory_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Point"))
    {
        EngineFacade::RenderingPanelAccess::EditableAreaLight light{};
        light.shape = LightShape::Point;
        light.position = app.camera().position + glm::vec3(0.0f, 2.0f, 0.0f);
        light.capsuleEndA = light.position + glm::vec3(-1.5f, 0.0f, 0.0f);
        light.capsuleEndB = light.position + glm::vec3(1.5f, 0.0f, 0.0f);
        lighting.areaLights_.push_back(light);
        lighting.lightCount_ = static_cast<int>(lighting.areaLights_.size());
        shadows.localShadowResetHistory_ = true;
    }

    int removeLightIndex = -1;
    bool anyLightChanged = false;
    static const char* kShapeNames[] = {"Sphere", "Rectangle", "Disc", "Capsule", "Point"};
    for (int i = 0; i < static_cast<int>(lighting.areaLights_.size()); ++i)
    {
        auto& light = lighting.areaLights_[i];
        ImGui::PushID(i);

        const bool active = i < lighting.lightCount_;
        char lightLabel[96];
        std::snprintf(lightLabel, sizeof(lightLabel), "Light %d (%s)%s", i,
                      kShapeNames[static_cast<int>(light.shape)],
                      active ? "" : " [inactive]");
        if (ImGui::TreeNode(lightLabel))
        {
            int shape = static_cast<int>(light.shape);
            if (ImGui::Combo("Shape", &shape, kShapeNames, IM_ARRAYSIZE(kShapeNames)))
            {
                light.shape = static_cast<LightShape>(std::clamp(shape, 0, 4));
                anyLightChanged = true;
            }

            anyLightChanged |= ImGui::Checkbox("Casts Local Shadows", &light.castsShadows);
            anyLightChanged |= ImGui::DragFloat3("Position", &light.position.x, 0.1f);
            anyLightChanged |=
                ImGui::SliderFloat("Influence Radius", &light.influenceRadius, 0.5f, 120.0f);
            anyLightChanged |= ImGui::ColorEdit3("Color", &light.color.x);
            anyLightChanged |= ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 20.0f);

            const bool hasSourceRadius = light.shape == LightShape::Sphere ||
                                         light.shape == LightShape::Disc ||
                                         light.shape == LightShape::Capsule;
            if (hasSourceRadius)
            {
                anyLightChanged |=
                    ImGui::SliderFloat("Source Radius", &light.sourceRadius, 0.0f, 5.0f);
            }

            if (light.shape == LightShape::Rectangle)
            {
                anyLightChanged |= ImGui::DragFloat3("Edge1", &light.edge1.x, 0.1f);
                anyLightChanged |= ImGui::DragFloat3("Edge2", &light.edge2.x, 0.1f);
            }
            else if (light.shape == LightShape::Disc)
            {
                anyLightChanged |= ImGui::DragFloat3("Disc Normal", &light.discNormal.x, 0.01f);
            }
            else if (light.shape == LightShape::Capsule)
            {
                anyLightChanged |= ImGui::DragFloat3("End A", &light.capsuleEndA.x, 0.1f);
                anyLightChanged |= ImGui::DragFloat3("End B", &light.capsuleEndB.x, 0.1f);
            }

            if (ImGui::Button("Delete Light"))
            {
                removeLightIndex = i;
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (removeLightIndex >= 0 && removeLightIndex < static_cast<int>(lighting.areaLights_.size()))
    {
        lighting.areaLights_.erase(lighting.areaLights_.begin() + removeLightIndex);
        lighting.lightCount_ = std::clamp(lighting.lightCount_, 0, static_cast<int>(lighting.areaLights_.size()));
        anyLightChanged = true;
    }
    if (anyLightChanged)
    {
        shadows.localShadowResetHistory_ = true;
    }

    // =========================================================================
    // sun light
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Sun Light"))
    {
        bool sunChanged = false;

        sunChanged |= ImGui::SliderFloat("Elevation", &lighting.sunElevation_, 5.0f, 90.0f, "%.1f deg");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sun height above horizon.\n"
                              "15-25 = dramatic long shadows (sunset)\n"
                              "45-60 = natural daytime\n"
                              "80-90 = directly overhead (noon)");

        sunChanged |= ImGui::SliderFloat("Azimuth", &lighting.sunAzimuth_, 0.0f, 360.0f, "%.1f deg");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sun compass direction.\n"
                              "Rotate to change shadow/caustic direction.");

        if (sunChanged)
        {
            app.updateSunDirection();
        }

        float sunCol[3] = {lighting.sunColor_.x, lighting.sunColor_.y, lighting.sunColor_.z};
        if (ImGui::ColorEdit3("Sun Color", sunCol))
        {
            lighting.sunColor_ = glm::vec3(sunCol[0], sunCol[1], sunCol[2]);
        }

        ImGui::SliderFloat("Sun Intensity", &lighting.sunIntensity_, 0.0f, 10.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("HDR brightness multiplier.\n"
                              "1.0 = dim, 2.5 = natural, 5.0+ = harsh");

        // quick presets
        ImGui::Separator();
        ImGui::Text("Presets:");
        if (ImGui::Button("Morning"))
        {
            lighting.sunElevation_ = 20.0f;
            lighting.sunAzimuth_ = 90.0f;
            lighting.sunColor_ = glm::vec3(1.0f, 0.85f, 0.6f);
            lighting.sunIntensity_ = 2.0f;
            app.updateSunDirection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Noon"))
        {
            lighting.sunElevation_ = 70.0f;
            lighting.sunAzimuth_ = 135.0f;
            lighting.sunColor_ = glm::vec3(1.0f, 0.98f, 0.92f);
            lighting.sunIntensity_ = 3.0f;
            app.updateSunDirection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Sunset"))
        {
            lighting.sunElevation_ = 12.0f;
            lighting.sunAzimuth_ = 270.0f;
            lighting.sunColor_ = glm::vec3(1.0f, 0.6f, 0.3f);
            lighting.sunIntensity_ = 2.5f;
            app.updateSunDirection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cool"))
        {
            lighting.sunElevation_ = 45.0f;
            lighting.sunAzimuth_ = 200.0f;
            lighting.sunColor_ = glm::vec3(0.8f, 0.9f, 1.0f);
            lighting.sunIntensity_ = 2.0f;
            app.updateSunDirection();
        }
    }

    if (EditorWidgets::SectionHeaderCollapsible("Hemisphere Ambient", false))
    {
        auto& ambient = lighting.hemisphereAmbient_;
        ImGui::SliderFloat("Strength##HemisphereAmbient", &ambient.strength, 0.0f, 1.0f);
        ImGui::ColorEdit3(
            "Sky Tint##HemisphereAmbient", &ambient.skyTint.x,
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3(
            "Ground Tint##HemisphereAmbient", &ambient.groundTint.x,
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        EditorWidgets::HelpMarker(
            "Normal-oriented ambient tint. Up-facing surfaces receive Sky Tint; "
            "down-facing surfaces receive Ground Tint. Strength 0 is the exact "
            "legacy compatibility path.");
        if (ImGui::Button("Warm/Cool Outdoor v0##HemisphereAmbient"))
        {
            ambient = engine::render::makeHemisphereAmbientSettings(
                engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Compatibility Off##HemisphereAmbient"))
        {
            ambient = {};
        }
    }

    if (EditorWidgets::SectionHeaderCollapsible("Scene Atmosphere", false))
    {
        auto& atmosphere = lighting.sceneAtmosphere_;
        ImGui::SliderFloat("Density##SceneAtmosphere", &atmosphere.density,
                           0.0f, 0.04f, "%.4f");
        ImGui::SliderFloat("Height Falloff##SceneAtmosphere",
                           &atmosphere.heightFalloff, 0.0f, 0.20f, "%.3f");
        ImGui::SliderFloat("Base Height##SceneAtmosphere",
                           &atmosphere.baseHeight, -10.0f, 50.0f, "%.2f m");
        ImGui::SliderFloat("Sun Phase Strength##SceneAtmosphere",
                           &atmosphere.sunPhaseStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Sun Phase Exponent##SceneAtmosphere",
                           &atmosphere.sunPhaseExponent, 1.0f, 16.0f, "%.1f");
        EditorWidgets::HelpMarker(
            "Transient air haze. Lighting resolves ordered water-volume "
            "intersections and integrates only the remaining air path. Density 0 is "
            "the exact compatibility path; persistent profile ownership is deferred.");
        if (ImGui::Button("Outdoor Haze v0##SceneAtmosphere"))
        {
            atmosphere = engine::render::makeSceneAtmosphereSettings(
                engine::render::SceneAtmospherePreset::OutdoorHazeV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Compatibility Off##SceneAtmosphere"))
        {
            atmosphere = {};
        }
    }

    if (EditorWidgets::SectionHeaderCollapsible("Painted Sky", false))
    {
        auto& sky = lighting.paintedSky_;
        bool enabled = sky.enabled();
        if (ImGui::Checkbox("Enabled##PaintedSky", &enabled))
        {
            if (enabled)
            {
                sky = engine::render::makePaintedSkySettings(
                    engine::render::PaintedSkyPreset::SoftDayV0);
            }
            else
            {
                sky.strength = 0.0f;
            }
        }
        ImGui::SliderFloat("Strength##PaintedSky", &sky.strength, 0.0f, 1.0f);
        ImGui::ColorEdit3("Horizon Tint##PaintedSky", &sky.horizonTint.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("Zenith Tint##PaintedSky", &sky.zenithTint.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("Lower Tint##PaintedSky",
                          &sky.lowerHemisphereTint.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::SliderFloat("Gradient Curve##PaintedSky", &sky.gradientExponent,
                           0.10f, 4.0f, "%.2f");
        ImGui::SliderFloat("Horizon Warmth##PaintedSky",
                           &sky.horizonBandStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Horizon Width##PaintedSky",
                           &sky.horizonBandExponent, 0.5f, 12.0f, "%.1f");
        ImGui::SliderFloat("Sun Disc Radius##PaintedSky",
                           &sky.sunDiscAngularRadius, 0.001f, 0.040f,
                           "%.4f rad");
        ImGui::SliderFloat("Sun Disc Softness##PaintedSky",
                           &sky.sunDiscSoftness, 0.0f, 0.010f, "%.4f rad");
        ImGui::SliderFloat("Sun Disc Energy##PaintedSky",
                           &sky.sunDiscIntensity, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Sun Halo Energy##PaintedSky",
                           &sky.sunHaloIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Sun Halo Focus##PaintedSky",
                           &sky.sunHaloExponent, 4.0f, 160.0f, "%.1f");
        EditorWidgets::HelpMarker(
            "Texture-free background gradient, horizon warmth, sun disc, and halo. "
            "It uses the authored Sky Color and sun direction, then enters the "
            "existing atmosphere and composite pipeline. Strength 0 preserves the "
            "historical flat sky exactly.");

        if (ImGui::Button("Day v0##PaintedSky"))
        {
            sky = engine::render::makePaintedSkySettings(
                engine::render::PaintedSkyPreset::SoftDayV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Sunset v0##PaintedSky"))
        {
            sky = engine::render::makePaintedSkySettings(
                engine::render::PaintedSkyPreset::WarmSunsetV0);
        }
        if (ImGui::Button("Dawn v0##PaintedSky"))
        {
            sky = engine::render::makePaintedSkySettings(
                engine::render::PaintedSkyPreset::DawnV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Night v0##PaintedSky"))
        {
            sky = engine::render::makePaintedSkySettings(
                engine::render::PaintedSkyPreset::NightV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Compatibility Off##PaintedSky"))
        {
            sky = {};
        }
        sky = engine::render::sanitizePaintedSkySettings(sky);
    }

    if (EditorWidgets::SectionHeaderCollapsible("Painted Clouds", false))
    {
        auto& clouds = lighting.paintedClouds_;
        bool enabled = clouds.enabled();
        if (ImGui::Checkbox("Enabled##PaintedClouds", &enabled))
        {
            if (enabled)
            {
                clouds = engine::render::makePaintedCloudSettings(
                    engine::render::PaintedCloudPreset::SoftDayV0);
            }
            else
            {
                clouds.strength = 0.0f;
            }
        }
        ImGui::SliderFloat("Strength##PaintedClouds", &clouds.strength,
                           0.0f, 1.0f);
        ImGui::SliderFloat("Coverage##PaintedClouds", &clouds.coverage,
                           0.0f, 1.0f);
        ImGui::SliderFloat("Opacity##PaintedClouds", &clouds.opacity,
                           0.0f, 1.0f);
        ImGui::SliderFloat("Edge Softness##PaintedClouds", &clouds.softness,
                           0.01f, 0.30f, "%.3f");
        ImGui::SliderFloat("Layer Altitude##PaintedClouds", &clouds.altitude,
                           10.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("Formation Scale##PaintedClouds", &clouds.worldScale,
                           16.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("Painted Detail##PaintedClouds",
                           &clouds.detailStrength, 0.0f, 1.0f);
        ImGui::ColorEdit3("Light Tint##PaintedClouds", &clouds.lightTint.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("Shadow Tint##PaintedClouds", &clouds.shadowTint.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::SliderFloat("Silver Lining##PaintedClouds",
                           &clouds.silverLiningStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Horizon Fade Start##PaintedClouds",
                           &clouds.horizonFadeStart, 0.0f, 0.40f, "%.3f");
        ImGui::SliderFloat("Horizon Fade End##PaintedClouds",
                           &clouds.horizonFadeEnd, 0.02f, 0.60f, "%.3f");
        EditorWidgets::HelpMarker(
            "One world-anchored, texture-free cloud layer in the existing Lighting "
            "sky branch. The shader owns exactly three analytic noise octaves and "
            "zero cloud shadow samples. Drift comes only from Scene > Environment "
            "Wind; no separate cloud wind authority exists. Strength 0 is exact "
            "compatibility.");

        if (ImGui::Button("Soft Day v0##PaintedClouds"))
        {
            clouds = engine::render::makePaintedCloudSettings(
                engine::render::PaintedCloudPreset::SoftDayV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Sparse v0##PaintedClouds"))
        {
            clouds = engine::render::makePaintedCloudSettings(
                engine::render::PaintedCloudPreset::SparseDayV0);
        }
        if (ImGui::Button("Overcast v0##PaintedClouds"))
        {
            clouds = engine::render::makePaintedCloudSettings(
                engine::render::PaintedCloudPreset::OvercastV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Compatibility Off##PaintedClouds"))
        {
            clouds = {};
        }
        clouds = engine::render::sanitizePaintedCloudSettings(clouds);
    }

    // =========================================================================
    // shadows
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Shadows"))
    {
        ImGui::Checkbox("Enable CSM Shadows", &shadows.csmEnabled_);
        if (ImGui::Checkbox("Adaptive Auxiliary Shadow Rays",
                            &shadows.adaptiveRayResolution_))
        {
            shadows.shadowResetHistory_ = true;
            shadows.localShadowResetHistory_ = true;
        }
        EditorWidgets::HelpMarker(
            "Keeps primary voxels full resolution while sun and local-light shadow rays "
            "step down at high projected voxel coverage. Camera-inside views use the "
            "half-resolution path; temporal reconstruction remains full resolution. "
            "This is the qualified product default; disable it for a fixed authored tier.");
        const bool ddaAvailable = app.isDdaShadowsAvailable();
        if (!ddaAvailable)
        {
            ImGui::TextDisabled("DDA shadow tracing unavailable in this run.");
        }

        ImGui::BeginDisabled(!ddaAvailable);
        ImGui::Checkbox("Use DDA Shadows (Stochastic)", &shadows.useDDAShadows_);

        if (shadows.useDDAShadows_ && ddaAvailable)
        {
            ImGui::Indent();

            int rayResolutionTier = AuxiliaryRayTier(shadows.ddaRayResolutionScale_);
            const char* rayResolutionNames[] = {"Native", "Quality (75%)", "Half (50%)"};
            if (ImGui::Combo("Ray Resolution##DdaShadow", &rayResolutionTier,
                             rayResolutionNames, IM_ARRAYSIZE(rayResolutionNames)))
            {
                app.setAuxiliaryRayScales(AuxiliaryRayScaleForTier(rayResolutionTier),
                                          ao.aoRayResolutionScale_);
                shadows.shadowResetHistory_ = true;
            }
            EditorWidgets::HelpMarker(
                "Sets the maximum sun/local shadow-ray resolution. Adaptive mode may select a "
                "lower active tier; temporal resolve and denoise remain at scene resolution.");

            ImGui::SliderFloat("Sun Angular Radius", &shadows.sunAngularRadius_, 0.0f, 0.1f);
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                // keep the old result responsive while dragging, then restart
                // accumulation once for the final sun-disc distribution.
                shadows.shadowResetHistory_ = true;
            }
            ImGui::SliderFloat("Max Shadow Distance", &shadows.maxShadowDist_, 10.0f, 500.0f);
            ImGui::SliderFloat("Normal Bias", &shadows.shadowNormalBias_, 0.0f, 0.2f);
            ImGui::SliderInt("Max Steps Per Volume", &shadows.maxShadowSteps_, 32, 256);
            if (ImGui::SliderFloat("Foliage Shadow Opacity", &shadows.foliageShadowOpacity_, 0.0f,
                                   2.0f))
            {
                shadows.shadowResetHistory_ = true;
            }
            EditorWidgets::HelpMarker(
                "Controls translucent sun shadows from both hero canopies and the "
                "instanced meadow's lightweight voxel proxy. Meadow shadows use a "
                "softer 45% internal scale; trunks and opaque objects are unaffected.");

            EditorWidgets::Spacing();

            ImGui::Text("Shadow Temporal:");
            ImGui::SliderFloat("Blend Alpha", &shadows.shadowBlendAlpha_, 0.01f, 0.5f);
            ImGui::SliderFloat("Depth Reject", &shadows.shadowDepthReject_, 0.0001f, 0.01f);

            if (ImGui::Button("Reset Shadow History"))
            {
                shadows.shadowResetHistory_ = true;
            }

            ImGui::Unindent();
        }
        ImGui::EndDisabled();

        if (EditorWidgets::SectionHeaderCollapsible("Local Light Shadows", false))
        {
            const bool localShadowsAvailable = app.isLocalLightShadowsAvailable();
            if (!localShadowsAvailable)
            {
                ImGui::TextDisabled("Local light shadows unavailable in this run.");
            }

            ImGui::BeginDisabled(!localShadowsAvailable);
            ImGui::Checkbox("Enable Local Light Shadows", &shadows.localLightShadowsEnabled_);
            ImGui::SliderInt("Shadow-Casting Slots", &shadows.localShadowCastingLightCount_, 0, 4);
            ImGui::SliderFloat("Local Blend Alpha", &shadows.localShadowBlendAlpha_, 0.01f, 0.5f);
            ImGui::SliderFloat("Local Depth Reject", &shadows.localShadowDepthReject_, 0.0001f, 0.01f);
            if (ImGui::Button("Reset Local Shadow History"))
            {
                shadows.localShadowResetHistory_ = true;
            }
            ImGui::Separator();
            const bool localBlurAvailable = app.isLocalShadowBlurAvailable();
            if (!localBlurAvailable && localShadowsAvailable)
            {
                ImGui::TextDisabled("Spatial blur unavailable in this run.");
            }
            ImGui::BeginDisabled(!localBlurAvailable);
            ImGui::Checkbox("Spatial Blur", &shadows.localShadowBlurEnabled_);
            if (shadows.localShadowBlurEnabled_ && localBlurAvailable)
            {
                ImGui::SliderFloat("Blur Sigma", &shadows.localShadowBlurSigma_, 0.5f, 3.0f);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }

        // shadow terminator
        if (EditorWidgets::SectionHeaderCollapsible("Shadow Terminator", false))
        {
            const char* modes[] = {"Off (Hard Edge)", "Wrap", "Smoothstep"};
            ImGui::Combo("Terminator Mode", &shadows.terminatorMode_, modes, 3);
            ImGui::SliderFloat("Softness", &shadows.terminatorSoftness_, 0.0f, 0.5f);

            if (ImGui::Button("Reset to Default"))
            {
                shadows.terminatorMode_ = 1;
                shadows.terminatorSoftness_ = 0.15f;
            }
        }
    }

    // =========================================================================
    // ambient occlusion
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Ambient Occlusion"))
    {
        const bool aoAvailable = app.isAmbientOcclusionAvailable();
        if (!aoAvailable)
        {
            ImGui::TextDisabled("Ambient occlusion unavailable in this run.");
        }

        ImGui::BeginDisabled(!aoAvailable);
        bool aoChanged = false;
        aoChanged |= ImGui::Checkbox("Enable AO", &ao.aoEnabled_);

        if (ao.aoEnabled_ && aoAvailable)
        {
            if (ImGui::Checkbox("Adaptive Ray Resolution##AO",
                                &ao.adaptiveRayResolution_))
            {
                aoChanged = true;
            }
            EditorWidgets::HelpMarker(
                "Keeps primary voxels full resolution. Adaptive AO remains Native through "
                "intermediate coverage, then selects Half at high coverage or when the "
                "camera is inside a volume; full-resolution reconstruction remains enabled. "
                "This is the qualified product default; disable it for a fixed authored tier.");

            int rayResolutionTier = AuxiliaryRayTier(ao.aoRayResolutionScale_);
            const char* rayResolutionNames[] = {"Native", "Quality (75%)", "Half (50%)"};
            if (ImGui::Combo("Ray Resolution##AO", &rayResolutionTier,
                             rayResolutionNames, IM_ARRAYSIZE(rayResolutionNames)))
            {
                app.setAuxiliaryRayScales(shadows.ddaRayResolutionScale_,
                                          AuxiliaryRayScaleForTier(rayResolutionTier));
                aoChanged = true;
            }
            EditorWidgets::HelpMarker(
                "Sets the maximum AO ray resolution. Adaptive AO uses Native or Half because "
                "the 75% reconstruction path is not a performance win; fixed Quality remains "
                "available for explicit authoring and comparison.");

            const char* distanceModes[] = {
                "Fixed World Reach",
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
                aoChanged |= ImGui::SliderFloat(
                    "Minimum World Distance", &ao.aoProjectedMinDistance_, 0.0f,
                    8.0f, "%.1f m");
                ImGui::TextDisabled("512 px / 2 m: validated performance tier");
            }
            const char* reachLabel =
                ao.aoDistanceMode_ ==
                        engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius
                    ? "Maximum Reach Cap"
                    : "Trace Reach";
            aoChanged |= ImGui::SliderFloat(reachLabel, &ao.aoMaxDistance_,
                                            0.25f, 20.0f, "%.2f m",
                                            ImGuiSliderFlags_Logarithmic);
            EditorWidgets::HelpMarker(
                "Sets the spatial scale of AO. Short reach isolates voxel contacts; long "
                "reach includes broader cavities. Screen-scaled mode treats this as a cap.");
            aoChanged |= ImGui::SliderFloat("Sample Step (Quality)", &ao.aoStepSize_,
                                            0.05f, 0.5f, "%.2f m");
            EditorWidgets::HelpMarker(
                "Ray-march spacing. Smaller values can catch thinner occluders but cost more; "
                "this is primarily a quality/performance control.");
            aoChanged |= ImGui::SliderFloat("Ray AO Intensity", &ao.aoIntensity_,
                                            0.0f, 2.0f);
            EditorWidgets::HelpMarker(
                "Darkness of the sampled occlusion before the painted-material response.");
            aoChanged |= ImGui::SliderFloat("Ambient Fill Energy",
                                            &ao.aoContribution_, 0.0f, 0.5f);
            EditorWidgets::HelpMarker(
                "Overall hemisphere-ambient energy. This changes scene fill brightness; it is "
                "not a second AO-darkness control.");
            aoChanged |= ImGui::SliderFloat("Surface Bias", &ao.aoBias_, 0.01f, 0.2f);
            EditorWidgets::HelpMarker(
                "Moves ray origins away from the visible surface. Raise it to reduce self-"
                "occlusion; excessive values detach contact AO.");
            aoChanged |= ImGui::SliderInt("Authored Samples / Pixel", &ao.aoRayCount_, 1, 4);
            EditorWidgets::HelpMarker(
                "More hemisphere rays reduce AO grain before temporal accumulation. Four is "
                "the low-noise quality mode; two remains the baseline. The Half ray-resolution "
                "tier automatically doubles this count, up to four, to control reconstruction "
                "noise while retaining a net ray-work saving.");

            if (ao.aoRayCount_ == 1)
            {
                EditorWidgets::TextWarning("(Performance mode - lower quality)");
            }
            else if (ao.aoRayCount_ >= 4)
            {
                ImGui::TextDisabled("Low-noise quality mode");
            }

            EditorWidgets::Spacing();

            ImGui::Text("AO Temporal:");
            aoChanged |= ImGui::SliderFloat("Blend Alpha##AO", &ao.aoBlendAlpha_,
                                            0.01f, 0.5f);
            EditorWidgets::HelpMarker(
                "Higher values react faster but expose more noise; lower values converge more "
                "slowly and can retain history longer. The Half ray-resolution tier uses half "
                "the authored alpha, so the default 0.08 resolves at 0.04.");
            aoChanged |= ImGui::SliderFloat("Depth Reject##AO", &ao.aoDepthReject_,
                                            0.005f, 0.1f);
            EditorWidgets::HelpMarker(
                "Maximum depth difference allowed for history. Lower values reject history "
                "more aggressively around disocclusion.");
            aoChanged |= ImGui::SliderFloat("Normal Reject Dot",
                                            &ao.aoNormalRejectDot_, 0.5f, 0.99f);
            EditorWidgets::HelpMarker(
                "Minimum normal agreement for history. Higher values reject history more "
                "aggressively across changing surface orientation.");

            if (ImGui::Button("Reset AO History"))
            {
                ao.aoResetHistory_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Inspect AO Applied"))
            {
                lighting.lightingDebugMode_ = 16;
                app.diagnostics.edgeFlickerDebugMode_ = 0;
            }
            EditorWidgets::HelpMarker(
                "AO affects ambient illumination, not direct sun or specular light. "
                "AO Applied shows the final material-aware multiplier; AO Raw shows "
                "the traced field before temporal and painted-material response.");
        }
        if (aoChanged)
        {
            ao.aoResetHistory_ = true;
        }
        ImGui::EndDisabled();
    }

    // =========================================================================
    // lighting debug
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Lighting Debug", false))
    {
        const char* modes[] = {"Full", "Diffuse Only", "Specular Only", "Env Only", "AO Raw",
                               "AO Resolved", "Caustics", "Local Shadow R",
                               "Local Shadow G", "Local Shadow B", "Local Shadow A",
                               "Material Roughness", "Material Metallic", "Material Emissive",
                               "Material Category", "Hemisphere Ambient", "AO Applied"};
        if (ImGui::Combo("Debug Mode", &lighting.lightingDebugMode_, modes,
                         IM_ARRAYSIZE(modes)))
        {
            app.diagnostics.edgeFlickerDebugMode_ = 0;
        }
    }
}

void DrawPostFXTab(EngineFacade::RenderingPanelAccess& app)
{
    auto& postFx = app.postFx;
    auto& renderResolution = postFx.renderResolution_;
    auto resetTemporalHistory = [&app, &postFx]()
    {
        postFx.taaResetHistory_ = true;
        app.shadows.shadowResetHistory_ = true;
        app.shadows.localShadowResetHistory_ = true;
        app.ao.aoResetHistory_ = true;
    };

    // =========================================================================
    // render resolution
    // =========================================================================
    EditorWidgets::SectionHeader("Render Resolution");

    const std::optional<engine::render::RenderQualityPreset> activePreset =
        app.shadows.adaptiveRayResolution_ || app.ao.adaptiveRayResolution_
            ? std::nullopt
            : engine::render::matchRenderQualityPreset(
                  renderResolution.scale, app.shadows.ddaRayResolutionScale_,
                  app.ao.aoRayResolutionScale_);
    const char* activePresetLabel = activePreset.has_value()
                                        ? engine::render::renderQualityPresetLabel(*activePreset)
                                        : "Custom";
    if (ImGui::BeginCombo("Combined Quality Preset", activePresetLabel))
    {
        for (const engine::render::RenderQualityPreset preset :
             engine::render::kRenderQualityPresets)
        {
            const bool selected = activePreset == preset;
            if (ImGui::Selectable(engine::render::renderQualityPresetLabel(preset),
                                  selected))
            {
                app.applyRenderQualityPreset(preset);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    EditorWidgets::HelpMarker(
        "Atomically coordinates the scene, sun-shadow-ray, and AO-ray resolutions. "
        "Native remains the portable default. Performance is the measured 30 FPS "
        "target for the 7-core M1; every tier uses the same cross-platform Vulkan path.");

    float renderScalePercent = renderResolution.scale * 100.0f;
    if (ImGui::SliderFloat("Internal Scale", &renderScalePercent, 50.0f, 100.0f,
                           "%.0f%%"))
    {
        app.setRenderScale(renderScalePercent * 0.01f);
    }
    ImGui::TextDisabled("Scene scale only");
    if (ImGui::Button("100%##SceneScale"))
    {
        app.setRenderScale(1.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("75%##SceneScale"))
    {
        app.setRenderScale(engine::render::kQualityRenderScale);
    }
    ImGui::SameLine();
    if (ImGui::Button("67%##SceneScale"))
    {
        app.setRenderScale(engine::render::kPerformanceRenderScale);
    }
    EditorWidgets::HelpMarker(
        "These shortcuts change only the scene target. Use Combined Quality Preset "
        "to coordinate scene, sun-shadow, and AO ray resolution together.");

    int upscaleMode = static_cast<int>(renderResolution.upscaleMode);
    const char* upscaleModes[] = {"Bilinear", "Edge-Adaptive"};
    if (ImGui::Combo("Spatial Upscaler", &upscaleMode, upscaleModes,
                     IM_ARRAYSIZE(upscaleModes)))
    {
        renderResolution.upscaleMode =
            static_cast<engine::render::SpatialUpscaleMode>(upscaleMode);
    }
    if (renderResolution.upscaleMode ==
        engine::render::SpatialUpscaleMode::EdgeAdaptive)
    {
        ImGui::SliderFloat("Upscale Sharpness", &renderResolution.sharpness,
                           0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Edge Preservation", &renderResolution.edgeStrength,
                           0.0f, 1.0f, "%.2f");
    }
    ImGui::TextDisabled("Scene targets scale; editor/runtime UI remains native.");

    // =========================================================================
    // tonemapping
    // =========================================================================
    EditorWidgets::SectionHeader("Tonemapping");

    ImGui::Checkbox("Enable Tonemapping (T)", &postFx.tonemapEnabled_);
    ImGui::SliderFloat("Exposure (-/=)", &postFx.exposure_, 0.05f, 8.0f);
    ImGui::SliderFloat("Highlight Recovery", &postFx.highlightRecovery_, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Compresses bright HDR highlights before and after tonemap so pale voxel\n"
            "materials retain more shape under pixel presentation.");
    }

    // =========================================================================
    // color grade
    // =========================================================================
    EditorWidgets::SectionHeader("Color Grade");

    ImGui::Checkbox("Enable Color Grade", &postFx.colorGradeEnabled_);
    if (postFx.colorGradeEnabled_)
    {
        ImGui::SliderFloat("Grade Strength", &postFx.colorGradeStrength_, 0.0f, 1.0f);
        ImGui::SliderFloat("Saturation", &postFx.colorGradeSaturation_, 0.0f, 2.0f);
        ImGui::SliderFloat("Contrast", &postFx.colorGradeContrast_, 0.25f, 2.0f);
        ImGui::SliderFloat("Temperature", &postFx.colorGradeTemperature_, -1.0f, 1.0f);

        if (ImGui::Button("Aquarium Clear"))
        {
            postFx.highlightRecovery_ = 0.25f;
            postFx.colorGradeEnabled_ = true;
            postFx.colorGradeStrength_ = 0.35f;
            postFx.colorGradeSaturation_ = 1.06f;
            postFx.colorGradeContrast_ = 1.05f;
            postFx.colorGradeTemperature_ = -0.04f;
            postFx.postMaterialDetailStrength_ = 0.55f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Neutral Grade"))
        {
            postFx.colorGradeStrength_ = 0.0f;
            postFx.colorGradeSaturation_ = 1.0f;
            postFx.colorGradeContrast_ = 1.0f;
            postFx.colorGradeTemperature_ = 0.0f;
        }
    }

    // =========================================================================
    // pixel presentation
    // =========================================================================
    EditorWidgets::SectionHeader("Pixel Presentation");

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
        ImGui::SliderFloat("Pixel Blend", &postFx.postPixelizationStrength_, 0.0f, 1.0f,
                           "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Blends native-resolution color back into the pixel grid.\n"
                "Lower values keep the image cleaner while retaining stepped silhouettes.");
        }
        ImGui::SliderFloat("Edge Focus", &postFx.postPixelizationEdgeFocus_, 0.0f, 1.0f,
                           "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Keeps stronger pixelization on depth, normal, and color edges while\n"
                "relaxing it across smooth water, glass, sky, and broad lit faces.");
        }
    }
    ImGui::SliderFloat("Material Detail", &postFx.postMaterialDetailStrength_, 0.0f, 1.0f,
                       "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Reintroduces voxel material albedo detail after tonemapping.\n"
            "Glass and water are skipped; gravel darkening is capped to avoid muddy floors.");
    }

    // =========================================================================
    // depth of field
    // =========================================================================
    EditorWidgets::SectionHeader("Depth of Field");

    ImGui::Checkbox("Manual DoF", &postFx.dofEnabled_);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Always-on focus blur using the manual focus distance below.\n"
            "Fish focus mode force-enables DoF and drives the distance automatically.");
    }
    if (postFx.dofEnabled_)
    {
        ImGui::SliderFloat("Focus Distance", &postFx.dofFocusDistance_, 0.5f, 100.0f, "%.1f");
    }
    ImGui::SliderFloat("Focus Range", &postFx.dofFocusRange_, 0.5f, 30.0f, "%.1f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Distance band around the focal plane that stays sharp.");
    }
    ImGui::SliderFloat("Blur Strength", &postFx.dofBlurStrength_, 0.0f, 1.0f, "%.2f");

    // =========================================================================
    // bloom
    // =========================================================================
    EditorWidgets::SectionHeader("Bloom");

    ImGui::Checkbox("Enable Bloom (B)", &postFx.bloomEnabled_);

    if (postFx.bloomEnabled_)
    {
        ImGui::SliderFloat("Threshold", &postFx.bloomThreshold_, 0.5f, 4.0f);
        ImGui::SliderFloat("Knee", &postFx.bloomKnee_, 0.0f, 1.0f);
        ImGui::SliderFloat("Intensity", &postFx.bloomIntensity_, 0.0f, 0.25f);
        ImGui::SliderFloat("Blur Sigma", &postFx.bloomSigma_, 0.5f, 6.0f);
    }

    // =========================================================================
    // taa
    // =========================================================================
    EditorWidgets::SectionHeader("TAA (Temporal Anti-Aliasing)");

    if (ImGui::Checkbox("Enable TAA", &postFx.taaEnabled_))
    {
        resetTemporalHistory();
    }

    if (postFx.taaEnabled_)
    {
        if (ImGui::Checkbox("Enable Jitter", &postFx.jitterEnabled_))
        {
            resetTemporalHistory();
        }

        ImGui::SliderFloat("Similarity Threshold", &postFx.taaSimilarityThreshold_, 0.01f, 0.5f);
        ImGui::SliderFloat("Velocity Scale", &postFx.taaVelocityScale_, 1.0f, 50.0f);
        ImGui::SliderFloat("Blend Min", &postFx.taaBlendMin_, 0.01f, 0.2f);
        ImGui::SliderFloat("Blend Max", &postFx.taaBlendMax_, 0.1f, 0.5f);
        ImGui::SliderFloat("Soft Edge Reconstruction", &postFx.taaSoftEdgeStrength_,
                           0.0f, 1.0f);

        // silhouette protection
        if (EditorWidgets::SectionHeaderCollapsible("Silhouette Protection", false))
        {
            ImGui::SliderFloat("Depth Edge Threshold", &postFx.taaDepthEdgeThreshold_, 0.0f, 0.5f);
            ImGui::SliderFloat("Cross-Frame Depth", &postFx.taaCrossFrameDepthThreshold_, 0.0f, 0.1f);
            ImGui::SliderFloat("Color Variance", &postFx.taaColorVarianceThreshold_, 0.0f, 0.5f);
        }

        if (ImGui::Button("Reset TAA History"))
        {
            postFx.taaResetHistory_ = true;
        }
    }

    ImGui::SliderFloat("Sharpen", &postFx.taaSharpen_, 0.0f, 1.0f);
    ImGui::Checkbox("FXAA (post-TAA)", &postFx.fxaaEnabled_);

    // =========================================================================
    // vignette & grain
    // =========================================================================
    EditorWidgets::SectionHeader("Effects");

    ImGui::SliderFloat("Vignette", &postFx.vignetteStrength_, 0.0f, 0.3f);
    ImGui::SliderFloat("Film Grain", &postFx.grainStrength_, 0.0f, 0.1f);

    // =========================================================================
    // post debug
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Post Debug (F10)", false))
    {
        const char* modes[] = {"Off",
                               "Bloom Extract",
                               "Bloom Blur",
                               "Bloom Combined",
                               "Luminance Heatmap",
                               "Blue Noise (Raw)",
                               "Stochastic Noise",
                               "Noise Hemisphere"};
        ImGui::Combo("Debug Mode", &postFx.postDebugMode_, modes, 8);
    }
}

void DrawMaterialsTab(EngineFacade::RenderingPanelAccess& app)
{
    auto& water = app.water;
    auto& glass = app.glass;

    // =========================================================================
    // water
    // =========================================================================
    EditorWidgets::SectionHeader("Water");

    ImGui::Checkbox("Enable Water (F6)", &water.waterEnabled_);
    bool useWaterV2 = water.useWaterV2_;
    if (ImGui::Checkbox("Use Water V2", &useWaterV2))
    {
        app.setUseWaterV2(useWaterV2);
    }
    ImGui::TextDisabled("A/B toggle between legacy WaterPass and Water V2 surface path.");
    ImGui::Text("Active Path: %s", water.useWaterV2_ ? "Water V2" : "Legacy Water");

    if (water.waterEnabled_)
    {
        bool waterParametersChanged = false;
        auto& waterMgr = app.waterVolumeMgr();
        auto& waterVolumes = waterMgr.volumes();
        if (ImGui::SliderFloat("Water Level", &water.waterLevel_, 0.0f, 40.0f))
        {
            if (!waterVolumes.empty())
            {
                waterVolumes[0].surfaceHeight = water.waterLevel_;
                waterMgr.markDirty();
            }
            waterParametersChanged = true;
        }
        if (water.useWaterV2_ && !waterVolumes.empty())
        {
            const float bakedVoxelTop = waterVolumes[0].boundsMax.y;
            ImGui::TextDisabled("Voxel water top: %.2f", bakedVoxelTop);
            if (ImGui::Button("Match Water Level To Voxel Top"))
            {
                water.waterLevel_ = bakedVoxelTop;
                waterVolumes[0].surfaceHeight = water.waterLevel_;
                waterMgr.markDirty();
                waterParametersChanged = true;
            }
            if (std::abs(water.waterLevel_ - bakedVoxelTop) > 0.25f)
            {
                ImGui::TextWrapped(
                    "Delta debug compares analytic water against baked voxel water. "
                    "Waterline color is expected when Water Level differs from voxel top.");
            }
        }

        if (EditorWidgets::SectionHeaderCollapsible("Water Surface"))
        {
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
                ImGui::SliderFloat("Distortion Depth Scale", &water.waterDistortionDepthScale_, 0.0f,
                                   0.6f);
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
                ImGui::SliderFloat("Specular Intensity", &water.waterSpecIntensity_, 0.1f, 4.0f);
            waterParametersChanged |=
                ImGui::SliderFloat("Edge Fade Depth", &water.waterEdgeFadeDepth_, 0.05f, 2.0f);
            waterParametersChanged |=
                ImGui::SliderFloat("Gradient Strength", &water.waterGradientStrength_, 0.0f, 1.0f);

            EditorWidgets::Spacing();
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
            ImGui::TextDisabled("Tip: use Lighting Debug -> Caustics to isolate this contribution.");

            if (EditorWidgets::SectionHeaderCollapsible("Voxel Water VFX", false))
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
            }

            if (app.isSunroofScene() &&
                EditorWidgets::SectionHeaderCollapsible(
                    "Sunroof Living Water VFX", false))
            {
                waterParametersChanged |=
                    ui::panels::DrawSunroofLivingWaterVfxControls(water);
            }

            EditorWidgets::Spacing();
            ImGui::Checkbox("Enable Planar Reflection (Spike)", &water.waterPlanarReflectionEnabled_);
            ImGui::Checkbox("Use Oblique Clip Plane", &water.waterPlanarObliqueClipEnabled_);
            waterParametersChanged |=
                ImGui::SliderFloat("Planar Reflection Strength", &water.waterPlanarStrength_, 0.0f, 1.0f);
            ImGui::TextWrapped("Test toggle: when disabled, planar reflection uses the mirrored camera without the oblique water-plane clip.");
        }

        if (waterParametersChanged)
        {
            app.markWaterParametersDirty();
        }

        if (EditorWidgets::SectionHeaderCollapsible("Underwater Absorption (Day 18)"))
        {
            // access water volume manager for absorption settings
            auto& volumes = waterMgr.volumes();
            if (water.waterStylizedMode_)
            {
                ImGui::TextDisabled(
                    "Absorption RGB/Deep Color are ignored in Stylized mode (depth bands drive color).");
            }
            if (!volumes.empty())
            {
                auto& vol = volumes[0];
                float absorptionRGB[3] = {vol.absorptionCoeff.x, vol.absorptionCoeff.y, vol.absorptionCoeff.z};
                float deepColor[3] = {vol.deepColor.x, vol.deepColor.y, vol.deepColor.z};

                if (ImGui::ColorEdit3("Absorption RGB", absorptionRGB))
                {
                    vol.absorptionCoeff.x = absorptionRGB[0];
                    vol.absorptionCoeff.y = absorptionRGB[1];
                    vol.absorptionCoeff.z = absorptionRGB[2];
                    waterMgr.markDirty();
                }
                if (ImGui::ColorEdit3("Deep Color", deepColor))
                {
                    vol.deepColor.x = deepColor[0];
                    vol.deepColor.y = deepColor[1];
                    vol.deepColor.z = deepColor[2];
                    waterMgr.markDirty();
                }

                float fogDensity = vol.fogDensity;
                if (ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 0.2f))
                {
                    vol.fogDensity = fogDensity;
                    waterMgr.markDirty();
                }

                EditorWidgets::Spacing();

                if (ImGui::Button("Aquarium Preset"))
                {
                    vol.absorptionCoeff = glm::vec3(0.45f, 0.08f, 0.04f);
                    waterMgr.markDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button("Ocean Preset"))
                {
                    vol.absorptionCoeff = glm::vec3(0.30f, 0.06f, 0.02f);
                    waterMgr.markDirty();
                }
            }
            else
            {
                ImGui::TextDisabled("No water volumes in scene");
            }
        }

        // water debug
        if (EditorWidgets::SectionHeaderCollapsible("Water Debug (F7)", false))
        {
            const char* modes[] = {"Off", "Water Path", "Fresnel", "Reflection Mix", "Alpha",
                                   "Refraction Src", "Path+Contact+Sparkle",
                                   "Reflection+Planar", "Reflection Color", "Refraction Color",
                                   "Planar Mask", "Body Tint", "Surface Normal",
                                   "Raw Planar Projected", "Fallback Reflection",
                                   "Raw Planar Screen UV", "Planar Projected UV",
                                   "Raw Planar No Distort", "Voxel VFX", "Voxel Foam",
                                   "Body Detail", "Water Body Path",
                                   "Water Body Density", "Water Body Composite"};
            ImGui::Combo("Debug Mode", &water.waterDebugMode_, modes, IM_ARRAYSIZE(modes));
        }
    }

    // =========================================================================
    // glass
    // =========================================================================
    EditorWidgets::SectionHeader("Glass");

    ImGui::Checkbox("Enable Glass (F8)", &glass.glassEnabled_);

    if (glass.glassEnabled_)
    {
        if (ImGui::Button(kRoundFishbowlGlassPreset.name))
        {
            applyGlassMaterialPreset(glass, kRoundFishbowlGlassPreset);
        }

        float glassTint[3] = {glass.glassTint_.x, glass.glassTint_.y, glass.glassTint_.z};
        if (ImGui::ColorEdit3("Glass Tint", glassTint))
        {
            glass.glassTint_ = glm::vec3(glassTint[0], glassTint[1], glassTint[2]);
        }
        ImGui::SliderFloat("Absorption", &glass.glassAbsorption_, 0.0f, 3.0f);
        ImGui::SliderFloat("Thickness Scale", &glass.glassThicknessScale_, 0.1f, 20.0f);
        ImGui::SliderFloat("Refract Strength", &glass.glassRefract_, 0.0f, 0.05f);
        ImGui::SliderFloat("IOR", &glass.glassIor_, 1.0f, 2.5f,
                           "%.2f (1.0=air, 1.5=glass, 2.4=diamond)");

        // glass debug
        if (EditorWidgets::SectionHeaderCollapsible("Glass Debug (F9)", false))
        {
            const char* modes[] = {"Off",          "Thickness",    "Refraction",
                                   "Fresnel",      "Scene Color",  "Transmittance",
                                   "Reflection",   "Waterline/Rim", "Bubbles",
                                   "Iridescence"};
            ImGui::Combo("Debug Mode", &glass.glassDebugMode_, modes, 10);
        }

        // bubble inclusions
        ImGui::Separator();
        ImGui::Text("Bubble Inclusions");

        ImGui::SliderFloat("Bubble Scale", &glass.glassBubbleScale_, 2.0f, 40.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bubble density. Higher = more, smaller bubbles.");

        ImGui::SliderFloat("Bubble Intensity", &glass.glassBubbleIntensity_, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Brightness of bubble highlights. 0 = off.");

        ImGui::SliderFloat("Bubble Thickness Gate", &glass.glassBubbleThicknessGate_, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum glass thickness for bubbles to appear.");

        ImGui::SliderFloat("Chromatic Split", &glass.glassBubbleChromaticSplit_, 0.0f, 0.2f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rainbow sparkle at bubble edges. 0 = monochrome.");

        // thin-film iridescence
        ImGui::Separator();
        ImGui::Text("Thin-Film Iridescence");

        ImGui::SliderFloat("Iridescent Strength", &glass.glassIridescentStrength_, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rainbow shimmer intensity on glass surface.\n"
                              "0 = off, 0.2-0.4 = subtle, 0.8+ = dramatic");

        ImGui::SliderFloat("Film Thickness", &glass.glassIridescentFilmThickness_, 0.1f, 5.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Controls which colors appear at which angles.\n"
                              "0.5 = warm tones, 1.5 = full spectrum, 3.0+ = rapid cycling");

        ImGui::SliderFloat("Color Frequency", &glass.glassIridescentFrequency_, 0.5f, 6.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Number of rainbow cycles across the viewing angle range.\n"
                              "1.0 = single broad rainbow, 2-3 = natural look, 5+ = busy");
    }

    // =========================================================================
    // voxel glass aaa refraction
    // =========================================================================
    EditorWidgets::SectionHeader("Voxel Glass AAA");

    ImGui::Checkbox("Enable Voxel Glass Refraction", &glass.voxelGlassRefractEnabled_);

    if (glass.voxelGlassRefractEnabled_ || glass.voxelGlassDebugMode_ > 0)
    {
        float voxelGlassTint[3] = {glass.voxelGlassTint_.x, glass.voxelGlassTint_.y,
                                   glass.voxelGlassTint_.z};
        if (ImGui::ColorEdit3("Voxel Glass Tint", voxelGlassTint))
        {
            glass.voxelGlassTint_ =
                glm::vec3(voxelGlassTint[0], voxelGlassTint[1], voxelGlassTint[2]);
        }

        float voxelGlassReflect[3] = {glass.voxelGlassReflectionColor_.x,
                                       glass.voxelGlassReflectionColor_.y,
                                       glass.voxelGlassReflectionColor_.z};
        if (ImGui::ColorEdit3("Reflection Color", voxelGlassReflect))
        {
            glass.voxelGlassReflectionColor_ =
                glm::vec3(voxelGlassReflect[0], voxelGlassReflect[1], voxelGlassReflect[2]);
        }

        ImGui::SliderFloat("Absorption##VoxelGlass", &glass.voxelGlassAbsorption_, 0.0f, 2.0f);
        ImGui::SliderFloat("Refract Strength##VoxelGlass", &glass.voxelGlassRefractStrength_, 0.0f,
                           0.1f);
        ImGui::SliderFloat("IOR", &glass.voxelGlassIOR_, 1.0f, 2.5f,
                           "%.2f (1.0=air, 1.5=glass, 2.4=diamond)");
        ImGui::SliderFloat("Reflection Strength", &glass.voxelGlassReflectStrength_, 0.0f, 1.0f);

        // voxel glass debug
        if (EditorWidgets::SectionHeaderCollapsible("Voxel Glass Debug", false))
        {
            const char* voxelGlassModes[] = {"Off", "Thickness", "Refraction", "Fresnel", "Mask",
                                              "Absorption", "Edge Seams", "Normal Smooth"};
            ImGui::Combo("Debug Mode##VoxelGlass", &glass.voxelGlassDebugMode_, voxelGlassModes, 8);
        }
    }
}

void DrawVoxelsTab(EngineFacade::RenderingPanelAccess& app)
{
    auto& voxelDebug = app.voxelDebug;

    // =========================================================================
    // voxel world
    // =========================================================================
    EditorWidgets::SectionHeader("Voxel World");

    ImGui::Checkbox("Show Voxel World (F1)", &voxelDebug.voxelVisible_);
    bool fishEnabled = voxelDebug.proceduralFishEnabled_;
    if (ImGui::Checkbox("Enable Procedural Fish", &fishEnabled))
    {
        app.setProceduralFishEnabled(fishEnabled);
    }
    int fishCount = voxelDebug.proceduralFishCount_;
    if (!fishEnabled)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderInt("Fish Count", &fishCount, 0, 50))
    {
        app.setProceduralFishCount(fishCount);
    }
    if (!fishEnabled)
    {
        ImGui::EndDisabled();
    }

    if (ImGui::Button("Regenerate World (F2)"))
    {
        app.regenerateVoxelWorld();
    }

    ImGui::Checkbox("Show Chunk Bounds (F3)", &voxelDebug.chunkBoundsVisible_);
    ImGui::Checkbox("Show Volume Bounds (F12)", &voxelDebug.volumeBoundsVisible_);
    ImGui::Checkbox("Show Axolotl Rig", &voxelDebug.axolotlRigDebugVisible_);

    ImGui::Checkbox("Freeze Meshing (F4)", &voxelDebug.voxelMeshingFrozen_);

    ImGui::Checkbox("Edit Mode (F5)", &voxelDebug.voxelEditMode_);

    if (voxelDebug.voxelEditMode_)
    {
        EditorWidgets::TextCyan("LMB: Remove, RMB: Place");
    }

    // =========================================================================
    // dda debug
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("DDA Debug"))
    {
        const char* modes[] = {"Off", "Hit Axis", "Step Heatmap", "HitId", "Occ Mip View"};
        ImGui::Combo("DDA Debug Mode", &voxelDebug.voxelDdaDebugMode_, modes, 5);

        if (voxelDebug.voxelDdaDebugMode_ == 2) // heatmap
        {
            const char* heatModes[] = {"Iterations", "Skip Jumps"};
            ImGui::Combo("Heatmap Mode", &voxelDebug.voxelHeatmapMode_, heatModes, 2);
            ImGui::SliderFloat("Max Value", &voxelDebug.voxelHeatmapMax_, 16.0f, 512.0f);
            ImGui::SliderFloat("Gamma", &voxelDebug.voxelHeatmapGamma_, 0.25f, 4.0f);
        }

        // advanced debug modes
        if (EditorWidgets::SectionHeaderCollapsible("Edge Debug (Day 12)", false))
        {
            const char* advModes[] = {"Off",
                                      "Edge Diagnostic",
                                      "Voxel Position",
                                      "Hit Axis (RGB)",
                                      "Closest Face",
                                      "Closest Face Diff",
                                      "Depth Validity",
                                      "Clamp Diagnostic",
                                      "Transmissive Layers",
                                      "Voxel Water Distance",
                                      "Bevel Mask",
                                      "Material Atlas Mask",
                                      "Material Atlas Only",
                                      "Cavity Mask",
                                      "Pixel Edge Shadow Mask",
                                      "Cell Variation Signal"};
            ImGui::Combo("Advanced Mode", &voxelDebug.voxelDdaAdvancedDebug_, advModes,
                         IM_ARRAYSIZE(advModes));
        }

        // skip mip control
        ImGui::Checkbox("Empty Skip", &voxelDebug.voxelDdaSkipEnabled_);
        if (voxelDebug.voxelDdaSkipEnabled_)
        {
            ImGui::SliderInt("Skip Mip Level", &voxelDebug.voxelDdaSkipMip_, 0, 6);
        }
    }

    // =========================================================================
    // normal smoothing
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Material Edge Bevel", false))
    {
        ImGui::SliderFloat("Edge Smoothing", &voxelDebug.voxelNormalEdgeSmoothing_, 0.0f, 0.5f);
        EditorWidgets::HelpMarker("Recommended: 0.10-0.15 for subtle material-aware beveling");
        ImGui::SliderFloat("Voxel Pixel Detail", &voxelDebug.voxelPixelEdgeShadowStrength_, 0.0f,
                           1.0f);
        ImGui::SliderFloat("Cavity Strength", &voxelDebug.voxelCavityStrength_, 0.0f, 1.0f);
        ImGui::SliderFloat("Painted Material", &voxelDebug.voxelPaintedMaterialStrength_, 0.0f,
                           1.0f);
        EditorWidgets::HelpMarker(
            "Stable voxel face pixels plus edge/contact darkening. Cast-shadow pixelization is handled by Post FX pixel presentation.");

        if (ImGui::Button("Reset to Default##Normal"))
        {
            voxelDebug.voxelNormalEdgeSmoothing_ = 0.12f;
            voxelDebug.voxelPixelEdgeShadowStrength_ = 0.35f;
        }
    }

    if (EditorWidgets::SectionHeaderCollapsible("Voxel Cell Variation", false))
    {
        auto& variation = voxelDebug.voxelCellVariation_;
        ImGui::SliderFloat("Master Strength", &variation.masterStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Generic", &variation.genericAmplitude, 0.0f, 1.0f);
        ImGui::SliderFloat("Gravel", &variation.gravelAmplitude, 0.0f, 1.0f);
        ImGui::SliderFloat("Plant", &variation.plantAmplitude, 0.0f, 1.0f);
        ImGui::SliderFloat("Stone", &variation.stoneAmplitude, 0.0f, 1.0f);
        ImGui::SliderFloat("Wood", &variation.woodAmplitude, 0.0f, 1.0f);
        ImGui::SliderFloat("Hue Spread", &variation.hueSpread, 0.0f, 0.25f);
        ImGui::SliderFloat("Saturation Spread", &variation.saturationSpread, 0.0f, 1.0f);
        ImGui::SliderFloat("Value Spread", &variation.valueSpread, 0.0f, 1.0f);
        ImGui::SliderFloat("Palette Families", &variation.paletteFamilyStrength, 0.0f, 1.0f);
        EditorWidgets::HelpMarker(
            "Cell-constant HSV variation keyed by integer voxel and stable volume index. "
            "All-zero settings preserve legacy output.");
        if (ImGui::Button("Painted Outdoor v0##Variation"))
        {
            variation = engine::makeVoxelCellVariationSettings(
                engine::VoxelCellVariationPreset::PaintedOutdoorV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Soft Outdoor v0##Variation"))
        {
            variation = engine::makeVoxelCellVariationSettings(
                engine::VoxelCellVariationPreset::SoftOutdoorV0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable##Variation"))
        {
            variation = {};
        }
    }

    // =========================================================================
    // debug freeze
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Debug Freeze", false))
    {
        ImGui::Checkbox("Freeze Debug (Master)", &voxelDebug.voxelFreezeDebug_);
        ImGui::Checkbox("Freeze Camera", &voxelDebug.voxelFreezeCamera_);
        ImGui::Checkbox("Freeze Time", &voxelDebug.voxelFreezeTime_);
        ImGui::Checkbox("Disable Jitter", &voxelDebug.voxelDisableJitter_);
    }
}

void DrawDebugTab(EngineFacade::RenderingPanelAccess& app)
{
    EditorState& editorState = app.editorState;
    auto& shadows = app.shadows;
    auto& ao = app.ao;
    auto& diagnostics = app.diagnostics;

    // =========================================================================
    // render pipeline showcase
    // =========================================================================
    EditorWidgets::SectionHeader("Render Pipeline Showcase");

    static float showcaseSecondsPerStage = 4.0f;
    static bool showcaseLoop = false;
    static bool showcaseStartPaused = true;
    ImGui::TextWrapped(
        "Record the current scene as an ordered, labeled rendering breakdown. "
        "The current camera and live graphics sliders are used without reloading a preset.");
    ImGui::SliderFloat("Seconds Per Stage", &showcaseSecondsPerStage, 0.25f, 15.0f,
                       "%.2f s");
    ImGui::Checkbox("Start Paused", &showcaseStartPaused);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &showcaseLoop);
    if (ImGui::Button("Start Render Showcase", ImVec2(-1.0f, 0.0f)))
    {
        app.startRenderPipelineShowcase(showcaseSecondsPerStage, showcaseLoop,
                                        showcaseStartPaused);
    }
    ImGui::TextDisabled("Space: pause  |  Left/Right: step  |  F5: return to editor");
    EditorWidgets::HelpMarker(
        "Each setting appears at its real pipeline stage. Bloom controls affect the bloom "
        "breakdown and final image; saturation and color grading appear in Final Composite.");

    // =========================================================================
    // gpu profiler
    // =========================================================================
    EditorWidgets::SectionHeader("GPU Profiler");

    bool gpuProfilerEnabled = diagnostics.gpuProfilerEnabled_;
    if (ImGui::Checkbox("Enable GPU Profiler", &gpuProfilerEnabled))
    {
        app.setGpuProfilerEnabled(gpuProfilerEnabled);
    }
    ImGui::Checkbox("Show Profiler Overlay", &editorState.profilerOverlayVisible);
    ImGui::Checkbox("Show Graphs", &editorState.profilerGraphVisible);
    ImGui::Checkbox("Show Top Passes", &editorState.profilerOverlayShowPasses);
    ImGui::SliderInt("History Frames", &editorState.profilerHistoryFrames, 30, 600, "%d");

    const auto& cpuHistory = app.cpuFrameHistoryMs();
    if (!cpuHistory.empty() && editorState.profilerGraphVisible)
    {
        float cpuMaxMs = app.cpuFrameTimeMs();
        for (float sample : cpuHistory)
        {
            cpuMaxMs = std::max(cpuMaxMs, sample);
        }
        cpuMaxMs = std::max(16.6f, cpuMaxMs * 1.15f);
        ImGui::PlotLines("CPU Frame ms", cpuHistory.data(), static_cast<int>(cpuHistory.size()),
                         0, nullptr, 0.0f, cpuMaxMs, ImVec2(-1.0f, 60.0f));
    }

    if (app.gpuProfiler().isEnabled())
    {
        EditorWidgets::BeginGroupBox("");

        const auto& samples = app.gpuProfiler().samples();
        EditorWidgets::ValueLabel("Total GPU", "%.2f ms", app.gpuProfiler().totalMs());
        EditorWidgets::ValueLabel("Present", "%s", app.activePresentModeLabel());
        if (app.effectiveFpsLimit() > 0.0f)
        {
            EditorWidgets::ValueLabel("Effective Cap", "%.0f FPS", app.effectiveFpsLimit());
        }
        else
        {
            EditorWidgets::ValueLabel("Effective Cap", "%s", "Uncapped");
        }
        const char* throttleLabel = app.isBackgroundThrottleActive()
                                        ? "Background"
                                        : (app.isFocusedIdleThrottleActive() ? "Focused Idle"
                                                                             : "Foreground");
        EditorWidgets::ValueLabel("Throttle", "%s", throttleLabel);
        bool focusedIdleEnabled = app.focusedIdleThrottleEnabled();
        if (ImGui::Checkbox("Focused Idle Throttle", &focusedIdleEnabled))
        {
            app.setFocusedIdleThrottleEnabled(focusedIdleEnabled);
        }
        ImGui::BeginDisabled(!focusedIdleEnabled);
        float focusedIdleFps = app.focusedIdleFpsLimit();
        if (ImGui::SliderFloat("Focused Idle FPS", &focusedIdleFps, 5.0f, 60.0f, "%.0f"))
        {
            app.setFocusedIdleFpsLimit(focusedIdleFps);
        }
        float focusedIdleDelay = app.focusedIdleDelaySeconds();
        if (ImGui::SliderFloat("Focused Idle Delay", &focusedIdleDelay, 0.10f, 2.0f, "%.2f s"))
        {
            app.setFocusedIdleDelaySeconds(focusedIdleDelay);
        }
        ImGui::EndDisabled();
        ImGui::Separator();

        const auto& gpuHistory = app.gpuFrameHistoryMs();
        if (!gpuHistory.empty() && editorState.profilerGraphVisible)
        {
            float gpuMaxMs = app.gpuFrameTimeMs();
            for (float sample : gpuHistory)
            {
                gpuMaxMs = std::max(gpuMaxMs, sample);
            }
            gpuMaxMs = std::max(16.6f, gpuMaxMs * 1.15f);
            ImGui::PlotLines("GPU Frame ms", gpuHistory.data(),
                             static_cast<int>(gpuHistory.size()), 0, nullptr, 0.0f, gpuMaxMs,
                             ImVec2(-1.0f, 60.0f));
            ImGui::Separator();
        }

        // display individual samples from profiler
        for (const auto& sample : samples)
        {
            EditorWidgets::ValueLabel(sample.label.c_str(), "%.2f ms", sample.ms);
        }

        EditorWidgets::EndGroupBox();
    }
    else if (diagnostics.gpuProfilerEnabled_ && !app.gpuProfiler().isSupported())
    {
        ImGui::TextDisabled("GPU timestamp queries are not supported on this device.");
    }
    else
    {
        ImGui::TextDisabled("Enable GPU Profiler to record GPU frame history.");
    }

    // =========================================================================
    // pixel inspector
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Pixel Inspector"))
    {
        ImGui::Checkbox("Enable Pixel Inspector", &diagnostics.pixelInspectEnabled_);

        if (diagnostics.pixelInspectEnabled_)
        {
            EditorWidgets::TextCyan("Middle-click to sample pixel");

            EditorWidgets::BeginGroupBox("");

            if (diagnostics.inspectPixel_.x >= 0 && diagnostics.inspectPixel_.y >= 0)
            {
                ImGui::Text("Pixel: (%d, %d)", diagnostics.inspectPixel_.x, diagnostics.inspectPixel_.y);
                ImGui::Text("Normal: (%.2f, %.2f, %.2f)",
                    diagnostics.lastInspectNormal_.x, diagnostics.lastInspectNormal_.y, diagnostics.lastInspectNormal_.z);
                ImGui::Text("Depth: %.4f", diagnostics.lastInspectDepth_);

                if (diagnostics.lastInspectHasWorld_)
                {
                    ImGui::Text("World Pos: (%.1f, %.1f, %.1f)",
                        diagnostics.lastInspectWorldPos_.x, diagnostics.lastInspectWorldPos_.y, diagnostics.lastInspectWorldPos_.z);
                }
                else
                {
                    ImGui::Text("World Pos: (-, -, -)");
                }
                ImGui::Text("NdotL: %.3f", diagnostics.lastInspectNdotL_);
            }
            else
            {
                ImGui::Text("Pixel: (-, -)");
                ImGui::Text("Normal: (-, -, -)");
                ImGui::Text("Depth: -");
                ImGui::Text("World Pos: (-, -, -)");
                ImGui::Text("NdotL: -");
            }

            EditorWidgets::EndGroupBox();

            if (ImGui::Button("Clear Pixel"))
            {
                diagnostics.inspectPixel_ = glm::ivec2(-1, -1);
            }
        }
    }

    // =========================================================================
    // dda metrics
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("DDA Metrics (Day 8.5)", false))
    {
        ImGui::Checkbox("Enable DDA Metrics", &diagnostics.voxelMetricsEnabled_);

        if (diagnostics.voxelMetricsEnabled_)
        {
            ImGui::SliderInt("Sample Stride", &diagnostics.voxelMetricsSampleStride_, 1, 16);
            ImGui::SliderInt("History Length", &diagnostics.voxelMetricsHistoryLength_, 1, 240);

            EditorWidgets::Spacing();

            EditorWidgets::BeginGroupBox("");

            const auto& metrics = app.cachedDdaMetrics();
            // compute averages from raw counters
            float avgIters = (metrics.sampleCount > 0)
                ? static_cast<float>(metrics.sumIters) / static_cast<float>(metrics.sampleCount)
                : 0.0f;
            float avgSkips = (metrics.sampleCount > 0)
                ? static_cast<float>(metrics.sumSkipJumps) / static_cast<float>(metrics.sampleCount)
                : 0.0f;
            uint32_t totalHits = metrics.hitCount + metrics.missCount;
            float hitRate = (totalHits > 0)
                ? static_cast<float>(metrics.hitCount) / static_cast<float>(totalHits)
                : 0.0f;

            EditorWidgets::ValueLabel("Avg Iterations", "%.1f", avgIters);
            EditorWidgets::ValueLabel("Avg Skip Jumps", "%.1f", avgSkips);
            EditorWidgets::ValueLabel("Hit Rate", "%.1f%%", hitRate * 100.0f);

            EditorWidgets::EndGroupBox();

            if (ImGui::Button("Dump Metrics (F11)"))
            {
                // metrics dump is handled by keyboard shortcut
            }
        }
    }

    // =========================================================================
    // day 16 checklist
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Day 16 Checklist", false))
    {
        ImGui::Checkbox("Shadow raw/resolved debug verified", &diagnostics.day16ShadowDebugVerified_);
        ImGui::Checkbox("AO raw/resolved debug verified", &diagnostics.day16AoDebugVerified_);
        ImGui::Checkbox("Depth reject mask verified", &diagnostics.day16DepthRejectMaskVerified_);
        ImGui::Checkbox("AO stable with Ray Count = 2", &diagnostics.day16AoStableRayCount2_);
        ImGui::Checkbox("GPU Profiler baseline recorded", &diagnostics.day16GpuBaselineRecorded_);
        ImGui::Checkbox("Parameters tuned for scene", &diagnostics.day16ParamsTuned_);
        ImGui::Checkbox("Dead code verified inactive", &diagnostics.day16DeadCodeChecked_);
        ImGui::Checkbox("Next step chosen", &diagnostics.day16NextStepChosen_);
    }

    // =========================================================================
    // temporal status
    // =========================================================================
    if (EditorWidgets::SectionHeaderCollapsible("Temporal Status", false))
    {
        // frame index would need to be exposed if needed
        EditorWidgets::ValueLabel("Frame Index", "%d", 0);
        EditorWidgets::ValueLabel("Depth History Index", "%d", 0);

        ImGui::Text("DDA Shadows:");
        ImGui::SameLine();
        if (!app.isDdaShadowsAvailable())
        {
            ImGui::TextDisabled("Unavailable");
        }
        else if (shadows.useDDAShadows_)
        {
            EditorWidgets::TextSuccess("Enabled");
        }
        else
        {
            EditorWidgets::TextWarning("Disabled");
        }

        ImGui::Text("AO:");
        ImGui::SameLine();
        if (!app.isAmbientOcclusionAvailable())
        {
            ImGui::TextDisabled("Unavailable");
        }
        else if (ao.aoEnabled_)
        {
            EditorWidgets::TextSuccess("Enabled");
        }
        else
        {
            EditorWidgets::TextWarning("Disabled");
        }
    }
}

} // namespace
} // namespace RenderingPanel
