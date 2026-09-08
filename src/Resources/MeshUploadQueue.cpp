#include "Resources/MeshUploadQueue.h"

#include <cstring>

#include "engine/render/Commands.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"

void MeshUploadQueue::enqueue(MeshGpu* mesh, std::vector<Vertex>&& vertices,
                              std::vector<uint32_t>&& indices)
{
    if (mesh == nullptr)
    {
        return;
    }
    UploadRequest req{};
    req.mesh = mesh;
    req.vertices = std::move(vertices);
    req.indices = std::move(indices);
    pending_.push_back(std::move(req));
}

void MeshUploadQueue::flush(VulkanContext& ctx, Commands& commands, VkCommandBuffer cmd,
                            uint32_t frameIndex)
{
    (void)commands;
    if (pending_.empty())
    {
        return;
    }

    std::vector<VkBufferMemoryBarrier> barriers{};
    barriers.reserve(pending_.size() * 2);

    for (auto& req : pending_)
    {
        MeshGpu* mesh = req.mesh;
        if (mesh == nullptr)
        {
            continue;
        }

        const VkDeviceSize vboSize = sizeof(Vertex) * req.vertices.size();
        const VkDeviceSize iboSize = sizeof(uint32_t) * req.indices.size();

        if (mesh->vbo != VK_NULL_HANDLE || mesh->ibo != VK_NULL_HANDLE)
        {
            MeshGarbage old{};
            old.vbo = mesh->vbo;
            old.vboMem = mesh->vboMem;
            old.ibo = mesh->ibo;
            old.iboMem = mesh->iboMem;
            old.useIndex = mesh->useIndex;
            garbage_[mesh->lastUsedFrame % kMaxFramesInFlight].push_back(old);
        }

        *mesh = MeshGpu{};

        if (vboSize == 0)
        {
            continue;
        }

        createBuffer(ctx, vboSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->vbo, mesh->vboMem);
        mesh->vertexCount = static_cast<uint32_t>(req.vertices.size());
        mesh->vboCapacityBytes = vboSize;

        StagingBuffer vboStaging{};
        createBuffer(ctx, vboSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vboStaging.buffer, vboStaging.memory);

        void* vboData = nullptr;
        if (vkMapMemory(ctx.device, vboStaging.memory, 0, vboSize, 0, &vboData) != VK_SUCCESS)
        {
            die("vkMapMemory (mesh upload vbo) failed");
        }
        std::memcpy(vboData, req.vertices.data(), static_cast<size_t>(vboSize));
        vkUnmapMemory(ctx.device, vboStaging.memory);

        VkBufferCopy vboCopy{};
        vboCopy.size = vboSize;
        vkCmdCopyBuffer(cmd, vboStaging.buffer, mesh->vbo, 1, &vboCopy);

        staging_[frameIndex].push_back(vboStaging);

        VkBufferMemoryBarrier vboBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        vboBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vboBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vboBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vboBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vboBarrier.buffer = mesh->vbo;
        vboBarrier.offset = 0;
        vboBarrier.size = vboSize;
        barriers.push_back(vboBarrier);

        if (!req.indices.empty())
        {
            createBuffer(ctx, iboSize,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh->ibo, mesh->iboMem);
            mesh->indexCount = static_cast<uint32_t>(req.indices.size());
            mesh->useIndex = true;
            mesh->iboCapacityBytes = iboSize;

            StagingBuffer iboStaging{};
            createBuffer(ctx, iboSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         iboStaging.buffer, iboStaging.memory);

            void* iboData = nullptr;
            if (vkMapMemory(ctx.device, iboStaging.memory, 0, iboSize, 0, &iboData) != VK_SUCCESS)
            {
                die("vkMapMemory (mesh upload ibo) failed");
            }
            std::memcpy(iboData, req.indices.data(), static_cast<size_t>(iboSize));
            vkUnmapMemory(ctx.device, iboStaging.memory);

            VkBufferCopy iboCopy{};
            iboCopy.size = iboSize;
            vkCmdCopyBuffer(cmd, iboStaging.buffer, mesh->ibo, 1, &iboCopy);

            staging_[frameIndex].push_back(iboStaging);

            VkBufferMemoryBarrier iboBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            iboBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            iboBarrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
            iboBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            iboBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            iboBarrier.buffer = mesh->ibo;
            iboBarrier.offset = 0;
            iboBarrier.size = iboSize;
            barriers.push_back(iboBarrier);
        }
    }

    if (!barriers.empty())
    {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(),
                             0, nullptr);
    }

    pending_.clear();
}

void MeshUploadQueue::releaseFrame(VulkanContext& ctx, uint32_t frameIndex)
{
    if (frameIndex >= staging_.size())
    {
        return;
    }

    for (auto& staging : staging_[frameIndex])
    {
        if (staging.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, staging.buffer, nullptr);
        }
        if (staging.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, staging.memory, nullptr);
        }
    }
    staging_[frameIndex].clear();

    for (auto& mesh : garbage_[frameIndex])
    {
        if (mesh.vbo != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, mesh.vbo, nullptr);
        }
        if (mesh.vboMem != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, mesh.vboMem, nullptr);
        }
        if (mesh.useIndex && mesh.ibo != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, mesh.ibo, nullptr);
        }
        if (mesh.useIndex && mesh.iboMem != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, mesh.iboMem, nullptr);
        }
    }
    garbage_[frameIndex].clear();
}

size_t MeshUploadQueue::pendingCount() const
{
    return pending_.size();
}

void MeshUploadQueue::clearPending()
{
    pending_.clear();
}
