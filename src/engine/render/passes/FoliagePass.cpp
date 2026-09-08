#include "engine/render/passes/FoliagePass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "Core/Logger.h"
#include "Resources/ShaderModule.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

namespace engine::render
{
namespace
{
bool createBuffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
                  VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags,
                  UniqueBuffer& buffer, const char* allocationName)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = allocationFlags;
    return createUniqueBuffer(context.memoryAllocator, bufferInfo, allocationInfo,
                              buffer, allocationName) == VK_SUCCESS;
}
} // namespace

FoliageRendererMode parseFoliageRendererMode(std::string_view value)
{
    return value.empty() || value == "instanced" ? FoliageRendererMode::Instanced
                                                  : FoliageRendererMode::Voxel;
}

const char* foliageRendererModeName(FoliageRendererMode mode)
{
    return mode == FoliageRendererMode::Instanced ? "instanced" : "voxel";
}

void FoliagePass::setShaderPaths(std::string vertexPath, std::string fragmentPath)
{
    vertexPath_ = std::move(vertexPath);
    fragmentPath_ = std::move(fragmentPath);
}

void FoliagePass::setRequestedRenderer(FoliageRendererMode mode)
{
    if (requestedRenderer_ != mode)
    {
        requestedRenderer_ = mode;
        motionHistory_.reset();
    }
    refreshReadyState();
}

void FoliagePass::setSwayEnabled(bool enabled)
{
    swayEnabled_ = enabled;
}

void FoliagePass::setSwayStrength(float strength)
{
    swayStrength_ = std::clamp(strength, 0.0f, 2.0f);
}

void FoliagePass::setEnvironmentWind(
    const engine::scene::EnvironmentWindSettings& settings)
{
    const engine::scene::EnvironmentWindSettings sanitized =
        engine::scene::sanitizeEnvironmentWindSettings(settings);
    if (!engine::scene::environmentWindSettingsEquivalent(environmentWind_,
                                                           sanitized))
    {
        environmentWind_ = sanitized;
        motionHistory_.reset();
    }
}

void FoliagePass::setWindborneParticleSettings(
    const engine::scene::WindborneParticleSettings& settings)
{
    const engine::scene::WindborneParticleSettings sanitized =
        engine::scene::sanitizeWindborneParticleSettings(settings);
    if (!engine::scene::windborneParticleSettingsEquivalent(
            windborneParticleSettings_, sanitized))
    {
        windborneParticleSettings_ = sanitized;
        motionHistory_.reset();
    }
}

void FoliagePass::setPaletteResponse(FoliagePaletteResponse response)
{
    paletteResponse_ = response;
}

bool FoliagePass::create(VulkanContext& context, VkRenderPass renderPass,
                         VkDescriptorSetLayout frameSetLayout, VkBuffer paletteBuffer)
{
    destroy(context);
    if (renderPass == VK_NULL_HANDLE || frameSetLayout == VK_NULL_HANDLE ||
        paletteBuffer == VK_NULL_HANDLE || vertexPath_.empty() || fragmentPath_.empty())
    {
        logWarning("FoliagePass", "Instanced foliage bindings are incomplete; using voxel foliage.");
        return false;
    }

    if (!createDescriptorResources(context, paletteBuffer) ||
        !createGraphicsPipeline(context, renderPass, frameSetLayout))
    {
        destroy(context);
        logWarning("FoliagePass", "Instanced foliage pipeline creation failed; using voxel foliage.");
        return false;
    }

    resourcesCreated_ = true;
    if ((!pendingInstances_.empty() || !pendingWindborneParticles_.empty()) &&
        !uploadPendingGeometry(context))
    {
        logWarning("FoliagePass", "Instanced foliage upload failed; using voxel foliage.");
    }
    refreshReadyState();
    return true;
}

void FoliagePass::destroy(VulkanContext& context)
{
    (void)context;
    ready_ = false;
    resourcesCreated_ = false;
    motionHistory_.reset();
    semanticInstanceCount_ = 0;
    patchCount_ = 0;
    foliagePrimitiveCount_ = 0;
    windborneParticleCount_ = 0;
    primitiveCount_ = 0;
    primitiveBuffer_.reset();
    pipeline_.reset();
    pipelineLayout_.reset();
    instanceSet_ = VK_NULL_HANDLE;
    instanceSetLayout_ = VK_NULL_HANDLE;
    paletteBuffer_ = VK_NULL_HANDLE;
}

bool FoliagePass::recreatePipeline(VulkanContext& context, VkRenderPass renderPass,
                                   VkDescriptorSetLayout frameSetLayout)
{
    pipeline_.reset();
    pipelineLayout_.reset();
    if (!resourcesCreated_ ||
        !createGraphicsPipeline(context, renderPass, frameSetLayout))
    {
        ready_ = false;
        return false;
    }
    refreshReadyState();
    return true;
}

bool FoliagePass::setSceneData(
    VulkanContext& context,
    std::span<const engine::game::FoliageBladeInstance> instances,
    uint32_t legacyVoxelVolumeIndex,
    std::span<const engine::scene::WindborneParticleCandidate>
        windborneParticles)
{
    pendingInstances_.assign(instances.begin(), instances.end());
    pendingWindborneParticles_.assign(windborneParticles.begin(),
                                      windborneParticles.end());
    legacyVoxelVolumeIndex_ = legacyVoxelVolumeIndex;
    motionHistory_.reset();
    if (!resourcesCreated_)
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        refreshReadyState();
        return true;
    }
    const bool uploaded = uploadPendingGeometry(context);
    if (!uploaded)
    {
        logWarning("FoliagePass",
                   "Voxel-tuft foliage upload failed; using voxel foliage.");
    }
    refreshReadyState();
    return uploaded;
}

void FoliagePass::clearSceneData()
{
    pendingInstances_.clear();
    pendingWindborneParticles_.clear();
    legacyVoxelVolumeIndex_ = std::numeric_limits<uint32_t>::max();
    semanticInstanceCount_ = 0;
    patchCount_ = 0;
    foliagePrimitiveCount_ = 0;
    windborneParticleCount_ = 0;
    primitiveCount_ = 0;
    primitiveBuffer_.reset();
    ready_ = false;
    motionHistory_.reset();
}

void FoliagePass::beginFrame(float currentTimeSeconds)
{
    motionHistory_.beginFrame(currentTimeSeconds,
                              swayEnabled_ ? swayStrength_ : 0.0f);
}

void FoliagePass::completeFrame()
{
    motionHistory_.completeFrame();
}

void FoliagePass::record(VkCommandBuffer commandBuffer, VkDescriptorSet frameSet,
                         const DrawSettings& settings) const
{
    if (!drawable() || commandBuffer == VK_NULL_HANDLE || frameSet == VK_NULL_HANDLE)
    {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    const std::array<VkDescriptorSet, 2> sets = {frameSet, instanceSet_};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_.get(), 0,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    PushConstants constants{};
    constants.timeWind = glm::vec4(motionHistory_.currentTimeSeconds(),
                                   motionHistory_.previousTimeSeconds(),
                                   environmentWind_.direction.x,
                                   environmentWind_.direction.y);
    constants.controls = glm::vec4(
        motionHistory_.currentSwayStrength() * environmentWind_.strength,
        motionHistory_.previousSwayStrength() * environmentWind_.strength,
        settings.reflectionClipY,
        settings.reflectionClipEnabled ? 1.0f : 0.0f);
    const engine::scene::EnvironmentWindWaveShape& wave =
        engine::scene::defaultEnvironmentWindWaveShape();
    constants.secondaryWave =
        glm::vec4(wave.secondaryTimeScale, wave.secondarySpatialScale,
                  wave.secondaryWeight, wave.secondaryPhaseOffsetRadians);
    constants.crossWave =
        glm::vec4(wave.crossTimeScale, wave.crossSpatialScale,
                  engine::scene::environmentWindFoliageCrossWeight(
                      environmentWind_, wave.crossWeight),
                  environmentWind_.verticalLift);
    const FoliagePresentationSettings presentation =
        makeFoliagePresentationSettings(paletteResponse_);
    constants.appearance =
        glm::vec4(presentation.strength, presentation.luminanceContrast,
                  presentation.saturationRetention,
                  presentation.sideNormalUpBias);
    constants.surface =
        glm::vec4(presentation.botanicalIdentityStrength,
                  presentation.roughnessSeparation,
                  presentation.faceValueContrast,
                  presentation.meadowVolumeStrength);
    constants.material =
        glm::vec4(presentation.softMaterialStrength, environmentWind_.speed,
                  environmentWind_.gustStrength,
                  environmentWind_.gustFrequencyHz);
    constants.ambient =
        glm::vec4(environmentWind_.strength,
                  windborneParticleSettings_.visibilityDistance,
                  windborneParticleSettings_.leafFraction,
                  windborneParticleSettings_.scale);
    vkCmdPushConstants(commandBuffer, pipelineLayout_.get(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(constants), &constants);
    vkCmdDraw(commandBuffer, kFoliageVoxelVerticesPerPrimitive,
              foliagePrimitiveCount_ + activeWindborneParticleCount(), 0, 0);
}

uint32_t FoliagePass::activeWindborneParticleCount() const
{
    if (!ready_ || requestedRenderer_ != FoliageRendererMode::Instanced)
    {
        return 0u;
    }
    return engine::scene::activeWindborneParticleCount(
        windborneParticleSettings_, windborneParticleCount_);
}

bool FoliagePass::drawable() const
{
    return ready_ && requestedRenderer_ == FoliageRendererMode::Instanced &&
           (foliagePrimitiveCount_ > 0u ||
            activeWindborneParticleCount() > 0u) &&
           primitiveCount_ > 0u;
}

bool FoliagePass::replacesVoxelVolume(uint32_t volumeIndex) const
{
    return drawable() && foliagePrimitiveCount_ > 0u &&
           volumeIndex == legacyVoxelVolumeIndex_;
}

bool FoliagePass::createDescriptorResources(VulkanContext& context,
                                            VkBuffer paletteBuffer)
{
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
    {
        bindings[bindingIndex].binding = bindingIndex;
        bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[bindingIndex].descriptorCount = 1;
        bindings[bindingIndex].stageFlags =
            bindingIndex == 0u ? VK_SHADER_STAGE_VERTEX_BIT
                               : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (!context.descriptorLayoutCache.get(context.device, layoutInfo,
                                           instanceSetLayout_))
    {
        return false;
    }

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2u};
    if (!context.descriptorAllocator.allocate(context.device,
                                              std::span(&poolSize, 1), 1,
                                              &instanceSetLayout_, 1, &instanceSet_))
    {
        instanceSet_ = VK_NULL_HANDLE;
        return false;
    }
    paletteBuffer_ = paletteBuffer;
    return true;
}

bool FoliagePass::createGraphicsPipeline(VulkanContext& context,
                                         VkRenderPass renderPass,
                                         VkDescriptorSetLayout frameSetLayout)
{
    std::vector<char> vertexCode;
    std::vector<char> fragmentCode;
    std::string error;
    if (!tryReadFile(vertexPath_.c_str(), vertexCode, &error) ||
        !tryReadFile(fragmentPath_.c_str(), fragmentCode, &error))
    {
        return false;
    }

    VkShaderModule vertexModule = tryCreateShaderModule(context.device, vertexCode, &error);
    VkShaderModule fragmentModule =
        tryCreateShaderModule(context.device, fragmentCode, &error);
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
    {
        if (vertexModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(context.device, vertexModule, nullptr);
        }
        if (fragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(context.device, fragmentModule, nullptr);
        }
        return false;
    }

    const std::array<VkDescriptorSetLayout, 2> setLayouts = {
        frameSetLayout, instanceSetLayout_};
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    VkPipelineLayout rawPipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr,
                               &rawPipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(context.device, fragmentModule, nullptr);
        vkDestroyShaderModule(context.device, vertexModule, nullptr);
        return false;
    }
    pipelineLayout_ = UniquePipelineLayout(context.device, rawPipelineLayout);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::array<VkPipelineColorBlendAttachmentState, 5> blendAttachments{};
    for (VkPipelineColorBlendAttachmentState& attachment : blendAttachments)
    {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                    VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult =
        vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
                                  &pipelineInfo, nullptr, &rawPipeline);
    vkDestroyShaderModule(context.device, fragmentModule, nullptr);
    vkDestroyShaderModule(context.device, vertexModule, nullptr);
    if (pipelineResult != VK_SUCCESS)
    {
        pipelineLayout_.reset();
        return false;
    }
    pipeline_ = UniquePipeline(context.device, rawPipeline);
    return true;
}

bool FoliagePass::uploadPendingGeometry(VulkanContext& context)
{
    if (pendingInstances_.empty() && pendingWindborneParticles_.empty())
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        ready_ = false;
        return true;
    }

    FoliageVoxelGeometry geometry =
        buildFoliageVoxelGeometry(pendingInstances_);
    const std::vector<FoliageGpuPrimitive> windborneGeometry =
        buildWindborneParticleVoxelGeometry(pendingWindborneParticles_);
    if ((!pendingInstances_.empty() && geometry.primitives.empty()) ||
        (!pendingWindborneParticles_.empty() && windborneGeometry.empty()) ||
        (geometry.primitives.empty() && windborneGeometry.empty()) ||
        geometry.semanticInstanceCount > std::numeric_limits<uint32_t>::max() ||
        geometry.patchCount > std::numeric_limits<uint32_t>::max() ||
        geometry.primitives.size() + windborneGeometry.size() >
            std::numeric_limits<uint32_t>::max())
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        return false;
    }

    const size_t foliagePrimitiveCount = geometry.primitives.size();
    geometry.primitives.insert(geometry.primitives.end(),
                               windborneGeometry.begin(),
                               windborneGeometry.end());

    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(geometry.primitives.size() *
                                  sizeof(FoliageGpuPrimitive));
    UniqueBuffer newBuffer{};
    if (!createBuffer(context, bytes,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, newBuffer,
                      "foliage_voxel_primitives"))
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        return false;
    }

    UniqueBuffer staging{};
    if (!createBuffer(context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                      staging, "foliage_voxel_primitives_staging"))
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        return false;
    }
    void* mapped = nullptr;
    if (vmaMapMemory(context.memoryAllocator, staging.allocation(), &mapped) !=
        VK_SUCCESS)
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        return false;
    }
    std::memcpy(mapped, geometry.primitives.data(), static_cast<size_t>(bytes));
    const VkResult flushResult =
        vmaFlushAllocation(context.memoryAllocator, staging.allocation(), 0, bytes);
    vmaUnmapMemory(context.memoryAllocator, staging.allocation());
    if (flushResult != VK_SUCCESS)
    {
        semanticInstanceCount_ = 0;
        patchCount_ = 0;
        foliagePrimitiveCount_ = 0;
        windborneParticleCount_ = 0;
        primitiveCount_ = 0;
        return false;
    }

    VkCommandBuffer commandBuffer = context.beginSingleTimeCommands();
    const VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(commandBuffer, staging.get(), newBuffer.get(), 1, &copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = newBuffer.get();
    barrier.size = bytes;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr,
                         1, &barrier, 0, nullptr);
    context.endSingleTimeCommands(commandBuffer);

    primitiveBuffer_ = std::move(newBuffer);
    const VkDescriptorBufferInfo primitiveInfo{primitiveBuffer_.get(), 0, bytes};
    const VkDescriptorBufferInfo paletteInfo{paletteBuffer_, 0, VK_WHOLE_SIZE};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = instanceSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &primitiveInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = instanceSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &paletteInfo;
    vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
    semanticInstanceCount_ = static_cast<uint32_t>(geometry.semanticInstanceCount);
    patchCount_ = static_cast<uint32_t>(geometry.patchCount);
    foliagePrimitiveCount_ = static_cast<uint32_t>(foliagePrimitiveCount);
    windborneParticleCount_ = static_cast<uint32_t>(windborneGeometry.size());
    primitiveCount_ = static_cast<uint32_t>(geometry.primitives.size());
    return true;
}

void FoliagePass::refreshReadyState()
{
    ready_ = resourcesCreated_ && static_cast<bool>(pipeline_) &&
             static_cast<bool>(primitiveBuffer_) && instanceSet_ != VK_NULL_HANDLE &&
             primitiveCount_ > 0u;
}

} // namespace engine::render
