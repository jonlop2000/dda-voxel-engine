#include "App/App.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include <GLFW/glfw3.h>

#include "Core/Logger.h"
#include "engine/render/FrameCharacterization.h"
#include "engine/render/voxel/VoxelRenderResources.h"
#include "engine/voxel/VoxelSystem.h"

#if VOXEL_WITH_RUNTIME_UI
bool App::resetFishFocusForSessionTransition()
{
    if (fishFocus_.fishId == 0)
    {
        return false;
    }

    camera_.position = fishFocus_.savedPosition;
    camera_.yaw = fishFocus_.savedYaw;
    camera_.pitch = fishFocus_.savedPitch;
    fishFocus_ = FishFocusState{};
    fishFocusCurrentDistance_ = 0.0f;
    return true;
}
#endif

bool App::bindRendererFrameLifecycle()
{
    if (renderer_.isFrameLifecycleBound())
    {
        return true;
    }

    return renderer_.bindFrameLifecycle(engine::render::Renderer::FrameLifecycleBindings{
        &ctx_,
        &framebufferResized_,
        &swapchainRecreateRequested_,
        [](void* app) { static_cast<App*>(app)->recreateSwapchainResources(); },
        this,
    });
}

void App::finishFrameRecording(VkCommandBuffer cmd, uint32_t gpuScopeFrame)
{
    engine::render::traceRuntimeFrameLoopStep("finishFrameTail.begin");
    gpuProfiler_.endScope(cmd, gpuScopeFrame);
}

void App::finishFrameAccounting(const FrameShellInputs& shell,
                                const FrameCompletionState& completion)
{
    engine::render::traceRuntimeFrameLoopStep("postFrameAccounting");

    prevViewProjUnjittered_ = completion.viewProjUnjittered;
    prevViewProjJittered_ = completion.viewProjJittered;
    renderPasses().foliage.completeFrame();
    prevJitter_ = currentJitter_;
    renderedFrameCount_ += 1;
    if ((automationAutoExitMs_ > 0 || automationAutoExitFrames_ > 0) && renderedFrameCount_ == 1)
    {
        logInfo("Automation", "First frame rendered.");
    }
    taaFrameIndex_++;
    voxelMetricsFrameIndex_++;

    effectiveFpsLimit_ = framePacingSettings_.maxFpsLimit_;
    focusedIdleThrottleActive_ = false;
    backgroundThrottleActive_ = false;
    const bool showcaseActive = renderPipelineShowcase_.enabled();
    if (!showcaseActive && framePacingSettings_.backgroundThrottleEnabled_ && !windowFocused_ &&
        framePacingSettings_.backgroundFpsLimit_ > 0.0f)
    {
        effectiveFpsLimit_ =
            effectiveFpsLimit_ > 0.0f
                ? std::min(effectiveFpsLimit_, framePacingSettings_.backgroundFpsLimit_)
                : framePacingSettings_.backgroundFpsLimit_;
        backgroundThrottleActive_ = true;
    }
    else if (!showcaseActive && framePacingSettings_.focusedIdleThrottleEnabled_ && windowFocused_ &&
             !input_.mouseLookActive() && framePacingSettings_.focusedIdleFpsLimit_ > 0.0f &&
             (shell.realNow - lastInteractionTime_) >=
                 static_cast<double>(framePacingSettings_.focusedIdleDelaySeconds_))
    {
        effectiveFpsLimit_ =
            effectiveFpsLimit_ > 0.0f
                ? std::min(effectiveFpsLimit_, framePacingSettings_.focusedIdleFpsLimit_)
                : framePacingSettings_.focusedIdleFpsLimit_;
        focusedIdleThrottleActive_ = true;
    }

    if (effectiveFpsLimit_ > 0.0f)
    {
        const double minFrameMs = 1000.0 / static_cast<double>(effectiveFpsLimit_);
        const auto capNow = std::chrono::high_resolution_clock::now();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(capNow - shell.frameStart).count();
        if (elapsedMs < minFrameMs)
        {
            const double sleepMs = minFrameMs - elapsedMs;
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleepMs));
        }
    }

    const auto frameEnd = std::chrono::high_resolution_clock::now();
    const double frameMs =
        std::chrono::duration<double, std::milli>(frameEnd - shell.frameStart).count();
    lastCpuFrameMs_ = static_cast<float>(frameMs);
    pendingCpuFrameMs_ = lastCpuFrameMs_;
    pendingProfilerSample_ = true;

    static double frameMsAccum = 0.0;
    static int frameCount = 0;
    static auto lastTitleUpdate = frameEnd;

    frameMsAccum += frameMs;
    frameCount += 1;

    if (std::chrono::duration<double>(frameEnd - lastTitleUpdate).count() >= 0.5)
    {
        const double avgMs = frameMsAccum / std::max(1, frameCount);
        const double fps = 1000.0 / std::max(0.0001, avgMs);
        const engine::render::VoxelRenderResources::MeshStats voxelMeshStats =
            voxelRenderResources_.meshStats();
        const size_t voxelVerts = voxelMeshStats.vertices;
        const size_t voxelIndices = voxelMeshStats.indices;
        const size_t voxelMeshedChunks = voxelMeshStats.meshedChunks;
        const engine::voxel::ChunkGridSummary voxelGridSummary =
            engine::voxel::summarizeChunkGrid(voxelGrid_);
        const size_t voxelChunkCount = voxelGridSummary.chunkCount;
        const size_t vK = voxelVerts / 1000;
        const size_t iK = voxelIndices / 1000;
        const size_t jobQueue = jobSystem_.pendingCount();
        const size_t uploadQueue = meshUploadQueue_.pendingCount();
        const uint32_t rebuilds = voxelRebuildsSinceTitle_;
        const size_t dirtyChunks = voxelGridSummary.dirtyChunks;

        char title[256];
        if (input_.waterEnabled())
        {
            std::snprintf(title, sizeof(title),
                          "%s - %.2f ms (%.0f FPS) | Vox %zu/%zu V:%zuk I:%zuk "
                          "J:%zu U:%zu R:%u T:%u F:%u D:%zu | W L%.1f R%.3f "
                          "DS%.2f SP%.0f SI%.2f RF%.2f FR%.2f WS%.2f WA%.2f CI%.2f",
                          kWindowTitle, avgMs, fps, voxelMeshedChunks, voxelChunkCount, vK, iK,
                          jobQueue, uploadQueue, rebuilds, worldTrees_, worldFoliage_, dirtyChunks,
                          waterSettings_.waterLevel_, waterSettings_.waterRefract_,
                          waterSettings_.waterDistortionDepthScale_,
                          waterSettings_.waterSpecPower_, waterSettings_.waterSpecIntensity_,
                          waterSettings_.waterReflectionStrength_, waterSettings_.waterFresnelBias_,
                          waterSettings_.waterWaveScale_, waterSettings_.waterWaveAmp_,
                          waterSettings_.waterCausticsIntensity_);
        }
        else
        {
            std::snprintf(title, sizeof(title),
                          "%s - %.2f ms (%.0f FPS) | Vox %zu/%zu V:%zuk I:%zuk "
                          "J:%zu U:%zu R:%u T:%u F:%u D:%zu",
                          kWindowTitle, avgMs, fps, voxelMeshedChunks, voxelChunkCount, vK, iK,
                          jobQueue, uploadQueue, rebuilds, worldTrees_, worldFoliage_, dirtyChunks);
        }
        glfwSetWindowTitle(window_, title);
        frameMsAccum = 0.0;
        frameCount = 0;
        lastTitleUpdate = frameEnd;
        voxelRebuildsSinceTitle_ = 0;
    }
}

bool App::bindRendererPassLifecycle()
{
    if (renderer_.isPassLifecycleBound())
    {
        return true;
    }

    return renderer_.bindPassLifecycle(engine::render::Renderer::PassLifecycleBindings{
        [](void* app) { static_cast<App*>(app)->createPassResources(); },
        [](void* app) { static_cast<App*>(app)->destroyPassResources(); },
        this,
    });
}
