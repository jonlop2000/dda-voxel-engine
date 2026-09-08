#pragma once

#include <cstddef>
#include <vector>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

namespace engine::render
{

struct DescriptorLayoutBindingKey
{
    uint32_t binding = 0;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    uint32_t descriptorCount = 0;
    VkShaderStageFlags stageFlags = 0;
    std::vector<VkSampler> immutableSamplers{};

    bool operator==(const DescriptorLayoutBindingKey& other) const;
};

struct DescriptorLayoutKey
{
    VkDescriptorSetLayoutCreateFlags flags = 0;
    std::vector<DescriptorLayoutBindingKey> bindings{};

    bool operator==(const DescriptorLayoutKey& other) const;
};

struct DescriptorLayoutKeyHash
{
    std::size_t operator()(const DescriptorLayoutKey& key) const;
};

bool makeDescriptorLayoutKey(const VkDescriptorSetLayoutCreateInfo& createInfo,
                             DescriptorLayoutKey& outKey);

class DescriptorLayoutCache
{
public:
    DescriptorLayoutCache() = default;
    ~DescriptorLayoutCache() { destroy(); }

    DescriptorLayoutCache(const DescriptorLayoutCache&) = delete;
    DescriptorLayoutCache& operator=(const DescriptorLayoutCache&) = delete;
    DescriptorLayoutCache(DescriptorLayoutCache&&) = delete;
    DescriptorLayoutCache& operator=(DescriptorLayoutCache&&) = delete;

    bool get(VkDevice device, const VkDescriptorSetLayoutCreateInfo& createInfo,
             VkDescriptorSetLayout& outLayout);
    VkDescriptorSetLayout find(const VkDescriptorSetLayoutCreateInfo& createInfo) const;
    std::size_t size() const { return layouts_.size(); }
    void destroy();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::unordered_map<DescriptorLayoutKey, UniqueDescriptorSetLayout,
                       DescriptorLayoutKeyHash>
        layouts_{};
};

} // namespace engine::render
