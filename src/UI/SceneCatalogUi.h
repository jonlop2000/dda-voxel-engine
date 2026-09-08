#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include "UI/SceneCatalogModel.h"
#include "UI/PreparedSceneCatalog.h"
#include "UI/EngineFacade.h"

namespace SceneCatalogUi
{

inline void drawSceneTooltip(const SceneCatalogEntry& scene)
{
    if (!ImGui::BeginItemTooltip())
    {
        return;
    }

    ImGui::TextUnformatted(scene.fileName.c_str());
    if (!scene.valid && !scene.errorMessage.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", scene.errorMessage.c_str());
    }
    else if (!scene.description.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", scene.description.c_str());
    }

    ImGui::EndTooltip();
}

inline void drawSceneSelectable(EngineFacade& engine, const SceneCatalogEntry& scene,
                                bool isCurrent)
{
    ImGui::PushID(scene.fileName.c_str());

    if (scene.valid)
    {
        if (ImGui::Selectable(scene.displayName.c_str(), isCurrent))
        {
            engine.loadSceneFromFile(scene.path);
        }
    }
    else
    {
        const std::string label = scene.displayName + " (invalid)";
        ImGui::BeginDisabled();
        ImGui::Selectable(label.c_str(), false);
        ImGui::EndDisabled();
    }

    drawSceneTooltip(scene);
    ImGui::PopID();
}

inline void DrawPanelCatalog(EngineFacade& engine, const char* filter,
                             PreparedSceneCatalog* prepared = nullptr)
{
    const auto& scenes = engine.availableScenes();
    if (scenes.empty())
    {
        ImGui::TextDisabled("No authored scene files found.");
        return;
    }

    const std::filesystem::path activePath = prepared ? engine.currentScenePath() :
        engine.currentScenePath().lexically_normal();
    const std::string activeName = engine.sceneConfig().name;
    if (prepared != nullptr)
    {
        prepared->update(scenes, engine.sceneCatalogRevision(), filter, activePath, activeName);
    }
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float listHeight = std::max(150.0f, lineHeight * 10.0f);

    bool matchedAny = false;
    if (ImGui::BeginChild("SceneCatalogList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders))
    {
        for (const SceneGroupDefinition& groupDef : kSceneGroups)
        {
            const int count = prepared ? static_cast<int>(prepared->visible(groupDef.group).size()) :
                countMatchingScenes(scenes, groupDef.group, filter);
            if (count == 0)
            {
                continue;
            }

            matchedAny = true;
            char header[96];
            std::snprintf(header, sizeof(header), "%s (%d)", groupDef.label, count);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
            if ((filter != nullptr && filter[0] != '\0') ||
                (prepared ? prepared->groupIsActive(groupDef.group) :
                    groupContainsActiveScene(scenes, groupDef.group, activePath, activeName)) ||
                groupDef.group == SceneGroup::Primary)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }

            if (ImGui::CollapsingHeader(header, flags))
            {
                if (prepared != nullptr)
                {
                    for (size_t index : prepared->visible(groupDef.group))
                    {
                        drawSceneSelectable(engine, scenes[index], prepared->isActive(index));
                    }
                    continue;
                }
                for (const SceneCatalogEntry& scene : scenes)
                {
                    if (classifyScene(scene) != groupDef.group ||
                        !sceneMatchesFilter(scene, filter))
                    {
                        continue;
                    }
                    drawSceneSelectable(engine, scene, isActiveScene(scene, activePath, activeName));
                }
            }
        }

        if (!matchedAny)
        {
            ImGui::TextDisabled("No scenes match the filter.");
        }
    }
    ImGui::EndChild();
}

inline void DrawMenuCatalog(EngineFacade& engine)
{
    const auto& scenes = engine.availableScenes();
    const std::filesystem::path activePath = engine.currentScenePath().lexically_normal();
    const std::string activeName = engine.sceneConfig().name;

    if (scenes.empty())
    {
        ImGui::MenuItem("No authored scenes found", nullptr, false, false);
        return;
    }

    for (const SceneGroupDefinition& groupDef : kSceneGroups)
    {
        const int count = countMatchingScenes(scenes, groupDef.group, nullptr);
        if (count == 0)
        {
            continue;
        }

        char label[96];
        std::snprintf(label, sizeof(label), "%s (%d)", groupDef.label, count);
        if (!ImGui::BeginMenu(label))
        {
            continue;
        }

        for (const SceneCatalogEntry& scene : scenes)
        {
            if (classifyScene(scene) != groupDef.group)
            {
                continue;
            }

            const bool isCurrent = isActiveScene(scene, activePath, activeName);
            if (scene.valid)
            {
                if (ImGui::MenuItem(scene.displayName.c_str(), nullptr, isCurrent))
                {
                    engine.loadSceneFromFile(scene.path);
                }
            }
            else
            {
                const std::string invalidLabel = scene.displayName + " (invalid)";
                ImGui::MenuItem(invalidLabel.c_str(), nullptr, false, false);
            }
            drawSceneTooltip(scene);
        }

        ImGui::EndMenu();
    }
}

} // namespace SceneCatalogUi
