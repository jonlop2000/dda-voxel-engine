cmake_minimum_required(VERSION 3.24)
project(voxel_aquarium VERSION 0.1.0 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VOXEL_WITH_EDITOR "Build the ImGui editor and overlay UI" ON)
option(VOXEL_WITH_RUNTIME_UI "Build the runtime game UI layer" ON)
option(VOXEL_ENABLE_RUNTIME_AUTOMATION_TESTS
       "Enable runtime smoke and content-validation integration tests" ON)

if(APPLE)
    add_compile_definitions(VOXEL_PLATFORM_MACOS=1)
elseif(WIN32)
    add_compile_definitions(VOXEL_PLATFORM_WINDOWS=1)
endif()

set(VOXEL_UI_SOURCES)
if(VOXEL_WITH_EDITOR)
    list(APPEND VOXEL_UI_SOURCES
        src/UI/ImGuiLayer.cpp
        src/UI/EngineFacade.cpp
        src/UI/EditorTheme.cpp
        src/UI/EditorWidgets.cpp
        src/UI/Editor.cpp
        src/UI/SceneCatalogExperiment.cpp
        src/UI/ViewportTransformTool.cpp
        src/App/AppEditorInteraction.cpp
        src/UI/Panels/StatusBar.cpp
        src/UI/Panels/MenuBar.cpp
        src/UI/Panels/ProfilerOverlay.cpp
        src/UI/Panels/DebugInfoPanel.cpp
        src/UI/Panels/SceneHierarchyPanel.cpp
        src/UI/Panels/ScenePanel.cpp
        src/UI/Panels/RenderingPanel.cpp
        src/UI/Panels/FishPanel.cpp
    )
endif()

add_executable(voxel_aquarium
    src/App/App.cpp
    src/App/AppAutomationProfile.cpp
    src/App/AppFrameLifecycle.cpp
    src/App/AppFrameShell.cpp
    src/App/AppFramePreparation.cpp
    src/App/AppVoxelFrame.cpp
    src/App/AppRenderHelpers.cpp
    src/App/AppWaterEffects.cpp
    src/App/AppRenderPipelineShowcase.cpp
    src/App/AppPassResources.cpp
    src/App/AppRenderCapabilities.cpp
    src/App/AppSceneGlass.cpp
    src/App/AppSceneSettings.cpp
    src/App/AppSceneVolume.cpp
    src/App/AppSwapchainResources.cpp
    src/App/ScenePresentationProfileRuntime.cpp
    src/App/Camera.cpp
    src/App/Input.cpp
    src/App/Transform.cpp
    src/Assets/Material.cpp
    src/Assets/MeshLoaderOBJ.cpp
    src/Assets/Texture.cpp
    src/Core/JobSystem.cpp
    src/Core/Logger.cpp
    src/Core/RuntimeContentManifest.cpp
    src/main.cpp
    src/engine/render/Commands.cpp
    src/engine/render/Frustum.cpp
    src/engine/render/FrameSync.cpp
    src/engine/render/GpuProfiler.cpp
    src/engine/render/Swapchain.cpp
    src/engine/render/VulkanContext.cpp
    src/engine/render/FrameCharacterization.cpp
    src/engine/performance/AutomationProfileSession.cpp
    src/engine/render/Renderer.cpp
    src/engine/render/RendererPassResources.cpp
    src/engine/render/AuxiliaryRayResolution.cpp
    src/engine/render/RenderResolution.cpp
    src/engine/render/RenderQualityPreset.cpp
    src/engine/render/RenderPipelineShowcase.cpp
    src/engine/render/SunShadowSamplingPolicy.cpp
    src/engine/render/HemisphereAmbient.cpp
    src/engine/render/PaintedClouds.cpp
    src/engine/render/PaintedSky.cpp
    src/engine/render/FoliagePresentation.cpp
    src/engine/render/FoliageVoxelGeometry.cpp
    src/engine/render/WindborneParticleVoxelGeometry.cpp
    src/engine/render/SceneAtmosphere.cpp
    src/engine/render/gpu/DescriptorAllocator.cpp
    src/engine/render/gpu/DescriptorLayoutCache.cpp
    src/engine/render/gpu/SamplerCache.cpp
    src/engine/render/voxel/TerrainShadowColumnEligibility.cpp
    src/engine/render/voxel/TerrainShadowColumnGpuBuffer.cpp
    src/engine/render/voxel/VoxelAlignedLayerTraversal.cpp
    src/engine/render/voxel/VoxelLightingOcclusionPolicy.cpp
    src/engine/render/voxel/VoxelRasterFaceSelection.cpp
    src/engine/render/voxel/VoxelRenderResources.cpp
    src/engine/render/passes/BloomPass.cpp
    src/engine/render/passes/CompositePass.cpp
    src/engine/render/passes/AOPass.cpp
    src/engine/render/passes/AuxiliaryTileListPass.cpp
    src/engine/render/passes/GBufferPass.cpp
    src/engine/render/passes/FoliagePass.cpp
    src/engine/render/passes/GlassPass.cpp
    src/engine/render/passes/VoxelGlassRefractPass.cpp
    src/engine/render/passes/LightingPass.cpp
    src/engine/render/passes/ShadowPass.cpp
    src/engine/render/passes/ShadowBuffer.cpp
    src/engine/render/passes/LocalLightShadowBuffer.cpp
    src/engine/render/passes/ShadowRayPass.cpp
    src/engine/render/passes/TemporalResolve.cpp
    src/engine/render/passes/ShadowDenoisePass.cpp
    src/engine/render/passes/SpatialUpscalePass.cpp
    src/engine/render/passes/LocalLightShadowPass.cpp
    src/engine/render/passes/LocalShadowBlur.cpp
    src/engine/render/passes/LocalLightTemporalResolve.cpp
    src/engine/render/passes/BlueNoiseTexture.cpp
    src/engine/render/passes/TAAPass.cpp
    src/engine/render/passes/UiOverlayPass.cpp
    src/engine/render/passes/WaterBodyPass.cpp
    src/engine/render/passes/WaterPass.cpp
    src/engine/render/passes/WaterVolumePrepass.cpp
    src/engine/render/passes/VoxelGBufferPass.cpp
    src/Resources/GpuBuffer.cpp
    src/Resources/GpuImage.cpp
    src/Resources/LightsBuffer.cpp
    src/Resources/Mesh.cpp
    src/Resources/MeshUploadQueue.cpp
    src/Resources/ShaderModule.cpp
    src/ThirdParty/stb_image.cpp
    src/UI/Runtime/BuildPlacement.cpp
    src/UI/Runtime/CollectionCodexScreen.cpp
    src/UI/Runtime/UiActions.cpp
    src/UI/Runtime/UiDebugFont.cpp
    src/UI/Runtime/UiContext.cpp
    src/UI/Runtime/UiElement.cpp
    src/UI/Runtime/UiBinding.cpp
    src/UI/Runtime/UiScreens.cpp
    src/UI/Runtime/UiScreenControllers.cpp
    src/UI/Runtime/UiTheme.cpp
    src/UI/Runtime/UiTween.cpp
    src/UI/Runtime/UiWidgets.cpp
    ${VOXEL_UI_SOURCES}
    src/engine/game/AxolotlBellyFloat.cpp
    src/engine/game/AxolotlCreaturePolish.cpp
    src/engine/game/AxolotlModel.cpp
    src/engine/game/FishCelebration.cpp
    src/engine/game/FishHabitatResolver.cpp
    src/engine/game/FoliageArchetype.cpp
    src/engine/game/FoliagePatch.cpp
    src/engine/game/GameRuntime.cpp
    src/engine/game/PlaceableRuntime.cpp
    src/engine/input/InputController.cpp
    src/engine/input/WindowCoordinates.cpp
    src/engine/scene/EnvironmentSettings.cpp
    src/engine/scene/EnvironmentTime.cpp
    src/engine/scene/EnvironmentWind.cpp
    src/engine/scene/WindborneParticleSettings.cpp
    src/engine/scene/WindborneParticleField.cpp
    src/engine/scene/SceneManager.cpp
    src/engine/scene/SceneObjectBuilder.cpp
    src/engine/render/passes/OBBPass.cpp
    src/engine/voxel/VoxelPalette.cpp
    src/engine/voxel/VoxelCellVariation.cpp
    src/engine/voxel/VoxelMaterialAtlas.cpp
    src/engine/voxel/VoxelSystem.cpp
    src/engine/voxel/ModularGlassMeshManager.cpp
    src/engine/voxel/ModularGlassMesher.cpp
    src/engine/voxel/VoxLoader.cpp
    src/engine/voxel/VoxelVolume.cpp
    src/engine/voxel/VoxelWorld.cpp
    src/engine/voxel/VolumeGpuBuffer.cpp
    src/engine/voxel/WaterVolume.cpp
    src/Water/WaterContainer.cpp
    src/engine/voxel/Chunk.cpp
    src/engine/voxel/ChunkGrid.cpp
    src/engine/voxel/GreedyMesher.cpp
    src/engine/voxel/Raycast.cpp
    src/engine/voxel/VoxelMesher.cpp
    src/engine/voxel/WorldGen.cpp
    src/engine/scene/SceneCatalog.cpp
    src/engine/scene/SceneConfig.cpp
    src/engine/scene/SceneSerializer.cpp
    src/engine/scene/AquariumScene.cpp
    src/engine/scene/SunroofGroundFoliage.cpp
    src/engine/scene/SunroofLivingWater.cpp
    src/engine/scene/CloudScene.cpp
    src/engine/scene/CloudSceneRuntime.cpp
    src/engine/game/FoliageCatalog.cpp
    src/engine/scene/DenseSkipProbeScene.cpp
    src/engine/scene/StarScene.cpp
    src/engine/scene/GlassTestScene.cpp
    src/engine/scene/ObbTestScene.cpp
    src/engine/scene/LightingParityProbe.cpp
    src/engine/scene/NaturePondGlassDome.cpp
    src/engine/scene/NaturePondFoliageDistribution.cpp
    src/engine/scene/NaturePondGenerator.cpp
    src/engine/scene/NaturePondScene.cpp
    src/engine/scene/NaturePondSunroofProbe.cpp
    src/engine/scene/ProceduralWorldMaterials.cpp
    src/engine/scene/ProceduralWorldScene.cpp
    src/engine/scene/ScenePresentationProfile.cpp
    src/engine/scene/ScenePresentationProfileVariants.cpp
    src/engine/scene/SoftVoxelPresentationProfile.cpp
    src/engine/scene/TankGlassBuilder.cpp
    src/engine/scene/VoxScene.cpp
    src/engine/scene/VoxelImportScene.cpp
    src/engine/voxel/VoxelBuilder.cpp
)

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

if(VOXEL_WITH_EDITOR)
    find_package(imgui CONFIG REQUIRED)
endif()

add_library(dda_voxel_game_care STATIC
    src/engine/game/CareRuntime.cpp
    src/engine/game/CleanupCrew.cpp
    src/engine/game/CollectionCodex.cpp
    src/engine/game/CreatureCareState.cpp
    src/engine/game/CreatureRuntimeBinding.cpp
    src/engine/game/CreatureVitality.cpp
    src/engine/game/GameStateSimulation.cpp
    src/engine/game/HabitatCompatibility.cpp
    src/engine/game/OfflineCare.cpp
    src/engine/game/SpeciesRegistry.cpp
)

add_library(dda_voxel_editor_transform STATIC
    src/engine/editor/PlaceableAuthoringCommands.cpp
    src/engine/editor/PlaceableAuthoringPlacement.cpp
    src/engine/editor/SceneHierarchy.cpp
    src/engine/editor/SceneDocument.cpp
    src/engine/editor/SelectionPicking.cpp
    src/engine/editor/TransformGizmoAdapter.cpp
    src/engine/editor/TransformSnapping.cpp
    src/engine/editor/ViewportPlacementSession.cpp
    src/engine/editor/ViewportToolSession.cpp
    src/engine/game/PlaceableEditJournal.cpp
    src/engine/game/PlaceableTransform.cpp
)

target_include_directories(dda_voxel_editor_transform PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(dda_voxel_editor_transform PUBLIC
    glm::glm
)

if(MSVC)
    target_compile_options(dda_voxel_editor_transform PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(dda_voxel_editor_transform PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(dda_voxel_game_care PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(dda_voxel_game_care PUBLIC
    glm::glm
)

if(MSVC)
    target_compile_options(dda_voxel_game_care PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(dda_voxel_game_care PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(voxel_aquarium PRIVATE
    dda_voxel_editor_transform
    dda_voxel_game_care
    glfw
    glm::glm
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    nlohmann_json::nlohmann_json
)

if(VOXEL_WITH_EDITOR)
    target_link_libraries(voxel_aquarium PRIVATE imgui::imgui)
endif()

target_include_directories(voxel_aquarium PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

# warnings
if(MSVC)
    target_compile_options(voxel_aquarium PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(voxel_aquarium PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Debug toggle for validation
target_compile_definitions(voxel_aquarium PRIVATE
    GLFW_INCLUDE_NONE
    VOXEL_WITH_EDITOR=$<BOOL:${VOXEL_WITH_EDITOR}>
    VOXEL_WITH_RUNTIME_UI=$<BOOL:${VOXEL_WITH_RUNTIME_UI}>
    DDA_VOXEL_BUILD_CONFIGURATION="${CMAKE_BUILD_TYPE}"
    $<$<CONFIG:Debug>:VOXEL_ENABLE_VALIDATION=1>
    $<$<NOT:$<CONFIG:Debug>>:VOXEL_ENABLE_VALIDATION=0>
)

# glsl -> spir-V build step
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
      "$ENV{VULKAN_SDK}/Bin"
      "$ENV{VULKAN_SDK}/Bin32"
      "$ENV{VULKAN_SDK}/bin"
)

if (NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found. Install Vulkan SDK or add it to PATH.")
endif()

set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
set(RUNTIME_CONTENT_MANIFEST "${SHADER_SRC_DIR}/runtime_content_manifest.json")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")
configure_file("${RUNTIME_CONTENT_MANIFEST}"
               "${SHADER_BIN_DIR}/runtime_content_manifest.json" COPYONLY)

file(READ "${RUNTIME_CONTENT_MANIFEST}" RUNTIME_CONTENT_MANIFEST_JSON)

set(SHADER_INCLUDES "")
string(JSON SHADER_INCLUDE_COUNT LENGTH "${RUNTIME_CONTENT_MANIFEST_JSON}" shader_includes)
if(NOT SHADER_INCLUDE_COUNT EQUAL 0)
  math(EXPR SHADER_INCLUDE_LAST "${SHADER_INCLUDE_COUNT} - 1")
  foreach(IDX RANGE 0 ${SHADER_INCLUDE_LAST})
    string(JSON SHADER_INCLUDE_REL GET "${RUNTIME_CONTENT_MANIFEST_JSON}" shader_includes ${IDX})
    list(APPEND SHADER_INCLUDES "${SHADER_SRC_DIR}/${SHADER_INCLUDE_REL}")
  endforeach()
endif()

set(SPVS "")
string(JSON SHADER_COUNT LENGTH "${RUNTIME_CONTENT_MANIFEST_JSON}" shaders)
if(NOT SHADER_COUNT EQUAL 0)
  math(EXPR SHADER_LAST "${SHADER_COUNT} - 1")
  foreach(IDX RANGE 0 ${SHADER_LAST})
    string(JSON SHADER_SOURCE_REL GET "${RUNTIME_CONTENT_MANIFEST_JSON}" shaders ${IDX} source)
    string(JSON SHADER_OUTPUT_NAME GET "${RUNTIME_CONTENT_MANIFEST_JSON}" shaders ${IDX} output)
    set(SHADER "${SHADER_SRC_DIR}/${SHADER_SOURCE_REL}")
    set(SPV "${SHADER_BIN_DIR}/${SHADER_OUTPUT_NAME}")
    set(SHADER_DEFINITION_ARGS "")
    string(JSON SHADER_DEFINE ERROR_VARIABLE SHADER_DEFINE_ERROR
           GET "${RUNTIME_CONTENT_MANIFEST_JSON}" shaders ${IDX} define)
    if(SHADER_DEFINE_ERROR STREQUAL "NOTFOUND")
      list(APPEND SHADER_DEFINITION_ARGS "-D${SHADER_DEFINE}")
    endif()
  add_custom_command(
    OUTPUT "${SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V ${SHADER_DEFINITION_ARGS}
            "${SHADER}" "-I${SHADER_SRC_DIR}" -o "${SPV}"
    DEPENDS "${SHADER}" ${SHADER_INCLUDES}
    VERBATIM
  )
  list(APPEND SPVS "${SPV}")
  endforeach()
endif()

add_custom_target(build_shaders ALL DEPENDS ${SPVS})
add_dependencies(voxel_aquarium build_shaders)

set(DDA_VOXEL_SCENE_PERSISTENCE_SUPPORT_SOURCES
    src/Core/Logger.cpp
    src/Core/RuntimeContentManifest.cpp
    src/engine/game/FoliageArchetype.cpp
    src/engine/scene/EnvironmentTime.cpp
    src/engine/scene/EnvironmentWind.cpp
    src/engine/scene/WindborneParticleSettings.cpp
    src/engine/scene/NaturePondFoliageDistribution.cpp
    src/engine/scene/NaturePondGenerator.cpp
    src/engine/scene/SceneCatalog.cpp
    src/engine/scene/SceneConfig.cpp
    src/engine/scene/ScenePresentationProfile.cpp
    src/engine/scene/ScenePresentationProfileVariants.cpp
    src/engine/scene/SoftVoxelPresentationProfile.cpp
    src/engine/scene/SceneSerializer.cpp
    src/engine/render/HemisphereAmbient.cpp
    src/engine/render/PaintedClouds.cpp
    src/engine/render/PaintedSky.cpp
    src/engine/render/SceneAtmosphere.cpp
    src/engine/voxel/VoxelCellVariation.cpp
)

add_executable(scene_config_tests
    tests/SceneConfigTests.cpp
    ${DDA_VOXEL_SCENE_PERSISTENCE_SUPPORT_SOURCES}
)

target_link_libraries(scene_config_tests PRIVATE
    dda_voxel_editor_transform
    dda_voxel_game_care
    glm::glm
    nlohmann_json::nlohmann_json
)

target_include_directories(scene_config_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(scene_config_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(scene_config_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME scene_config_tests COMMAND scene_config_tests)

add_library(dda_voxel_scene_catalog_view STATIC src/UI/PreparedSceneCatalog.cpp)
target_include_directories(dda_voxel_scene_catalog_view PUBLIC "${CMAKE_SOURCE_DIR}/src")
if(VOXEL_WITH_EDITOR)
    target_link_libraries(voxel_aquarium PRIVATE dda_voxel_scene_catalog_view)
endif()

add_executable(scene_catalog_view_tests tests/SceneCatalogViewTests.cpp)
target_link_libraries(scene_catalog_view_tests PRIVATE dda_voxel_scene_catalog_view)
foreach(catalog_target IN ITEMS dda_voxel_scene_catalog_view scene_catalog_view_tests)
    if(MSVC)
        target_compile_options(${catalog_target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${catalog_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
add_test(NAME scene_catalog_view_tests COMMAND scene_catalog_view_tests)

add_executable(input_controller_tests
    tests/InputControllerTests.cpp
    src/App/Input.cpp
    src/Core/Logger.cpp
    src/engine/input/InputController.cpp
    src/engine/input/WindowCoordinates.cpp
)
target_include_directories(input_controller_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(input_controller_tests PRIVATE glfw glm::glm)
target_compile_definitions(input_controller_tests PRIVATE
    GLFW_INCLUDE_NONE
    VOXEL_WITH_EDITOR=$<BOOL:${VOXEL_WITH_EDITOR}>
    VOXEL_WITH_RUNTIME_UI=$<BOOL:${VOXEL_WITH_RUNTIME_UI}>
)
if(MSVC)
    target_compile_options(input_controller_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(input_controller_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME input_controller_tests COMMAND input_controller_tests)

add_executable(editor_transform_foundation_tests
    tests/EditorTransformFoundationTests.cpp
)

target_link_libraries(editor_transform_foundation_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(editor_transform_foundation_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(editor_transform_foundation_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(editor_transform_foundation_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME editor_transform_foundation_tests
         COMMAND editor_transform_foundation_tests)

add_executable(editor_selection_picking_tests
    tests/EditorSelectionPickingTests.cpp
)

target_link_libraries(editor_selection_picking_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(editor_selection_picking_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(editor_selection_picking_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(editor_selection_picking_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME editor_selection_picking_tests
         COMMAND editor_selection_picking_tests)

add_executable(transform_gizmo_adapter_tests
    tests/TransformGizmoAdapterTests.cpp
)

target_link_libraries(transform_gizmo_adapter_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(transform_gizmo_adapter_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(transform_gizmo_adapter_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(transform_gizmo_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME transform_gizmo_adapter_tests
         COMMAND transform_gizmo_adapter_tests)

add_executable(viewport_tool_session_tests
    tests/ViewportToolSessionTests.cpp
)

target_link_libraries(viewport_tool_session_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(viewport_tool_session_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(viewport_tool_session_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(viewport_tool_session_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME viewport_tool_session_tests
         COMMAND viewport_tool_session_tests)

add_executable(editor_scene_hierarchy_tests
    tests/EditorSceneHierarchyTests.cpp
)

target_link_libraries(editor_scene_hierarchy_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(editor_scene_hierarchy_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(editor_scene_hierarchy_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(editor_scene_hierarchy_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME editor_scene_hierarchy_tests
         COMMAND editor_scene_hierarchy_tests)

add_executable(placeable_authoring_commands_tests
    tests/PlaceableAuthoringCommandsTests.cpp
)

target_link_libraries(placeable_authoring_commands_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(placeable_authoring_commands_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(placeable_authoring_commands_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(placeable_authoring_commands_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME placeable_authoring_commands_tests
         COMMAND placeable_authoring_commands_tests)

add_executable(placeable_authoring_integration_tests
    tests/PlaceableAuthoringIntegrationTests.cpp
)

target_link_libraries(placeable_authoring_integration_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(placeable_authoring_integration_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(placeable_authoring_integration_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(placeable_authoring_integration_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME placeable_authoring_integration_tests
         COMMAND placeable_authoring_integration_tests)

add_executable(viewport_placement_session_tests
    tests/ViewportPlacementSessionTests.cpp
)

target_link_libraries(viewport_placement_session_tests PRIVATE
    dda_voxel_editor_transform
    glm::glm
)

target_include_directories(viewport_placement_session_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(viewport_placement_session_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(viewport_placement_session_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME viewport_placement_session_tests
         COMMAND viewport_placement_session_tests)

add_executable(creature_care_state_tests
    tests/CreatureCareStateTests.cpp
)

target_link_libraries(creature_care_state_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(creature_care_state_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(creature_care_state_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(creature_care_state_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME creature_care_state_tests COMMAND creature_care_state_tests)

add_executable(creature_runtime_binding_tests
    tests/CreatureRuntimeBindingTests.cpp
)

target_link_libraries(creature_runtime_binding_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(creature_runtime_binding_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(creature_runtime_binding_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(creature_runtime_binding_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME creature_runtime_binding_tests COMMAND creature_runtime_binding_tests)

add_executable(species_registry_tests
    tests/SpeciesRegistryTests.cpp
)

target_link_libraries(species_registry_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(species_registry_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(species_registry_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(species_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME species_registry_tests COMMAND species_registry_tests)

add_executable(habitat_compatibility_tests
    tests/HabitatCompatibilityTests.cpp
)

target_link_libraries(habitat_compatibility_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(habitat_compatibility_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(habitat_compatibility_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(habitat_compatibility_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME habitat_compatibility_tests COMMAND habitat_compatibility_tests)

add_executable(cleanup_crew_tests
    tests/CleanupCrewTests.cpp
)

target_link_libraries(cleanup_crew_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(cleanup_crew_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(cleanup_crew_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(cleanup_crew_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME cleanup_crew_tests COMMAND cleanup_crew_tests)

add_executable(collection_codex_tests
    tests/CollectionCodexTests.cpp
)

target_link_libraries(collection_codex_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(collection_codex_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(collection_codex_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(collection_codex_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME collection_codex_tests COMMAND collection_codex_tests)

add_executable(creature_needs_simulation_tests
    tests/CreatureNeedsSimulationTests.cpp
)

target_link_libraries(creature_needs_simulation_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(creature_needs_simulation_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(creature_needs_simulation_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(creature_needs_simulation_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME creature_needs_simulation_tests COMMAND creature_needs_simulation_tests)

add_executable(offline_care_tests
    tests/OfflineCareTests.cpp
)

target_link_libraries(offline_care_tests PRIVATE
    dda_voxel_game_care
    glm::glm
)

target_include_directories(offline_care_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(offline_care_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(offline_care_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME offline_care_tests COMMAND offline_care_tests)

add_executable(scene_presentation_profile_tests
    tests/ScenePresentationProfileTests.cpp
    src/App/Input.cpp
    src/App/ScenePresentationProfileRuntime.cpp
    src/Core/Logger.cpp
    src/engine/game/FoliageArchetype.cpp
    src/engine/scene/EnvironmentWind.cpp
    src/engine/scene/WindborneParticleSettings.cpp
    src/engine/scene/NaturePondFoliageDistribution.cpp
    src/engine/scene/NaturePondGenerator.cpp
    src/engine/scene/ScenePresentationProfile.cpp
    src/engine/scene/ScenePresentationProfileVariants.cpp
    src/engine/scene/SoftVoxelPresentationProfile.cpp
    src/engine/render/HemisphereAmbient.cpp
    src/engine/render/PaintedClouds.cpp
    src/engine/render/PaintedSky.cpp
    src/engine/render/SceneAtmosphere.cpp
    src/engine/voxel/VoxelCellVariation.cpp
)

target_link_libraries(scene_presentation_profile_tests PRIVATE
    glfw
    glm::glm
)

target_include_directories(scene_presentation_profile_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(scene_presentation_profile_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(scene_presentation_profile_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME scene_presentation_profile_tests COMMAND scene_presentation_profile_tests)

add_executable(nature_pond_foliage_distribution_tests
    tests/NaturePondFoliageDistributionTests.cpp
    src/engine/scene/NaturePondFoliageDistribution.cpp
)

target_include_directories(nature_pond_foliage_distribution_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(nature_pond_foliage_distribution_tests PRIVATE
    glm::glm
)

if(MSVC)
    target_compile_options(nature_pond_foliage_distribution_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(nature_pond_foliage_distribution_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME nature_pond_foliage_distribution_tests
         COMMAND nature_pond_foliage_distribution_tests)

add_executable(sunroof_ground_foliage_tests
    tests/SunroofGroundFoliageTests.cpp
    src/engine/game/FoliageArchetype.cpp
    src/engine/game/FoliagePatch.cpp
    src/engine/scene/EnvironmentWind.cpp
    src/engine/scene/WindborneParticleSettings.cpp
    src/engine/render/FoliageVoxelGeometry.cpp
    src/engine/scene/SunroofGroundFoliage.cpp
    src/engine/scene/SunroofLivingWater.cpp
    src/engine/voxel/VoxelBuilder.cpp
)

target_include_directories(sunroof_ground_foliage_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(sunroof_ground_foliage_tests PRIVATE
    glm::glm
)

if(MSVC)
    target_compile_options(sunroof_ground_foliage_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(sunroof_ground_foliage_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME sunroof_ground_foliage_tests
         COMMAND sunroof_ground_foliage_tests)

add_executable(water_preset_tests
    tests/WaterPresetTests.cpp
)

target_include_directories(water_preset_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(water_preset_tests PRIVATE
    glm::glm
)

if(MSVC)
    target_compile_options(water_preset_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(water_preset_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME water_preset_tests COMMAND water_preset_tests)

add_executable(engine_core_tests
    tests/EngineCoreTests.cpp
    tests/RuntimeUiTestStubs.cpp
    src/Core/JobSystem.cpp
    src/Core/Logger.cpp
    src/engine/game/AxolotlBellyFloat.cpp
    src/engine/game/AxolotlCreaturePolish.cpp
    src/engine/game/AxolotlModel.cpp
    src/engine/game/FishCelebration.cpp
    src/engine/game/FoliageArchetype.cpp
    src/engine/game/FoliagePatch.cpp
    src/engine/game/GameRuntime.cpp
    src/engine/game/PlaceableRuntime.cpp
    src/engine/input/WindowCoordinates.cpp
    src/engine/scene/EnvironmentSettings.cpp
    src/engine/scene/EnvironmentTime.cpp
    src/engine/scene/EnvironmentWind.cpp
    src/engine/scene/WindborneParticleSettings.cpp
    src/engine/scene/WindborneParticleField.cpp
    src/engine/scene/SceneManager.cpp
    src/engine/scene/SceneObjectBuilder.cpp
    src/engine/render/FrameCharacterization.cpp
    src/engine/render/FoliagePresentation.cpp
    src/engine/render/FoliageVoxelGeometry.cpp
    src/engine/render/WindborneParticleVoxelGeometry.cpp
    src/engine/performance/AutomationProfileSession.cpp
    src/engine/render/Renderer.cpp
    src/engine/render/AuxiliaryRayResolution.cpp
    src/engine/render/RenderResolution.cpp
    src/engine/render/RenderQualityPreset.cpp
    src/engine/render/RenderPipelineShowcase.cpp
    src/engine/render/SunShadowSamplingPolicy.cpp
    src/engine/render/HemisphereAmbient.cpp
    src/engine/render/PaintedClouds.cpp
    src/engine/render/PaintedSky.cpp
    src/engine/render/SceneAtmosphere.cpp
    src/engine/render/gpu/DescriptorAllocator.cpp
    src/engine/render/gpu/DescriptorLayoutCache.cpp
    src/engine/render/gpu/SamplerCache.cpp
    src/engine/render/voxel/TerrainShadowColumnEligibility.cpp
    src/engine/render/voxel/VoxelAlignedLayerTraversal.cpp
    src/engine/render/voxel/VoxelLightingOcclusionPolicy.cpp
    src/engine/render/voxel/VoxelRasterFaceSelection.cpp
    src/engine/render/Commands.cpp
    src/engine/render/FrameSync.cpp
    src/engine/render/Frustum.cpp
    src/engine/render/Swapchain.cpp
    src/engine/render/VulkanContext.cpp
    src/engine/scene/CloudScene.cpp
    src/engine/game/FoliageCatalog.cpp
    src/engine/scene/SceneCatalog.cpp
    src/engine/scene/SceneConfig.cpp
    src/engine/scene/ScenePresentationProfile.cpp
    src/engine/scene/ScenePresentationProfileVariants.cpp
    src/engine/scene/SoftVoxelPresentationProfile.cpp
    src/engine/scene/SceneSerializer.cpp
    src/engine/scene/LightingParityProbe.cpp
    src/engine/scene/NaturePondGlassDome.cpp
    src/engine/scene/NaturePondFoliageDistribution.cpp
    src/engine/scene/NaturePondGenerator.cpp
    src/engine/scene/NaturePondSunroofProbe.cpp
    src/engine/scene/ProceduralWorldMaterials.cpp
    src/engine/scene/TankGlassBuilder.cpp
    src/Core/RuntimeContentManifest.cpp
    src/UI/Runtime/BuildPlacement.cpp
    src/UI/Runtime/CollectionCodexScreen.cpp
    src/UI/Runtime/UiActions.cpp
    src/UI/Runtime/UiDebugFont.cpp
    src/UI/Runtime/UiContext.cpp
    src/UI/Runtime/UiElement.cpp
    src/UI/Runtime/UiBinding.cpp
    src/UI/Runtime/UiScreens.cpp
    src/UI/Runtime/UiScreenControllers.cpp
    src/UI/Runtime/UiTheme.cpp
    src/UI/Runtime/UiTween.cpp
    src/UI/Runtime/UiWidgets.cpp
    src/engine/voxel/Chunk.cpp
    src/engine/voxel/ChunkGrid.cpp
    src/engine/voxel/GreedyMesher.cpp
    src/engine/voxel/Raycast.cpp
    src/engine/voxel/WorldGen.cpp
    src/engine/voxel/ModularGlassMesher.cpp
    src/engine/voxel/VoxelBuilder.cpp
    src/engine/voxel/VoxelCellVariation.cpp
    src/engine/voxel/VoxelSystem.cpp
)

target_link_libraries(engine_core_tests PRIVATE
    dda_voxel_editor_transform
    dda_voxel_game_care
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
)

target_include_directories(engine_core_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(engine_core_tests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(engine_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME engine_core_tests COMMAND engine_core_tests)

add_executable(care_loop_integration_tests
    tests/CareLoopIntegrationTests.cpp
    ${DDA_VOXEL_SCENE_PERSISTENCE_SUPPORT_SOURCES}
)

target_link_libraries(care_loop_integration_tests PRIVATE
    dda_voxel_editor_transform
    dda_voxel_game_care
    glm::glm
    nlohmann_json::nlohmann_json
)

target_include_directories(care_loop_integration_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(care_loop_integration_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(care_loop_integration_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME care_loop_integration_tests COMMAND care_loop_integration_tests)

add_executable(collection_loop_integration_tests
    tests/CollectionLoopIntegrationTests.cpp
    ${DDA_VOXEL_SCENE_PERSISTENCE_SUPPORT_SOURCES}
)

target_link_libraries(collection_loop_integration_tests PRIVATE
    dda_voxel_editor_transform
    dda_voxel_game_care
    glm::glm
    nlohmann_json::nlohmann_json
)

target_include_directories(collection_loop_integration_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(collection_loop_integration_tests PRIVATE
        /W4 /permissive- /EHsc)
else()
    target_compile_options(collection_loop_integration_tests PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_test(NAME collection_loop_integration_tests
         COMMAND collection_loop_integration_tests)

# source-file line limits. pure source-line check; no
# build dependency, so it runs in every ctest preset including the no-editor build.
add_test(
    NAME structural_app_size_ratchet
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/StructuralRatchet.cmake)

add_test(
    NAME structural_vma_ownership_ratchet
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/VmaOwnershipRatchet.cmake)

if(VOXEL_ENABLE_RUNTIME_AUTOMATION_TESTS)
    set(RUNTIME_AUTOMATION_SCRIPT "${CMAKE_SOURCE_DIR}/tests/RuntimeAutomation.cmake")
    set(RUNTIME_AUTOMATION_ASSET_ROOT "${CMAKE_SOURCE_DIR}/assets")
    set(RUNTIME_AUTOMATION_SCENES_ROOT "${CMAKE_SOURCE_DIR}/scenes")

    add_test(
        NAME runtime_smoke
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=smoke
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_smoke
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_missing_required_shader_fails
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=missing_required_shader
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_missing_required_shader
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_autodiscovery_smoke
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=autodiscovery_smoke
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_autodiscovery_smoke
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=obb_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=4
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_scaled_resolution_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=obb_probe
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,360
            -DRENDER_SCALE=0.75
            -DEXPECTED_RENDER_TARGET=960x540
            -DEXPECTED_EFFECTIVE_RENDER_SCALE=0.75
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_scaled_resolution_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_auxiliary_ray_scaled_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,430
            -DRENDER_SCALE=0.75
            -DSHADOW_RAY_SCALE=0.5
            -DAO_RAY_SCALE=0.75
            -DEXPECTED_RENDER_TARGET=960x540
            -DEXPECTED_EFFECTIVE_RENDER_SCALE=0.75
            -DEXPECTED_SHADOW_RAY_TARGET=480x270
            -DEXPECTED_EFFECTIVE_SHADOW_RAY_SCALE=0.5
            -DEXPECTED_AO_RAY_TARGET=720x405
            -DEXPECTED_EFFECTIVE_AO_RAY_SCALE=0.75
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=15
            -DPROFILE_SAMPLE_FRAMES=60
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=60
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_auxiliary_ray_scaled_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_aligned_primary_traversal_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,430
            -DRENDER_SCALE=0.75
            -DSHADOW_RAY_SCALE=0.5
            -DAO_RAY_SCALE=0.5
            -DFOLIAGE_RENDERER=voxel
            -DVERIFY_ALIGNED_PRIMARY_TRAVERSAL=ON
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=10
            -DPROFILE_SAMPLE_FRAMES=30
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=30
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_aligned_primary_traversal_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_instanced_foliage_static_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DFOLIAGE_RENDERER=instanced
            -DDISABLE_FOLIAGE_SWAY=ON
            -DVERIFY_INSTANCED_FOLIAGE=ON
            -DMIN_FOLIAGE_INSTANCES=3142
            -DMAX_FOLIAGE_INSTANCES=3142
            -DMIN_FOLIAGE_PATCHES=75
            -DMAX_FOLIAGE_PATCHES=75
            -DMIN_FOLIAGE_PRIMITIVES=47843
            -DMAX_FOLIAGE_PRIMITIVES=47843
            -DEXPECTED_WINDBORNE_PARTICLES=42
            -DEXPECTED_WINDBORNE_RESIDENT_PARTICLES=64
            -DVERIFY_FROZEN_FOLIAGE_MOTION=ON
            -DDISABLE_PROCEDURAL_FISH=ON
            -DEXPECTED_PROFILE_NAME=painted-outdoor-clouds-v1
            -DPRESERVE_TAA_JITTER=ON
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,430
            -DRENDER_SCALE=0.75
            -DSHADOW_RAY_SCALE=0.5
            -DAO_RAY_SCALE=0.5
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=10
            -DPROFILE_SAMPLE_FRAMES=30
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=30
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_instanced_foliage_static_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_instanced_foliage_motion_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DFOLIAGE_RENDERER=instanced
            -DVERIFY_INSTANCED_FOLIAGE=ON
            -DMIN_FOLIAGE_INSTANCES=3142
            -DMAX_FOLIAGE_INSTANCES=3142
            -DMIN_FOLIAGE_PATCHES=75
            -DMAX_FOLIAGE_PATCHES=75
            -DMIN_FOLIAGE_PRIMITIVES=47843
            -DMAX_FOLIAGE_PRIMITIVES=47843
            -DEXPECTED_WINDBORNE_PARTICLES=42
            -DEXPECTED_WINDBORNE_RESIDENT_PARTICLES=64
            -DVERIFY_LIVE_FOLIAGE_MOTION=ON
            -DPROFILE_LIVE_ANIMATION=ON
            -DDISABLE_PROCEDURAL_FISH=ON
            -DEXPECTED_PROFILE_NAME=painted-outdoor-clouds-v1
            -DPRESERVE_TAA_JITTER=ON
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,430
            -DRENDER_SCALE=0.75
            -DSHADOW_RAY_SCALE=0.5
            -DAO_RAY_SCALE=0.5
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=15
            -DPROFILE_SAMPLE_FRAMES=60
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=60
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_instanced_foliage_motion_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_instanced_foliage_raw_palette_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DFOLIAGE_RENDERER=instanced
            -DFOLIAGE_PALETTE=raw
            -DEXPECTED_FOLIAGE_PALETTE=raw
            -DDISABLE_FOLIAGE_SWAY=ON
            -DVERIFY_INSTANCED_FOLIAGE=ON
            -DMIN_FOLIAGE_INSTANCES=3142
            -DMAX_FOLIAGE_INSTANCES=3142
            -DMIN_FOLIAGE_PATCHES=75
            -DMAX_FOLIAGE_PATCHES=75
            -DMIN_FOLIAGE_PRIMITIVES=47843
            -DMAX_FOLIAGE_PRIMITIVES=47843
            -DEXPECTED_WINDBORNE_PARTICLES=42
            -DEXPECTED_WINDBORNE_RESIDENT_PARTICLES=64
            -DVERIFY_FROZEN_FOLIAGE_MOTION=ON
            -DDISABLE_PROCEDURAL_FISH=ON
            -DEXPECTED_PROFILE_NAME=painted-outdoor-clouds-v1
            -DPRESERVE_TAA_JITTER=ON
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,430
            -DRENDER_SCALE=0.75
            -DSHADOW_RAY_SCALE=0.5
            -DAO_RAY_SCALE=0.5
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=5
            -DPROFILE_SAMPLE_FRAMES=15
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=15
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_instanced_foliage_raw_palette_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_npot_dense_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=dense_npot_skip_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_npot_dense_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_pot_dense_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=dense_skip_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_pot_dense_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_irregular_dense_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=dense_irregular_skip_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_irregular_dense_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_voxel_import_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=voxel_import_probe
            -DPIXEL_INSPECT_POINT=640,500
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_voxel_import_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_aquarium_test_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=aquarium_test_probe
            -DPIXEL_INSPECT_POINT=640,360
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_aquarium_test_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_aquarium_sunroof_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=aquarium_sunroof_probe
            -DEXPECTED_PROFILE_NAME=painted-outdoor-v3
            -DPIXEL_INSPECT_POINT=640,360
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_aquarium_sunroof_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    # cross-composite atmosphere probes exercise the two glass implementations with
    # water-aware air-path attenuation enabled. the nature-pond enabled/disabled
    # pair below remains the compatibility/no-op coverage for the shared setting.
    add_test(
        NAME runtime_voxel_glass_atmosphere_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=glass_probe
            -DRECONSTRUCTION_MODE=taa
            -DSCENE_ATMOSPHERE_PRESET=outdoor-haze-v0
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=640,360
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=120
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_voxel_glass_atmosphere_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_mesh_glass_atmosphere_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=aquarium_test_probe
            -DRECONSTRUCTION_MODE=taa
            -DSCENE_ATMOSPHERE_PRESET=outdoor-haze-v0
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=640,360
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=120
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_mesh_glass_atmosphere_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    set_tests_properties(
        runtime_voxel_glass_atmosphere_probe
        runtime_mesh_glass_atmosphere_probe
        PROPERTIES
            RESOURCE_LOCK atmosphere_glass_runtime)

    add_test(
        NAME runtime_nature_pond_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DDISABLE_ALIGNED_PRIMARY_TRAVERSAL=ON
            -DEXPECTED_PROFILE_NAME=painted-outdoor-clouds-v1
            -DEXPECTED_PAINTED_SKY=1
            -DEXPECTED_PAINTED_CLOUDS=1
            -DEXPECTED_ENVIRONMENT_TIME=1
            -DEXPECTED_ENVIRONMENT_PHASE=DAY
            -DVERIFY_NATURE_POND_GLASS_DOME=ON
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=30
            -DPROFILE_SAMPLE_FRAMES=150
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=150
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_sunroof_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_sunroof_probe
            -DFOLIAGE_RENDERER=instanced
            -DVERIFY_INSTANCED_FOLIAGE=ON
            -DMIN_FOLIAGE_INSTANCES=3272
            -DMAX_FOLIAGE_INSTANCES=3272
            -DMIN_FOLIAGE_PATCHES=74
            -DMAX_FOLIAGE_PATCHES=74
            -DMIN_FOLIAGE_PRIMITIVES=49473
            -DMAX_FOLIAGE_PRIMITIVES=49473
            -DEXPECTED_WINDBORNE_PARTICLES=33
            -DEXPECTED_WINDBORNE_RESIDENT_PARTICLES=64
            -DDISABLE_ALIGNED_PRIMARY_TRAVERSAL=ON
            -DEXPECTED_PROFILE_NAME=painted-outdoor-clouds-v1
            -DEXPECTED_PAINTED_SKY=1
            -DEXPECTED_PAINTED_CLOUDS=1
            -DEXPECTED_ENVIRONMENT_TIME=1
            -DEXPECTED_ENVIRONMENT_PHASE=DAY
            -DEXPECTED_GLASS=1
            -DVERIFY_NATURE_POND_SUNROOF_GLASS=ON
            -DVERIFY_NATURE_POND_SUNROOF_TREE=ON
            -DPIXEL_INSPECT_POINT=960,540
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=30
            -DPROFILE_SAMPLE_FRAMES=150
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=150
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_sunroof_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_20cm_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_20cm_probe
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_20cm_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_density_dense_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_density_dense_probe
            -DEXPECTED_PROFILE_NAME=painted-outdoor-clouds-v1
            -DFOLIAGE_RENDERER=instanced
            -DVERIFY_INSTANCED_FOLIAGE=ON
            -DMIN_FOLIAGE_INSTANCES=6962
            -DMAX_FOLIAGE_INSTANCES=6962
            -DMIN_FOLIAGE_PATCHES=121
            -DMAX_FOLIAGE_PATCHES=121
            -DMIN_FOLIAGE_PRIMITIVES=108482
            -DMAX_FOLIAGE_PRIMITIVES=108482
            -DEXPECTED_WINDBORNE_PARTICLES=0
            -DEXPECTED_WINDBORNE_RESIDENT_PARTICLES=64
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=30
            -DPROFILE_SAMPLE_FRAMES=150
            -DWAIT_FOR_CLOUD_COMMIT=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=150
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_density_dense_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_25cm_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_25cm_probe
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_25cm_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_bank_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_bank_probe
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_bank_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_20cm_bank_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_20cm_bank_probe
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_20cm_bank_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_25cm_bank_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_25cm_bank_probe
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_25cm_bank_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_soft_taa_reconstruction_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DRECONSTRUCTION_MODE=taa
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_soft_taa_reconstruction_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_cell_variation_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DDDA_ADVANCED_DEBUG=15
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_cell_variation_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_hemisphere_ambient_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DLIGHTING_DEBUG_MODE=15
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,645
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_hemisphere_ambient_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_atmosphere_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DSCENE_ATMOSPHERE_PRESET=outdoor-haze-v0
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,540
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_atmosphere_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_atmosphere_disabled_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DSCENE_ATMOSPHERE_PRESET=disabled
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,540
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_atmosphere_disabled_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_star_atmosphere_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_star_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DSCENE_ATMOSPHERE_PRESET=outdoor-haze-v0
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,700
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_star_atmosphere_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_star_atmosphere_disabled_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_star_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DSCENE_ATMOSPHERE_PRESET=disabled
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,700
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_star_atmosphere_disabled_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_foliage_occlusion_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DLIGHTING_DEBUG_MODE=4
            -DVERIFY_FOLIAGE_OCCLUSION_POLICY=ON
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=600,480
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_foliage_occlusion_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_nature_pond_lighting_parity_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=nature_pond_lighting_probe
            -DRECONSTRUCTION_MODE=taa
            -DVOXEL_CELL_VARIATION_PRESET=soft-v0
            -DHEMISPHERE_AMBIENT_PRESET=warm-cool-v0
            -DVERIFY_LIGHTING_PARITY_PROBE=ON
            -DPRESERVE_TAA_JITTER=ON
            -DPIXEL_INSPECT_POINT=960,540
            -DAUTOMATION_WINDOW_SIZE=1920x1080
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=180
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_nature_pond_lighting_parity_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    set_tests_properties(
        runtime_instanced_foliage_static_probe
        runtime_instanced_foliage_motion_probe
        runtime_instanced_foliage_raw_palette_probe
        runtime_nature_pond_render_probe
        runtime_nature_pond_sunroof_render_probe
        runtime_nature_pond_density_dense_render_probe
        runtime_nature_pond_20cm_render_probe
        runtime_nature_pond_25cm_render_probe
        runtime_nature_pond_bank_render_probe
        runtime_nature_pond_20cm_bank_render_probe
        runtime_nature_pond_25cm_bank_render_probe
        runtime_nature_pond_soft_taa_reconstruction_probe
        runtime_nature_pond_cell_variation_probe
        runtime_nature_pond_hemisphere_ambient_probe
        runtime_nature_pond_atmosphere_probe
        runtime_nature_pond_atmosphere_disabled_probe
        runtime_nature_pond_star_atmosphere_probe
        runtime_nature_pond_star_atmosphere_disabled_probe
        runtime_nature_pond_foliage_occlusion_probe
        runtime_nature_pond_lighting_parity_probe
        PROPERTIES
            RESOURCE_LOCK nature_pond_gpu_profile)

    add_test(
        NAME runtime_procedural_world_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=procedural_world
            -DEXPECTED_PROFILE_NAME=painted-outdoor-sky-v1
            -DPIXEL_INSPECT_POINT=640,430
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_procedural_world_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_beach_sand_render_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=beach_sand_palettes
            -DEXPECTED_PROFILE_NAME=painted-outdoor-sky-v1
            -DPIXEL_INSPECT_POINT=640,450
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_beach_sand_render_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_aquarium_test_perf_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=aquarium_test_perf_probe
            -DPIXEL_INSPECT_POINT=640,360
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_aquarium_test_perf_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_fishbowl_perf_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_probe
            -DSCENE_NAME=fishbowl_perf_probe
            -DEXPECTED_PROFILE_NAME=painted-outdoor-v3
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DPIXEL_INSPECT_POINT=640,360
            -DRENDER_QUALITY_PRESET=performance
            -DEXPECTED_RENDER_TARGET=768x432
            -DEXPECTED_EFFECTIVE_RENDER_SCALE=0.6
            -DEXPECTED_SHADOW_RAY_TARGET=384x216
            -DEXPECTED_EFFECTIVE_SHADOW_RAY_SCALE=0.5
            -DEXPECTED_AO_RAY_TARGET=384x216
            -DEXPECTED_EFFECTIVE_AO_RAY_SCALE=0.5
            -DEXPECTED_AO_VOLUME_COUNT=2
            -DENABLE_GPU_PROFILE_SUMMARY=ON
            -DPROFILE_WAIT_FOR_SCENE_READY=ON
            -DPROFILE_WARMUP_FRAMES=2
            -DPROFILE_SAMPLE_FRAMES=4
            -DUSE_AUTO_EXIT_FRAMES=ON
            -DAUTO_EXIT_FRAMES=4
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_fishbowl_perf_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    function(add_temporal_sun_shadow_probe test_name sampling_mode effective_samples)
        set(sampling_argument)
        set(expected_sampling_mode ${sampling_mode})
        if(sampling_mode STREQUAL "default")
            set(expected_sampling_mode full)
        else()
            list(APPEND sampling_argument
                -DSUN_SHADOW_SAMPLING=${sampling_mode})
        endif()
        add_test(
            NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=render_probe
                -DSCENE_NAME=fishbowl_perf_probe
                -DEXPECTED_PROFILE_NAME=painted-outdoor-v3
                -DAUTOMATION_WINDOW_SIZE=1280x720
                -DPIXEL_INSPECT_POINT=640,360
                -DRENDER_QUALITY_PRESET=native
                -DEXPECTED_RENDER_TARGET=1280x720
                -DEXPECTED_EFFECTIVE_RENDER_SCALE=1
                -DEXPECTED_SHADOW_RAY_TARGET=1280x720
                -DEXPECTED_EFFECTIVE_SHADOW_RAY_SCALE=1
                -DEXPECTED_AO_RAY_TARGET=1280x720
                -DEXPECTED_EFFECTIVE_AO_RAY_SCALE=1
                ${sampling_argument}
                -DEXPECTED_SUN_SHADOW_SAMPLING=${expected_sampling_mode}
                -DEXPECTED_SUN_SHADOW_SAMPLES_AUTHORED=4
                -DEXPECTED_SUN_SHADOW_SAMPLES_EFFECTIVE=${effective_samples}
                -DFIXED_SCENE_TIME_SECONDS=1.0
                -DEXPECTED_SCENE_TIME_SECONDS=1
                -DENABLE_GPU_PROFILE_SUMMARY=ON
                -DPROFILE_WAIT_FOR_SCENE_READY=ON
                -DPRESERVE_TAA_JITTER=ON
                -DPROFILE_WARMUP_FRAMES=2
                -DPROFILE_SAMPLE_FRAMES=4
                -DUSE_AUTO_EXIT_FRAMES=ON
                -DAUTO_EXIT_FRAMES=4
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/${test_name}
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DAUTO_EXIT_MS=1500
                -P ${RUNTIME_AUTOMATION_SCRIPT})
    endfunction()

    add_temporal_sun_shadow_probe(
        runtime_fishbowl_sun_shadow_full_probe full 4)
    add_temporal_sun_shadow_probe(
        runtime_fishbowl_sun_shadow_temporal_two_probe temporal-2 2)
    add_temporal_sun_shadow_probe(
        runtime_fishbowl_sun_shadow_default_probe default 4)
    set_tests_properties(
        runtime_fishbowl_sun_shadow_full_probe
        runtime_fishbowl_sun_shadow_temporal_two_probe
        runtime_fishbowl_sun_shadow_default_probe
        PROPERTIES RESOURCE_LOCK nature_pond_gpu_profile)

    function(add_half_resolution_sun_shadow_probe test_name sampling_mode effective_samples)
        add_test(
            NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=render_probe
                -DSCENE_NAME=fishbowl_perf_probe
                -DEXPECTED_PROFILE_NAME=painted-outdoor-v3
                -DAUTOMATION_WINDOW_SIZE=1280x720
                -DPIXEL_INSPECT_POINT=640,360
                -DRENDER_QUALITY_PRESET=performance
                -DEXPECTED_RENDER_TARGET=768x432
                -DEXPECTED_EFFECTIVE_RENDER_SCALE=0.6
                -DEXPECTED_SHADOW_RAY_TARGET=384x216
                -DEXPECTED_EFFECTIVE_SHADOW_RAY_SCALE=0.5
                -DEXPECTED_AO_RAY_TARGET=384x216
                -DEXPECTED_EFFECTIVE_AO_RAY_SCALE=0.5
                -DSUN_SHADOW_SAMPLING=${sampling_mode}
                -DEXPECTED_SUN_SHADOW_SAMPLING=${sampling_mode}
                -DEXPECTED_SUN_SHADOW_SAMPLES_AUTHORED=4
                -DEXPECTED_SUN_SHADOW_SAMPLES_EFFECTIVE=${effective_samples}
                -DFIXED_SCENE_TIME_SECONDS=1.0
                -DEXPECTED_SCENE_TIME_SECONDS=1
                -DENABLE_GPU_PROFILE_SUMMARY=ON
                -DPROFILE_WAIT_FOR_SCENE_READY=ON
                -DPRESERVE_TAA_JITTER=ON
                -DPROFILE_WARMUP_FRAMES=2
                -DPROFILE_SAMPLE_FRAMES=4
                -DUSE_AUTO_EXIT_FRAMES=ON
                -DAUTO_EXIT_FRAMES=4
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/${test_name}
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DAUTO_EXIT_MS=1500
                -P ${RUNTIME_AUTOMATION_SCRIPT})
    endfunction()

    add_half_resolution_sun_shadow_probe(
        runtime_fishbowl_sun_shadow_half_compensated_probe temporal-2 4)
    add_half_resolution_sun_shadow_probe(
        runtime_fishbowl_sun_shadow_half_uncompensated_probe
        temporal-2-uncompensated 2)
    set_tests_properties(
        runtime_fishbowl_sun_shadow_half_compensated_probe
        runtime_fishbowl_sun_shadow_half_uncompensated_probe
        PROPERTIES RESOURCE_LOCK nature_pond_gpu_profile)

    add_test(
        NAME runtime_fish_focus_smoke
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=fish_focus_smoke
            -DSCENE_NAME=fishbowl_perf_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_fish_focus_smoke
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=10000
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_render_debug_probe
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=render_debug_probe
            -DSCENE_NAME=obb_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_render_debug_probe
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_characterization_pass_order
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=characterization_pass_order
            -DSCENE_NAME=obb_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_characterization_pass_order
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_dda_metrics_characterization
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=dda_metrics_characterization
            -DSCENE_NAME=obb_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_dda_metrics_characterization
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_dense_hierarchy_characterization
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=dda_hierarchy_characterization
            -DSCENE_NAME=dense_npot_skip_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_dense_hierarchy_characterization
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_single_probe_pond_characterization
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=dda_hierarchy_characterization
            -DSCENE_NAME=nature_pond_bank_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_single_probe_pond_characterization
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTOMATION_WINDOW_SIZE=1280x720
            -DFREEZE_DEBUG=ON
            -DDISABLE_PROCEDURAL_FISH=ON
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_pass_lifecycle_characterization
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=pass_lifecycle_characterization
            -DSCENE_NAME=obb_probe
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_pass_lifecycle_characterization
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_deferred_cloud_commit
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=deferred_cloud_commit
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_deferred_cloud_commit
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=100
            -DSCENE_NAME=obb_test
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    if(VOXEL_WITH_RUNTIME_UI)
        add_test(
            NAME runtime_ui_overlay_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_overlay_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_overlay_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=2
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_resize_smoke_1280x720
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_resize_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_resize_smoke_1280x720
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTOMATION_WINDOW_SIZE=1280x720
                -DAUTO_EXIT_FRAMES=2
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_resize_smoke_640x360
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_resize_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_resize_smoke_640x360
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTOMATION_WINDOW_SIZE=640x360
                -DAUTO_EXIT_FRAMES=2
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_fish_list_focus_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_fish_list_focus_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_fish_list_focus_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=9
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_pause_stack_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_pause_stack_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_pause_stack_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=7
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_pause_options_menu_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_pause_options_menu_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_pause_options_menu_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=14
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_game_mode_default_menu_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_game_mode_default_menu_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_game_mode_default_menu_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=6
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_main_menu_start_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_main_menu_start_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_main_menu_start_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=8
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_main_menu_options_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_main_menu_options_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_main_menu_options_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_main_menu_options_labels_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_main_menu_options_labels_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_main_menu_options_labels_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=20
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_main_menu_escape_ignored_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_main_menu_escape_ignored_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_main_menu_escape_ignored_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=6
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_click_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_click_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_click_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=4
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_water_maintain_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_water_maintain_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_water_maintain_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=4
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_catalog_item_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_catalog_item_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_catalog_item_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=7
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_catalog_scroll_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_catalog_scroll_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_catalog_scroll_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=11
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_catalog_close_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_catalog_close_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_catalog_close_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=11
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_pending_cancel_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_pending_cancel_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_pending_cancel_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_pending_escape_cancel_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_pending_escape_cancel_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_pending_escape_cancel_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=8
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_pending_right_mouse_ignored_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_pending_right_mouse_ignored_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_pending_right_mouse_ignored_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=14
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_pending_confirm_invalid_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_pending_confirm_invalid_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_pending_confirm_invalid_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_snapped_candidate_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_snapped_candidate_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_snapped_candidate_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=7
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_manual_placement_qa_target_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_manual_placement_qa_target_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_manual_placement_qa_target_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_placeable_prototype_ghost_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_placeable_prototype_ghost_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_placeable_prototype_ghost_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=8
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_placeable_prototype_commit_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_placeable_prototype_commit_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_placeable_prototype_commit_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=12
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_placeable_prototype_remove_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_placeable_prototype_remove_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_placeable_prototype_remove_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=20
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_snapped_commit_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_snapped_commit_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_snapped_commit_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_multi_footprint_commit_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_multi_footprint_commit_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_multi_footprint_commit_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_multi_footprint_blocked_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_multi_footprint_blocked_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_multi_footprint_blocked_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=11
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_rotated_footprint_commit_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_rotated_footprint_commit_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_rotated_footprint_commit_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=11
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_rotated_footprint_blocked_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_rotated_footprint_blocked_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_rotated_footprint_blocked_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=12
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_rotation_idle_ignored_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_rotation_idle_ignored_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_rotation_idle_ignored_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=2
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        add_test(
            NAME runtime_ui_remove_commit_smoke
            COMMAND ${CMAKE_COMMAND}
                -DTEST_CASE=runtime_ui_remove_commit_smoke
                -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_remove_commit_smoke
                -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                -DSCENE_NAME=fishbowl_perf_probe
                -DAUTO_EXIT_FRAMES=10
                -P ${RUNTIME_AUTOMATION_SCRIPT})

        if(VOXEL_WITH_EDITOR)
            add_test(
                NAME runtime_ui_play_preview_smoke
                COMMAND ${CMAKE_COMMAND}
                    -DTEST_CASE=runtime_ui_play_preview_smoke
                    -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                    -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                    -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_play_preview_smoke
                    -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                    -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                    -DSCENE_NAME=fishbowl_perf_probe
                    -DAUTO_EXIT_FRAMES=3
                    -P ${RUNTIME_AUTOMATION_SCRIPT})

            add_test(
                NAME runtime_ui_play_preview_main_menu_smoke
                COMMAND ${CMAKE_COMMAND}
                    -DTEST_CASE=runtime_ui_play_preview_main_menu_smoke
                    -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
                    -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
                    -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_ui_play_preview_main_menu_smoke
                    -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
                    -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
                    -DSCENE_NAME=fishbowl_perf_probe
                    -DAUTO_EXIT_FRAMES=3
                    -P ${RUNTIME_AUTOMATION_SCRIPT})
        endif()
    endif()

    add_test(
        NAME runtime_missing_optional_shader_warns
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=missing_optional_shader
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_missing_optional_shader
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_missing_foliage_shader_falls_back
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=missing_foliage_shader_fallback
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_missing_foliage_shader_fallback
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_FRAMES=30
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_missing_required_asset_fails
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=missing_required_asset
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_missing_required_asset
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    add_test(
        NAME runtime_corrupt_required_pass_shader_fails
        COMMAND ${CMAKE_COMMAND}
            -DTEST_CASE=corrupt_required_pass_shader
            -DEXECUTABLE_PATH=$<TARGET_FILE:voxel_aquarium>
            -DBUILD_DIR=$<TARGET_FILE_DIR:voxel_aquarium>
            -DRUN_DIR=${CMAKE_BINARY_DIR}/test-runtime/runtime_corrupt_required_pass_shader
            -DASSET_ROOT=${RUNTIME_AUTOMATION_ASSET_ROOT}
            -DSCENES_ROOT=${RUNTIME_AUTOMATION_SCENES_ROOT}
            -DAUTO_EXIT_MS=1500
            -P ${RUNTIME_AUTOMATION_SCRIPT})

    set_tests_properties(
        runtime_smoke
        runtime_autodiscovery_smoke
        runtime_render_probe
        runtime_scaled_resolution_render_probe
        runtime_auxiliary_ray_scaled_render_probe
        runtime_aligned_primary_traversal_probe
        runtime_instanced_foliage_static_probe
        runtime_instanced_foliage_motion_probe
        runtime_instanced_foliage_raw_palette_probe
        runtime_npot_dense_render_probe
        runtime_pot_dense_render_probe
        runtime_irregular_dense_render_probe
        runtime_voxel_import_render_probe
        runtime_aquarium_test_render_probe
        runtime_aquarium_sunroof_render_probe
        runtime_nature_pond_render_probe
        runtime_nature_pond_sunroof_render_probe
        runtime_nature_pond_density_dense_render_probe
        runtime_nature_pond_20cm_render_probe
        runtime_nature_pond_25cm_render_probe
        runtime_nature_pond_bank_render_probe
        runtime_nature_pond_20cm_bank_render_probe
        runtime_nature_pond_25cm_bank_render_probe
        runtime_nature_pond_soft_taa_reconstruction_probe
        runtime_nature_pond_cell_variation_probe
        runtime_nature_pond_hemisphere_ambient_probe
        runtime_nature_pond_foliage_occlusion_probe
        runtime_nature_pond_lighting_parity_probe
        runtime_procedural_world_render_probe
        runtime_beach_sand_render_probe
        runtime_aquarium_test_perf_probe
        runtime_fishbowl_perf_probe
        runtime_fishbowl_sun_shadow_full_probe
        runtime_fishbowl_sun_shadow_temporal_two_probe
        runtime_fishbowl_sun_shadow_default_probe
        runtime_render_debug_probe
        runtime_characterization_pass_order
        runtime_dda_metrics_characterization
        runtime_dense_hierarchy_characterization
        runtime_single_probe_pond_characterization
        runtime_pass_lifecycle_characterization
        runtime_deferred_cloud_commit
        runtime_missing_required_shader_fails
        runtime_missing_optional_shader_warns
        runtime_missing_foliage_shader_falls_back
        runtime_missing_required_asset_fails
        runtime_corrupt_required_pass_shader_fails
        PROPERTIES
            TIMEOUT 45
            LABELS "runtime;integration")

    if(VOXEL_WITH_RUNTIME_UI)
        set(RUNTIME_UI_TESTS
            runtime_ui_overlay_smoke
            runtime_ui_resize_smoke_1280x720
            runtime_ui_resize_smoke_640x360
            runtime_ui_fish_list_focus_smoke
            runtime_ui_pause_stack_smoke
            runtime_ui_game_mode_default_menu_smoke
            runtime_ui_pause_options_menu_smoke
            runtime_ui_main_menu_start_smoke
            runtime_ui_main_menu_options_smoke
            runtime_ui_main_menu_options_labels_smoke
            runtime_ui_main_menu_escape_ignored_smoke
            runtime_ui_click_smoke
            runtime_ui_water_maintain_smoke
            runtime_ui_catalog_item_smoke
            runtime_ui_catalog_scroll_smoke
            runtime_ui_catalog_close_smoke
            runtime_ui_pending_cancel_smoke
            runtime_ui_pending_escape_cancel_smoke
            runtime_ui_pending_right_mouse_ignored_smoke
            runtime_ui_pending_confirm_invalid_smoke
            runtime_ui_snapped_candidate_smoke
            runtime_ui_manual_placement_qa_target_smoke
            runtime_ui_placeable_prototype_ghost_smoke
            runtime_ui_placeable_prototype_commit_smoke
            runtime_ui_placeable_prototype_remove_smoke
            runtime_ui_snapped_commit_smoke
            runtime_ui_multi_footprint_commit_smoke
            runtime_ui_multi_footprint_blocked_smoke
            runtime_ui_rotated_footprint_commit_smoke
            runtime_ui_rotated_footprint_blocked_smoke
            runtime_ui_rotation_idle_ignored_smoke
            runtime_ui_remove_commit_smoke)
        if(VOXEL_WITH_EDITOR)
            list(APPEND RUNTIME_UI_TESTS runtime_ui_play_preview_smoke)
            list(APPEND RUNTIME_UI_TESTS runtime_ui_play_preview_main_menu_smoke)
        endif()

        set_tests_properties(
            ${RUNTIME_UI_TESTS}
            PROPERTIES
                TIMEOUT 45
                LABELS "runtime;integration;ui")
    endif()

    # keep headless test selection safe as runtime coverage grows. individual
    # runtime tests may carry additional labels and properties above, but every
    # test that can launch the application must always be excluded by -LE runtime.
    get_property(VOXEL_DIRECTORY_TESTS DIRECTORY PROPERTY TESTS)
    foreach(VOXEL_TEST_NAME IN LISTS VOXEL_DIRECTORY_TESTS)
        if(VOXEL_TEST_NAME MATCHES "^runtime_")
            get_property(VOXEL_TEST_LABELS
                TEST "${VOXEL_TEST_NAME}" PROPERTY LABELS)
            list(FIND VOXEL_TEST_LABELS "runtime" VOXEL_RUNTIME_LABEL_INDEX)
            if(VOXEL_RUNTIME_LABEL_INDEX EQUAL -1)
                set_property(TEST "${VOXEL_TEST_NAME}"
                    APPEND PROPERTY LABELS runtime)
            endif()
        endif()
    endforeach()
endif()
