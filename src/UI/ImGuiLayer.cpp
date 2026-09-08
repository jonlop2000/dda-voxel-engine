#include "UI/ImGuiLayer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

#include "engine/render/VulkanContext.h"
#include "engine/render/Utils.h"
#include "UI/EditorTheme.h"

void ImGuiLayer::create(VulkanContext& ctx, GLFWwindow* window, VkRenderPass renderPass,
                        uint32_t imageCount)
{
    destroy(ctx);

    // create descriptor pool for ImGui
    // the sizes are generous to avoid running out of space
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool rawPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &rawPool) != VK_SUCCESS)
    {
        die("Failed to create ImGui descriptor pool");
    }
    imguiDescPool_ = engine::render::UniqueDescriptorPool(ctx.device, rawPool);

    // initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // enable keyboard controls

    // setup dear ImGui style - voxel aquarium vibrant theme
    EditorTheme::Apply();

    // initialize ImGui backends
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = ctx.instance;
    initInfo.PhysicalDevice = ctx.gpu;
    initInfo.Device = ctx.device;
    initInfo.QueueFamily = ctx.queueFamilies.graphics.value();
    initInfo.Queue = ctx.graphicsQueue;
    initInfo.DescriptorPool = imguiDescPool_.get();
    initInfo.RenderPass = renderPass;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);

    // upload ImGui fonts to gpu
    ImGui_ImplVulkan_CreateFontsTexture();

    initialized_ = true;
}

void ImGuiLayer::destroy(VulkanContext& ctx)
{
    (void)ctx;
    if (initialized_)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        initialized_ = false;
    }

    imguiDescPool_.reset();
}

void ImGuiLayer::onResize(VulkanContext& ctx, VkRenderPass renderPass, uint32_t imageCount)
{
    (void)ctx;
    (void)renderPass;
    (void)imageCount;
    // ImGui handles resize automatically, no action needed
}

void ImGuiLayer::beginFrame(bool cameraMouseCaptured)
{
    if (!initialized_)
    {
        return;
    }

    // GLFW's disabled cursor still reports virtual positions to ImGui. prevent
    // those unbounded coordinates from hovering or activating editor controls.
    ImGuiIO& io = ImGui::GetIO();
    if (cameraMouseCaptured)
    {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }
    else
    {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render(VkCommandBuffer cmd, VkExtent2D framebufferExtent)
{
    if (!initialized_)
    {
        return;
    }

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr || drawData->DisplaySize.x <= 0.0f ||
        drawData->DisplaySize.y <= 0.0f)
    {
        return;
    }

    // on macOS, disabling GLFW's retina framebuffer can leave the cocoa content
    // scale at 2x even though MoltenVK created a 1x swapchain. the GLFW backend
    // consequently reports a 2x ImGui framebuffer scale and the vulkan backend
    // renders the editor at twice its intended size. the active render target is
    // authoritative for viewport and clip scaling in both retina and 1x modes.
    const ImVec2 backendFramebufferScale = drawData->FramebufferScale;
    drawData->FramebufferScale =
        ImVec2(static_cast<float>(framebufferExtent.width) / drawData->DisplaySize.x,
               static_cast<float>(framebufferExtent.height) / drawData->DisplaySize.y);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    drawData->FramebufferScale = backendFramebufferScale;
}
