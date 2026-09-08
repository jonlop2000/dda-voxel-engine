
#include "engine/render/passes/CompositePass.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "engine/render/passes/BloomPass.h"
#include "engine/render/passes/BlueNoiseTexture.h"
#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/ShadowPass.h"
#include "engine/render/passes/TAAPass.h"
#include "engine/render/passes/WaterVolumePrepass.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Swapchain.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/ShaderModule.h"

namespace
{
struct CompositePushConstants
{
    // camera data for world-space star rendering
    glm::mat4 invViewProj{1.0f};
    // rendering parameters
    int mode = 0;
    int tonemapOn = 1;
    float exposure = 1.0f;
    float highlightRecovery = 0.0f;
    float bloomIntensity = 0.0f;
    int bloomEnabled = 0;
    float vignetteStrength = 0.0f;
    float grainStrength = 0.0f;
    float time = 0.0f;
    float sharpenIntensity = 0.0f;
    float texelSizeX = 0.0f;
    float texelSizeY = 0.0f;
    float fxaaEnabled = 1.0f;
    uint32_t frameIndex = 0;
    float swapchainIsSRGB = 1.0f;
    // procedural star parameters
    int starsEnabled = 0;
    uint32_t starSeed = 123;
    float starDensity = 0.02f;
    float starTwinkleSpeed = 1.0f;
    int applyBloomInComposite = 0;
    int colorGradeEnabled = 0;
    float colorGradeStrength = 0.0f;
    float colorGradeSaturation = 1.0f;
    float colorGradeContrast = 1.0f;
    float colorGradeTemperature = 0.0f;
    int pixelizeEnabled = 0;
    float pixelizeBlockSize = 4.0f;
    float pixelizeStrength = 0.70f;
    float pixelizeEdgeFocus = 0.45f;
    float materialDetailStrength = 0.0f;
    // depth of field (in-composite focus blur). camera position is passed as three
    // scalars (not a vec3/vec4) so the c++ packing and the shader std430 block
    // cannot disagree about 16-byte vector alignment.
    int dofEnabled = 0;
    float dofFocusDistance = 10.0f;
    float dofFocusRange = 6.0f;
    float dofBlurStrength = 0.85f;
    float cameraPosX = 0.0f;
    float cameraPosY = 0.0f;
    float cameraPosZ = 0.0f;
};

constexpr const char* kCompositePassSubsystem = "CompositePass";
constexpr const char* kCompositePassRationale =
    "CompositePass is required because it presents the final frame to the swapchain and samples "
    "the lighting, bloom, TAA, shadow-debug, and blue-noise outputs.";

[[noreturn]] void failCompositePass(std::string_view detail)
{
    logAndExit(kCompositePassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kCompositePassRationale);
}

std::vector<char> loadCompositeShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failCompositePass(std::string("Failed to read ") + std::string(stage) + " shader '" +
                          path + "': " + error);
    }
    return code;
}

VkShaderModule createCompositeShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                                  const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failCompositePass(std::string("Failed to create ") + std::string(stage) +
                          " shader module for '" + path + "': " +
                          (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadCompositeShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadCompositeShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createCompositeShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createCompositeShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

    VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

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

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.logicOpEnable = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

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
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = pipelineLayout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
        VK_SUCCESS)
    {
        failCompositePass("vkCreateGraphicsPipelines failed for the final swapchain composite.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void CompositePass::setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath)
{
    fullscreenVertPath_ = fullscreenVert;
    fragPath_ = fragPath;
}

void CompositePass::setViewMode(int mode)
{
    viewMode_ = mode;
}

void CompositePass::setTonemapEnabled(bool enabled)
{
    tonemapEnabled_ = enabled;
}

void CompositePass::setExposure(float exposure)
{
    exposure_ = std::max(0.01f, exposure);
}

void CompositePass::setHighlightRecovery(float recovery)
{
    highlightRecovery_ = std::clamp(recovery, 0.0f, 1.0f);
}

void CompositePass::setBloomEnabled(bool enabled)
{
    bloomEnabled_ = enabled;
}

void CompositePass::setBloomIntensity(float intensity)
{
    bloomIntensity_ = std::max(0.0f, intensity);
}

void CompositePass::setApplyBloomInComposite(bool enabled)
{
    applyBloomInComposite_ = enabled;
}

void CompositePass::setVignetteStrength(float strength)
{
    vignetteStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

void CompositePass::setGrainStrength(float strength)
{
    grainStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

void CompositePass::setSharpenIntensity(float intensity)
{
    sharpenIntensity_ = std::clamp(intensity, 0.0f, 1.0f);
}

void CompositePass::setFxaaEnabled(bool enabled)
{
    fxaaEnabled_ = enabled;
}

void CompositePass::setColorGradeEnabled(bool enabled)
{
    colorGradeEnabled_ = enabled;
}

void CompositePass::setColorGradeStrength(float strength)
{
    colorGradeStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

void CompositePass::setColorGradeSaturation(float saturation)
{
    colorGradeSaturation_ = std::clamp(saturation, 0.0f, 2.0f);
}

void CompositePass::setColorGradeContrast(float contrast)
{
    colorGradeContrast_ = std::clamp(contrast, 0.25f, 2.0f);
}

void CompositePass::setColorGradeTemperature(float temperature)
{
    colorGradeTemperature_ = std::clamp(temperature, -1.0f, 1.0f);
}

void CompositePass::setPixelizationEnabled(bool enabled)
{
    pixelizationEnabled_ = enabled;
}

void CompositePass::setPixelizationBlockSize(float blockSize)
{
    pixelizationBlockSize_ = std::clamp(blockSize, 1.0f, 32.0f);
}

void CompositePass::setPixelizationStrength(float strength)
{
    pixelizationStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

void CompositePass::setPixelizationEdgeFocus(float edgeFocus)
{
    pixelizationEdgeFocus_ = std::clamp(edgeFocus, 0.0f, 1.0f);
}

void CompositePass::setMaterialDetailStrength(float strength)
{
    materialDetailStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

void CompositePass::setTime(float time)
{
    time_ = time;
}

void CompositePass::setFrameIndex(uint32_t frameIndex)
{
    frameIndex_ = frameIndex;
}

void CompositePass::setStarsEnabled(bool enabled)
{
    starsEnabled_ = enabled;
}

void CompositePass::setStarSeed(uint32_t seed)
{
    starSeed_ = seed;
}

void CompositePass::setStarDensity(float density)
{
    starDensity_ = std::clamp(density, 0.001f, 0.5f);
}

void CompositePass::setStarTwinkleSpeed(float speed)
{
    starTwinkleSpeed_ = std::clamp(speed, 0.0f, 5.0f);
}

void CompositePass::setDofEnabled(bool enabled)
{
    dofEnabled_ = enabled;
}

void CompositePass::setDofFocusDistance(float distance)
{
    dofFocusDistance_ = distance;
}

void CompositePass::setDofFocusRange(float range)
{
    dofFocusRange_ = range;
}

void CompositePass::setDofBlurStrength(float strength)
{
    dofBlurStrength_ = strength;
}

void CompositePass::setCameraPosition(const glm::vec3& position)
{
    cameraPos_ = position;
}

void CompositePass::setSceneAtmosphere(
    const engine::render::SceneAtmosphereSettings& settings)
{
    sceneAtmosphere_ = settings;
}

void CompositePass::setActiveWaterVolumeCount(uint32_t count)
{
    activeWaterVolumeCount_ = count;
}

void CompositePass::setWaterVolumeBuffer(VkBuffer buffer)
{
    waterVolumeBuffer_ = buffer;
}

void CompositePass::setInvViewProj(const glm::mat4& invViewProj)
{
    invViewProj_ = invViewProj;
}

void CompositePass::create(VulkanContext& ctx, const PassCreateInfo& ci, const Swapchain& sc)
{
    swapchain_ = &sc;
    (void)ci;
    if (waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        failCompositePass(
            "A valid water-volume buffer is required for star-atmosphere path ownership.");
    }

    // set 0: array of sampled inputs (lighting + g-buffer).
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = static_cast<uint32_t>(kCompositeSlots);
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 1;
    dlci.pBindings = &b;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dlci, setLayout_))
    {
        failCompositePass("vkCreateDescriptorSetLayout failed for composite sampled inputs.");
    }

    // set 1: transient atmosphere parameters plus the shared shape-aware water
    // volumes used to remove non-air intervals from sky rays.
    VkDescriptorSetLayoutBinding atmosphereBindings[2]{};
    atmosphereBindings[0].binding = 0;
    atmosphereBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    atmosphereBindings[0].descriptorCount = 1;
    atmosphereBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    atmosphereBindings[1].binding = 1;
    atmosphereBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    atmosphereBindings[1].descriptorCount = 1;
    atmosphereBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo atmosphereDlci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    atmosphereDlci.bindingCount = 2;
    atmosphereDlci.pBindings = atmosphereBindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, atmosphereDlci,
                                       atmosphereSetLayout_))
    {
        failCompositePass(
            "vkCreateDescriptorSetLayout failed for composite atmosphere inputs.");
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2;
    VkDescriptorSetLayout setLayouts[] = {setLayout_, atmosphereSetLayout_};
    plci.pSetLayouts = setLayouts;
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(CompositePushConstants);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failCompositePass("vkCreatePipelineLayout failed for the final composite pass.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount =
        kMaxFramesInFlight * static_cast<uint32_t>(kCompositeSlots);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kMaxFramesInFlight;

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, setLayout_);

    descSets_.resize(kMaxFramesInFlight);
    atmosphereSets_.resize(kMaxFramesInFlight);
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    constexpr uint32_t kMaxDescriptorSets = kMaxFramesInFlight * 2;
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          layouts.data(), kMaxFramesInFlight, descSets_.data()))
    {
        failCompositePass("Failed to allocate composite descriptors.");
    }

    std::vector<VkDescriptorSetLayout> atmosphereLayouts(
        kMaxFramesInFlight, atmosphereSetLayout_);
    if (!ctx.descriptorAllocator.allocate(
            ctx.device, poolSizesView, kMaxDescriptorSets, atmosphereLayouts.data(),
            kMaxFramesInFlight, atmosphereSets_.data()))
    {
        failCompositePass("Failed to allocate composite atmosphere descriptors.");
    }

    atmosphereUbos_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < atmosphereUbos_.size(); ++i)
    {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = sizeof(AtmosphereUbo);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VK_CHECK(engine::render::createUniqueBuffer(
            ctx.memoryAllocator, bufferInfo, allocationInfo,
            atmosphereUbos_[i].buffer, "composite_atmosphere_ubo"));

        VmaAllocationInfo mappedInfo{};
        vmaGetAllocationInfo(ctx.memoryAllocator,
                             atmosphereUbos_[i].buffer.allocation(), &mappedInfo);
        atmosphereUbos_[i].mapped = mappedInfo.pMappedData;
        if (atmosphereUbos_[i].mapped == nullptr)
        {
            failCompositePass("Failed to map the composite atmosphere UBO.");
        }

        VkDescriptorBufferInfo atmosphereInfo{};
        atmosphereInfo.buffer = atmosphereUbos_[i].buffer.get();
        atmosphereInfo.offset = 0;
        atmosphereInfo.range = sizeof(AtmosphereUbo);
        VkDescriptorBufferInfo waterInfo{};
        waterInfo.buffer = waterVolumeBuffer_;
        waterInfo.offset = 0;
        waterInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = atmosphereSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &atmosphereInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = atmosphereSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &waterInfo;
        vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);
    }

    createPipeline(ctx);
}

void CompositePass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);

    (void)ctx;
    pipelineLayout_.reset();
    setLayout_ = VK_NULL_HANDLE;
    atmosphereSetLayout_ = VK_NULL_HANDLE;
    descSets_.clear();
    atmosphereSets_.clear();
    atmosphereUbos_.clear();
    swapchain_ = nullptr;
}

void CompositePass::onResize(VulkanContext& ctx, const PassCreateInfo& ci, const Swapchain& sc)
{
    swapchain_ = &sc;
    (void)ci;
    destroyPipeline(ctx);
    createPipeline(ctx);
}

void CompositePass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                         const LightingPass& lighting, const ShadowMap& shadow,
                                         const BloomPass& bloom, const TAAPass& taa,
                                         const BlueNoiseTexture& blueNoise,
                                         const WaterVolumePrepass* waterVolumePrepass)
{
    const size_t count = std::min(descSets_.size(), static_cast<size_t>(kMaxFramesInFlight));
    for (size_t i = 0; i < count; i++)
    {
        VkDescriptorImageInfo infos[kCompositeSlots]{};
        infos[kSceneSlot].sampler = lighting.lit(static_cast<uint32_t>(i)).sampler;
        infos[kSceneSlot].imageView = lighting.lit(static_cast<uint32_t>(i)).view;
        infos[kSceneSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        for (size_t g = 0; g < GBufferPass::kGBufferCount; g++)
        {
            const size_t dst = kGBufferStartSlot + g;
            infos[dst].sampler =
                gbuffer.color(static_cast<uint32_t>(i), static_cast<GBufferPass::Slot>(g)).sampler;
            infos[dst].imageView =
                gbuffer.color(static_cast<uint32_t>(i), static_cast<GBufferPass::Slot>(g)).view;
            infos[dst].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        infos[kShadowSlot].sampler = shadow.debugColorSampler;
        infos[kShadowSlot].imageView = shadow.cascades[0].debugView;
        infos[kShadowSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[kBloomExtractSlot].sampler = bloom.extract(static_cast<uint32_t>(i)).sampler;
        infos[kBloomExtractSlot].imageView = bloom.extract(static_cast<uint32_t>(i)).view.get();
        infos[kBloomExtractSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[kBloomBlurSlot].sampler = bloom.blur(static_cast<uint32_t>(i)).sampler;
        infos[kBloomBlurSlot].imageView = bloom.blur(static_cast<uint32_t>(i)).view.get();
        infos[kBloomBlurSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[kTaaSlot].sampler = taa.sampler();
        infos[kTaaSlot].imageView = taa.outputView();
        infos[kTaaSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[kBlueNoiseSlot].sampler = blueNoise.getSampler();
        infos[kBlueNoiseSlot].imageView = blueNoise.getImageView();
        infos[kBlueNoiseSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[kDepthSlot].sampler = gbuffer.depth(static_cast<uint32_t>(i)).sampler;
        infos[kDepthSlot].imageView = gbuffer.depth(static_cast<uint32_t>(i)).view;
        infos[kDepthSlot].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkImageView legacyWaterDistView =
            waterVolumePrepass != nullptr
                ? waterVolumePrepass->legacyWaterDistView(static_cast<uint32_t>(i))
                : VK_NULL_HANDLE;
        VkSampler legacyWaterDistSampler =
            waterVolumePrepass != nullptr
                ? waterVolumePrepass->legacyWaterDistSampler(static_cast<uint32_t>(i))
                : VK_NULL_HANDLE;
        if (legacyWaterDistView == VK_NULL_HANDLE || legacyWaterDistSampler == VK_NULL_HANDLE)
        {
            legacyWaterDistView =
                gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).view;
            legacyWaterDistSampler =
                gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).sampler;
        }
        infos[kLegacyWaterDistSlot].sampler = legacyWaterDistSampler;
        infos[kLegacyWaterDistSlot].imageView = legacyWaterDistView;
        infos[kLegacyWaterDistSlot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = descSets_[i];
        w.dstBinding = 0;
        w.descriptorCount = static_cast<uint32_t>(kCompositeSlots);
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = infos;

        vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
    }
}

void CompositePass::updateTaaView(VulkanContext& ctx, uint32_t frameIndex, VkImageView taaView,
                                  VkSampler taaSampler)
{
    if (frameIndex >= descSets_.size())
    {
        return;
    }

    VkDescriptorImageInfo info{};
    info.sampler = taaSampler;
    info.imageView = taaView;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = descSets_[frameIndex];
    w.dstBinding = 0;
    w.dstArrayElement = static_cast<uint32_t>(kTaaSlot);
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;

    vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
}

void CompositePass::record(VulkanContext& ctx, const FrameContext& fc)
{
    if (swapchain_ == nullptr)
    {
        return;
    }
    if (fc.frameIndex >= descSets_.size() ||
        fc.frameIndex >= atmosphereSets_.size() ||
        fc.frameIndex >= atmosphereUbos_.size())
    {
        return;
    }

    updateAtmosphereUbo(ctx, fc.frameIndex);
    if (fc.swapImageIndex >= swapchain_->framebuffers.size())
    {
        return;
    }

    VkClearValue swapchainClear{};
    swapchainClear.color = {{0.02f, 0.05f, 0.08f, 1.0f}};

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = swapchain_->renderPass;
    rp.framebuffer = swapchain_->framebuffers[fc.swapImageIndex];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = fc.extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &swapchainClear;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    VkDescriptorSet descriptorSets[] = {descSets_[fc.frameIndex],
                                        atmosphereSets_[fc.frameIndex]};
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_.get(), 0, 2, descriptorSets, 0, nullptr);
    // push view mode + tonemap settings.
    CompositePushConstants pc{};
    pc.invViewProj = invViewProj_;
    pc.mode = viewMode_;
    pc.tonemapOn = tonemapEnabled_ ? 1 : 0;
    pc.exposure = exposure_;
    pc.highlightRecovery = highlightRecovery_;
    pc.bloomIntensity = bloomIntensity_;
    pc.bloomEnabled = bloomEnabled_ ? 1 : 0;
    pc.vignetteStrength = vignetteStrength_;
    pc.grainStrength = grainStrength_;
    pc.time = time_;
    pc.sharpenIntensity = sharpenIntensity_;
    pc.texelSizeX = 1.0f / static_cast<float>(std::max(1u, fc.extent.width));
    pc.texelSizeY = 1.0f / static_cast<float>(std::max(1u, fc.extent.height));
    pc.fxaaEnabled = fxaaEnabled_ ? 1.0f : 0.0f;
    pc.frameIndex = frameIndex_;
    pc.swapchainIsSRGB =
        (swapchain_->imageFormat == VK_FORMAT_B8G8R8A8_SRGB ||
         swapchain_->imageFormat == VK_FORMAT_R8G8B8A8_SRGB)
            ? 1.0f
            : 0.0f;
    pc.starsEnabled = starsEnabled_ ? 1 : 0;
    pc.starSeed = starSeed_;
    pc.starDensity = starDensity_;
    pc.starTwinkleSpeed = starTwinkleSpeed_;
    pc.applyBloomInComposite = applyBloomInComposite_ ? 1 : 0;
    pc.colorGradeEnabled = colorGradeEnabled_ ? 1 : 0;
    pc.colorGradeStrength = colorGradeStrength_;
    pc.colorGradeSaturation = colorGradeSaturation_;
    pc.colorGradeContrast = colorGradeContrast_;
    pc.colorGradeTemperature = colorGradeTemperature_;
    pc.pixelizeEnabled = pixelizationEnabled_ ? 1 : 0;
    pc.pixelizeBlockSize = pixelizationBlockSize_;
    pc.pixelizeStrength = pixelizationStrength_;
    pc.pixelizeEdgeFocus = pixelizationEdgeFocus_;
    pc.materialDetailStrength = materialDetailStrength_;
    pc.dofEnabled = dofEnabled_ ? 1 : 0;
    pc.dofFocusDistance = dofFocusDistance_;
    pc.dofFocusRange = dofFocusRange_;
    pc.dofBlurStrength = dofBlurStrength_;
    pc.cameraPosX = cameraPos_.x;
    pc.cameraPosY = cameraPos_.y;
    pc.cameraPosZ = cameraPos_.z;
    vkCmdPushConstants(fc.cmd, pipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(CompositePushConstants), &pc);

    VkViewport swapVp{};
    swapVp.x = 0.0f;
    swapVp.y = 0.0f;
    swapVp.width = static_cast<float>(fc.extent.width);
    swapVp.height = static_cast<float>(fc.extent.height);
    swapVp.minDepth = 0.0f;
    swapVp.maxDepth = 1.0f;
    VkRect2D swapScissor{{0, 0}, fc.extent};
    vkCmdSetViewport(fc.cmd, 0, 1, &swapVp);
    vkCmdSetScissor(fc.cmd, 0, 1, &swapScissor);
    // full-screen triangle.
    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    // note: render pass is not ended here to allow ImGui rendering
    // the render pass will be ended in app::drawFrame() after ImGui renders
}

void CompositePass::updateAtmosphereUbo(VulkanContext& ctx, uint32_t frameIndex)
{
    PerFrameAtmosphereUbo& frame = atmosphereUbos_[frameIndex];
    const AtmosphereUbo ubo =
        buildAtmosphereUbo(sceneAtmosphere_, activeWaterVolumeCount_);
    std::memcpy(frame.mapped, &ubo, sizeof(ubo));
    VK_CHECK(vmaFlushAllocation(ctx.memoryAllocator, frame.buffer.allocation(), 0,
                                sizeof(ubo)));
}

void CompositePass::createPipeline(VulkanContext& ctx)
{
    if (swapchain_ == nullptr)
    {
        failCompositePass("CompositePass requires a valid swapchain before pipeline creation.");
    }
    pipeline_ = engine::render::UniquePipeline(
        ctx.device, createFullscreenPipeline(ctx, swapchain_->renderPass, pipelineLayout_.get(),
                                             fullscreenVertPath_.c_str(), fragPath_.c_str()));
}

void CompositePass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}
