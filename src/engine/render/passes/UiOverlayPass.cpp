#include "engine/render/passes/UiOverlayPass.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

#include "Assets/Texture.h"
#include "Core/Logger.h"
#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"
#include "Resources/ShaderModule.h"
#include "UI/Runtime/UiTypes.h"

namespace
{

struct UiOverlayPushConstants
{
    glm::vec2 viewportSize{1.0f};
};

constexpr const char* kUiOverlayPassSubsystem = "UiOverlayPass";
constexpr VkDeviceSize kInitialVertexBufferBytes = 16u * 1024u;
constexpr VkDeviceSize kInitialIndexBufferBytes = 8u * 1024u;

[[noreturn]] void failUiOverlayPass(std::string_view detail)
{
    logAndExit(kUiOverlayPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) +
                   " Runtime UI overlay rendering requires a working swapchain pass.");
}

std::vector<char> loadUiShaderOrFatal(const std::string& path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path.c_str(), code, &error))
    {
        failUiOverlayPass(std::string("Failed to read ") + std::string(stage) + " shader '" +
                          path + "': " + error);
    }
    return code;
}

VkShaderModule createUiShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                           const std::string& path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failUiOverlayPass(std::string("Failed to create ") + std::string(stage) +
                          " shader module for '" + path + "': " +
                          (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkDeviceSize growCapacity(VkDeviceSize current, VkDeviceSize required, VkDeviceSize minimum)
{
    VkDeviceSize next = std::max(current, minimum);
    while (next < required)
    {
        next *= 2;
    }
    return next;
}

VkRect2D clampScissor(const ui::UiScissor& scissor, VkExtent2D extent)
{
    const int32_t maxX = static_cast<int32_t>(extent.width);
    const int32_t maxY = static_cast<int32_t>(extent.height);
    const int32_t x0 = std::clamp(scissor.x, 0, maxX);
    const int32_t y0 = std::clamp(scissor.y, 0, maxY);
    const int32_t x1 =
        std::clamp(scissor.x + static_cast<int32_t>(scissor.width), 0, maxX);
    const int32_t y1 =
        std::clamp(scissor.y + static_cast<int32_t>(scissor.height), 0, maxY);

    VkRect2D rect{};
    rect.offset = {x0, y0};
    rect.extent = {
        static_cast<uint32_t>(std::max(0, x1 - x0)),
        static_cast<uint32_t>(std::max(0, y1 - y0)),
    };
    return rect;
}

} // namespace

void UiOverlayPass::setShaderPaths(const std::string& vertPath, const std::string& fragPath)
{
    vertPath_ = vertPath;
    fragPath_ = fragPath;
}

void UiOverlayPass::setFontAtlasImage(CpuImage image, std::string label)
{
    fontAtlasImage_ = std::move(image);
    fontAtlasLabel_ = std::move(label);
}

void UiOverlayPass::create(VulkanContext& ctx, Commands& commands, VkRenderPass renderPass)
{
    createAtlasResources(ctx, commands);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(UiOverlayPushConstants);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    VkDescriptorSetLayout setLayouts[] = {atlasSetLayout_};
    plci.pSetLayouts = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failUiOverlayPass("vkCreatePipelineLayout failed.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    createPipeline(ctx, renderPass);
}

void UiOverlayPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);

    pipelineLayout_.reset();

    for (FrameBuffers& frame : frames_)
    {
        destroyFrameBuffers(ctx, frame);
    }

    destroyAtlasResources(ctx);
}

UiOverlayDrawStats UiOverlayPass::record(VulkanContext& ctx, const FrameContext& fc,
                                         const ui::UiDrawList& drawList)
{
    UiOverlayDrawStats stats{};
    stats.commandCount = static_cast<uint32_t>(drawList.commands.size());
    stats.vertexCount = static_cast<uint32_t>(drawList.vertices.size());
    stats.indexCount = static_cast<uint32_t>(drawList.indices.size());

    if (!pipeline_ || atlasSets_[0] == VK_NULL_HANDLE || drawList.empty() ||
        fc.frameIndex >= frames_.size())
    {
        return {};
    }

    const VkDeviceSize vertexBytes =
        static_cast<VkDeviceSize>(drawList.vertices.size() * sizeof(ui::UiVertex));
    const VkDeviceSize indexBytes =
        static_cast<VkDeviceSize>(drawList.indices.size() * sizeof(uint32_t));
    ensureFrameBuffers(ctx, fc.frameIndex, vertexBytes, indexBytes);

    FrameBuffers& frame = frames_[fc.frameIndex];
    void* mapped = nullptr;
    if (vkMapMemory(ctx.device, frame.vertexMemory, 0, vertexBytes, 0, &mapped) != VK_SUCCESS)
    {
        failUiOverlayPass("vkMapMemory failed for runtime UI vertex data.");
    }
    std::memcpy(mapped, drawList.vertices.data(), static_cast<size_t>(vertexBytes));
    vkUnmapMemory(ctx.device, frame.vertexMemory);

    if (vkMapMemory(ctx.device, frame.indexMemory, 0, indexBytes, 0, &mapped) != VK_SUCCESS)
    {
        failUiOverlayPass("vkMapMemory failed for runtime UI index data.");
    }
    std::memcpy(mapped, drawList.indices.data(), static_cast<size_t>(indexBytes));
    vkUnmapMemory(ctx.device, frame.indexMemory);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(fc.extent.width);
    viewport.height = static_cast<float>(fc.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    vkCmdSetViewport(fc.cmd, 0, 1, &viewport);

    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(fc.cmd, 0, 1, &frame.vertexBuffer, &vertexOffset);
    vkCmdBindIndexBuffer(fc.cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    UiOverlayPushConstants pc{};
    pc.viewportSize =
        glm::vec2(static_cast<float>(std::max(1u, fc.extent.width)),
                  static_cast<float>(std::max(1u, fc.extent.height)));
    vkCmdPushConstants(fc.cmd, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(UiOverlayPushConstants), &pc);

    uint32_t drawnCommands = 0;
    VkDescriptorSet boundAtlasSet = VK_NULL_HANDLE;
    for (const ui::UiDrawCommand& command : drawList.commands)
    {
        const VkRect2D scissor = clampScissor(command.scissor, fc.extent);
        if (scissor.extent.width == 0 || scissor.extent.height == 0 || command.indexCount == 0)
        {
            continue;
        }

        const uint32_t atlasIndex = command.atlasTextureId == ui::kFontAtlasTextureId ? 1u : 0u;
        VkDescriptorSet atlasSet = atlasSets_[atlasIndex];
        if (atlasSet == VK_NULL_HANDLE)
        {
            atlasSet = atlasSets_[0];
        }
        if (atlasSet != boundAtlasSet)
        {
            vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_.get(), 0, 1, &atlasSet, 0, nullptr);
            boundAtlasSet = atlasSet;
        }

        vkCmdSetScissor(fc.cmd, 0, 1, &scissor);
        vkCmdDrawIndexed(fc.cmd, command.indexCount, 1, command.indexOffset, 0, 0);
        ++drawnCommands;
    }

    stats.commandCount = drawnCommands;
    return stats;
}

void UiOverlayPass::createAtlasResources(VulkanContext& ctx, Commands& commands)
{
    const CpuImage white = makeSolidImage(255, 255, 255, 255, false);
    createTexture2D(ctx, commands, white, whiteAtlas_);
    if (fontAtlasImage_.rgba.empty() || fontAtlasImage_.w <= 0 || fontAtlasImage_.h <= 0)
    {
        failUiOverlayPass("Runtime UI font atlas image was not registered before create().");
    }
    createTexture2D(ctx, commands, fontAtlasImage_, fontAtlas_);
    logInfo(kUiOverlayPassSubsystem,
            std::string("Registered runtime UI atlas slot=1 label=") +
                (fontAtlasLabel_.empty() ? std::string("<unnamed>") : fontAtlasLabel_) +
                " extent=" + std::to_string(fontAtlasImage_.w) + "x" +
                std::to_string(fontAtlasImage_.h));

    VkDescriptorSetLayoutBinding atlasBinding{};
    atlasBinding.binding = 0;
    atlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    atlasBinding.descriptorCount = 1;
    atlasBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &atlasBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, layoutInfo, atlasSetLayout_))
    {
        failUiOverlayPass("vkCreateDescriptorSetLayout failed for runtime UI atlas.");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<uint32_t>(atlasSets_.size());

    std::array<VkDescriptorSetLayout, 2> layouts{atlasSetLayout_, atlasSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizes(&poolSize, 1);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizes,
                                          static_cast<uint32_t>(layouts.size()),
                                          layouts.data(), static_cast<uint32_t>(layouts.size()),
                                          atlasSets_.data()))
    {
        failUiOverlayPass("Failed to allocate runtime UI atlas descriptors.");
    }

    std::array<VkDescriptorImageInfo, 2> imageInfos{};
    imageInfos[0].sampler = whiteAtlas_.sampler;
    imageInfos[0].imageView = whiteAtlas_.view;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler = fontAtlas_.sampler;
    imageInfos[1].imageView = fontAtlas_.view;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    for (size_t i = 0; i < writes.size(); ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = atlasSets_[i];
        writes[i].dstBinding = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);
}

void UiOverlayPass::destroyAtlasResources(VulkanContext& ctx)
{
    atlasSets_.fill(VK_NULL_HANDLE);

    atlasSetLayout_ = VK_NULL_HANDLE;
    destroyTexture(ctx, whiteAtlas_);
    destroyTexture(ctx, fontAtlas_);
}

void UiOverlayPass::createPipeline(VulkanContext& ctx, VkRenderPass renderPass)
{
    const std::vector<char> vertCode = loadUiShaderOrFatal(vertPath_, "vertex");
    const std::vector<char> fragCode = loadUiShaderOrFatal(fragPath_, "fragment");

    VkShaderModule vertModule =
        createUiShaderModuleOrFatal(ctx.device, vertCode, vertPath_, "vertex");
    VkShaderModule fragModule =
        createUiShaderModuleOrFatal(ctx.device, fragCode, fragPath_, "fragment");

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
    binding.stride = sizeof(ui::UiVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[4]{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(ui::UiVertex, pos);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(ui::UiVertex, uv);
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributes[2].offset = offsetof(ui::UiVertex, color);
    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32_UINT;
    attributes[3].offset = offsetof(ui::UiVertex, flags);

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAsm{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = static_cast<uint32_t>(std::size(stages));
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = pipelineLayout_.get();
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
        VK_SUCCESS)
    {
        failUiOverlayPass("vkCreateGraphicsPipelines failed.");
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);
}

void UiOverlayPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void UiOverlayPass::ensureFrameBuffers(VulkanContext& ctx, uint32_t frameIndex,
                                       VkDeviceSize vertexBytes, VkDeviceSize indexBytes)
{
    FrameBuffers& frame = frames_[frameIndex];
    if (frame.vertexBuffer == VK_NULL_HANDLE || frame.vertexCapacityBytes < vertexBytes)
    {
        if (frame.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, frame.vertexBuffer, nullptr);
            vkFreeMemory(ctx.device, frame.vertexMemory, nullptr);
        }
        frame.vertexCapacityBytes =
            growCapacity(frame.vertexCapacityBytes, vertexBytes, kInitialVertexBufferBytes);
        createBuffer(ctx, frame.vertexCapacityBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     frame.vertexBuffer, frame.vertexMemory);
    }

    if (frame.indexBuffer == VK_NULL_HANDLE || frame.indexCapacityBytes < indexBytes)
    {
        if (frame.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, frame.indexBuffer, nullptr);
            vkFreeMemory(ctx.device, frame.indexMemory, nullptr);
        }
        frame.indexCapacityBytes =
            growCapacity(frame.indexCapacityBytes, indexBytes, kInitialIndexBufferBytes);
        createBuffer(ctx, frame.indexCapacityBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     frame.indexBuffer, frame.indexMemory);
    }
}

void UiOverlayPass::destroyFrameBuffers(VulkanContext& ctx, FrameBuffers& buffers)
{
    if (buffers.vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx.device, buffers.vertexBuffer, nullptr);
        buffers.vertexBuffer = VK_NULL_HANDLE;
    }
    if (buffers.vertexMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, buffers.vertexMemory, nullptr);
        buffers.vertexMemory = VK_NULL_HANDLE;
    }
    if (buffers.indexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx.device, buffers.indexBuffer, nullptr);
        buffers.indexBuffer = VK_NULL_HANDLE;
    }
    if (buffers.indexMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, buffers.indexMemory, nullptr);
        buffers.indexMemory = VK_NULL_HANDLE;
    }
    buffers.vertexCapacityBytes = 0;
    buffers.indexCapacityBytes = 0;
}
