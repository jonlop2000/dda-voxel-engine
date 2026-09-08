#include "engine/render/passes/GBufferPass.h"

#include <array>
#include <cstring>
#include <span>

#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"
#include "Resources/GpuImage.h"
#include "Resources/Mesh.h"
#include "Resources/ShaderModule.h"

namespace
{
struct ObjectPushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 rmAo{0.6f, 0.0f, 1.0f, 0.0f};
};

constexpr const char* kGBufferPassSubsystem = "GBufferPass";
constexpr const char* kGBufferPassRationale =
    "GBufferPass is required because it produces the material, normal, velocity, and depth "
    "targets consumed by lighting, water, glass, shadows, TAA, and composite.";

[[noreturn]] void failGBufferPass(std::string_view detail)
{
    logAndExit(kGBufferPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kGBufferPassRationale);
}

std::vector<char> loadGBufferShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failGBufferPass(std::string("Failed to read ") + std::string(stage) + " shader '" +
                        path + "': " + error);
    }
    return code;
}

VkShaderModule createGBufferShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                                const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failGBufferPass(std::string("Failed to create ") + std::string(stage) +
                        " shader module for '" + path + "': " +
                        (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createOffscreenRenderPass(VkDevice device,
                                       const std::array<VkFormat, GBufferPass::kGBufferCount>&
                                           colorFormats,
                                       VkFormat depthFormat)
{
    std::array<VkAttachmentDescription, GBufferPass::kGBufferCount + 1> attachments{};
    std::array<VkAttachmentReference, GBufferPass::kGBufferCount> colorRefs{};
    for (size_t i = 0; i < GBufferPass::kGBufferCount; i++)
    {
        VkAttachmentDescription color{};
        color.format = colorFormats[i];
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // keep attachments in a shader-readable layout for the lighting/composite passes.
        color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[i] = color;

        VkAttachmentReference ref{};
        ref.attachment = static_cast<uint32_t>(i);
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs[i] = ref;
    }

    VkAttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // depth is sampled in the lighting pass.
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    attachments[GBufferPass::kGBufferCount] = depth;

    VkAttachmentReference depthRef{};
    depthRef.attachment = static_cast<uint32_t>(GBufferPass::kGBufferCount);
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(GBufferPass::kGBufferCount);
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failGBufferPass("vkCreateRenderPass failed for the offscreen G-buffer targets.");
    }
    return rp;
}

VkPipeline createMeshPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                              VkPipelineLayout pipelineLayout, const char* vertPath,
                              const char* fragPath)
{
    const std::vector<char> vertCode = loadGBufferShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadGBufferShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createGBufferShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createGBufferShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

    VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Vertex layout: position, normal, uv.
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = static_cast<uint32_t>(offsetof(Vertex, pos));
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = static_cast<uint32_t>(offsetof(Vertex, uv));

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAsm{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAsm.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.sampleShadingEnable = VK_FALSE;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = VK_FALSE;

    std::array<VkPipelineColorBlendAttachmentState, GBufferPass::kGBufferCount> blendAttachments{};
    for (size_t i = 0; i < GBufferPass::kGBufferCount; i++)
    {
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                             VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT |
                                             VK_COLOR_COMPONENT_A_BIT;
        blendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.logicOpEnable = VK_FALSE;
    blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = pipelineLayout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
        VK_SUCCESS)
    {
        failGBufferPass("vkCreateGraphicsPipelines failed for the mesh G-buffer pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void GBufferPass::setShaderPaths(const std::string& vertPath, const std::string& fragPath)
{
    vertPath_ = vertPath;
    fragPath_ = fragPath;
}

void GBufferPass::setMaterialLayout(VkDescriptorSetLayout layout)
{
    materialSetLayout_ = layout;
}

void GBufferPass::create(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failGBufferPass("PassCreateInfo.commands is required for GBufferPass.");
    }

    if (materialSetLayout_ == VK_NULL_HANDLE)
    {
        failGBufferPass("GBufferPass requires a valid material descriptor set layout.");
    }

    extent_ = ci.extent;
    depthFormat_ = findSupportedDepthFormat(ctx);

    // set 0: per-frame camera ubo.
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 1;
    dlci.pBindings = &uboBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dlci, setLayout_))
    {
        failGBufferPass("vkCreateDescriptorSetLayout failed for the per-frame mesh UBO set.");
    }

    // set 1 layout is owned by the material system.
    VkDescriptorSetLayout setLayouts[] = {setLayout_, materialSetLayout_};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = static_cast<uint32_t>(sizeof(setLayouts) / sizeof(setLayouts[0]));
    plci.pSetLayouts = setLayouts;
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(ObjectPushConstants);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failGBufferPass("vkCreatePipelineLayout failed for the mesh G-buffer pipeline.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kMaxFramesInFlight;

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, setLayout_);

    descSets_.resize(kMaxFramesInFlight);
    const std::span<const VkDescriptorPoolSize> poolSizesView(&poolSize, 1);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight,
                                          layouts.data(), kMaxFramesInFlight, descSets_.data()))
    {
        failGBufferPass("Failed to allocate the G-buffer frame descriptors.");
    }

    ubos_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++)
    {
        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     ubos_[i].buffer, ubos_[i].memory);

        if (vkMapMemory(ctx.device, ubos_[i].memory, 0, sizeof(FrameUbo), 0, &ubos_[i].mapped) !=
            VK_SUCCESS)
        {
            failGBufferPass("vkMapMemory failed for the G-buffer frame UBO.");
        }

        VkDescriptorBufferInfo bi{};
        bi.buffer = ubos_[i].buffer;
        bi.offset = 0;
        bi.range = sizeof(FrameUbo);

        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = descSets_[i];
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;

        vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
    }

    createFrameResources(ctx, ci);
    createPipeline(ctx);
}

void GBufferPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyFrameResources(ctx);
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    for (auto& ubo : ubos_)
    {
        if (ubo.mapped != nullptr)
        {
            vkUnmapMemory(ctx.device, ubo.memory);
            ubo.mapped = nullptr;
        }
        if (ubo.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, ubo.buffer, nullptr);
        }
        if (ubo.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, ubo.memory, nullptr);
        }
    }
    ubos_.clear();

    descSets_.clear();
    setLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();

}

void GBufferPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci)
{
    extent_ = ci.extent;
    destroyPipeline(ctx);
    destroyFrameResources(ctx);
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    createFrameResources(ctx, ci);
    createPipeline(ctx);
}

void GBufferPass::updateFrameUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
{
    updateUbo(frameIndex, frameUbo);
}

void GBufferPass::record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                         const std::vector<RenderObject>& objects,
                         const ExtraDrawCall& extraDraw)
{
    (void)ctx;
    if (fc.frameIndex >= frames_.size())
    {
        return;
    }

    updateUbo(fc.frameIndex, frameUbo);

    VkClearValue clears[kGBufferCount + 1]{};
    for (size_t i = 0; i < kGBufferCount; i++)
    {
        clears[i].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    }
    clears[kGBufferCount].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo offscreenRp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    offscreenRp.renderPass = renderPass_;
    offscreenRp.framebuffer = frames_[fc.frameIndex].framebuffer;
    offscreenRp.renderArea.offset = {0, 0};
    offscreenRp.renderArea.extent = extent_;
    offscreenRp.clearValueCount = static_cast<uint32_t>(kGBufferCount + 1);
    offscreenRp.pClearValues = clears;

    vkCmdBeginRenderPass(fc.cmd, &offscreenRp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    // bind per-frame ubo set.
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 1,
                            &descSets_[fc.frameIndex], 0, nullptr);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(extent_.width);
    vp.height = static_cast<float>(extent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(fc.cmd, 0, 1, &vp);
    vkCmdSetScissor(fc.cmd, 0, 1, &scissor);

    for (const auto& obj : objects)
    {
        if (obj.mesh == nullptr || obj.material == nullptr || obj.material->set == VK_NULL_HANDLE)
        {
            continue;
        }
        if (obj.mesh->vbo == VK_NULL_HANDLE)
        {
            continue;
        }
        if (obj.mesh->useIndex && obj.mesh->ibo == VK_NULL_HANDLE)
        {
            continue;
        }

        obj.mesh->lastUsedFrame = fc.frameIndex;

        // bind per-material textures and push per-object constants.
        VkDescriptorSet materialSet = obj.material->set;
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 1,
                                1, &materialSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {obj.mesh->vbo};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(fc.cmd, 0, 1, vertexBuffers, offsets);

        if (obj.mesh->useIndex)
        {
            vkCmdBindIndexBuffer(fc.cmd, obj.mesh->ibo, 0, VK_INDEX_TYPE_UINT32);
        }

        ObjectPushConstants pc{};
        pc.model = obj.model;
        pc.baseColorFactor = obj.material->baseColorFactor;
        pc.rmAo = glm::vec4(obj.material->roughness, obj.material->metallic, obj.material->ao,
                            0.0f);
        vkCmdPushConstants(fc.cmd, pipelineLayout_.get(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);

        if (obj.mesh->useIndex)
        {
            vkCmdDrawIndexed(fc.cmd, obj.mesh->indexCount, 1, 0, 0, 0);
        }
        else
        {
            vkCmdDraw(fc.cmd, obj.mesh->vertexCount, 1, 0, 0);
        }
    }

    if (extraDraw.fn != nullptr)
    {
        extraDraw.fn(fc.cmd, descSets_[fc.frameIndex], extraDraw.user);
    }

    vkCmdEndRenderPass(fc.cmd);
}

const GBufferPass::ColorAttachment& GBufferPass::color(uint32_t frameIndex, Slot slot) const
{
    return frames_.at(frameIndex).g[static_cast<size_t>(slot)];
}

const GBufferPass::DepthTarget& GBufferPass::depth(uint32_t frameIndex) const
{
    return frames_.at(frameIndex).depth;
}

VkRenderPass GBufferPass::renderPass() const
{
    return renderPass_;
}

VkExtent2D GBufferPass::extent() const
{
    return extent_;
}

VkFormat GBufferPass::depthFormat() const
{
    return depthFormat_;
}

VkDescriptorSetLayout GBufferPass::frameSetLayout() const
{
    return setLayout_;
}

VkDescriptorSet GBufferPass::frameSet(uint32_t frameIndex) const
{
    if (frameIndex >= descSets_.size())
    {
        return VK_NULL_HANDLE;
    }
    return descSets_[frameIndex];
}

void GBufferPass::createPipeline(VulkanContext& ctx)
{
    if (renderPass_ == VK_NULL_HANDLE)
    {
        failGBufferPass("GBufferPass render pass must exist before pipeline creation.");
    }
    VkPipeline pipeline = createMeshPipeline(ctx, renderPass_, pipelineLayout_.get(),
                                             vertPath_.c_str(), fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void GBufferPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void GBufferPass::createFrameResources(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failGBufferPass("PassCreateInfo.commands is required for GBufferPass frame resources.");
    }

    frames_.resize(kMaxFramesInFlight);

    std::array<VkFormat, kGBufferCount> formats{};
    formats[static_cast<size_t>(Slot::Albedo)] = VK_FORMAT_R8G8B8A8_UNORM;
    formats[static_cast<size_t>(Slot::Normal)] = VK_FORMAT_R16G16B16A16_SFLOAT;
    formats[static_cast<size_t>(Slot::Material)] = VK_FORMAT_R8G8B8A8_UNORM;
    formats[static_cast<size_t>(Slot::Velocity)] = VK_FORMAT_R16G16B16A16_SFLOAT;
    formats[static_cast<size_t>(Slot::WaterDist)] = VK_FORMAT_R16G16_SFLOAT;

    if (renderPass_ == VK_NULL_HANDLE)
    {
        renderPass_ = createOffscreenRenderPass(ctx.device, formats, depthFormat_);
    }

    for (auto& frame : frames_)
    {
        for (size_t i = 0; i < kGBufferCount; i++)
        {
            auto& g = frame.g[i];
            g.format = formats[i];
            createImage(ctx, extent_.width, extent_.height, g.format,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        g.image, g.memory);
            g.view = createImageView(ctx.device, g.image, g.format, VK_IMAGE_ASPECT_COLOR_BIT);

            const bool isNearest = i == static_cast<size_t>(Slot::Velocity) ||
                                   i == static_cast<size_t>(Slot::WaterDist);
            const engine::render::SamplerPreset preset =
                isNearest ? engine::render::SamplerPreset::ClampNearest
                          : engine::render::SamplerPreset::ClampLinear;
            if (!ctx.samplerCache.get(ctx.device, preset, g.sampler))
            {
                failGBufferPass("vkCreateSampler failed for a G-buffer color attachment.");
            }
        }

        VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        frame.depth.format = depthFormat_;
        createImage(ctx, extent_.width, extent_.height, frame.depth.format,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    frame.depth.image, frame.depth.memory);
        frame.depth.view = createImageView(ctx.device, frame.depth.image, frame.depth.format,
                                           depthAspect);

        {
            if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampNearest,
                                      frame.depth.sampler))
            {
                failGBufferPass("vkCreateSampler failed for the G-buffer depth attachment.");
            }
        }

        for (size_t i = 0; i < kGBufferCount; i++)
        {
            transitionImageLayout(ctx, *ci.commands, frame.g[i].image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        transitionImageLayout(ctx, *ci.commands, frame.depth.image, depthAspect,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        std::array<VkImageView, kGBufferCount + 1> attachments{};
        for (size_t i = 0; i < kGBufferCount; i++)
        {
            attachments[i] = frame.g[i].view;
        }
        attachments[kGBufferCount] = frame.depth.view;

        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = renderPass_;
        fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbci.pAttachments = attachments.data();
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &frame.framebuffer) != VK_SUCCESS)
        {
            failGBufferPass("vkCreateFramebuffer failed for an offscreen G-buffer target.");
        }
    }
}

void GBufferPass::destroyFrameResources(VulkanContext& ctx)
{
    for (auto& frame : frames_)
    {
        if (frame.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, frame.framebuffer, nullptr);
            frame.framebuffer = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < kGBufferCount; i++)
        {
            auto& g = frame.g[i];
            g.sampler = VK_NULL_HANDLE;
            if (g.view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(ctx.device, g.view, nullptr);
            }
            if (g.image != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx.device, g.image, nullptr);
            }
            if (g.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(ctx.device, g.memory, nullptr);
            }
            g = ColorAttachment{};
        }

        if (frame.depth.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, frame.depth.view, nullptr);
        }
        frame.depth.sampler = VK_NULL_HANDLE;
        if (frame.depth.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, frame.depth.image, nullptr);
        }
        if (frame.depth.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, frame.depth.memory, nullptr);
        }
        frame.depth = DepthTarget{};
    }

    frames_.clear();
}

void GBufferPass::updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
{
    if (frameIndex >= ubos_.size())
    {
        return;
    }
    if (ubos_[frameIndex].mapped == nullptr)
    {
        return;
    }

    std::memcpy(ubos_[frameIndex].mapped, &frameUbo, sizeof(frameUbo));
}
