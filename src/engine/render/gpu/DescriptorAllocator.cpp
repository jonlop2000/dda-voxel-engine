#include "engine/render/gpu/DescriptorAllocator.h"

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

bool isPoolExhaustion(VkResult result)
{
    return result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL;
}

VkResult allocateFromPool(VkDevice device, VkDescriptorPool pool,
                          const VkDescriptorSetLayout* layouts, uint32_t setCount,
                          VkDescriptorSet* outSets)
{
    VkDescriptorSetAllocateInfo allocateInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = pool;
    allocateInfo.descriptorSetCount = setCount;
    allocateInfo.pSetLayouts = layouts;
    return vkAllocateDescriptorSets(device, &allocateInfo, outSets);
}

std::vector<VkDescriptorPoolSize> poolSizesFromKey(const DescriptorPoolKey& key)
{
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(key.sizes.size());
    for (const DescriptorPoolSizeKey& size : key.sizes)
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = size.type;
        poolSize.descriptorCount = size.descriptorCount;
        sizes.push_back(poolSize);
    }
    return sizes;
}

} // namespace

bool DescriptorPoolSizeKey::operator==(const DescriptorPoolSizeKey& other) const
{
    return type == other.type && descriptorCount == other.descriptorCount;
}

bool DescriptorPoolKey::operator==(const DescriptorPoolKey& other) const
{
    return flags == other.flags && maxSets == other.maxSets && sizes == other.sizes;
}

std::size_t DescriptorPoolKeyHash::operator()(const DescriptorPoolKey& key) const
{
    std::size_t seed = 0;
    hashCombine(seed, static_cast<uint32_t>(key.flags));
    hashCombine(seed, key.maxSets);
    hashCombine(seed, key.sizes.size());
    for (const DescriptorPoolSizeKey& size : key.sizes)
    {
        hashCombine(seed, static_cast<uint32_t>(size.type));
        hashCombine(seed, size.descriptorCount);
    }
    return seed;
}

bool makeDescriptorPoolKey(std::span<const VkDescriptorPoolSize> poolSizes, uint32_t maxSets,
                           VkDescriptorPoolCreateFlags flags, DescriptorPoolKey& outKey)
{
    outKey = {};
    if (maxSets == 0 || poolSizes.empty())
    {
        return false;
    }

    outKey.flags = flags;
    outKey.maxSets = maxSets;
    outKey.sizes.reserve(poolSizes.size());

    for (const VkDescriptorPoolSize& source : poolSizes)
    {
        if (source.descriptorCount == 0)
        {
            return false;
        }

        auto existing = std::find_if(outKey.sizes.begin(), outKey.sizes.end(),
                                     [&source](const DescriptorPoolSizeKey& size) {
                                         return size.type == source.type;
                                     });
        if (existing != outKey.sizes.end())
        {
            existing->descriptorCount += source.descriptorCount;
        }
        else
        {
            outKey.sizes.push_back(
                DescriptorPoolSizeKey{source.type, source.descriptorCount});
        }
    }

    std::sort(outKey.sizes.begin(), outKey.sizes.end(),
              [](const DescriptorPoolSizeKey& lhs, const DescriptorPoolSizeKey& rhs) {
                  if (lhs.type != rhs.type)
                  {
                      return lhs.type < rhs.type;
                  }
                  return lhs.descriptorCount < rhs.descriptorCount;
              });
    return true;
}

bool DescriptorAllocator::allocate(VkDevice device,
                                   std::span<const VkDescriptorPoolSize> poolSizes,
                                   uint32_t maxSets,
                                   const VkDescriptorSetLayout* layouts,
                                   uint32_t setCount,
                                   VkDescriptorSet* outSets,
                                   VkDescriptorPoolCreateFlags flags)
{
    if (device == VK_NULL_HANDLE || layouts == nullptr || outSets == nullptr || setCount == 0 ||
        maxSets < setCount)
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

    DescriptorPoolKey key{};
    if (!makeDescriptorPoolKey(poolSizes, maxSets, flags, key))
    {
        return false;
    }

    auto [poolEntry, _] =
        pools_.emplace(std::move(key), std::vector<UniqueDescriptorPool>{});
    std::vector<UniqueDescriptorPool>& poolList = poolEntry->second;

    for (const UniqueDescriptorPool& pool : poolList)
    {
        const VkResult result =
            allocateFromPool(device_, pool.get(), layouts, setCount, outSets);
        if (result == VK_SUCCESS)
        {
            return true;
        }
        if (!isPoolExhaustion(result))
        {
            return false;
        }
    }

    const DescriptorPoolKey& storedKey = poolEntry->first;
    std::vector<VkDescriptorPoolSize> createdPoolSizes = poolSizesFromKey(storedKey);
    VkDescriptorPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    createInfo.flags = storedKey.flags;
    createInfo.maxSets = storedKey.maxSets;
    createInfo.poolSizeCount = static_cast<uint32_t>(createdPoolSizes.size());
    createInfo.pPoolSizes = createdPoolSizes.data();

    VkDescriptorPool rawPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &rawPool) != VK_SUCCESS)
    {
        return false;
    }

    poolList.emplace_back(device_, rawPool);
    const VkResult allocateResult =
        allocateFromPool(device_, rawPool, layouts, setCount, outSets);
    if (allocateResult == VK_SUCCESS)
    {
        return true;
    }

    poolList.pop_back();
    return false;
}

bool DescriptorAllocator::resetPools()
{
    if (device_ == VK_NULL_HANDLE)
    {
        return true;
    }

    for (const auto& [_, poolList] : pools_)
    {
        for (const UniqueDescriptorPool& pool : poolList)
        {
            if (vkResetDescriptorPool(device_, pool.get(), 0) != VK_SUCCESS)
            {
                return false;
            }
        }
    }
    return true;
}

std::size_t DescriptorAllocator::poolCount() const
{
    std::size_t count = 0;
    for (const auto& [_, poolList] : pools_)
    {
        count += poolList.size();
    }
    return count;
}

void DescriptorAllocator::destroy()
{
    pools_.clear();
    device_ = VK_NULL_HANDLE;
}

} // namespace engine::render
