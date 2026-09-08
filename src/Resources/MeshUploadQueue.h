#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"
#include "Resources/Mesh.h"

struct VulkanContext;
struct Commands;

class MeshUploadQueue
{
public:
    void enqueue(MeshGpu* mesh, std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices);
    void flush(VulkanContext& ctx, Commands& commands, VkCommandBuffer cmd, uint32_t frameIndex);
    void releaseFrame(VulkanContext& ctx, uint32_t frameIndex);
    size_t pendingCount() const;
    void clearPending();

private:
    struct StagingBuffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct MeshGarbage
    {
        VkBuffer vbo = VK_NULL_HANDLE;
        VkDeviceMemory vboMem = VK_NULL_HANDLE;
        VkBuffer ibo = VK_NULL_HANDLE;
        VkDeviceMemory iboMem = VK_NULL_HANDLE;
        bool useIndex = false;
    };

    struct UploadRequest
    {
        MeshGpu* mesh = nullptr;
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> indices{};
    };

    std::vector<UploadRequest> pending_{};
    std::array<std::vector<StagingBuffer>, kMaxFramesInFlight> staging_{};
    std::array<std::vector<MeshGarbage>, kMaxFramesInFlight> garbage_{};
};
