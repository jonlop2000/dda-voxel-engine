#include "UI/ViewportTransformTool.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include "UI/EditorState.h"
#include "UI/EngineFacade.h"
#include "engine/editor/TransformSnapping.h"

namespace ViewportTransformTool
{
namespace
{

using engine::editor::TransformGizmoOperation;
using engine::editor::TransformGizmoResult;
using engine::editor::TransformState;
using engine::editor::ViewportToolSession;

std::optional<ImVec2> projectPoint(const glm::vec3& world,
                                   const glm::mat4& viewProjection,
                                   const ImVec2& displaySize)
{
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) || !std::isfinite(clip.w) || clip.w <= 1e-5f)
    {
        return std::nullopt;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return std::nullopt;
    }
    return ImVec2((ndc.x * 0.5f + 0.5f) * displaySize.x,
                  (ndc.y * 0.5f + 0.5f) * displaySize.y);
}

void setResultStatus(EditorState& state, const TransformGizmoResult& result,
                     const char* action)
{
    if (!result)
    {
        state.viewportToolStatus = std::string(action) + " rejected: " +
                                   engine::editor::transformGizmoErrorLabel(
                                       result.error);
    }
}

void finishGesture(EngineFacade& engine, EditorState& state)
{
    const TransformGizmoResult result = state.viewportTool.commit();
    if (!result)
    {
        setResultStatus(state, result, "Transform commit");
        return;
    }
    if (!result.command)
    {
        state.viewportToolStatus = "Transform unchanged.";
        return;
    }
    state.viewportToolStatus = engine.commitEditorPlaceableEdit(*result.command)
                                   ? "Transform committed."
                                   : "Transform commit failed; scene restored.";
}

TransformState stateFrom(const PlaceableInstance& instance)
{
    return {instance.position, instance.rotation, instance.scale};
}

void beginGesture(EditorState& state, const PlaceableInstance& selected,
                  bool snapScale)
{
    const TransformGizmoResult result = state.viewportTool.begin(
        selected, engine::editor::placeableSnapProfile(std::nullopt, snapScale));
    setResultStatus(state, result, "Transform begin");
}

void updateGesture(EditorState& state, const TransformState& proposed)
{
    const TransformGizmoResult result = state.viewportTool.update(proposed);
    setResultStatus(state, result, "Transform preview");
}

bool operationButton(const char* label, TransformGizmoOperation operation,
                     ViewportToolSession& session)
{
    const bool active = session.operation() == operation;
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.78f, 1.0f));
    }
    const bool pressed = ImGui::Button(label, ImVec2(70.0f, 0.0f));
    if (active)
    {
        ImGui::PopStyleColor();
    }
    if (pressed)
    {
        session.setOperation(operation);
    }
    return pressed;
}

void drawTransformControl(EngineFacade& engine, EditorState& state,
                          const PlaceableInstance& selected)
{
    const PlaceableInstance& shown = state.viewportTool.preview() != nullptr
                                         ? *state.viewportTool.preview()
                                         : selected;
    const TransformGizmoOperation operation = state.viewportTool.operation();

    if (operation == TransformGizmoOperation::Translate)
    {
        float xz[2] = {shown.position.x, shown.position.z};
        const bool changed = ImGui::DragFloat2("Position XZ", xz, 0.05f,
                                               0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemActivated())
        {
            beginGesture(state, selected, false);
        }
        if (changed && state.viewportTool.gestureActive())
        {
            TransformState proposed = stateFrom(shown);
            proposed.translation.x = xz[0];
            proposed.translation.z = xz[1];
            updateGesture(state, proposed);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() &&
            state.viewportTool.gestureActive())
        {
            finishGesture(engine, state);
        }
        ImGui::TextDisabled("Snap: 0.25 units; authored Y preserved");
        return;
    }

    if (operation == TransformGizmoOperation::Rotate)
    {
        float yawDegrees = glm::degrees(glm::yaw(shown.rotation));
        const bool changed = ImGui::DragFloat("Yaw", &yawDegrees, 1.0f,
                                              -360.0f, 360.0f, "%.0f deg");
        if (ImGui::IsItemActivated())
        {
            beginGesture(state, selected, false);
        }
        if (changed && state.viewportTool.gestureActive())
        {
            TransformState proposed = stateFrom(shown);
            proposed.rotation = glm::angleAxis(glm::radians(yawDegrees),
                                               glm::vec3(0.0f, 1.0f, 0.0f));
            updateGesture(state, proposed);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() &&
            state.viewportTool.gestureActive())
        {
            finishGesture(engine, state);
        }
        ImGui::TextDisabled("Snap: 15 degrees; yaw only");
        return;
    }

    float uniformScale = shown.scale.x;
    const bool changed = ImGui::DragFloat("Uniform Scale", &uniformScale,
                                          0.02f, 0.1f, 20.0f, "%.2f");
    if (ImGui::IsItemActivated())
    {
        beginGesture(state, selected, true);
    }
    if (changed && state.viewportTool.gestureActive())
    {
        TransformState proposed = stateFrom(shown);
        proposed.scale = glm::vec3(std::max(uniformScale, 0.1f));
        updateGesture(state, proposed);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state.viewportTool.gestureActive())
    {
        finishGesture(engine, state);
    }
    ImGui::TextDisabled("Snap: 0.10 uniform scale");
}

void drawWorldMarker(EngineFacade& engine, const ImVec4& viewport,
                     const PlaceableInstance& selected)
{
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
    {
        return;
    }
    const glm::mat4 viewProjection = engine.cameraViewProjection(
        displaySize.x / displaySize.y);
    const auto center = projectPoint(selected.position, viewProjection, displaySize);
    if (!center)
    {
        return;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(viewport.x, viewport.y),
                       ImVec2(viewport.x + viewport.z, viewport.y + viewport.w), true);
    draw->AddCircleFilled(*center, 6.0f, IM_COL32(255, 235, 120, 230));
    draw->AddCircle(*center, 11.0f, IM_COL32(255, 255, 255, 245), 20, 2.0f);

    constexpr float axisLength = 2.0f;
    const struct
    {
        glm::vec3 axis;
        ImU32 color;
    } axes[] = {
        {{axisLength, 0.0f, 0.0f}, IM_COL32(238, 82, 83, 255)},
        {{0.0f, axisLength, 0.0f}, IM_COL32(92, 220, 120, 255)},
        {{0.0f, 0.0f, axisLength}, IM_COL32(90, 145, 255, 255)},
    };
    for (const auto& axis : axes)
    {
        if (const auto endpoint = projectPoint(selected.position + axis.axis,
                                               viewProjection, displaySize))
        {
            draw->AddLine(*center, *endpoint, axis.color, 3.0f);
            draw->AddCircleFilled(*endpoint, 3.0f, axis.color);
        }
    }
    draw->PopClipRect();
}

} // namespace

void Draw(EngineFacade& engine, const ImVec4& viewportRect)
{
    if (viewportRect.z < 260.0f || viewportRect.w < 120.0f)
    {
        return;
    }

    EditorState& state = engine.editorState();
    PlaceableInstance selected{};
    const bool selectedPlaceableExists =
        engine.resolveEditorSelectedPlaceable(selected);
    if (!state.viewportTool.reconcile(selectedPlaceableExists,
                                      engine.editorVoxelStructureRevision()))
    {
        state.viewportToolStatus = "Selection cleared after scene rebuild.";
    }

    if (selectedPlaceableExists && state.viewportTool.hasSelection())
    {
        const PlaceableInstance& marker = state.viewportTool.preview() != nullptr
                                              ? *state.viewportTool.preview()
                                              : selected;
        drawWorldMarker(engine, viewportRect, marker);
    }

    ImGui::SetNextWindowPos(ImVec2(viewportRect.x + 12.0f,
                                  viewportRect.y + 12.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Viewport Transform", nullptr, flags))
    {
        if (!state.viewportTool.hasSelection())
        {
            if (engine.sceneConfig().loadAquariumTest)
            {
                ImGui::TextUnformatted("Select: left-click a placeable");
            }
            else
            {
                ImGui::TextUnformatted("No authored placeables in this scene");
                ImGui::TextDisabled("Runtime voxel volumes are read-only.");
            }
            ImGui::TextWrapped("%s", state.viewportToolStatus.c_str());
        }
        else if (!selectedPlaceableExists)
        {
            ImGui::TextUnformatted("Read-only selection");
            ImGui::TextWrapped("%s", state.viewportToolStatus.c_str());
        }
        else
        {
            ImGui::Text("%s", selected.prototypeSlug.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                state.viewportTool.clearSelection();
                state.viewportToolStatus = "Selection cleared.";
            }

            if (state.viewportTool.hasSelection())
            {
                operationButton("Move", TransformGizmoOperation::Translate,
                                state.viewportTool);
                ImGui::SameLine();
                operationButton("Rotate", TransformGizmoOperation::Rotate,
                                state.viewportTool);
                ImGui::SameLine();
                operationButton("Scale", TransformGizmoOperation::Scale,
                                state.viewportTool);
                drawTransformControl(engine, state, selected);
                ImGui::TextWrapped("%s", state.viewportToolStatus.c_str());
            }
        }
    }
    ImGui::End();

}

} // namespace ViewportTransformTool
