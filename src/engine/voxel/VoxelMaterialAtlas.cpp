#include "engine/voxel/VoxelMaterialAtlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#include "Core/Logger.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "ThirdParty/stb_image.h"

namespace engine
{
namespace
{
constexpr int kTileSize = 32;
constexpr int kTileCount = 6;
constexpr int kAtlasWidth = kTileSize * kTileCount;
constexpr int kAtlasHeight = kTileSize;
constexpr int kAuthoredPatternSize = 16;

struct Rgb
{
    int r;
    int g;
    int b;
};

using AuthoredPattern = std::array<const char*, kAuthoredPatternSize>;

uint32_t hashPixel(int x, int y, uint32_t seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint8_t byteClamp(int value)
{
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

void writePixel(std::vector<uint8_t>& data, int tile, int x, int y, int r, int g, int b)
{
    const int atlasX = tile * kTileSize + x;
    const size_t offset = static_cast<size_t>((y * kAtlasWidth + atlasX) * 4);
    data[offset + 0] = byteClamp(r);
    data[offset + 1] = byteClamp(g);
    data[offset + 2] = byteClamp(b);
    data[offset + 3] = 255;
}

Rgb addValue(Rgb color, int value)
{
    return Rgb{color.r + value, color.g + value, color.b + value};
}

Rgb mixColor(Rgb a, Rgb b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return Rgb{static_cast<int>(std::round(static_cast<float>(a.r) +
                                           static_cast<float>(b.r - a.r) * t)),
               static_cast<int>(std::round(static_cast<float>(a.g) +
                                           static_cast<float>(b.g - a.g) * t)),
               static_cast<int>(std::round(static_cast<float>(a.b) +
                                           static_cast<float>(b.b - a.b) * t))};
}

void writeAuthoredCell(std::vector<uint8_t>& data, int tile, int cellX, int cellY, Rgb color)
{
    const int px = cellX * 2;
    const int py = cellY * 2;
    for (int oy = 0; oy < 2; ++oy)
    {
        for (int ox = 0; ox < 2; ++ox)
        {
            writePixel(data, tile, px + ox, py + oy, color.r, color.g, color.b);
        }
    }
}

template <typename ColorFn>
void paintAuthoredPattern(std::vector<uint8_t>& data, int tile, const AuthoredPattern& pattern,
                          ColorFn colorFor, uint32_t seed, int jitterAmount)
{
    for (int y = 0; y < kAuthoredPatternSize; ++y)
    {
        for (int x = 0; x < kAuthoredPatternSize; ++x)
        {
            const uint32_t h = hashPixel(x, y, seed);
            const int jitter =
                jitterAmount > 0 ? (static_cast<int>(h % uint32_t(jitterAmount * 2 + 1)) -
                                    jitterAmount)
                                 : 0;
            writeAuthoredCell(data, tile, x, y, addValue(colorFor(pattern[y][x]), jitter));
        }
    }
}

Rgb frameColor(char symbol)
{
    switch (symbol)
    {
    case '0': return Rgb{58, 64, 70};
    case '1': return Rgb{82, 90, 98};
    case '3': return Rgb{144, 154, 164};
    case '4': return Rgb{170, 181, 191};
    case '5': return Rgb{196, 206, 216};
    case '2':
    default: return Rgb{118, 128, 138};
    }
}

Rgb woodColor(char symbol)
{
    switch (symbol)
    {
    case '0': return Rgb{62, 39, 25};
    case '1': return Rgb{88, 56, 32};
    case '2': return Rgb{116, 72, 40};
    case '4': return Rgb{164, 104, 56};
    case '5': return Rgb{196, 130, 72};
    case 'k': return Rgb{76, 45, 26};
    case 'K': return Rgb{46, 29, 18};
    case '3':
    default: return Rgb{140, 86, 46};
    }
}

Rgb stoneColor(char symbol)
{
    switch (symbol)
    {
    case 'm': return Rgb{74, 76, 78};
    case '1': return Rgb{94, 96, 98};
    case '3': return Rgb{138, 140, 142};
    case '4': return Rgb{164, 166, 168};
    case '2':
    default: return Rgb{116, 118, 120};
    }
}

Rgb gravelColor(char symbol)
{
    switch (symbol)
    {
    case '1': return Rgb{92, 86, 72};
    case '3': return Rgb{138, 130, 104};
    case '4': return Rgb{164, 156, 128};
    case '5': return Rgb{198, 190, 158};
    case '2':
    default: return Rgb{118, 110, 88};
    }
}

void generateFrameTile(std::vector<uint8_t>& data)
{
    const Rgb base{92, 105, 116};
    const Rgb seam{42, 50, 59};
    const Rgb highlight{154, 171, 184};
    const Rgb wear{186, 198, 207};
    for (int y = 0; y < kTileSize; ++y)
    {
        for (int x = 0; x < kTileSize; ++x)
        {
            const int panelX = x / 16;
            const int panelY = y / 16;
            const int lx = x % 16;
            const int ly = y % 16;
            const uint32_t panelHash = hashPixel(panelX, panelY, 131u);
            const int panelValue = static_cast<int>(panelHash % 25u) - 12;

            Rgb c = addValue(base, panelValue);
            const bool outerSeam = lx == 0 || ly == 0;
            const bool seamLift = lx == 1 || ly == 1;
            const bool lowerShade = lx == 15 || ly == 15;
            if (outerSeam)
            {
                c = addValue(seam, static_cast<int>(panelHash % 9u) - 4);
            }
            else
            {
                if (seamLift) c = mixColor(c, highlight, 0.34f);
                if (lowerShade) c = addValue(c, -24);
                if (ly >= 3 && ly <= 5) c = addValue(c, 8);
                if (ly >= 11 && ly <= 13) c = addValue(c, -8);
            }

            const uint32_t scratchHash = hashPixel(x / 2, y / 2, 137u);
            const bool brightScratch = !outerSeam && ((x + y * 2 + int(panelHash & 7u)) % 31) == 0;
            const bool darkChip = !outerSeam && (scratchHash % 67u) == 0;
            if (brightScratch)
            {
                c = mixColor(c, wear, 0.45f);
            }
            if (darkChip)
            {
                c = addValue(c, -34);
            }

            writePixel(data, 0, x, y, c.r, c.g, c.b);
        }
    }

    for (int y = 5; y < kTileSize; y += 16)
    {
        for (int x = 5; x < kTileSize; x += 16)
        {
            const int v = static_cast<int>(hashPixel(x, y, 139u) % 12u);
            writePixel(data, 0, x, y, highlight.r + v, highlight.g + v, highlight.b + v);
            writePixel(data, 0, x + 1, y, 86 + v, 98 + v, 108 + v);
            writePixel(data, 0, x, y + 1, 72 + v, 82 + v, 92 + v);
            writePixel(data, 0, x + 1, y + 1, seam.r - 6, seam.g - 6, seam.b - 6);
        }
    }

    for (int y = 12; y < kTileSize; y += 16)
    {
        for (int x = 12; x < kTileSize; x += 16)
        {
            writePixel(data, 0, x, y, 118, 132, 144);
            writePixel(data, 0, x + 1, y, 56, 65, 74);
            writePixel(data, 0, x, y + 1, 48, 56, 64);
            writePixel(data, 0, x + 1, y + 1, 34, 40, 48);
        }
    }
}

void generateWoodTile(std::vector<uint8_t>& data)
{
    for (int y = 0; y < kTileSize; ++y)
    {
        for (int x = 0; x < kTileSize; ++x)
        {
            const float wave = std::sin((static_cast<float>(y) + std::sin(x * 0.45f) * 2.0f) *
                                        0.70f);
            const int band = static_cast<int>(wave * 22.0f);
            const int fiber = static_cast<int>(hashPixel(x, y / 2, 29u) % 17u) - 8;
            const int streak = ((x + y * 2) % 11 == 0) ? -28 : 0;
            Rgb c{126 + band + fiber + streak, 76 + band / 2 + fiber,
                  40 + band / 3 + fiber / 2};

            const int dx = x - 22;
            const int dy = y - 11;
            const int ring = std::abs(dx * dx + dy * dy - 42);
            if (ring < 18)
            {
                c = Rgb{76 + ring / 2, 43 + ring / 3, 24 + ring / 4};
            }
            if (dx * dx + dy * dy < 13)
            {
                c = Rgb{43, 27, 18};
            }

            const int dx2 = x - 7;
            const int dy2 = y - 24;
            if (std::abs(dx2 * dx2 + dy2 * dy2 - 24) < 11)
            {
                c = Rgb{92, 55, 31};
            }

            writePixel(data, 1, x, y, c.r, c.g, c.b);
        }
    }
}

void generateStoneTile(std::vector<uint8_t>& data)
{
    for (int y = 0; y < kTileSize; ++y)
    {
        const int course = y / 8;
        const int courseY = y % 8;
        const int offset = (course & 1) != 0 ? 6 : 0;
        for (int x = 0; x < kTileSize; ++x)
        {
            const int localX = (x + offset) % 12;
            const int blockX = (x + offset) / 12;
            const bool mortar = courseY == 0 || localX == 0 || localX == 11;
            const uint32_t h = hashPixel(blockX, course, 173u);
            const int value = static_cast<int>(h % 45u) - 18;
            const float warmth = static_cast<float>((h >> 5u) % 5u) / 4.0f;
            Rgb block = mixColor(Rgb{106 + value, 111 + value, 112 + value},
                                 Rgb{128 + value, 122 + value, 111 + value / 2}, warmth * 0.38f);

            if (mortar)
            {
                const int mv = static_cast<int>(hashPixel(x / 2, y / 2, 179u) % 9u) - 4;
                writePixel(data, 2, x, y, 54 + mv, 56 + mv, 56 + mv);
                continue;
            }

            if (courseY == 1 || localX == 1) block = addValue(block, 22);
            if (courseY >= 6 || localX >= 9) block = addValue(block, -18);

            const bool crack = ((h & 3u) == 0u && courseY == 4 && localX >= 3 && localX <= 7) ||
                               ((h & 7u) == 5u && localX == 6 && courseY >= 2 && courseY <= 5);
            const bool chip = hashPixel(x, y, 181u) % 53u == 0u;
            if (crack)
            {
                block = addValue(block, -46);
            }
            if (chip)
            {
                block = addValue(block, ((h & 16u) != 0u) ? 26 : -28);
            }

            writePixel(data, 2, x, y, block.r, block.g, block.b);
        }
    }
}

void generateGravelTile(std::vector<uint8_t>& data)
{
    const std::array<Rgb, 7> pebbleColors = {
        Rgb{91, 83, 65},  Rgb{111, 101, 78}, Rgb{132, 121, 92}, Rgb{151, 141, 110},
        Rgb{171, 163, 132}, Rgb{196, 188, 154}, Rgb{118, 113, 96}};

    for (int y = 0; y < kTileSize; ++y)
    {
        for (int x = 0; x < kTileSize; ++x)
        {
            const int gx = x / 8;
            const int gy = y / 8;
            int bestDist = 1 << 30;
            int secondDist = 1 << 30;
            int bestCx = gx;
            int bestCy = gy;
            int bestCenterX = x;
            int bestCenterY = y;

            for (int cy = gy - 1; cy <= gy + 1; ++cy)
            {
                for (int cx = gx - 1; cx <= gx + 1; ++cx)
                {
                    const int wrappedCx = (cx % 4 + 4) % 4;
                    const int wrappedCy = (cy % 4 + 4) % 4;
                    const uint32_t h = hashPixel(wrappedCx, wrappedCy, 211u);
                    const int centerX = cx * 8 + 2 + static_cast<int>(h % 5u);
                    const int centerY = cy * 8 + 2 + static_cast<int>((h >> 4u) % 5u);
                    const int dx = x - centerX;
                    const int dy = y - centerY;
                    const int dist = dx * dx + dy * dy;
                    if (dist < bestDist)
                    {
                        secondDist = bestDist;
                        bestDist = dist;
                        bestCx = wrappedCx;
                        bestCy = wrappedCy;
                        bestCenterX = centerX;
                        bestCenterY = centerY;
                    }
                    else if (dist < secondDist)
                    {
                        secondDist = dist;
                    }
                }
            }

            const uint32_t pebbleHash = hashPixel(bestCx, bestCy, 223u);
            const int radius = 13 + static_cast<int>(pebbleHash % 14u);
            const bool gap = (secondDist - bestDist) < 7 || bestDist > radius;
            Rgb c{66, 60, 48};
            if (!gap)
            {
                c = pebbleColors[pebbleHash % pebbleColors.size()];
                const int fine = static_cast<int>(hashPixel(x, y, 227u) % 9u) - 4;
                const int shade = (bestCenterY - y) * 4 + (bestCenterX - x) * 2;
                c = addValue(c, fine + std::clamp(shade, -24, 26));
                if (bestDist > radius - 7)
                {
                    c = addValue(c, -24);
                }
                if (x <= bestCenterX && y <= bestCenterY && bestDist < radius / 2)
                {
                    c = addValue(c, 18);
                }
                if ((pebbleHash % 11u) == 0u && bestDist < 5)
                {
                    c = mixColor(c, Rgb{218, 211, 178}, 0.62f);
                }
            }
            else
            {
                const int gapNoise = static_cast<int>(hashPixel(x, y, 229u) % 9u) - 4;
                c = addValue(c, gapNoise);
            }

            writePixel(data, 3, x, y, c.r, c.g, c.b);
        }
    }
}

void generatePlantTile(std::vector<uint8_t>& data)
{
    for (int y = 0; y < kTileSize; ++y)
    {
        for (int x = 0; x < kTileSize; ++x)
        {
            const float veinWave = std::sin((static_cast<float>(x) * 0.55f) +
                                            std::sin(static_cast<float>(y) * 0.35f));
            const int fiber = static_cast<int>(hashPixel(x / 2, y, 83u) % 15u) - 7;
            const bool stem = (x % 8 == 3) || (x % 8 == 4);
            const bool vein = std::abs((x % 8) - 4) == (y % 4);
            int r = 42 + fiber;
            int g = 126 + static_cast<int>(veinWave * 20.0f) + fiber;
            int b = 92 + fiber / 2;
            if (stem)
            {
                r -= 10;
                g -= 28;
                b -= 12;
            }
            if (vein)
            {
                r += 18;
                g += 42;
                b += 28;
            }
            if (((x + y * 5) % 31) == 0)
            {
                g += 28;
                b += 16;
            }
            writePixel(data, 4, x, y, r, g, b);
        }
    }
}

void generateCoralTile(std::vector<uint8_t>& data)
{
    for (int y = 0; y < kTileSize; ++y)
    {
        for (int x = 0; x < kTileSize; ++x)
        {
            const uint32_t h = hashPixel(x / 2, y / 2, 97u);
            const int pore = static_cast<int>(h % 7u);
            const int fine = static_cast<int>(hashPixel(x, y, 101u) % 17u) - 8;
            int r = 70 + fine;
            int g = 160 + fine;
            int b = 158 + fine;
            if (pore == 0)
            {
                r -= 36;
                g -= 46;
                b -= 44;
            }
            if (pore == 3)
            {
                r += 32;
                g += 24;
                b += 18;
            }
            if (((x - 16) * (x - 16) + (y - 15) * (y - 15)) < 52)
            {
                r += 32;
                g += 24;
                b += 22;
            }
            if (((x + y) % 13) == 0)
            {
                r += 44;
                g += 34;
                b += 28;
            }
            writePixel(data, 5, x, y, r, g, b);
        }
    }
}

std::vector<uint8_t> generateAtlasData()
{
    std::vector<uint8_t> data(static_cast<size_t>(kAtlasWidth * kAtlasHeight * 4), 255);
    generateFrameTile(data);
    generateWoodTile(data);
    generateStoneTile(data);
    generateGravelTile(data);
    generatePlantTile(data);
    generateCoralTile(data);
    return data;
}

bool loadAtlasDataFromFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
{
    data.clear();
    if (path.empty() || !std::filesystem::exists(path))
    {
        return false;
    }

    stbi_set_flip_vertically_on_load(0);

    int width = 0;
    int height = 0;
    int components = 0;
    unsigned char* pixels =
        stbi_load(path.string().c_str(), &width, &height, &components, STBI_rgb_alpha);
    if (pixels == nullptr)
    {
        logWarning("VoxelMaterialAtlas",
                   std::string("Failed to load material atlas asset '") + path.string() +
                       "': " + stbi_failure_reason());
        return false;
    }

    if (width != kAtlasWidth || height != kAtlasHeight)
    {
        logWarning("VoxelMaterialAtlas",
                   std::string("Ignoring material atlas asset '") + path.string() +
                       "': expected " + std::to_string(kAtlasWidth) + "x" +
                       std::to_string(kAtlasHeight) + ", got " + std::to_string(width) + "x" +
                       std::to_string(height));
        stbi_image_free(pixels);
        return false;
    }

    const size_t byteCount = static_cast<size_t>(kAtlasWidth * kAtlasHeight * 4);
    data.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    return true;
}

bool createImage2D(VulkanContext& ctx, engine::render::UniqueImage& image)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {kAtlasWidth, kAtlasHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    return engine::render::createUniqueImage(ctx.memoryAllocator, imageInfo, allocationInfo,
                                              image, "voxel_material_atlas") == VK_SUCCESS;
}
} // namespace

bool VoxelMaterialAtlas::create(VulkanContext& ctx, const std::filesystem::path& assetPath)
{
    if (isValid())
    {
        return true;
    }

    std::vector<uint8_t> atlasData;
    if (loadAtlasDataFromFile(assetPath, atlasData))
    {
        logInfo("VoxelMaterialAtlas",
                std::string("Loaded editable material atlas: ") + assetPath.string());
    }
    else
    {
        atlasData = generateAtlasData();
        if (!assetPath.empty())
        {
            logWarning("VoxelMaterialAtlas",
                       std::string("Using generated material atlas fallback; editable asset "
                                   "was unavailable or invalid: ") +
                           assetPath.string());
        }
    }

    if (!createImage2D(ctx, image_))
    {
        logError("VoxelMaterialAtlas", "Failed to create material atlas image.");
        return false;
    }

    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(atlasData.size());

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocationInfo{};
    stagingAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    stagingAllocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    engine::render::UniqueBuffer stagingBuffer{};
    if (engine::render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo,
                                            stagingAllocationInfo, stagingBuffer,
                                            "voxel_material_atlas_staging") != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(ctx.memoryAllocator, stagingBuffer.allocation(), &mapped) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    std::memcpy(mapped, atlasData.data(), atlasData.size());
    const VkResult flushResult =
        vmaFlushAllocation(ctx.memoryAllocator, stagingBuffer.allocation(), 0, dataSize);
    vmaUnmapMemory(ctx.memoryAllocator, stagingBuffer.allocation());
    if (flushResult != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }

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
    region.imageExtent = {kAtlasWidth, kAtlasHeight, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer.get(), image_.get(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    ctx.endSingleTimeCommands(cmd);

    stagingBuffer.reset();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView rawImageView = VK_NULL_HANDLE;
    if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &rawImageView) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    imageView_ = engine::render::UniqueImageView(ctx.device, rawImageView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    VkSampler rawSampler = VK_NULL_HANDLE;
    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &rawSampler) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    sampler_ = engine::render::UniqueSampler(ctx.device, rawSampler);

    ctx.setDebugName(reinterpret_cast<uint64_t>(image_.get()), VK_OBJECT_TYPE_IMAGE,
                     "voxel_material_atlas");
    ctx.setDebugName(reinterpret_cast<uint64_t>(imageView_.get()), VK_OBJECT_TYPE_IMAGE_VIEW,
                     "voxel_material_atlas_view");
    ctx.setDebugName(reinterpret_cast<uint64_t>(sampler_.get()), VK_OBJECT_TYPE_SAMPLER,
                     "voxel_material_atlas_nearest_sampler");

    return true;
}

void VoxelMaterialAtlas::destroy(VulkanContext&)
{
    sampler_.reset();
    imageView_.reset();
    image_.reset();
}

} // namespace engine
