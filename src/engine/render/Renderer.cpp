#include "engine/render/Renderer.h"

#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Swapchain.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/FrameCharacterization.h"
#include "engine/render/PassRegistry.h"
#include "engine/render/RendererCapabilities.h"

#include <string>

namespace engine::render
{

bool Renderer::bindFrameLifecycle(const FrameLifecycleBindings& bindings)
{
    if (bindings.context == nullptr || bindings.framebufferResized == nullptr ||
        bindings.swapchainRecreateRequested == nullptr ||
        bindings.recreateSwapchain == nullptr)
    {
        return false;
    }

    frameLifecycle_ = bindings;
    return true;
}

bool Renderer::isFrameLifecycleBound() const
{
    return frameLifecycle_.context != nullptr && frameLifecycle_.framebufferResized != nullptr &&
           frameLifecycle_.swapchainRecreateRequested != nullptr &&
           frameLifecycle_.recreateSwapchain != nullptr;
}

bool Renderer::bindPassLifecycle(const PassLifecycleBindings& bindings)
{
    if (bindings.createPasses == nullptr || bindings.destroyPasses == nullptr)
    {
        return false;
    }

    passLifecycle_ = bindings;
    return true;
}

bool Renderer::isPassLifecycleBound() const
{
    return passLifecycle_.createPasses != nullptr && passLifecycle_.destroyPasses != nullptr;
}

uint32_t Renderer::swapchainImageCount() const
{
    return static_cast<uint32_t>(swapchain_.images.size());
}

VkImage Renderer::swapchainImage(uint32_t imageIndex) const
{
    if (imageIndex >= swapchain_.images.size())
    {
        return VK_NULL_HANDLE;
    }
    return swapchain_.images[imageIndex];
}

Renderer::FrameLifecycleBindings& Renderer::frameLifecycleBindings()
{
    if (!isFrameLifecycleBound())
    {
        die("Renderer frame lifecycle is not bound");
    }
    return frameLifecycle_;
}

const Renderer::FrameLifecycleBindings& Renderer::frameLifecycleBindings() const
{
    if (!isFrameLifecycleBound())
    {
        die("Renderer frame lifecycle is not bound");
    }
    return frameLifecycle_;
}

const Renderer::PassLifecycleBindings& Renderer::passLifecycleBindings() const
{
    if (!isPassLifecycleBound())
    {
        die("Renderer pass lifecycle is not bound");
    }
    return passLifecycle_;
}

void Renderer::recreateSwapchain() const
{
    const FrameLifecycleBindings& bindings = frameLifecycleBindings();
    bindings.recreateSwapchain(bindings.recreateSwapchainUser);
}

void Renderer::createPasses() const
{
    const PassLifecycleBindings& bindings = passLifecycleBindings();
    bindings.createPasses(bindings.user);
}

void Renderer::destroyPasses() const
{
    const PassLifecycleBindings& bindings = passLifecycleBindings();
    bindings.destroyPasses(bindings.user);
}

bool Renderer::isDdaShadowsAvailable(const CapabilityState& state) const
{
    return engine::render::isDdaShadowsAvailable(state.shadowRayPassAvailable,
                                                 state.shadowTemporalResolveAvailable);
}

bool Renderer::isAmbientOcclusionAvailable(const CapabilityState& state) const
{
    return engine::render::isAmbientOcclusionAvailable(state.aoPassAvailable,
                                                       state.aoTemporalResolveAvailable);
}

bool Renderer::isLocalLightShadowsAvailable(const CapabilityState& state) const
{
    return engine::render::isLocalLightShadowsAvailable(
        state.localLightShadowPassAvailable, state.localLightShadowResolveAvailable);
}

bool Renderer::isLocalShadowBlurAvailable(const CapabilityState& state) const
{
    return engine::render::isLocalShadowBlurAvailable(
        state.localShadowBlurAvailable, state.localLightShadowPassAvailable,
        state.localLightShadowResolveAvailable);
}

bool Renderer::canUseDdaShadows(const CapabilityState& state) const
{
    return engine::render::canUseDdaShadows(state.useDdaShadows,
                                            state.shadowRayPassAvailable,
                                            state.shadowTemporalResolveAvailable);
}

bool Renderer::canUseAmbientOcclusion(const CapabilityState& state) const
{
    return engine::render::canUseAmbientOcclusion(state.ambientOcclusionEnabled,
                                                  state.aoPassAvailable,
                                                  state.aoTemporalResolveAvailable);
}

bool Renderer::canUseLocalLightShadows(const CapabilityState& state) const
{
    return engine::render::canUseLocalLightShadows(
        state.localLightShadowsEnabled, state.localLightShadowPassAvailable,
        state.localLightShadowResolveAvailable);
}

bool Renderer::canUseLocalShadowBlur(const CapabilityState& state) const
{
    return engine::render::canUseLocalShadowBlur(
        state.localShadowBlurEnabled, state.localShadowBlurAvailable,
        state.localLightShadowPassAvailable, state.localLightShadowResolveAvailable);
}

bool Renderer::runFrameLifecycle(const FrameRunCallbacks& callbacks)
{
    if (callbacks.record == nullptr || callbacks.afterEnd == nullptr)
    {
        return false;
    }

    FrameContext fc{};
    if (!beginFrame(fc))
    {
        return false;
    }

    callbacks.record(callbacks.user, fc);
    endFrame(fc);
    callbacks.afterEnd(callbacks.user, fc);
    advanceFrame();
    return true;
}

bool Renderer::beginFrame(FrameContext& fc)
{
    FrameLifecycleBindings& bindings = frameLifecycleBindings();
    VulkanContext& ctx = *bindings.context;
    Swapchain& swapchain = swapchain_;
    FrameSync& sync = sync_;
    Commands& commands = commands_;
    auto& frameFencePendingSubmission = frameFencePendingSubmission_;

    uint32_t imageIndex = 0;
    VkResult acquire =
        vkAcquireNextImageKHR(ctx.device, swapchain.swapchain, UINT64_MAX,
                              sync.imageAvailable[sync.currentFrame], VK_NULL_HANDLE,
                              &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
    {
        die("vkAcquireNextImageKHR failed");
    }

    if (sync.imagesInFlight[imageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(ctx.device, 1, &sync.imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    sync.imagesInFlight[imageIndex] = sync.inFlightFences[sync.currentFrame];

    vkResetFences(ctx.device, 1, &sync.inFlightFences[sync.currentFrame]);
    frameFencePendingSubmission[sync.currentFrame] = true;

    VkCommandBuffer cmd = commands.buffers[sync.currentFrame];
    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS)
    {
        die("vkResetCommandBuffer failed");
    }

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
    {
        die("vkBeginCommandBuffer failed");
    }

    fc = {};
    fc.frameIndex = sync.currentFrame;
    fc.swapImageIndex = imageIndex;
    fc.cmd = cmd;
    fc.extent = swapchain.extent;
    beginRuntimeFrameCharacterization();
    return true;
}

void Renderer::createCommandResources()
{
    commands_.create(*frameLifecycleBindings().context);
}

void Renderer::destroyCommandResources()
{
    commands_.destroy(*frameLifecycleBindings().context);
}

void Renderer::createSwapchainResources(GLFWwindow* window,
                                        SwapPresentMode preferredPresentMode)
{
    swapchain_.create(*frameLifecycleBindings().context, window, preferredPresentMode);
}

void Renderer::recreateSwapchainResources(GLFWwindow* window,
                                          SwapPresentMode preferredPresentMode)
{
    swapchain_.recreate(*frameLifecycleBindings().context, window, preferredPresentMode);
}

void Renderer::destroySwapchainResources()
{
    swapchain_.destroy(*frameLifecycleBindings().context);
}

void Renderer::recordPasses(const PassRegistry& registry, const FrameInputs& inputs,
                            const FrameContext& fc)
{
    VulkanContext& ctx = *frameLifecycleBindings().context;
    RenderPassContext context{ctx, fc, inputs};
    recordRuntimeFrameCharacterizationPasses(registry.enabledNames(inputs));
    registry.recordAll(context);
}

void Renderer::endFrame(const FrameContext& fc)
{
    FrameLifecycleBindings& bindings = frameLifecycleBindings();
    VulkanContext& ctx = *bindings.context;
    Swapchain& swapchain = swapchain_;
    FrameSync& sync = sync_;
    auto& frameFencePendingSubmission = frameFencePendingSubmission_;

    VkCommandBuffer cmd = fc.cmd;
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        die("vkEndCommandBuffer failed");
    }

    VkSemaphore renderFinished = sync.renderFinishedPerImage[fc.swapImageIndex];
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &sync.imageAvailable[fc.frameIndex];
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished;

    if (vkQueueSubmit(ctx.graphicsQueue, 1, &submit, sync.inFlightFences[fc.frameIndex]) !=
        VK_SUCCESS)
    {
        die("vkQueueSubmit failed");
    }
    frameFencePendingSubmission[fc.frameIndex] = false;

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain.swapchain;
    present.pImageIndices = &fc.swapImageIndex;

    VkResult presentRes = vkQueuePresentKHR(ctx.presentQueue, &present);

    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR ||
        *bindings.framebufferResized || *bindings.swapchainRecreateRequested)
    {
        recreateSwapchain();
    }
    else if (presentRes != VK_SUCCESS)
    {
        die("vkQueuePresentKHR failed");
    }

    endRuntimeFrameCharacterization();
}

void Renderer::createFrameSyncResources()
{
    const FrameLifecycleBindings& bindings = frameLifecycleBindings();
    createSyncObjects(*bindings.context, swapchain_, sync_);
    frameFencePendingSubmission_.fill(false);
}

void Renderer::destroyFrameSyncResources()
{
    const FrameLifecycleBindings& bindings = frameLifecycleBindings();
    destroySyncObjects(*bindings.context, sync_);
}

bool Renderer::waitForCurrentFrameFence(std::string* error) const
{
    const FrameLifecycleBindings& bindings = frameLifecycleBindings();
    VulkanContext& ctx = *bindings.context;
    if (ctx.device == VK_NULL_HANDLE)
    {
        return true;
    }

    const VkFence fence = sync_.inFlightFences[sync_.currentFrame];
    if (fence == VK_NULL_HANDLE)
    {
        return true;
    }

    const VkResult result = vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (result == VK_SUCCESS)
    {
        return true;
    }

    if (error != nullptr)
    {
        *error = "Failed to wait for current frame fence (VkResult " +
                 std::to_string(static_cast<int>(result)) + ")";
    }
    return false;
}

bool Renderer::waitForInFlightFrameWork(const char* reason, std::string* error) const
{
    const FrameLifecycleBindings& bindings = frameLifecycleBindings();
    VulkanContext& ctx = *bindings.context;
    if (ctx.device == VK_NULL_HANDLE)
    {
        return true;
    }

    std::array<VkFence, kMaxFramesInFlight> fences{};
    uint32_t fenceCount = 0;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        const VkFence fence = sync_.inFlightFences[i];
        if (fence == VK_NULL_HANDLE || frameFencePendingSubmission_[i])
        {
            continue;
        }
        fences[fenceCount++] = fence;
    }

    if (fenceCount == 0)
    {
        return true;
    }

    const VkResult result =
        vkWaitForFences(ctx.device, fenceCount, fences.data(), VK_TRUE, UINT64_MAX);
    if (result == VK_SUCCESS)
    {
        return true;
    }

    if (error != nullptr)
    {
        *error = "Failed to wait for in-flight frame fences during " +
                 std::string(reason != nullptr ? reason : "unspecified work") +
                 " (VkResult " + std::to_string(static_cast<int>(result)) + ")";
    }
    return false;
}

void Renderer::advanceFrame()
{
    traceRuntimeFrameLoopStep("renderer.advanceFrame");
    sync_.currentFrame = (sync_.currentFrame + 1) % kMaxFramesInFlight;
}

}  // namespace engine::render
