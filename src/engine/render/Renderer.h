#pragma once

#include <array>
#include <string>

#include "engine/render/Commands.h"
#include "engine/render/FrameSync.h"
#include "engine/render/Swapchain.h"
#include "engine/render/RendererPassResources.h"

struct FrameContext;
struct VulkanContext;

namespace engine::render
{

struct FrameInputs;
class PassRegistry;

class Renderer
{
public:
    using RecreateSwapchainFn = void (*)(void*);
    using PassLifecycleFn = void (*)(void*);
    using FrameStageFn = void (*)(void*, FrameContext&);

    struct CapabilityState
    {
        bool useDdaShadows = false;
        bool ambientOcclusionEnabled = false;
        bool localLightShadowsEnabled = false;
        bool localShadowBlurEnabled = false;
        bool shadowRayPassAvailable = false;
        bool shadowTemporalResolveAvailable = false;
        bool aoPassAvailable = false;
        bool aoTemporalResolveAvailable = false;
        bool localLightShadowPassAvailable = false;
        bool localLightShadowResolveAvailable = false;
        bool localShadowBlurAvailable = false;
    };

    struct FrameLifecycleBindings
    {
        VulkanContext* context = nullptr;
        bool* framebufferResized = nullptr;
        bool* swapchainRecreateRequested = nullptr;
        RecreateSwapchainFn recreateSwapchain = nullptr;
        void* recreateSwapchainUser = nullptr;
    };

    struct PassLifecycleBindings
    {
        PassLifecycleFn createPasses = nullptr;
        PassLifecycleFn destroyPasses = nullptr;
        void* user = nullptr;
    };

    struct FrameRunCallbacks
    {
        FrameStageFn record = nullptr;
        FrameStageFn afterEnd = nullptr;
        void* user = nullptr;
    };

    [[nodiscard]] bool bindFrameLifecycle(const FrameLifecycleBindings& bindings);
    [[nodiscard]] bool isFrameLifecycleBound() const;
    [[nodiscard]] bool bindPassLifecycle(const PassLifecycleBindings& bindings);
    [[nodiscard]] bool isPassLifecycleBound() const;
    [[nodiscard]] Commands& commands() { return commands_; }
    [[nodiscard]] const Commands& commands() const { return commands_; }
    [[nodiscard]] Swapchain& swapchain() { return swapchain_; }
    [[nodiscard]] const Swapchain& swapchain() const { return swapchain_; }
    [[nodiscard]] VkExtent2D swapchainExtent() const { return swapchain_.extent; }
    [[nodiscard]] VkFormat swapchainImageFormat() const { return swapchain_.imageFormat; }
    [[nodiscard]] VkPresentModeKHR swapchainPresentMode() const { return swapchain_.presentMode; }
    [[nodiscard]] VkRenderPass swapchainRenderPass() const { return swapchain_.renderPass; }
    [[nodiscard]] uint32_t swapchainImageCount() const;
    [[nodiscard]] bool swapchainSupportsTransferSrc() const
    {
        return swapchain_.supportsTransferSrc;
    }
    [[nodiscard]] VkImage swapchainImage(uint32_t imageIndex) const;
    [[nodiscard]] RendererPassResources& passes() { return passResources_; }
    [[nodiscard]] const RendererPassResources& passes() const { return passResources_; }

    void createCommandResources();
    void destroyCommandResources();
    void createSwapchainResources(GLFWwindow* window, SwapPresentMode preferredPresentMode);
    void recreateSwapchainResources(GLFWwindow* window, SwapPresentMode preferredPresentMode);
    void destroySwapchainResources();
    [[nodiscard]] bool runFrameLifecycle(const FrameRunCallbacks& callbacks);
    [[nodiscard]] bool beginFrame(FrameContext& fc);
    void recordPasses(const PassRegistry& registry, const FrameInputs& inputs,
                      const FrameContext& fc);
    void endFrame(const FrameContext& fc);
    void createFrameSyncResources();
    void destroyFrameSyncResources();
    [[nodiscard]] uint32_t currentFrameIndex() const { return sync_.currentFrame; }
    [[nodiscard]] bool waitForCurrentFrameFence(std::string* error = nullptr) const;
    [[nodiscard]] bool waitForInFlightFrameWork(const char* reason,
                                                std::string* error = nullptr) const;
    void advanceFrame();
    void recreateSwapchain() const;
    void createPasses() const;
    void destroyPasses() const;
    [[nodiscard]] bool isDdaShadowsAvailable(const CapabilityState& state) const;
    [[nodiscard]] bool isAmbientOcclusionAvailable(const CapabilityState& state) const;
    [[nodiscard]] bool isLocalLightShadowsAvailable(const CapabilityState& state) const;
    [[nodiscard]] bool isLocalShadowBlurAvailable(const CapabilityState& state) const;
    [[nodiscard]] bool canUseDdaShadows(const CapabilityState& state) const;
    [[nodiscard]] bool canUseAmbientOcclusion(const CapabilityState& state) const;
    [[nodiscard]] bool canUseLocalLightShadows(const CapabilityState& state) const;
    [[nodiscard]] bool canUseLocalShadowBlur(const CapabilityState& state) const;

private:
    [[nodiscard]] FrameLifecycleBindings& frameLifecycleBindings();
    [[nodiscard]] const FrameLifecycleBindings& frameLifecycleBindings() const;
    [[nodiscard]] const PassLifecycleBindings& passLifecycleBindings() const;

    FrameLifecycleBindings frameLifecycle_{};
    PassLifecycleBindings passLifecycle_{};
    Commands commands_{};
    Swapchain swapchain_{};
    FrameSync sync_{};
    std::array<bool, kMaxFramesInFlight> frameFencePendingSubmission_{};
    RendererPassResources passResources_{};
};

}  // namespace engine::render
