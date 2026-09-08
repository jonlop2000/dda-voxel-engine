#define VMA_IMPLEMENTATION
#include "engine/render/VulkanContext.h"

#include <cstring>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "Core/Logger.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/Utils.h"

#if !defined(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

static bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name)
{
    for (const auto& layer : layers)
    {
        if (std::strcmp(layer.layerName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool deviceSupportsExtensions(VkPhysicalDevice gpu, const std::vector<const char*>& required)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, available.data());

    for (const char* ext : required)
    {
        bool found = false;
        for (const auto& avail : available)
        {
            if (std::strcmp(avail.extensionName, ext) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }

    return true;
}

static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
    QueueFamilyIndices out{};

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props.data());

    for (uint32_t i = 0; i < count; i++)
    {
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            out.graphics = i;
        }

        VkBool32 supportsPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supportsPresent);
        if (supportsPresent != VK_FALSE)
        {
            out.present = i;
        }

        if (out.complete())
        {
            break;
        }
    }

    return out;
}

SwapchainSupportDetails VulkanContext::querySwapchainSupport(VkPhysicalDevice device) const
{
    SwapchainSupportDetails details{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount, nullptr);
    if (presentCount != 0)
    {
        details.presentModes.resize(presentCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentCount,
                                                  details.presentModes.data());
    }

    return details;
}

static VkPhysicalDevice pickPhysicalDevice(VulkanContext& ctx,
                                           const std::vector<const char*>& deviceExts)
{
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &gpuCount, nullptr);
    if (gpuCount == 0)
    {
        die("No Vulkan GPUs found.");
    }

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(ctx.instance, &gpuCount, gpus.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;

    for (auto gpu : gpus)
    {
        QueueFamilyIndices q = findQueueFamilies(gpu, ctx.surface);
        if (!q.complete())
        {
            continue;
        }

        if (!deviceSupportsExtensions(gpu, deviceExts))
        {
            continue;
        }

        SwapchainSupportDetails swapchainSupport = ctx.querySwapchainSupport(gpu);
        if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
        {
            continue;
        }

        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(gpu, &p);

        int score = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score += 1000;
        }
        score += static_cast<int>(p.limits.maxImageDimension2D);

        if (score > bestScore)
        {
            bestScore = score;
            best = gpu;
        }
    }

    if (best == VK_NULL_HANDLE)
    {
        die("No suitable GPU found (needs graphics + present + swapchain).");
    }

    return best;
}

void VulkanContext::create(GLFWwindow* window)
{
    uint32_t extCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&extCount);
    if (glfwExt == nullptr || extCount == 0)
    {
        die("glfwGetRequiredInstanceExtensions failed");
    }

    std::vector<const char*> instanceExts(glfwExt, glfwExt + extCount);

    if constexpr (kUseMoltenVK)
    {
        instanceExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

    if (kEnableValidation)
    {
        instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (kEnableValidation)
    {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());

        if (hasLayer(available, kValidationLayerName))
        {
            layers.push_back(kValidationLayerName);
        }
        else
        {
            logWarning("Vulkan", "Validation layer not found; continuing without it.");
        }
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Voxel Aquarium";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "VoxelEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    ci.ppEnabledExtensionNames = instanceExts.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    if constexpr (kUseMoltenVK)
    {
        ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
    {
        die("vkCreateInstance failed");
    }

    const VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (surfaceResult != VK_SUCCESS)
    {
        const char* glfwDescription = nullptr;
        const int glfwError = glfwGetError(&glfwDescription);
        std::string message = "glfwCreateWindowSurface failed with VkResult " +
                              std::to_string(static_cast<int>(surfaceResult));
        if (glfwError != GLFW_NO_ERROR && glfwDescription != nullptr)
        {
            message += " (GLFW " + std::to_string(glfwError) + ": " + glfwDescription + ")";
        }
        message += "; enabled instance extensions:";
        for (const char* extension : instanceExts)
        {
            message += " ";
            message += extension;
        }
        logAndExit("Vulkan", message);
    }

    std::vector<const char*> deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if constexpr (kUseMoltenVK)
    {
        deviceExts.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    gpu = pickPhysicalDevice(*this, deviceExts);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu, &props);
    logInfo("Vulkan", std::string("Selected GPU: ") + props.deviceName);

    queueFamilies = findQueueFamilies(gpu, surface);

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(gpu, &supportedFeatures);
    if (!supportedFeatures.fragmentStoresAndAtomics)
    {
        die("fragmentStoresAndAtomics not supported (required for metrics buffer writes).");
    }

    VkPhysicalDeviceFeatures enabledFeatures{};
    enabledFeatures.fragmentStoresAndAtomics = VK_TRUE;

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCIs;

    {
        std::vector<uint32_t> uniqueFamilies;
        uniqueFamilies.push_back(queueFamilies.graphics.value());
        if (queueFamilies.present.value() != queueFamilies.graphics.value())
        {
            uniqueFamilies.push_back(queueFamilies.present.value());
        }

        for (uint32_t fam : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qci.queueFamilyIndex = fam;
            qci.queueCount = 1;
            qci.pQueuePriorities = &queuePriority;
            queueCIs.push_back(qci);
        }
    }

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = static_cast<uint32_t>(queueCIs.size());
    dci.pQueueCreateInfos = queueCIs.data();
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
    dci.ppEnabledExtensionNames = deviceExts.data();
    dci.pEnabledFeatures = &enabledFeatures;

    if (vkCreateDevice(gpu, &dci, nullptr, &device) != VK_SUCCESS)
    {
        die("vkCreateDevice failed");
    }

    vkGetDeviceQueue(device, queueFamilies.graphics.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, queueFamilies.present.value(), 0, &presentQueue);

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = gpu;
    allocatorInfo.device = device;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&allocatorInfo, &memoryAllocator) != VK_SUCCESS)
    {
        die("vmaCreateAllocator failed");
    }
}

void VulkanContext::destroy()
{
    if (device != VK_NULL_HANDLE)
    {
        descriptorAllocator.destroy();
        descriptorLayoutCache.destroy();
        samplerCache.destroy();
        if (singleTimePool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, singleTimePool_, nullptr);
            singleTimePool_ = VK_NULL_HANDLE;
        }
        if (memoryAllocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(memoryAllocator);
            memoryAllocator = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

bool VulkanContext::tryBeginSingleTimeCommands(VkCommandBuffer* outCmd, std::string* outError)
{
    if (outError != nullptr)
    {
        outError->clear();
    }
    if (outCmd == nullptr)
    {
        if (outError != nullptr)
        {
            *outError = "Output command buffer pointer is null.";
        }
        return false;
    }

    *outCmd = VK_NULL_HANDLE;

    if (singleTimePool_ == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = queueFamilies.graphics.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &singleTimePool_) != VK_SUCCESS)
        {
            if (outError != nullptr)
            {
                *outError = "Failed to create single-time command pool.";
            }
            return false;
        }
    }

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = singleTimePool_;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, outCmd) != VK_SUCCESS)
    {
        if (outError != nullptr)
        {
            *outError = "Failed to allocate single-time command buffer.";
        }
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(*outCmd, &beginInfo) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, singleTimePool_, 1, outCmd);
        *outCmd = VK_NULL_HANDLE;
        if (outError != nullptr)
        {
            *outError = "Failed to begin single-time command buffer.";
        }
        return false;
    }

    return true;
}

bool VulkanContext::tryEndSingleTimeCommands(VkCommandBuffer cmd, std::string* outError)
{
    if (outError != nullptr)
    {
        outError->clear();
    }
    if (cmd == VK_NULL_HANDLE)
    {
        if (outError != nullptr)
        {
            *outError = "Single-time command buffer is null.";
        }
        return false;
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, singleTimePool_, 1, &cmd);
        if (outError != nullptr)
        {
            *outError = "Failed to end single-time command buffer.";
        }
        return false;
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, singleTimePool_, 1, &cmd);
        if (outError != nullptr)
        {
            *outError = "Failed to create single-time submit fence.";
        }
        return false;
    }

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
    {
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, singleTimePool_, 1, &cmd);
        if (outError != nullptr)
        {
            *outError = "Failed to submit single-time command buffer.";
        }
        return false;
    }

    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, singleTimePool_, 1, &cmd);
        if (outError != nullptr)
        {
            *outError = "Failed to wait for the single-time command fence.";
        }
        return false;
    }

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, singleTimePool_, 1, &cmd);
    return true;
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands()
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::string error;
    if (!tryBeginSingleTimeCommands(&cmd, &error))
    {
        die(error.c_str());
    }
    return cmd;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer cmd)
{
    std::string error;
    if (!tryEndSingleTimeCommands(cmd, &error))
    {
        die(error.c_str());
    }
}

uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeBits & (1u << i)) != 0 &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }

    die("Failed to find suitable memory type.");
    return 0;
}

void VulkanContext::setDebugName(uint64_t handle, VkObjectType type, const char* name) const
{
    if (!kEnableValidation || handle == 0 || name == nullptr || name[0] == '\0')
    {
        return;
    }

    auto* fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    if (fn == nullptr)
    {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    fn(device, &info);
}
