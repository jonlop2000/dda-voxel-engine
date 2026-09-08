#pragma once

#include <utility>

#include <vulkan/vulkan.h>

namespace engine::render
{

// move-only raii owner for device-child vulkan handles destroyed by
// vkDestroy<x>(device, handle, allocator). the DestroyFn uses VKAPI_PTR so the
// platform calling convention (e.g. __stdcall on windows) matches the core
// vkDestroy* functions.
//
// this owns simple device-child handles only. VkImage/VkBuffer (which also own
// backing VkDeviceMemory) get dedicated bundled owners under the vma initiative
// and are intentionally not aliased here yet.
template <class T, void(VKAPI_PTR* DestroyFn)(VkDevice, T, const VkAllocationCallbacks*)>
class UniqueHandle
{
public:
    UniqueHandle() = default;
    UniqueHandle(VkDevice device, T handle) : device_(device), handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(UniqueHandle&& other) noexcept { swap(other); }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    T get() const { return handle_; }
    explicit operator bool() const { return handle_ != VK_NULL_HANDLE; }

    [[nodiscard]] T release()
    {
        T released = handle_;
        handle_ = VK_NULL_HANDLE;
        return released;
    }

    void reset()
    {
        if (handle_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
        {
            DestroyFn(device_, handle_, nullptr);
        }
        handle_ = VK_NULL_HANDLE;
    }

private:
    void swap(UniqueHandle& other) noexcept
    {
        std::swap(device_, other.device_);
        std::swap(handle_, other.handle_);
    }

    VkDevice device_ = VK_NULL_HANDLE;
    T handle_ = VK_NULL_HANDLE;
};

using UniqueImageView = UniqueHandle<VkImageView, vkDestroyImageView>;
using UniqueSampler = UniqueHandle<VkSampler, vkDestroySampler>;
using UniquePipeline = UniqueHandle<VkPipeline, vkDestroyPipeline>;
using UniquePipelineLayout = UniqueHandle<VkPipelineLayout, vkDestroyPipelineLayout>;
using UniqueDescriptorSetLayout =
    UniqueHandle<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>;
using UniqueDescriptorPool = UniqueHandle<VkDescriptorPool, vkDestroyDescriptorPool>;
using UniqueRenderPass = UniqueHandle<VkRenderPass, vkDestroyRenderPass>;
using UniqueFramebuffer = UniqueHandle<VkFramebuffer, vkDestroyFramebuffer>;

} // namespace engine::render
