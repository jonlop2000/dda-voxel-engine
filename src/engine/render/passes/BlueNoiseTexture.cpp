#include "BlueNoiseTexture.h"

#include "Assets/Texture.h"
#include "Core/Logger.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace
{
float frac(float v)
{
    return v - std::floor(v);
}
} // namespace

bool BlueNoiseTexture::loadFromFile(const std::filesystem::path& path,
                                    std::vector<uint8_t>& data)
{
    if (path.empty())
    {
        return false;
    }

    CpuImage image{};
    std::string err;
    if (!loadImageRGBA8(path, false, image, &err))
    {
        logWarning("Assets", std::string("BlueNoiseTexture: ") + err);
        return false;
    }

    if (image.w != kSize || image.h != kSize)
    {
        logWarning("Assets", std::string("BlueNoiseTexture: expected ") +
                                 std::to_string(kSize) + "x" + std::to_string(kSize) +
                                 " image, got " + std::to_string(image.w) + "x" +
                                 std::to_string(image.h));
        return false;
    }

    data.resize(static_cast<size_t>(kSize * kSize * 2));
    for (int i = 0; i < kSize * kSize; ++i)
    {
        data[i * 2 + 0] = image.rgba[i * 4 + 0];
        data[i * 2 + 1] = image.rgba[i * 4 + 1];
    }

    return true;
}

void BlueNoiseTexture::generateFallbackNoise(uint8_t* data, int width, int height)
{
    // interleaved gradient noise (ign) fallback for two channels.
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float n0 = frac(52.9829189f * frac(0.06711056f * float(x) +
                                              0.00583715f * float(y)));
            float n1 = frac(52.9829189f * frac(0.06711056f * float(y) +
                                              0.00583715f * float(x + 67)));

            const int idx = (y * width + x) * 2;
            data[idx + 0] = static_cast<uint8_t>(n0 * 255.0f);
            data[idx + 1] = static_cast<uint8_t>(n1 * 255.0f);
        }
    }
}

bool BlueNoiseTexture::create(VulkanContext& ctx, const std::filesystem::path& filePath)
{
    const int WIDTH = kSize;
    const int HEIGHT = kSize;

    // load or generate noise data (RG8).
    std::vector<uint8_t> noiseData;
    if (!loadFromFile(filePath, noiseData))
    {
        noiseData.resize(static_cast<size_t>(WIDTH * HEIGHT * 2));
        generateFallbackNoise(noiseData.data(), WIDTH, HEIGHT);
    }

    // create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8_UNORM;
    imageInfo.extent = {WIDTH, HEIGHT, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocationInfo{};
    imageAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(engine::render::createUniqueImage(ctx.memoryAllocator, imageInfo,
                                               imageAllocationInfo, image_,
                                               "blue_noise_texture"));

    // upload via staging buffer
    const VkDeviceSize dataSize = WIDTH * HEIGHT * 2;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocationInfo{};
    stagingAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    stagingAllocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    engine::render::UniqueBuffer stagingBuffer{};
    VK_CHECK(engine::render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo,
                                                stagingAllocationInfo, stagingBuffer,
                                                "blue_noise_texture_staging"));

    void* mapped = nullptr;
    VK_CHECK(vmaMapMemory(ctx.memoryAllocator, stagingBuffer.allocation(), &mapped));
    std::memcpy(mapped, noiseData.data(), static_cast<size_t>(dataSize));
    const VkResult flushResult =
        vmaFlushAllocation(ctx.memoryAllocator, stagingBuffer.allocation(), 0, dataSize);
    vmaUnmapMemory(ctx.memoryAllocator, stagingBuffer.allocation());
    VK_CHECK(flushResult);

    // copy to image
    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {WIDTH, HEIGHT, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer.get(), image_.get(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    ctx.endSingleTimeCommands(cmd);

    stagingBuffer.reset();

    // create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView rawImageView = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(ctx.device, &viewInfo, nullptr, &rawImageView));
    imageView_ = engine::render::UniqueImageView(ctx.device, rawImageView);

    // create sampler (wrapping for tiled access)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // tile across screen
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    VkSampler rawSampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(ctx.device, &samplerInfo, nullptr, &rawSampler));
    sampler_ = engine::render::UniqueSampler(ctx.device, rawSampler);

    ctx.setDebugName(reinterpret_cast<uint64_t>(image_.get()), VK_OBJECT_TYPE_IMAGE,
                     "blue_noise_texture");
    ctx.setDebugName(reinterpret_cast<uint64_t>(imageView_.get()), VK_OBJECT_TYPE_IMAGE_VIEW,
                     "blue_noise_texture_view");
    ctx.setDebugName(reinterpret_cast<uint64_t>(sampler_.get()), VK_OBJECT_TYPE_SAMPLER,
                     "blue_noise_texture_sampler");

    return true;
}

void BlueNoiseTexture::destroy(VulkanContext&)
{
    sampler_.reset();
    imageView_.reset();
    image_.reset();
}
