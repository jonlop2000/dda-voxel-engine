#include "UI/Panels/ScenePanel.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "engine/scene/SceneConfig.h"
#include "UI/EngineFacade.h"
#include "UI/EditorWidgets.h"
#include "UI/Panels/SceneHierarchyPanel.h"
#include "UI/SceneCatalogExperiment.h"

namespace ScenePanel
{

namespace
{

bool cloudGenerationSettingsEqual(const SceneConfig& lhs, const SceneConfig& rhs)
{
    return lhs.loadCloudScene == rhs.loadCloudScene && lhs.cloudSeed == rhs.cloudSeed &&
           std::abs(lhs.cloudAltitude - rhs.cloudAltitude) <= 1e-4f &&
           std::abs(lhs.cloudCoverage - rhs.cloudCoverage) <= 1e-4f &&
           std::abs(lhs.cloudScale - rhs.cloudScale) <= 1e-4f;
}

void copyCloudGenerationSettings(SceneConfig& dst, const SceneConfig& src)
{
    dst.loadCloudScene = src.loadCloudScene;
    dst.cloudSeed = src.cloudSeed;
    dst.cloudAltitude = src.cloudAltitude;
    dst.cloudCoverage = src.cloudCoverage;
    dst.cloudScale = src.cloudScale;
}

} // namespace

void Init()
{
    SceneCatalogExperiment::Init();
}

void Shutdown()
{
    SceneCatalogExperiment::Shutdown();
}

ImVec2 Draw(EngineFacade& engine, ImVec2 position, ImVec2 size)
{
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, size.y), ImVec2(FLT_MAX, size.y));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Scene", nullptr, flags))
    {
        SceneHierarchyPanel::Draw(engine);

        // =====================================================================
        // scene configuration
        // =====================================================================
        EditorWidgets::SectionHeader("Scene Configuration");

        // current scene name
        const SceneConfig& cfg = engine.sceneConfig();
        ImGui::Text("Current Scene:");
        ImGui::SameLine();
        EditorWidgets::TextCyan(cfg.name.c_str());
        const std::filesystem::path currentScenePath = engine.currentScenePath();
        if (!currentScenePath.empty())
        {
            const std::string currentSceneFile = currentScenePath.filename().string();
            ImGui::Text("Scene Asset:");
            ImGui::SameLine();
            EditorWidgets::TextCyan(currentSceneFile.c_str());
        }
        const engine::scene::ScenePresentationProfileReadout profile =
            engine.presentationProfileReadout();
        ImGui::Text("Profile:");
        ImGui::SameLine();
        EditorWidgets::TextCyan(profile.appliedName.c_str());
        ImGui::Text("Named Base:");
        ImGui::SameLine();
        EditorWidgets::TextCyan(profile.namedBaseName.c_str());
        ImGui::Text("Resolved Source:");
        ImGui::SameLine();
        const std::string resolvedSourceName{
            engine::scene::scenePresentationProfileSourceName(profile.resolvedSource)};
        EditorWidgets::TextCyan(resolvedSourceName.c_str());
        if (profile.effectiveSource != profile.resolvedSource)
        {
            ImGui::Text("Effective Source:");
            ImGui::SameLine();
            const std::string effectiveSourceName{
                engine::scene::scenePresentationProfileSourceName(
                    profile.effectiveSource)};
            EditorWidgets::TextCyan(effectiveSourceName.c_str());
        }
        if (!profile.liveEditDelta.empty())
        {
            ImGui::Text("Live Overrides: %zu field(s)", profile.liveEditDelta.size());
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                for (const std::string& field : profile.liveEditDelta)
                {
                    ImGui::TextUnformatted(field.c_str());
                }
                ImGui::EndTooltip();
            }
        }

        ImGui::Spacing();

        if (ImGui::Button("Refresh Scene Catalog", ImVec2(-1, 0)))
        {
            engine.refreshAvailableScenes();
        }

        static char sceneFilter[96]{};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##SceneFilter", "Filter scenes", sceneFilter,
                                 IM_ARRAYSIZE(sceneFilter));
        if (sceneFilter[0] != '\0')
        {
            if (EditorWidgets::CompactButton("Clear Filter"))
            {
                sceneFilter[0] = '\0';
            }
        }
        SceneCatalogExperiment::Draw(engine, sceneFilter);

        EditorWidgets::Spacing(2);

        // content flags
        if (EditorWidgets::SectionHeaderCollapsible("Content Flags", false))
        {
            SceneConfig newCfg = engine.sceneConfig();
            bool changed = false;

            if (ImGui::Checkbox("Test Floor", &newCfg.loadTestFloor))
                changed = true;
            if (ImGui::Checkbox("Voxel World", &newCfg.loadVoxelWorld))
                changed = true;
            if (ImGui::Checkbox("OBB Volumes", &newCfg.loadOBBVolumes))
                changed = true;
            if (ImGui::Checkbox("Glass Panel", &newCfg.loadGlassPanel))
                changed = true;
            if (newCfg.loadAquariumTest &&
                ImGui::Checkbox("Use Mesh Tank Glass", &newCfg.useMeshTankGlass))
            {
                changed = true;
            }

            if (changed)
            {
                engine.reloadScene(newCfg);
            }
        }

        if (EditorWidgets::SectionHeaderCollapsible("Environment Wind", false))
        {
            ImGui::PushID("EnvironmentWind");
            auto wind = engine.sceneConfig().environmentWind;
            bool changed = false;
            changed |= ImGui::SliderFloat2("Direction (XZ)", &wind.direction.x,
                                           -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Shared world-space direction; normalized automatically.");
            }
            changed |= ImGui::SliderFloat("Speed", &wind.speed, 0.0f, 4.0f,
                                          "%.2f");
            changed |= ImGui::SliderFloat("Strength", &wind.strength, 0.0f,
                                          2.0f, "%.2f");
            changed |= ImGui::SliderFloat("Gust Strength", &wind.gustStrength,
                                          0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Gust Frequency", &wind.gustFrequencyHz,
                                          0.01f, 1.0f, "%.2f Hz");
            changed |= ImGui::SliderFloat("Turbulence", &wind.turbulenceStrength,
                                          0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Vertical Lift", &wind.verticalLift,
                                          -2.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Reserved for airborne particles; rooted foliage ignores lift.");
            }
            if (changed)
            {
                engine.setEnvironmentWindSettings(wind);
            }
            if (EditorWidgets::CompactButton("Reset Environment Wind"))
            {
                engine.setEnvironmentWindSettings(
                    engine::scene::defaultEnvironmentWindSettings());
            }
            ImGui::PopID();
        }

        if (EditorWidgets::SectionHeaderCollapsible("Time of Day", false))
        {
            ImGui::PushID("EnvironmentTime");
            auto time = engine.sceneConfig().environmentTime;
            bool changed = false;
            changed |= ImGui::Checkbox("Enable Time of Day", &time.enabled);
            ImGui::BeginDisabled(!time.enabled);
            changed |= ImGui::Checkbox("Cycle Automatically", &time.cycleEnabled);
            changed |= ImGui::SliderFloat("Time", &time.timeOfDayHours,
                                          0.0f, 23.999f, "%.2f h");
            ImGui::BeginDisabled(!time.cycleEnabled);
            changed |= ImGui::SliderFloat("Day Length", &time.dayLengthMinutes,
                                          0.25f, 60.0f, "%.2f min");
            ImGui::EndDisabled();

            const auto& sample = engine.environmentTimeSample();
            const int hour = static_cast<int>(std::floor(sample.hour));
            const int minute = static_cast<int>(std::floor(
                (sample.hour - static_cast<float>(hour)) * 60.0f));
            ImGui::Text("Current: %02d:%02d  %s", hour, minute,
                        engine::scene::environmentTimePhaseLabel(sample.phase).data());

            if (EditorWidgets::CompactButton("Night"))
            {
                engine.setEnvironmentTimePreset(
                    engine::scene::EnvironmentTimePreset::Night);
            }
            ImGui::SameLine();
            if (EditorWidgets::CompactButton("Dawn"))
            {
                engine.setEnvironmentTimePreset(
                    engine::scene::EnvironmentTimePreset::Dawn);
            }
            ImGui::SameLine();
            if (EditorWidgets::CompactButton("Day"))
            {
                engine.setEnvironmentTimePreset(
                    engine::scene::EnvironmentTimePreset::Day);
            }
            ImGui::SameLine();
            if (EditorWidgets::CompactButton("Sunset"))
            {
                engine.setEnvironmentTimePreset(
                    engine::scene::EnvironmentTimePreset::Sunset);
            }
            ImGui::EndDisabled();

            if (changed)
            {
                engine.setEnvironmentTimeSettings(time);
            }
            ImGui::PopID();
        }

        if (EditorWidgets::SectionHeaderCollapsible(
                "Windborne Ambient Particles", false))
        {
            ImGui::PushID("WindborneParticles");
            auto particles = engine.sceneConfig().windborneParticles;
            bool changed = false;
            changed |= ImGui::Checkbox("Enable Windborne Particles",
                                       &particles.enabled);
            ImGui::BeginDisabled(!particles.enabled);
            changed |= ImGui::SliderFloat("Amount", &particles.amount,
                                          0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Leaf / Mote Mix",
                                          &particles.leafFraction,
                                          0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("0 = wind motes; 1 = leaves");
            }
            changed |= ImGui::SliderFloat("Particle Scale", &particles.scale,
                                          0.5f, 1.75f, "%.2f");
            changed |= ImGui::SliderFloat("Visibility Distance",
                                          &particles.visibilityDistance,
                                          8.0f, 64.0f, "%.0f m");
            ImGui::EndDisabled();
            if (changed)
            {
                engine.setWindborneParticleSettings(particles);
            }
            if (EditorWidgets::CompactButton("Reset Windborne Particles"))
            {
                engine.setWindborneParticleSettings(
                    engine::scene::defaultWindborneParticleSettings());
            }
            ImGui::TextDisabled(
                "Uses Environment Wind and scene world seed; fixed ceiling: %u.",
                engine::scene::kMaxWindborneParticleCount);
            ImGui::PopID();
        }

        // experimental voxel-cloud settings. sky and atmosphere remain independent.
        if (EditorWidgets::SectionHeaderCollapsible("Experimental Voxel Clouds", true))
        {
            ImGui::PushID("CloudSettings");
            static SceneConfig cloudDraft{};
            static std::string cloudDraftSceneKey{};
            const std::string sceneKey =
                currentScenePath.empty() ? cfg.name : currentScenePath.lexically_normal().string();
            if (cloudDraftSceneKey != sceneKey)
            {
                cloudDraft = cfg;
                cloudDraftSceneKey = sceneKey;
            }

            if (cloudGenerationSettingsEqual(cloudDraft, cfg))
            {
                copyCloudGenerationSettings(cloudDraft, cfg);
            }

            ImGui::Checkbox("Enable Experimental Voxel Clouds", &cloudDraft.loadCloudScene);

            if (cloudDraft.loadCloudScene)
            {
                int seed = static_cast<int>(cloudDraft.cloudSeed);
                if (ImGui::InputInt("Seed", &seed))
                {
                    cloudDraft.cloudSeed = static_cast<uint32_t>(std::max(0, seed));
                }

                ImGui::SliderFloat("Altitude", &cloudDraft.cloudAltitude, 50.0f, 200.0f, "%.0f");
                if (ImGui::SliderFloat("Coverage", &cloudDraft.cloudCoverage, 0.1f, 1.0f, "%.2f"))
                {
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Lower = more space between clouds");

                ImGui::SliderFloat("Scale", &cloudDraft.cloudScale, 16.0f, 96.0f, "%.0f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Larger = bigger cloud formations");

                SceneConfig liveCloudCfg = engine.sceneConfig();
                if (ImGui::SliderFloat("Wind Speed", &liveCloudCfg.cloudWindSpeed, 0.0f, 5.0f,
                                       "%.2f"))
                {
                    engine.setCloudWindSpeed(liveCloudCfg.cloudWindSpeed);
                }

                if (ImGui::SliderFloat2("Wind Direction", &liveCloudCfg.cloudWindDirection.x,
                                        -1.0f, 1.0f, "%.2f"))
                {
                    engine.setCloudWindDirection(liveCloudCfg.cloudWindDirection);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("XZ direction of wind (will be normalized)");

                if (ImGui::SliderFloat("Sun Shadow Strength", &liveCloudCfg.cloudShadowStrength,
                                       0.0f, 1.0f, "%.2f"))
                {
                    engine.setSceneCloudShadowStrength(liveCloudCfg.cloudShadowStrength);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 = clouds do not dim sun; 1 = strongest cloud sun attenuation.");
            }

            const bool hasPendingCloudChanges = !cloudGenerationSettingsEqual(cloudDraft, cfg);
            const char* cloudStatus = "Up to date";
            if (hasPendingCloudChanges)
            {
                cloudStatus = "Pending changes";
            }
            else if (engine.isCloudBuildInProgress())
            {
                cloudStatus = "Rebuilding clouds...";
            }

            ImGui::Separator();
            ImGui::Text("Status:");
            ImGui::SameLine();
            EditorWidgets::TextCyan(cloudStatus);

            const float applyButtonWidth =
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::BeginDisabled(!hasPendingCloudChanges);
            if (ImGui::Button("Apply Clouds", ImVec2(applyButtonWidth, 0.0f)))
            {
                SceneConfig applyCfg = engine.sceneConfig();
                copyCloudGenerationSettings(applyCfg, cloudDraft);
                engine.applyCloudSettings(applyCfg);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!hasPendingCloudChanges);
            if (ImGui::Button("Reset Clouds", ImVec2(-1, 0.0f)))
            {
                copyCloudGenerationSettings(cloudDraft, cfg);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        // sky settings
        if (EditorWidgets::SectionHeaderCollapsible("Sky Settings", true))
        {
            ImGui::PushID("SkySettings");
            SceneConfig skyCfg = engine.sceneConfig();

            const char* presetNames[] = {"Day", "Sunset", "Night", "Dawn", "Custom"};
            if (ImGui::Combo("Preset", &skyCfg.skyPreset, presetNames, 5))
            {
                engine.applySkyPreset(skyCfg.skyPreset);
                skyCfg.skyColor = engine.skyColor();
                engine.setSkyColor(skyCfg.skyColor);
            }

            if (skyCfg.skyPreset == 4) // custom
            {
                if (ImGui::ColorEdit3("Sky Color", &skyCfg.skyColor.x))
                {
                    engine.setSkyColor(skyCfg.skyColor);
                }
            }
            else
            {
                // show current sky color (read-only for presets)
                glm::vec3 skyColor = engine.skyColor();
                ImGui::ColorEdit3("Sky Color", &skyColor.x, ImGuiColorEditFlags_NoInputs);
            }
            ImGui::PopID();
        }

        // star settings
        if (EditorWidgets::SectionHeaderCollapsible("Star Settings", false))
        {
            ImGui::PushID("StarSettings");
            SceneConfig starCfg = engine.sceneConfig();
            bool starChanged = false;

            if (ImGui::Checkbox("Enable Stars", &starCfg.loadStarScene))
            {
                starChanged = true;
            }

            if (starCfg.loadStarScene)
            {
                int seed = static_cast<int>(starCfg.starSeed);
                if (ImGui::InputInt("Seed", &seed))
                {
                    starCfg.starSeed = static_cast<uint32_t>(std::max(0, seed));
                    starChanged = true;
                }

                if (ImGui::SliderFloat("Density", &starCfg.starDensity, 0.005f, 0.1f, "%.3f"))
                {
                    starChanged = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Lower = fewer stars, Higher = more stars");
            }

            if (starChanged)
            {
                engine.reloadScene(starCfg);
            }
            ImGui::PopID();
        }

        // =====================================================================
        // view mode
        // =====================================================================
        EditorWidgets::SectionHeader("View Mode");

        int viewMode = engine.viewMode();
        ImGui::RadioButton("Final Composite (0)", &viewMode, 0);
        ImGui::RadioButton("G-Buffer: Albedo (1)", &viewMode, 1);
        ImGui::RadioButton("G-Buffer: Normal (2)", &viewMode, 2);
        ImGui::RadioButton("G-Buffer: Material (3)", &viewMode, 3);
        ImGui::RadioButton("Shadow Map (4)", &viewMode, 4);
        ImGui::RadioButton("Shadow Factor (5)", &viewMode, 5);
        ImGui::RadioButton("Water Distance (6)", &viewMode, 6);
        ImGui::RadioButton("Water Distance Delta (7)", &viewMode, 7);
        if (viewMode != engine.viewMode())
        {
            engine.setViewMode(viewMode);
        }

        // =====================================================================
        // camera
        // =====================================================================
        EditorWidgets::SectionHeader("Camera");

        const glm::vec3 pos = engine.cameraPosition();

        char posBuf[64];
        snprintf(posBuf, sizeof(posBuf), "(%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        ImGui::Text("Position:");
        ImGui::SameLine();
        EditorWidgets::TextCyan(posBuf);

        char rotBuf[64];
        snprintf(rotBuf, sizeof(rotBuf), "%.1f / %.1f", engine.cameraYaw(), engine.cameraPitch());
        ImGui::Text("Yaw / Pitch:");
        ImGui::SameLine();
        EditorWidgets::TextCyan(rotBuf);

        ImGui::Spacing();

        if (ImGui::Button("Reset Camera (R)", ImVec2(-1, 0)))
        {
            engine.resetCamera();
        }

        EditorWidgets::Spacing();

        // controls help (collapsed by default)
        if (EditorWidgets::SectionHeaderCollapsible("Controls Help", false))
        {
            ImGui::TextWrapped("WASD - Move\n"
                               "Q/E - Up/Down\n"
                               "Shift - Sprint\n"
                               "Right Mouse - Look\n"
                               "M - Toggle mouse capture");
        }

        // =====================================================================
        // performance
        // =====================================================================
        EditorWidgets::SectionHeader("Performance");

        const char* presentModeLabels[] = {"FIFO", "MAILBOX", "IMMEDIATE"};
        int presentMode = static_cast<int>(engine.presentMode());
        if (ImGui::Combo("Present Mode", &presentMode, presentModeLabels,
                         IM_ARRAYSIZE(presentModeLabels)))
        {
            engine.setPresentMode(static_cast<SwapPresentMode>(presentMode));
        }

        int maxFps = static_cast<int>(engine.maxFpsLimit());
        if (ImGui::SliderInt("Max FPS", &maxFps, 0, 240, maxFps == 0 ? "Uncapped" : "%d"))
        {
            engine.setMaxFpsLimit(static_cast<float>(maxFps));
        }

        bool backgroundThrottleEnabled = engine.backgroundThrottleEnabled();
        if (ImGui::Checkbox("Background Throttle", &backgroundThrottleEnabled))
        {
            engine.setBackgroundThrottleEnabled(backgroundThrottleEnabled);
        }
        ImGui::BeginDisabled(!backgroundThrottleEnabled);
        int backgroundFps = static_cast<int>(engine.backgroundFpsLimit());
        if (ImGui::SliderInt("Background FPS", &backgroundFps, 15, 60, "%d"))
        {
            engine.setBackgroundFpsLimit(static_cast<float>(backgroundFps));
        }
        ImGui::EndDisabled();

        float fps = ImGui::GetIO().Framerate;
        float frameTimeMs = 1000.0f / fps;

        EditorWidgets::ValueLabel("FPS", "%.1f", fps);
        EditorWidgets::ValueLabel("Frame Time", "%.2f ms", frameTimeMs);
    }
    const ImVec2 finalSize = ImGui::GetWindowSize();
    ImGui::End();
    return finalSize;
}

} // namespace ScenePanel
