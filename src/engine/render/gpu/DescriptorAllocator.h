#pragma once

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

namespace engine::render
{

struct DescriptorPoolSizeKey
{
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLER;
    uint32_t descriptorCount = 0;

    bool operator==(const DescriptorPoolSizeKey& other) const;
};

struct DescriptorPoolKey
{
    VkDescriptorPoolCreateFlags flags = 0;
    uint32_t maxSets = 0;
    std::vector<DescriptorPoolSizeKey> sizes{};

    bool operator==(const DescriptorPoolKey& other) const;
};

struct DescriptorPoolKeyHash
{
    std::size_t operator()(const DescriptorPoolKey& key) const;
};

bool makeDescriptorPoolKey(std::span<const VkDescriptorPoolSize> poolSizes, uint32_t maxSets,
                           VkDescriptorPoolCreateFlags flags, DescriptorPoolKey& outKey);

class DescriptorAllocator
{
public:
    DescriptorAllocator() = default;
    ~DescriptorAllocator() { destroy(); }

    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
    DescriptorAllocator(DescriptorAllocator&&) = delete;
    DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;

    bool allocate(VkDevice device, std::span<const VkDescriptorPoolSize> poolSizes,
                  uint32_t maxSets, const VkDescriptorSetLayout* layouts, uint32_t setCount,
                  VkDescriptorSet* outSets, VkDescriptorPoolCreateFlags flags = 0);
    bool resetPools();
    std::size_t poolCount() const;
    void destroy();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::unordered_map<DescriptorPoolKey, std::vector<UniqueDescriptorPool>,
                       DescriptorPoolKeyHash>
        pools_{};
};

} // namespace engine::render
