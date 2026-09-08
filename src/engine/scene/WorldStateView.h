#pragma once

#include <cstdint>

#include "engine/scene/SceneConfig.h"
#include "engine/render/RenderSettings.h"

namespace engine::scene
{

// the applied SceneConfig and the per-domain render
// settings buckets are shared *global read-state*, not any one subsystem's
// property. this view is the read-only packaging of that state: SceneManager
// owns the SceneConfig lifecycle (mutations go through its single
// sceneConfigMutable() door), app owns the settings buckets' lifetime, and
// consumers read both through this view instead of reaching into app members.
//
// the per-frame (FrameState) half - camera matrices, jitter, frame index -
// is carried into pass record() by FrameContext, alongside this shared state.
// the renderer owns the frame loop.
struct WorldStateView
{
    const SceneConfig& sceneConfig;
    const engine::render::LightingSettings& lighting;
    const engine::render::ShadowSettings& shadows;
    const engine::render::AmbientOcclusionSettings& ambientOcclusion;
    const engine::render::PostFxSettings& postFx;
    const engine::render::WaterSettings& water;
    const engine::render::GlassSettings& glass;
    const engine::render::VoxelDebugSettings& voxelDebug;
    const engine::render::DiagnosticsSettings& diagnostics;
    const engine::render::FramePacingSettings& framePacing;
    // SceneManager-owned refresh identity for derived editor catalog views.
    uint64_t sceneCatalogRevision = 0;
};

} // namespace engine::scene
