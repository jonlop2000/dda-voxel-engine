#pragma once

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

struct GLFWwindow;
struct VulkanContext;
struct FrameSync;

class ImGuiLayer
{
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() = default;

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // initialize ImGui with vulkan and glfw backends
    void create(VulkanContext& ctx, GLFWwindow* window, VkRenderPass renderPass,
                uint32_t imageCount);

    // cleanup all ImGui resources
    void destroy(VulkanContext& ctx);

    // called when swapchain is recreated (window resize)
    void onResize(VulkanContext& ctx, VkRenderPass renderPass, uint32_t imageCount);

    // begin a new ImGui frame
    void beginFrame(bool cameraMouseCaptured = false);

    // render ImGui draw data to command buffer
    void render(VkCommandBuffer cmd, VkExtent2D framebufferExtent);

private:
    engine::render::UniqueDescriptorPool imguiDescPool_{};
    bool initialized_ = false;
};
