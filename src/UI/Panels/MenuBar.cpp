#include "UI/Panels/MenuBar.h"

#include <imgui.h>

#include "UI/EngineFacade.h"
#include "UI/SceneCatalogUi.h"

namespace MenuBar
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
    EditorState& editorState = engine.editorState();
    bool saveRequested = false;
    bool undoRequested = false;
    bool redoRequested = false;
    if (ImGui::BeginMainMenuBar())
    {
        saveRequested |= !engine.currentScenePath().empty() &&
                         ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S,
                                         ImGuiInputFlags_RouteGlobal);
        undoRequested |= engine.editorHasPlaceableUndo() &&
                         ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z,
                                         ImGuiInputFlags_RouteGlobal);
        redoRequested |= engine.editorHasPlaceableRedo() &&
                         ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y,
                                         ImGuiInputFlags_RouteGlobal);

        // file menu
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false,
                                !engine.currentScenePath().empty()))
            {
                saveRequested = true;
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Load Scene"))
            {
                if (ImGui::MenuItem("Refresh Scene Catalog"))
                {
                    engine.refreshAvailableScenes();
                }
                ImGui::Separator();

                SceneCatalogUi::DrawMenuCatalog(engine);
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                // exit handled by glfw
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo Placeable Edit", "Ctrl+Z", false,
                                engine.editorHasPlaceableUndo()))
            {
                undoRequested = true;
            }
            if (ImGui::MenuItem("Redo Placeable Edit", "Ctrl+Y", false,
                                engine.editorHasPlaceableRedo()))
            {
                redoRequested = true;
            }
            ImGui::EndMenu();
        }

        // run menu
        if (ImGui::BeginMenu("Run"))
        {
            if (ImGui::MenuItem("Play Runtime UI", "F5", false,
                                !engine.isRuntimeUiPlayPreviewActive()))
            {
                engine.enterRuntimeUiPlayPreview();
            }
            if (ImGui::MenuItem("Play From Main Menu", "Shift+F5", false,
                                !engine.isRuntimeUiPlayPreviewActive()))
            {
                engine.enterRuntimeUiPlayPreview(true);
            }

            ImGui::EndMenu();
        }

        // view menu
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Scene Panel", "F1", &editorState.leftPanelVisible);
            ImGui::MenuItem("Rendering Panel", "F2", &editorState.rightPanelVisible);
            ImGui::MenuItem("Fish Panel", nullptr, &editorState.fishPanelVisible);
            ImGui::MenuItem("Status Bar", nullptr, &editorState.statusBarVisible);

            ImGui::Separator();

            if (ImGui::BeginMenu("View Mode"))
            {
                if (ImGui::MenuItem("Final Composite", "0", engine.viewMode() == 0))
                {
                    engine.setViewMode(0);
                }
                if (ImGui::MenuItem("Albedo", "1", engine.viewMode() == 1))
                {
                    engine.setViewMode(1);
                }
                if (ImGui::MenuItem("Normal", "2", engine.viewMode() == 2))
                {
                    engine.setViewMode(2);
                }
                if (ImGui::MenuItem("Material", "3", engine.viewMode() == 3))
                {
                    engine.setViewMode(3);
                }
                if (ImGui::MenuItem("Shadow Map", "4", engine.viewMode() == 4))
                {
                    engine.setViewMode(4);
                }
                if (ImGui::MenuItem("Shadow Factor", "5", engine.viewMode() == 5))
                {
                    engine.setViewMode(5);
                }
                if (ImGui::MenuItem("Water Distance", "6", engine.viewMode() == 6))
                {
                    engine.setViewMode(6);
                }
                if (ImGui::MenuItem("Water Distance Delta", "7", engine.viewMode() == 7))
                {
                    engine.setViewMode(7);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        // rendering menu
        if (ImGui::BeginMenu("Rendering"))
        {
            if (ImGui::MenuItem("Reset TAA History"))
            {
                engine.requestTaaHistoryReset();
            }
            if (ImGui::MenuItem("Reset Shadow History"))
            {
                engine.requestShadowHistoryReset();
            }
            if (ImGui::MenuItem("Reset AO History"))
            {
                engine.requestAoHistoryReset();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Toggle Tonemapping", "T", engine.tonemapEnabled()))
            {
                engine.setTonemapEnabled(!engine.tonemapEnabled());
            }
            if (ImGui::MenuItem("Toggle Bloom", "B", engine.bloomEnabled()))
            {
                engine.setBloomEnabled(!engine.bloomEnabled());
            }

            ImGui::EndMenu();
        }

        // debug menu
        if (ImGui::BeginMenu("Debug"))
        {
            if (ImGui::MenuItem("GPU Profiler", nullptr, engine.gpuProfilerEnabled()))
            {
                engine.setGpuProfilerEnabled(!engine.gpuProfilerEnabled());
            }
            if (ImGui::MenuItem("Profiler Overlay", nullptr, editorState.profilerOverlayVisible))
            {
                editorState.profilerOverlayVisible = !editorState.profilerOverlayVisible;
            }
            if (ImGui::MenuItem("Pixel Inspector", nullptr, engine.pixelInspectEnabled()))
            {
                engine.setPixelInspectEnabled(!engine.pixelInspectEnabled());
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Dump DDA Metrics", "F11"))
            {
                // metrics dump handled by keyboard shortcut
            }

            ImGui::EndMenu();
        }

        // help menu
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Controls"))
            {
                // could show a popup with controls
            }
            if (ImGui::MenuItem("About"))
            {
                // could show an about popup
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (undoRequested)
    {
        engine.editorUndoPlaceableEdit();
    }
    if (redoRequested)
    {
        engine.editorRedoPlaceableEdit();
    }
    if (saveRequested)
    {
        engine.editorSaveCurrentScene();
    }
}

} // namespace MenuBar
