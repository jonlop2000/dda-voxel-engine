#include "engine/render/passes/LocalLightShadowBuffer.h"

#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

bool LocalLightShadowBuffer::create(VulkanContext& ctx, uint32_t width, uint32_t height)
{
    width_ = width;
    height_ = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(engine::render::createUniqueImage(ctx.memoryAllocator, imageInfo, allocationInfo,
                                               currentImage_, "local_shadow_current"));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = currentImage_.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView rawCurrentView = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(ctx.device, &viewInfo, nullptr, &rawCurrentView));
    currentImageView_ = engine::render::UniqueImageView(ctx.device, rawCurrentView);
    VkImageView rawCurrentStorageView = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(ctx.device, &viewInfo, nullptr, &rawCurrentStorageView));
    currentStorageView_ = engine::render::UniqueImageView(ctx.device, rawCurrentStorageView);

    // create blurred image (same format and size)
    VK_CHECK(engine::render::createUniqueImage(ctx.memoryAllocator, imageInfo, allocationInfo,
                                               blurredImage_, "local_shadow_blurred"));

    VkImageViewCreateInfo blurredViewInfo = viewInfo;
    blurredViewInfo.image = blurredImage_.get();
    VkImageView rawBlurredView = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(ctx.device, &blurredViewInfo, nullptr, &rawBlurredView));
    blurredImageView_ = engine::render::UniqueImageView(ctx.device, rawBlurredView);
    VkImageView rawBlurredStorageView = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(ctx.device, &blurredViewInfo, nullptr, &rawBlurredStorageView));
    blurredStorageView_ = engine::render::UniqueImageView(ctx.device, rawBlurredStorageView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkSampler rawSampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(ctx.device, &samplerInfo, nullptr, &rawSampler));
    sampler_ = engine::render::UniqueSampler(ctx.device, rawSampler);

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = currentImage_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    VkImageMemoryBarrier blurredBarrier = barrier;
    blurredBarrier.image = blurredImage_.get();

    VkImageMemoryBarrier barriers[2] = {barrier, blurredBarrier};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         barriers);

    ctx.endSingleTimeCommands(cmd);
    return true;
}

void LocalLightShadowBuffer::destroy(VulkanContext&)
{
    sampler_.reset();
    currentStorageView_.reset();
    currentImageView_.reset();
    currentImage_.reset();
    blurredStorageView_.reset();
    blurredImageView_.reset();
    blurredImage_.reset();
    width_ = 0;
    height_ = 0;
}

void LocalLightShadowBuffer::resize(VulkanContext& ctx, uint32_t width, uint32_t height)
{
    if (width == width_ && height == height_)
    {
        return;
    }
    destroy(ctx);
    create(ctx, width, height);
}
