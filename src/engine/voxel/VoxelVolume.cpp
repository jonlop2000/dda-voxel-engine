#include "engine/voxel/VoxelVolume.h"

#include "Resources/ShaderModule.h"
#include "engine/render/VulkanContext.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine
{

static inline size_t voxelByteCount(const glm::ivec3& d)
{
    return static_cast<size_t>(d.x) * static_cast<size_t>(d.y) * static_cast<size_t>(d.z);
}

static uint32_t calcMipCount(const glm::ivec3& d)
{
    uint32_t maxDim = static_cast<uint32_t>(std::max({d.x, d.y, d.z}));
    uint32_t count = 1;
    while (maxDim > 1)
    {
        maxDim >>= 1;
        ++count;
    }
    return count;
}

static int nextPowerOfTwoAtLeastOne(int value)
{
    if (value <= 1)
    {
        return 1;
    }

    uint32_t v = static_cast<uint32_t>(value - 1);
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return static_cast<int>(v + 1);
}

static glm::ivec3 calcOccupancyBaseDimensions(const glm::ivec3& d)
{
    return {
        nextPowerOfTwoAtLeastOne(d.x),
        nextPowerOfTwoAtLeastOne(d.y),
        nextPowerOfTwoAtLeastOne(d.z),
    };
}

static void computeOccupiedBounds(const uint8_t* data, const glm::ivec3& dims, glm::ivec3& outMin,
                                  glm::ivec3& outMaxExclusive)
{
    outMin = dims;
    outMaxExclusive = glm::ivec3(0);
    bool found = false;

    const int rowStride = dims.x;
    const int sliceStride = dims.x * dims.y;
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            const size_t rowBase = static_cast<size_t>(z * sliceStride + y * rowStride);
            for (int x = 0; x < dims.x; ++x)
            {
                if (data[rowBase + static_cast<size_t>(x)] == 0u)
                {
                    continue;
                }

                found = true;
                outMin = glm::min(outMin, glm::ivec3(x, y, z));
                outMaxExclusive = glm::max(outMaxExclusive, glm::ivec3(x + 1, y + 1, z + 1));
            }
        }
    }

    if (!found)
    {
        outMin = glm::ivec3(0);
        outMaxExclusive = glm::ivec3(0);
    }
}

static void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                            VkImageLayout newLayout, VkPipelineStageFlags srcStage,
                            VkPipelineStageFlags dstStage, VkAccessFlags srcAccess,
                            VkAccessFlags dstAccess, uint32_t baseMipLevel, uint32_t levelCount)
{
    if (oldLayout == newLayout)
    {
        return;
    }

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static bool createHostBuffer(::VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                             VmaAllocationCreateFlags accessFlags, render::UniqueBuffer& buffer,
                             const char* allocationName)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.flags = accessFlags;
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.requiredFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo, allocationInfo, buffer,
                                      allocationName) == VK_SUCCESS;
}

VoxelVolume::VoxelVolume(VoxelVolume&& other) noexcept
{
    *this = std::move(other);
}

VoxelVolume& VoxelVolume::operator=(VoxelVolume&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    m_occMipViews.clear();
    m_occSampler.reset();
    m_occView.reset();
    m_occImage.reset();
    m_sampler.reset();
    m_view.reset();
    m_image.reset();

    m_dimensions = other.m_dimensions;
    m_paletteId = other.m_paletteId;
    m_flags = other.m_flags;
    m_lightingOcclusionMode = other.m_lightingOcclusionMode;
    m_debugName = std::move(other.m_debugName);

    m_worldPosition = other.m_worldPosition;
    m_worldRotation = other.m_worldRotation;
    m_worldScale = other.m_worldScale;
    m_wrapOffset = other.m_wrapOffset;
    m_worldFromLocal = other.m_worldFromLocal;
    m_localFromWorld = other.m_localFromWorld;
    m_worldAabbMin = other.m_worldAabbMin;
    m_worldAabbMax = other.m_worldAabbMax;
    m_occupiedLocalMin = other.m_occupiedLocalMin;
    m_occupiedLocalMaxExclusive = other.m_occupiedLocalMaxExclusive;

    m_image = std::move(other.m_image);
    m_view = std::move(other.m_view);
    m_sampler = std::move(other.m_sampler);
    m_occImage = std::move(other.m_occImage);
    m_occView = std::move(other.m_occView);
    m_occSampler = std::move(other.m_occSampler);
    m_occMipViews = std::move(other.m_occMipViews);
    m_occDimensions = other.m_occDimensions;
    m_occMipCount = other.m_occMipCount;
    m_layout = other.m_layout;
    m_occLayout = other.m_occLayout;

    other.m_occDimensions = glm::ivec3(0);
    other.m_occMipCount = 0;
    other.m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    other.m_occLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return *this;
}

bool VoxelVolume::validateFormatSupport(::VulkanContext& ctx) const
{
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(ctx.gpu, VK_FORMAT_R8_UINT, &props);

    const bool okSampled = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    const bool okTransferDst =
        (props.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
    const bool okTransferSrc =
        (props.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0;
    const bool okStorage = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;

    return okSampled && okTransferDst && okTransferSrc && okStorage;
}

bool VoxelVolume::create(::VulkanContext& ctx, const CreateInfo& info)
{
    if (isValid())
    {
        return false;
    }

    m_dimensions = info.dimensions;
    m_worldPosition = info.worldPosition;
    m_worldScale = info.worldScale;
    m_worldRotation = info.worldRotation;
    m_paletteId = info.paletteId;
    m_flags = info.flags;
    m_lightingOcclusionMode = info.lightingOcclusionMode;
    m_debugName = info.debugName;
    m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_occLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_occupiedLocalMin = glm::ivec3(0);
    m_occupiedLocalMaxExclusive = m_dimensions;

    if (m_dimensions.x <= 0 || m_dimensions.y <= 0 || m_dimensions.z <= 0)
    {
        return false;
    }
    m_occDimensions = calcOccupancyBaseDimensions(m_dimensions);
    m_occMipCount = calcMipCount(m_occDimensions);

    VkPhysicalDeviceProperties gpuProps{};
    vkGetPhysicalDeviceProperties(ctx.gpu, &gpuProps);
    const uint32_t max3D = gpuProps.limits.maxImageDimension3D;
    if (static_cast<uint32_t>(m_dimensions.x) > max3D ||
        static_cast<uint32_t>(m_dimensions.y) > max3D ||
        static_cast<uint32_t>(m_dimensions.z) > max3D)
    {
        return false;
    }

    if (!validateFormatSupport(ctx))
    {
        return false;
    }

    VkImageCreateInfo img{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img.imageType = VK_IMAGE_TYPE_3D;
    img.format = VK_FORMAT_R8_UINT;
    img.extent = {static_cast<uint32_t>(m_dimensions.x), static_cast<uint32_t>(m_dimensions.y),
                  static_cast<uint32_t>(m_dimensions.z)};
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocationInfo{};
    imageAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    imageAllocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const char* imageAllocationName = m_debugName.empty() ? "voxel_volume" : m_debugName.c_str();
    if (render::createUniqueImage(ctx.memoryAllocator, img, imageAllocationInfo, m_image,
                                  imageAllocationName) != VK_SUCCESS)
    {
        return false;
    }

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_image.get();
    view.viewType = VK_IMAGE_VIEW_TYPE_3D;
    view.format = VK_FORMAT_R8_UINT;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;

    VkImageView rawView = VK_NULL_HANDLE;
    if (vkCreateImageView(ctx.device, &view, nullptr, &rawView) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    m_view = render::UniqueImageView(ctx.device, rawView);

    VkSamplerCreateInfo samp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samp.magFilter = VK_FILTER_NEAREST;
    samp.minFilter = VK_FILTER_NEAREST;
    samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.unnormalizedCoordinates = VK_FALSE;
    samp.minLod = 0.0f;
    samp.maxLod = 0.0f;

    VkSampler rawSampler = VK_NULL_HANDLE;
    if (vkCreateSampler(ctx.device, &samp, nullptr, &rawSampler) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    m_sampler = render::UniqueSampler(ctx.device, rawSampler);

    VkImageCreateInfo occImg{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    occImg.imageType = VK_IMAGE_TYPE_3D;
    occImg.format = VK_FORMAT_R8_UINT;
    occImg.extent = {static_cast<uint32_t>(m_occDimensions.x),
                     static_cast<uint32_t>(m_occDimensions.y),
                     static_cast<uint32_t>(m_occDimensions.z)};
    occImg.mipLevels = m_occMipCount;
    occImg.arrayLayers = 1;
    occImg.samples = VK_SAMPLE_COUNT_1_BIT;
    occImg.tiling = VK_IMAGE_TILING_OPTIMAL;
    occImg.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    occImg.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    occImg.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const std::string occupancyAllocationName =
        m_debugName.empty() ? "voxel_volume_occupancy" : m_debugName + "_occ";
    if (render::createUniqueImage(ctx.memoryAllocator, occImg, imageAllocationInfo, m_occImage,
                                  occupancyAllocationName.c_str()) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }

    VkImageViewCreateInfo occView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    occView.image = m_occImage.get();
    occView.viewType = VK_IMAGE_VIEW_TYPE_3D;
    occView.format = VK_FORMAT_R8_UINT;
    occView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    occView.subresourceRange.baseMipLevel = 0;
    occView.subresourceRange.levelCount = m_occMipCount;
    occView.subresourceRange.baseArrayLayer = 0;
    occView.subresourceRange.layerCount = 1;

    VkImageView rawOccupancyView = VK_NULL_HANDLE;
    if (vkCreateImageView(ctx.device, &occView, nullptr, &rawOccupancyView) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    m_occView = render::UniqueImageView(ctx.device, rawOccupancyView);

    m_occMipViews.resize(m_occMipCount);
    for (uint32_t mip = 0; mip < m_occMipCount; ++mip)
    {
        VkImageViewCreateInfo mipView = occView;
        mipView.subresourceRange.baseMipLevel = mip;
        mipView.subresourceRange.levelCount = 1;
        VkImageView rawMipView = VK_NULL_HANDLE;
        if (vkCreateImageView(ctx.device, &mipView, nullptr, &rawMipView) != VK_SUCCESS)
        {
            destroy(ctx);
            return false;
        }
        m_occMipViews[mip] = render::UniqueImageView(ctx.device, rawMipView);
    }

    VkSamplerCreateInfo occSamp = samp;
    occSamp.maxLod = static_cast<float>(m_occMipCount > 0 ? m_occMipCount - 1 : 0);
    VkSampler rawOccupancySampler = VK_NULL_HANDLE;
    if (vkCreateSampler(ctx.device, &occSamp, nullptr, &rawOccupancySampler) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    m_occSampler = render::UniqueSampler(ctx.device, rawOccupancySampler);

    if (!m_debugName.empty())
    {
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_image.get()), VK_OBJECT_TYPE_IMAGE,
                         m_debugName.c_str());
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_view.get()), VK_OBJECT_TYPE_IMAGE_VIEW,
                         (m_debugName + "_view").c_str());
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_sampler.get()), VK_OBJECT_TYPE_SAMPLER,
                         (m_debugName + "_sampler").c_str());
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_occImage.get()), VK_OBJECT_TYPE_IMAGE,
                         (m_debugName + "_occ").c_str());
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_occView.get()), VK_OBJECT_TYPE_IMAGE_VIEW,
                         (m_debugName + "_occ_view").c_str());
        ctx.setDebugName(reinterpret_cast<uint64_t>(m_occSampler.get()), VK_OBJECT_TYPE_SAMPLER,
                         (m_debugName + "_occ_sampler").c_str());
        for (uint32_t mip = 0; mip < m_occMipViews.size(); ++mip)
        {
            ctx.setDebugName(reinterpret_cast<uint64_t>(m_occMipViews[mip].get()),
                             VK_OBJECT_TYPE_IMAGE_VIEW,
                             (m_debugName + "_occ_mip" + std::to_string(mip)).c_str());
        }
    }

    updateTransform(m_worldPosition, m_worldRotation, m_worldScale);
    return true;
}

void VoxelVolume::destroy(::VulkanContext& ctx)
{
    (void)ctx;
    m_occMipViews.clear();
    m_occSampler.reset();
    m_occView.reset();
    m_occImage.reset();
    m_sampler.reset();
    m_view.reset();
    m_image.reset();
    m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_occLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_occDimensions = glm::ivec3(0);
    m_occMipCount = 0;
}

bool VoxelVolume::upload(::VulkanContext& ctx, const uint8_t* data, size_t bytes,
                         VkBuffer paletteBuffer)
{
    if (!isValid() || data == nullptr || paletteBuffer == VK_NULL_HANDLE)
    {
        return false;
    }

    const size_t expected = voxelByteCount(m_dimensions);
    if (bytes != expected)
    {
        return false;
    }

    computeOccupiedBounds(data, m_dimensions, m_occupiedLocalMin, m_occupiedLocalMaxExclusive);

    render::UniqueBuffer staging{};
    if (!createHostBuffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, staging,
                          "voxel_volume_upload_staging"))
    {
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(ctx.memoryAllocator, staging.allocation(), &mapped) != VK_SUCCESS)
    {
        return false;
    }
    std::memcpy(mapped, data, bytes);
    vmaFlushAllocation(ctx.memoryAllocator, staging.allocation(), 0, bytes);
    vmaUnmapMemory(ctx.memoryAllocator, staging.allocation());

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags srcAccess = 0;
    if (m_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        srcAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (m_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
    }
    else if (m_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

    transitionImage(cmd, m_image.get(), m_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, srcStage,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, srcAccess, VK_ACCESS_TRANSFER_WRITE_BIT, 0, 1);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(m_dimensions.x),
                          static_cast<uint32_t>(m_dimensions.y),
                          static_cast<uint32_t>(m_dimensions.z)};

    vkCmdCopyBufferToImage(cmd, staging.get(), m_image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    transitionImage(cmd, m_image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, 0, 1);

    ctx.endSingleTimeCommands(cmd);
    m_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (!rebuildOccupancy(ctx, paletteBuffer))
    {
        return false;
    }

    return true;
}

bool VoxelVolume::upload(::VulkanContext& ctx, const std::vector<uint8_t>& data,
                         VkBuffer paletteBuffer)
{
    return upload(ctx, data.data(), data.size(), paletteBuffer);
}

bool VoxelVolume::rebuildOccupancy(::VulkanContext& ctx, VkBuffer paletteBuffer)
{
    if (!m_occImage || !m_occView || m_occMipViews.empty() ||
        paletteBuffer == VK_NULL_HANDLE)
    {
        return false;
    }
    const bool buildJumpMip =
        m_occMipCount > 1 && (m_flags & FLAG_ALLOW_DENSE_SKIP) != 0u;

    const std::string mip0Path = resolveShaderPath(nullptr, "occ_mip0.comp.spv");
    const std::string downPath = resolveShaderPath(nullptr, "occ_downsample.comp.spv");

    VkShaderModule mip0Module = VK_NULL_HANDLE;
    VkShaderModule downModule = VK_NULL_HANDLE;
    VkShaderModule jumpModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline mip0Pipeline = VK_NULL_HANDLE;
    VkPipeline downPipeline = VK_NULL_HANDLE;
    VkPipeline jumpPipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;

    auto cleanup = [&]() {
        if (pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(ctx.device, pool, nullptr);
        }
        if (mip0Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, mip0Pipeline, nullptr);
        }
        if (downPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, downPipeline, nullptr);
        }
        if (jumpPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, jumpPipeline, nullptr);
        }
        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
        }
        if (setLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(ctx.device, setLayout, nullptr);
        }
        if (mip0Module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(ctx.device, mip0Module, nullptr);
        }
        if (downModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(ctx.device, downModule, nullptr);
        }
        if (jumpModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(ctx.device, jumpModule, nullptr);
        }
    };

    const std::vector<char> mip0Code = readFile(mip0Path.c_str());
    const std::vector<char> downCode = readFile(downPath.c_str());
    mip0Module = createShaderModule(ctx.device, mip0Code);
    downModule = createShaderModule(ctx.device, downCode);
    if (buildJumpMip)
    {
        const std::string jumpPath = resolveShaderPath(nullptr, "occ_jump_mip.comp.spv");
        const std::vector<char> jumpCode = readFile(jumpPath.c_str());
        jumpModule = createShaderModule(ctx.device, jumpCode);
    }

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(sizeof(bindings) / sizeof(bindings[0]));
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
    {
        cleanup();
        return false;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout;
    VkPushConstantRange paletteRange{};
    paletteRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    paletteRange.offset = 0;
    paletteRange.size = sizeof(uint32_t);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &paletteRange;

    if (vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
        VK_SUCCESS)
    {
        cleanup();
        return false;
    }

    VkPipelineShaderStageCreateInfo mip0Stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    mip0Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    mip0Stage.module = mip0Module;
    mip0Stage.pName = "main";

    VkComputePipelineCreateInfo mip0Info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    mip0Info.stage = mip0Stage;
    mip0Info.layout = pipelineLayout;

    if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &mip0Info, nullptr,
                                 &mip0Pipeline) != VK_SUCCESS)
    {
        cleanup();
        return false;
    }

    VkPipelineShaderStageCreateInfo downStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    downStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    downStage.module = downModule;
    downStage.pName = "main";

    VkComputePipelineCreateInfo downInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    downInfo.stage = downStage;
    downInfo.layout = pipelineLayout;

    if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &downInfo, nullptr,
                                 &downPipeline) != VK_SUCCESS)
    {
        cleanup();
        return false;
    }

    if (buildJumpMip)
    {
        VkPipelineShaderStageCreateInfo jumpStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        jumpStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        jumpStage.module = jumpModule;
        jumpStage.pName = "main";

        VkComputePipelineCreateInfo jumpInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        jumpInfo.stage = jumpStage;
        jumpInfo.layout = pipelineLayout;

        if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &jumpInfo, nullptr,
                                     &jumpPipeline) != VK_SUCCESS)
        {
            cleanup();
            return false;
        }
    }

    vkDestroyShaderModule(ctx.device, mip0Module, nullptr);
    vkDestroyShaderModule(ctx.device, downModule, nullptr);
    if (jumpModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(ctx.device, jumpModule, nullptr);
    }
    mip0Module = VK_NULL_HANDLE;
    downModule = VK_NULL_HANDLE;
    jumpModule = VK_NULL_HANDLE;

    const uint32_t hierarchySetCount = m_occMipCount;
    if (hierarchySetCount == 0)
    {
        cleanup();
        return false;
    }
    const uint32_t setCount = hierarchySetCount + (buildJumpMip ? 1u : 0u);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = setCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = setCount;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    // every descriptor set is allocated from the shared layout, so vulkan
    // requires the pool to reserve this binding for every mip set even though
    // only the mip-0 classifier shader statically reads it.
    poolSizes[2].descriptorCount = setCount;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = setCount;

    if (vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
        cleanup();
        return false;
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, setLayout);
    std::vector<VkDescriptorSet> sets(setCount, VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(ctx.device, &allocInfo, sets.data()) != VK_SUCCESS)
    {
        cleanup();
        return false;
    }

    std::vector<VkDescriptorImageInfo> srcInfos(setCount);
    std::vector<VkDescriptorImageInfo> dstInfos(setCount);
    std::vector<VkWriteDescriptorSet> writes(setCount * 2 + 1);

    for (uint32_t mip = 0; mip < hierarchySetCount; ++mip)
    {
        VkDescriptorImageInfo src{};
        if (mip == 0)
        {
            src.sampler = m_sampler.get();
            src.imageView = m_view.get();
            src.imageLayout = m_layout;
        }
        else
        {
            src.sampler = m_occSampler.get();
            src.imageView = m_occMipViews[mip - 1].get();
            src.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        VkDescriptorImageInfo dst{};
        dst.imageView = m_occMipViews[mip].get();
        dst.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        srcInfos[mip] = src;
        dstInfos[mip] = dst;

        VkWriteDescriptorSet srcWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        srcWrite.dstSet = sets[mip];
        srcWrite.dstBinding = 0;
        srcWrite.descriptorCount = 1;
        srcWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        srcWrite.pImageInfo = &srcInfos[mip];

        VkWriteDescriptorSet dstWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        dstWrite.dstSet = sets[mip];
        dstWrite.dstBinding = 1;
        dstWrite.descriptorCount = 1;
        dstWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        dstWrite.pImageInfo = &dstInfos[mip];

        writes[mip * 2] = srcWrite;
        writes[mip * 2 + 1] = dstWrite;
    }

    if (buildJumpMip)
    {
        const uint32_t jumpSetIndex = hierarchySetCount;

        VkDescriptorImageInfo src{};
        src.sampler = m_occSampler.get();
        src.imageView = m_occView.get();
        src.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        srcInfos[jumpSetIndex] = src;

        VkDescriptorImageInfo dst{};
        dst.imageView = m_occMipViews[1].get();
        dst.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        dstInfos[jumpSetIndex] = dst;

        VkWriteDescriptorSet srcWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        srcWrite.dstSet = sets[jumpSetIndex];
        srcWrite.dstBinding = 0;
        srcWrite.descriptorCount = 1;
        srcWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        srcWrite.pImageInfo = &srcInfos[jumpSetIndex];
        writes[jumpSetIndex * 2] = srcWrite;

        VkWriteDescriptorSet dstWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        dstWrite.dstSet = sets[jumpSetIndex];
        dstWrite.dstBinding = 1;
        dstWrite.descriptorCount = 1;
        dstWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        dstWrite.pImageInfo = &dstInfos[jumpSetIndex];
        writes[jumpSetIndex * 2 + 1] = dstWrite;
    }

    VkDescriptorBufferInfo paletteInfo{};
    paletteInfo.buffer = paletteBuffer;
    paletteInfo.offset = 0;
    paletteInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet paletteWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    paletteWrite.dstSet = sets[0];
    paletteWrite.dstBinding = 2;
    paletteWrite.descriptorCount = 1;
    paletteWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    paletteWrite.pBufferInfo = &paletteInfo;
    writes.back() = paletteWrite;

    vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkPipelineStageFlags occSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags occSrcAccess = 0;
    if (m_occLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        occSrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        occSrcAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (m_occLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        occSrcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        occSrcAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }

    transitionImage(cmd, m_occImage.get(), m_occLayout, VK_IMAGE_LAYOUT_GENERAL, occSrcStage,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, occSrcAccess,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 0, m_occMipCount);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mip0Pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &sets[0], 0,
                            nullptr);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(m_paletteId), &m_paletteId);

    auto dimAtMip = [](int base, uint32_t mip) -> uint32_t {
        int v = base >> mip;
        return static_cast<uint32_t>(std::max(1, v));
    };
    auto divUp = [](uint32_t v, uint32_t d) -> uint32_t { return (v + d - 1u) / d; };

    const uint32_t baseX = dimAtMip(m_occDimensions.x, 0);
    const uint32_t baseY = dimAtMip(m_occDimensions.y, 0);
    const uint32_t baseZ = dimAtMip(m_occDimensions.z, 0);
    const uint32_t groupX = divUp(baseX, 8);
    const uint32_t groupY = divUp(baseY, 8);
    const uint32_t groupZ = divUp(baseZ, 8);
    vkCmdDispatch(cmd, groupX, groupY, groupZ);

    VkImageMemoryBarrier mipBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    mipBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    mipBarrier.image = m_occImage.get();
    mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    mipBarrier.subresourceRange.baseArrayLayer = 0;
    mipBarrier.subresourceRange.layerCount = 1;
    mipBarrier.subresourceRange.baseMipLevel = 0;
    mipBarrier.subresourceRange.levelCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &mipBarrier);

    for (uint32_t mip = 1; mip < m_occMipCount; ++mip)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, downPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                                &sets[mip], 0, nullptr);

        const uint32_t mx = dimAtMip(m_occDimensions.x, mip);
        const uint32_t my = dimAtMip(m_occDimensions.y, mip);
        const uint32_t mz = dimAtMip(m_occDimensions.z, mip);
        vkCmdDispatch(cmd, divUp(mx, 8), divUp(my, 8), divUp(mz, 8));

        mipBarrier.subresourceRange.baseMipLevel = mip;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &mipBarrier);
    }

    if (buildJumpMip)
    {
        const uint32_t jumpSetIndex = hierarchySetCount;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, jumpPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
                                &sets[jumpSetIndex], 0, nullptr);

        const uint32_t mx = dimAtMip(m_occDimensions.x, 1);
        const uint32_t my = dimAtMip(m_occDimensions.y, 1);
        const uint32_t mz = dimAtMip(m_occDimensions.z, 1);
        vkCmdDispatch(cmd, divUp(mx, 8), divUp(my, 8), divUp(mz, 8));

        mipBarrier.subresourceRange.baseMipLevel = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &mipBarrier);
    }

    transitionImage(cmd, m_occImage.get(), VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, 0, m_occMipCount);

    ctx.endSingleTimeCommands(cmd);
    m_occLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    cleanup();
    return true;
}

bool VoxelVolume::download(::VulkanContext& ctx, std::vector<uint8_t>& outData) const
{
    if (!isValid())
    {
        return false;
    }
    // download is a temporary transition; layout restored before return.
    // we support any "steady-state" layout (SHADER_READ_ONLY, general, etc.)
    const VkImageLayout prevLayout = m_layout;
    if (prevLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        return false; // image has never been uploaded
    }

    // determine source stage/access based on previous layout
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkAccessFlags srcAccess = 0;
    if (prevLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (prevLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        srcAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }

    const size_t bytes = voxelByteCount(m_dimensions);
    outData.resize(bytes);

    render::UniqueBuffer staging{};
    if (!createHostBuffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, staging,
                          "voxel_volume_download_staging"))
    {
        return false;
    }

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    transitionImage(cmd, m_image.get(), prevLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcStage,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, srcAccess, VK_ACCESS_TRANSFER_READ_BIT, 0, 1);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(m_dimensions.x),
                          static_cast<uint32_t>(m_dimensions.y),
                          static_cast<uint32_t>(m_dimensions.z)};

    vkCmdCopyImageToBuffer(cmd, m_image.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.get(),
                           1, &region);

    transitionImage(cmd, m_image.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, prevLayout,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT, srcAccess, 0, 1);

    ctx.endSingleTimeCommands(cmd);

    void* mapped = nullptr;
    if (vmaMapMemory(ctx.memoryAllocator, staging.allocation(), &mapped) != VK_SUCCESS)
    {
        return false;
    }

    vmaInvalidateAllocation(ctx.memoryAllocator, staging.allocation(), 0, bytes);
    std::memcpy(outData.data(), mapped, bytes);
    vmaUnmapMemory(ctx.memoryAllocator, staging.allocation());

    return true;
}

bool VoxelVolume::fillTestPatternAndUpload(::VulkanContext& ctx, int patternType,
                                           VkBuffer paletteBuffer)
{
    const size_t total = voxelByteCount(m_dimensions);
    std::vector<uint8_t> data(total, 0);

    auto idx3 = [&](int x, int y, int z) -> size_t {
        return static_cast<size_t>(x) +
               static_cast<size_t>(y) * static_cast<size_t>(m_dimensions.x) +
               static_cast<size_t>(z) * static_cast<size_t>(m_dimensions.x) *
                   static_cast<size_t>(m_dimensions.y);
    };

    switch (patternType)
    {
    case 0: // hollow cube shell
    {
        const int wall = 2;
        for (int z = 0; z < m_dimensions.z; ++z)
        {
            for (int y = 0; y < m_dimensions.y; ++y)
            {
                for (int x = 0; x < m_dimensions.x; ++x)
                {
                    const bool isWall = x < wall || x >= m_dimensions.x - wall || y < wall ||
                                        y >= m_dimensions.y - wall || z < wall ||
                                        z >= m_dimensions.z - wall;
                    if (isWall)
                    {
                        data[idx3(x, y, z)] = 1;
                    }
                }
            }
        }
    }
    break;

    case 1: // sphere
    {
        glm::vec3 c = glm::vec3(m_dimensions) * 0.5f;
        float r = std::min({c.x, c.y, c.z}) - 1.0f;
        for (int z = 0; z < m_dimensions.z; ++z)
        {
            for (int y = 0; y < m_dimensions.y; ++y)
            {
                for (int x = 0; x < m_dimensions.x; ++x)
                {
                    glm::vec3 p(x + 0.5f, y + 0.5f, z + 0.5f);
                    if (glm::length(p - c) <= r)
                    {
                        data[idx3(x, y, z)] = 2;
                    }
                }
            }
        }
    }
    break;

    case 2: // layered checker
    {
        for (int z = 0; z < m_dimensions.z; ++z)
        {
            for (int y = 0; y < m_dimensions.y; ++y)
            {
                for (int x = 0; x < m_dimensions.x; ++x)
                {
                    const bool check = ((x / 4) + (y / 4) + (z / 4)) % 2 == 0;
                    if (check)
                    {
                        data[idx3(x, y, z)] = static_cast<uint8_t>((x % 8) + 1);
                    }
                }
            }
        }
    }
    break;

    default:
        std::fill(data.begin(), data.end(), 1);
        break;
    }

    return upload(ctx, data, paletteBuffer);
}

void VoxelVolume::updateTransform(const glm::vec3& position, const glm::quat& rotation,
                                  const glm::vec3& scale)
{
    m_worldPosition = position;
    m_worldRotation = rotation;
    m_worldScale = scale;

    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);

    m_worldFromLocal = T * R * S;
    m_localFromWorld = glm::inverse(m_worldFromLocal);

    computeWorldAabb();
}

void VoxelVolume::computeWorldAabb()
{
    glm::vec3 mn = localMin();
    glm::vec3 mx = localMax();

    glm::vec3 corners[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
    };

    m_worldAabbMin = glm::vec3(FLT_MAX);
    m_worldAabbMax = glm::vec3(-FLT_MAX);

    for (const auto& c : corners)
    {
        glm::vec3 wc = glm::vec3(m_worldFromLocal * glm::vec4(c, 1.0f));
        m_worldAabbMin = glm::min(m_worldAabbMin, wc);
        m_worldAabbMax = glm::max(m_worldAabbMax, wc);
    }
}

} // namespace engine
