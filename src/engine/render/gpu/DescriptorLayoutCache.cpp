#include "engine/render/gpu/DescriptorLayoutCache.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace engine::render
{

namespace
{

template <typename T>
void hashCombine(std::size_t& seed, const T& value)
{
    seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

} // namespace

bool DescriptorLayoutBindingKey::operator==(
    const DescriptorLayoutBindingKey& other) const
{
    return binding == other.binding && descriptorType == other.descriptorType &&
           descriptorCount == other.descriptorCount && stageFlags == other.stageFlags &&
           immutableSamplers == other.immutableSamplers;
}

bool DescriptorLayoutKey::operator==(const DescriptorLayoutKey& other) const
{
    return flags == other.flags && bindings == other.bindings;
}

std::size_t DescriptorLayoutKeyHash::operator()(const DescriptorLayoutKey& key) const
{
    std::size_t seed = 0;
    hashCombine(seed, static_cast<uint32_t>(key.flags));
    hashCombine(seed, key.bindings.size());
    for (const DescriptorLayoutBindingKey& binding : key.bindings)
    {
        hashCombine(seed, binding.binding);
        hashCombine(seed, static_cast<uint32_t>(binding.descriptorType));
        hashCombine(seed, binding.descriptorCount);
        hashCombine(seed, static_cast<uint32_t>(binding.stageFlags));
        hashCombine(seed, binding.immutableSamplers.size());
        for (VkSampler sampler : binding.immutableSamplers)
        {
            hashCombine(seed, sampler);
        }
    }
    return seed;
}

bool makeDescriptorLayoutKey(const VkDescriptorSetLayoutCreateInfo& createInfo,
                             DescriptorLayoutKey& outKey)
{
    outKey = {};
    if (createInfo.pNext != nullptr)
    {
        return false;
    }
    if (createInfo.bindingCount > 0 && createInfo.pBindings == nullptr)
    {
        return false;
    }

    outKey.flags = createInfo.flags;
    outKey.bindings.reserve(createInfo.bindingCount);
    for (uint32_t i = 0; i < createInfo.bindingCount; ++i)
    {
        const VkDescriptorSetLayoutBinding& source = createInfo.pBindings[i];
        DescriptorLayoutBindingKey binding{};
        binding.binding = source.binding;
        binding.descriptorType = source.descriptorType;
        binding.descriptorCount = source.descriptorCount;
        binding.stageFlags = source.stageFlags;
        if (source.pImmutableSamplers != nullptr)
        {
            binding.immutableSamplers.reserve(source.descriptorCount);
            for (uint32_t samplerIndex = 0; samplerIndex < source.descriptorCount;
                 ++samplerIndex)
            {
                binding.immutableSamplers.push_back(source.pImmutableSamplers[samplerIndex]);
            }
        }
        outKey.bindings.push_back(std::move(binding));
    }

    std::sort(outKey.bindings.begin(), outKey.bindings.end(),
              [](const DescriptorLayoutBindingKey& lhs,
                 const DescriptorLayoutBindingKey& rhs) {
                  if (lhs.binding != rhs.binding)
                  {
                      return lhs.binding < rhs.binding;
                  }
                  if (lhs.descriptorType != rhs.descriptorType)
                  {
                      return lhs.descriptorType < rhs.descriptorType;
                  }
                  if (lhs.descriptorCount != rhs.descriptorCount)
                  {
                      return lhs.descriptorCount < rhs.descriptorCount;
                  }
                  if (lhs.stageFlags != rhs.stageFlags)
                  {
                      return lhs.stageFlags < rhs.stageFlags;
                  }
                  return false;
              });
    return true;
}

bool DescriptorLayoutCache::get(VkDevice device,
                                const VkDescriptorSetLayoutCreateInfo& createInfo,
                                VkDescriptorSetLayout& outLayout)
{
    outLayout = VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE)
    {
        return false;
    }
    if (device_ == VK_NULL_HANDLE)
    {
        device_ = device;
    }
    else if (device_ != device)
    {
        return false;
    }

    DescriptorLayoutKey key{};
    if (!makeDescriptorLayoutKey(createInfo, key))
    {
        return false;
    }

    const auto existing = layouts_.find(key);
    if (existing != layouts_.end())
    {
        outLayout = existing->second.get();
        return true;
    }

    VkDescriptorSetLayout rawLayout = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &rawLayout);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    auto [inserted, _] =
        layouts_.emplace(std::move(key), UniqueDescriptorSetLayout(device_, rawLayout));
    outLayout = inserted->second.get();
    return true;
}

VkDescriptorSetLayout DescriptorLayoutCache::find(
    const VkDescriptorSetLayoutCreateInfo& createInfo) const
{
    DescriptorLayoutKey key{};
    if (!makeDescriptorLayoutKey(createInfo, key))
    {
        return VK_NULL_HANDLE;
    }
    const auto it = layouts_.find(key);
    return it != layouts_.end() ? it->second.get() : VK_NULL_HANDLE;
}

void DescriptorLayoutCache::destroy()
{
    layouts_.clear();
    device_ = VK_NULL_HANDLE;
}

} // namespace engine::render
