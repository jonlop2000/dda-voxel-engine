#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct VulkanContext;
struct Commands;

struct CpuImage
{
    int w = 0;
    int h = 0;
    int comp = 0;
    bool srgb = false;
    std::vector<uint8_t> rgba{};
};

struct Texture2D
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
};

bool loadImageRGBA8(const std::filesystem::path& path, bool srgb, CpuImage& out,
                    std::string* error, bool flipVertically = true);
CpuImage makeSolidImage(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb);
void createTexture2D(VulkanContext& ctx, Commands& commands, const CpuImage& image, Texture2D& out);
void destroyTexture(VulkanContext& ctx, Texture2D& texture);
