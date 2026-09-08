#include "UI/Panels/SceneHierarchyPanel.h"

#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>

#include "UI/EditorState.h"
#include "UI/EditorWidgets.h"
#include "UI/EngineFacade.h"
#include "engine/editor/SceneHierarchy.h"

namespace SceneHierarchyPanel
{
namespace
{

void drawEditActions(EngineFacade& engine)
{
    ImGui::BeginDisabled(!engine.editorHasPlaceableUndo());
    if (ImGui::SmallButton("Undo"))
    {
        engine.editorUndoPlaceableEdit();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!engine.editorHasPlaceableRedo());
    if (ImGui::SmallButton("Redo"))
    {
        engine.editorRedoPlaceableEdit();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool hasSavePath = !engine.currentScenePath().empty();
    ImGui::BeginDisabled(!hasSavePath);
    if (ImGui::SmallButton("Save"))
    {
        engine.editorSaveCurrentScene();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        if (hasSavePath)
        {
            ImGui::SetTooltip("Save to %s",
                              engine.currentScenePath().string().c_str());
        }
        else
        {
            ImGui::SetTooltip("This scene has no authored file path.");
        }
    }

    ImGui::SameLine();
    if (engine.editorPlaceableSceneDirty())
    {
        EditorWidgets::TextWarning("Unsaved");
    }
    else
    {
        EditorWidgets::TextSuccess("Saved");
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Tracks journal-backed placeable edits. Legacy live scene sliders "
            "are not yet part of this dirty indicator.");
    }
}

void drawAuthoringActions(EngineFacade& engine)
{
    EditorState& state = engine.editorState();
    const bool available = engine.editorPlaceableAuthoringAvailable();
    const std::vector<PlaceablePrototype> prototypes =
        engine.editorPlaceablePrototypes();
    state.placeableAuthoringPrototypeIndex = std::clamp(
        state.placeableAuthoringPrototypeIndex, 0,
        std::max(0, static_cast<int>(prototypes.size()) - 1));

    if (!ImGui::TreeNodeEx("Author Placeables",
                           ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth))
    {
        return;
    }

    ImGui::BeginDisabled(!available || prototypes.empty());
    const char* selectedPrototype = prototypes.empty()
                                        ? "No prototypes"
                                        : prototypes[static_cast<size_t>(
                                                         state.placeableAuthoringPrototypeIndex)]
                                              .slug.c_str();
    if (ImGui::BeginCombo("Prototype", selectedPrototype))
    {
        for (size_t index = 0; index < prototypes.size(); ++index)
        {
            const bool selected = static_cast<int>(index) ==
                                  state.placeableAuthoringPrototypeIndex;
            const std::string label =
                prototypes[index].slug + " v" +
                std::to_string(prototypes[index].version);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                state.placeableAuthoringPrototypeIndex =
                    static_cast<int>(index);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::DragFloat2("Create XZ", &state.placeableAuthoringCreateXZ.x,
                      0.25f, 0.0f, 0.0f, "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Final position snaps to the 0.25-unit placeable lattice and the "
            "aquarium substrate surface.");
    }
    if (ImGui::Button("Create Placeable", ImVec2(-1.0f, 0.0f)) &&
        !prototypes.empty())
    {
        const PlaceablePrototype& prototype =
            prototypes[static_cast<size_t>(
                state.placeableAuthoringPrototypeIndex)];
        engine.editorCreatePlaceable(
            prototype.slug, prototype.version,
            state.placeableAuthoringCreateXZ);
    }
    ImGui::EndDisabled();

    PlaceableInstance selected{};
    const bool hasSelected = engine.resolveEditorSelectedPlaceable(selected);
    ImGui::BeginDisabled(!available || !hasSelected);
    ImGui::DragFloat2("Duplicate Offset XZ",
                      &state.placeableAuthoringDuplicateOffsetXZ.x, 0.25f,
                      0.0f, 0.0f, "%.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Default +3.25 X clears two unscaled 1.55-unit plant footprints; "
            "occupied or out-of-bounds results are rejected.");
    }
    if (ImGui::SmallButton("Duplicate"))
    {
        engine.editorDuplicateSelectedPlaceable(
            state.placeableAuthoringDuplicateOffsetXZ);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelected);
    if (ImGui::SmallButton("Delete..."))
    {
        ImGui::OpenPopup("Delete selected placeable?");
    }
    ImGui::EndDisabled();

    if (!available)
    {
        ImGui::TextDisabled(
            "Create and duplicate currently require the default aquarium tank.");
    }

    if (ImGui::BeginPopupModal("Delete selected placeable?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (hasSelected)
        {
            ImGui::Text("Delete %s?", selected.prototypeSlug.c_str());
            ImGui::TextDisabled("The operation is journal-backed and can be undone.");
        }
        else
        {
            ImGui::TextDisabled("The selected placeable is no longer available.");
        }

        ImGui::BeginDisabled(!hasSelected);
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            engine.editorDeleteSelectedPlaceable();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::TreePop();
}

void drawPlaceableRows(EngineFacade& engine,
                       const engine::editor::SceneHierarchySnapshot& hierarchy)
{
    EditorState& state = engine.editorState();
    if (hierarchy.placeables.empty())
    {
        ImGui::TextDisabled("No authored placeables in this scene.");
        ImGui::TextDisabled("Runtime voxel volumes remain read-only.");
        return;
    }

    const std::string groupLabel =
        "Placeables (" + std::to_string(hierarchy.placeables.size()) + ")";
    if (!ImGui::TreeNodeEx(groupLabel.c_str(),
                           ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth))
    {
        return;
    }

    ImGui::BeginChild("##AuthoredPlaceableHierarchy", ImVec2(0.0f, 180.0f),
                      true);
    for (const engine::editor::SceneHierarchyPlaceable& row :
         hierarchy.placeables)
    {
        ImGui::PushID(row.uuid.c_str());
        const bool selected = engine::editor::selectionTargetEqual(
            state.viewportTool.selection(), row.target);
        if (ImGui::Selectable(row.label.c_str(), selected))
        {
            state.viewportTool.select(row.target);
            state.viewportToolStatus =
                "Selected " + row.prototypeSlug + " from hierarchy.";
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Prototype: %s v%u", row.prototypeSlug.c_str(),
                        row.prototypeVersion);
            ImGui::Text("Position: %.2f, %.2f, %.2f", row.position.x,
                        row.position.y, row.position.z);
            ImGui::TextDisabled("UUID: %s", row.uuid.c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TreePop();
}

} // namespace

void Draw(EngineFacade& engine)
{
    EditorWidgets::SectionHeader("Scene Hierarchy");
    drawEditActions(engine);
    drawAuthoringActions(engine);

    const std::vector<PlaceableInstance> effective = engine.editorPlaceables();
    const SceneConfig& config = engine.sceneConfig();
    const bool usesImplicitDefaults =
        config.useDefaultPlaceables && config.placeables.empty() &&
        !effective.empty();
    const engine::editor::SceneHierarchySnapshot hierarchy =
        engine::editor::buildPlaceableSceneHierarchy(effective,
                                                      usesImplicitDefaults);

    ImGui::Text("%s", config.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(hierarchy.usesImplicitDefaults ? "(default set)"
                                                       : "(authored set)");

    if (!hierarchy)
    {
        EditorWidgets::TextError(
            engine::editor::sceneHierarchyErrorLabel(hierarchy.error));
        ImGui::TextDisabled("Invalid row: %zu", hierarchy.invalidPlaceableIndex);
    }
    else
    {
        drawPlaceableRows(engine, hierarchy);
    }

    const std::string& editStatus = engine.editorPlaceableEditStatus();
    if (!editStatus.empty())
    {
        ImGui::TextWrapped("%s", editStatus.c_str());
    }
}

} // namespace SceneHierarchyPanel
