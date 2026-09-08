#pragma once

#include "engine/render/FrameInputs.h"

struct FrameContext;
struct PassCreateInfo;
struct VulkanContext;

namespace engine::render
{

class PassResourceBuilder;

struct RenderPassContext
{
    VulkanContext& ctx;
    const FrameContext& frame;
    const FrameInputs& inputs;
};

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual const char* name() const = 0;
    virtual void create(VulkanContext& ctx, const PassCreateInfo& createInfo) = 0;
    virtual void onResize(VulkanContext& ctx, const PassCreateInfo& createInfo) = 0;
    virtual void record(const RenderPassContext& context) = 0;
    virtual void destroy(VulkanContext& ctx) = 0;
    virtual bool enabled(const FrameInputs&) const { return true; }
    virtual void declareResources(PassResourceBuilder&) const {}
};

}  // namespace engine::render
