#pragma once

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

namespace engine::render
{

class PipelineBuilder
{
public:
    PipelineBuilder& graphics(const VkGraphicsPipelineCreateInfo& createInfo)
    {
        graphicsInfo_ = &createInfo;
        return *this;
    }

    PipelineBuilder& compute(const VkComputePipelineCreateInfo& createInfo)
    {
        computeInfo_ = &createInfo;
        return *this;
    }

    UniquePipeline createGraphics(VkDevice device,
                                  VkPipelineCache cache = VK_NULL_HANDLE) const
    {
        if (device == VK_NULL_HANDLE || graphicsInfo_ == nullptr)
        {
            return {};
        }

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device, cache, 1, graphicsInfo_, nullptr, &pipeline) !=
            VK_SUCCESS)
        {
            return {};
        }
        return UniquePipeline(device, pipeline);
    }

    UniquePipeline createCompute(VkDevice device, VkPipelineCache cache = VK_NULL_HANDLE) const
    {
        if (device == VK_NULL_HANDLE || computeInfo_ == nullptr)
        {
            return {};
        }

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(device, cache, 1, computeInfo_, nullptr, &pipeline) !=
            VK_SUCCESS)
        {
            return {};
        }
        return UniquePipeline(device, pipeline);
    }

private:
    const VkGraphicsPipelineCreateInfo* graphicsInfo_ = nullptr;
    const VkComputePipelineCreateInfo* computeInfo_ = nullptr;
};

} // namespace engine::render
