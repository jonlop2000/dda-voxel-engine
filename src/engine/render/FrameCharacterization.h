#pragma once

#include <array>
#include <string_view>
#include <vector>

namespace engine::render
{

inline constexpr std::array<std::string_view, 31> kPassRecordOrder = {
    "shadow-map",
    "gbuffer",
    "voxel-gbuffer",
    "water-volume-prepass",
    "shadow-tile-list",
    "shadow-rays",
    "shadow-resolve",
    "shadow-denoise",
    "local-shadow-tile-list",
    "local-light-shadows",
    "local-shadow-blur",
    "local-shadow-resolve",
    "ao-tile-list",
    "ao-rays",
    "ao-resolve",
    "lighting",
    "voxel-glass-refract",
    "Water Body Debug",
    "reflection-gbuffer",
    "reflection-voxel-gbuffer",
    "reflection-lighting",
    "water-v2",
    "water",
    "glass-source-copy",
    "glass-back-depth",
    "glass-shade",
    "Water Body Final",
    "bloom",
    "taa",
    "spatial-upscale",
    "composite-ui",
};

inline constexpr std::array<std::string_view, 10> kFrameHandshakeOrder = {
    "pre-frame-sync",
    "pre-acquire-update",
    "beginFrame",
    "applyPendingVoxelMeshes",
    "cameraMatricesAndJitter",
    "visibilityAndSceneRebuilds",
    "updateLights",
    "recordPasses",
    "endFrame",
    "post-frame-accounting",
};

inline constexpr std::array<std::string_view, 16> kRendererOwnedFrameLoopTrace = {
    "drawFrame.begin",
    "updateFrame.begin",
    "updateFrame.end",
    "recordFrame.begin",
    "renderer.beginFrame",
    "applyPendingVoxelMeshes",
    "cameraMatricesAndJitter",
    "visibilityAndSceneRebuilds",
    "updateLights",
    "recordPasses",
    "finishFrameTail.begin",
    "renderer.endFrame",
    "postFrameAccounting",
    "renderer.advanceFrame",
    "recordFrame.end",
    "drawFrame.end",
};

void beginRuntimeFrameLoopCharacterization();
void traceRuntimeFrameLoopStep(std::string_view step);
void finishRuntimeFrameCharacterization();
void beginRuntimeFrameCharacterization();
void traceRuntimeFrameCharacterizationStep(std::string_view step);
void recordRuntimeFrameCharacterizationPasses(const std::vector<std::string_view>& passNames);
void endRuntimeFrameCharacterization();
void beginRuntimePassLifecycleCharacterization(std::string_view phase);
void traceRuntimePassLifecycleEvent(std::string_view passName);
void endRuntimePassLifecycleCharacterization();

}  // namespace engine::render
