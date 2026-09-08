#pragma once

#include <utility>

#include <vk_mem_alloc.h>

namespace engine::render
{

template <typename Handle, auto Destroy>
class UniqueAllocation
{
public:
    UniqueAllocation() = default;
    UniqueAllocation(VmaAllocator allocator, Handle handle, VmaAllocation allocation)
        : allocator_(allocator), handle_(handle), allocation_(allocation)
    {
    }
    ~UniqueAllocation() { reset(); }

    UniqueAllocation(UniqueAllocation&& other) noexcept { swap(other); }
    UniqueAllocation& operator=(UniqueAllocation&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }

    UniqueAllocation(const UniqueAllocation&) = delete;
    UniqueAllocation& operator=(const UniqueAllocation&) = delete;

    [[nodiscard]] Handle get() const { return handle_; }
    [[nodiscard]] VmaAllocation allocation() const { return allocation_; }
    explicit operator bool() const
    {
        return allocator_ != VK_NULL_HANDLE && handle_ != VK_NULL_HANDLE &&
               allocation_ != VK_NULL_HANDLE;
    }

    void reset()
    {
        if (allocator_ != VK_NULL_HANDLE && handle_ != VK_NULL_HANDLE &&
            allocation_ != VK_NULL_HANDLE)
        {
            Destroy(allocator_, handle_, allocation_);
        }
        allocator_ = VK_NULL_HANDLE;
        handle_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }

private:
    void swap(UniqueAllocation& other) noexcept
    {
        std::swap(allocator_, other.allocator_);
        std::swap(handle_, other.handle_);
        std::swap(allocation_, other.allocation_);
    }

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    Handle handle_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
};

using UniqueBuffer = UniqueAllocation<VkBuffer, vmaDestroyBuffer>;
using UniqueImage = UniqueAllocation<VkImage, vmaDestroyImage>;

inline VkResult createUniqueBuffer(VmaAllocator allocator,
                                   const VkBufferCreateInfo& bufferInfo,
                                   const VmaAllocationCreateInfo& allocationInfo,
                                   UniqueBuffer& out, const char* allocationName = nullptr)
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    const VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
                                            &buffer, &allocation, nullptr);
    if (result == VK_SUCCESS)
    {
        if (allocationName != nullptr)
        {
            vmaSetAllocationName(allocator, allocation, allocationName);
        }
        out = UniqueBuffer(allocator, buffer, allocation);
    }
    return result;
}

inline VkResult createUniqueImage(VmaAllocator allocator, const VkImageCreateInfo& imageInfo,
                                  const VmaAllocationCreateInfo& allocationInfo,
                                  UniqueImage& out, const char* allocationName = nullptr)
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    const VkResult result = vmaCreateImage(allocator, &imageInfo, &allocationInfo,
                                           &image, &allocation, nullptr);
    if (result == VK_SUCCESS)
    {
        if (allocationName != nullptr)
        {
            vmaSetAllocationName(allocator, allocation, allocationName);
        }
        out = UniqueImage(allocator, image, allocation);
    }
    return result;
}

} // namespace engine::render
