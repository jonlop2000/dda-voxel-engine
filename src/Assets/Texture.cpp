#include "Assets/Texture.h"

#include <cstring>

#include "engine/render/Commands.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"
#include "Resources/GpuImage.h"
#include "ThirdParty/stb_image.h"

bool loadImageRGBA8(const std::filesystem::path& path, bool srgb, CpuImage& out,
                    std::string* error, bool flipVertically)
{
    out = CpuImage{};
    if (!std::filesystem::exists(path))
    {
        if (error != nullptr)
        {
            *error = "Image not found: " + path.string();
        }
        return false;
    }

    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &comp, STBI_rgb_alpha);
    if (data == nullptr)
    {
        if (error != nullptr)
        {
            *error = stbi_failure_reason();
        }
        return false;
    }

    const size_t size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    out.w = w;
    out.h = h;
    out.comp = 4;
    out.srgb = srgb;
    out.rgba.assign(data, data + size);
    stbi_image_free(data);
    return true;
}

CpuImage makeSolidImage(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb)
{
    CpuImage image{};
    image.w = 1;
    image.h = 1;
    image.comp = 4;
    image.srgb = srgb;
    image.rgba = {r, g, b, a};
    return image;
}

void createTexture2D(VulkanContext& ctx, Commands& commands, const CpuImage& image, Texture2D& out)
{
    if (image.rgba.empty() || image.w <= 0 || image.h <= 0)
    {
        die("createTexture2D received an empty CpuImage.");
    }

    const VkFormat format =
        image.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const VkDeviceSize size = static_cast<VkDeviceSize>(image.rgba.size());

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);

    void* data = nullptr;
    if (vkMapMemory(ctx.device, stagingMem, 0, size, 0, &data) != VK_SUCCESS)
    {
        die("vkMapMemory (texture) failed");
    }
    std::memcpy(data, image.rgba.data(), static_cast<size_t>(size));
    vkUnmapMemory(ctx.device, stagingMem);

    createImage(ctx, static_cast<uint32_t>(image.w), static_cast<uint32_t>(image.h), format,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, out.image,
                out.memory);

    transitionImageLayout(ctx, commands, out.image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(ctx, commands, staging, out.image, static_cast<uint32_t>(image.w),
                      static_cast<uint32_t>(image.h));
    transitionImageLayout(ctx, commands, out.image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);

    out.view = createImageView(ctx.device, out.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    out.format = format;
    out.extent = {static_cast<uint32_t>(image.w), static_cast<uint32_t>(image.h)};

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;

    if (vkCreateSampler(ctx.device, &sci, nullptr, &out.sampler) != VK_SUCCESS)
    {
        die("vkCreateSampler (texture) failed");
    }
}

void destroyTexture(VulkanContext& ctx, Texture2D& texture)
{
    if (texture.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(ctx.device, texture.sampler, nullptr);
    }
    if (texture.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(ctx.device, texture.view, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(ctx.device, texture.image, nullptr);
    }
    if (texture.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, texture.memory, nullptr);
    }
    texture = Texture2D{};
}
