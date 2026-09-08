#include "engine/render/passes/VoxelGBufferPass.h"

#include <array>

#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/FrameContext.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

namespace
{
VkRenderPass createLoadRenderPass(
    VkDevice device,
    VkFormat depthFormat,
    const std::array<VkFormat, GBufferPass::kGBufferCount>& colorFormats)
{
    std::array<VkAttachmentDescription, GBufferPass::kGBufferCount + 1> attachments{};
    std::array<VkAttachmentReference, GBufferPass::kGBufferCount> colorRefs{};
    for (size_t i = 0; i < GBufferPass::kGBufferCount; ++i)
    {
        VkAttachmentDescription color{};
        color.format = colorFormats[i];
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[i] = color;

        VkAttachmentReference ref{};
        ref.attachment = static_cast<uint32_t>(i);
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs[i] = ref;
    }

    VkAttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    attachments[GBufferPass::kGBufferCount] = depth;

    VkAttachmentReference depthRef{};
    depthRef.attachment = static_cast<uint32_t>(GBufferPass::kGBufferCount);
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(GBufferPass::kGBufferCount);
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        die("vkCreateRenderPass (voxel gbuffer) failed");
    }
    return rp;
}
} // namespace

void VoxelGBufferPass::create(VulkanContext& ctx, const PassCreateInfo& ci,
                              const GBufferPass& gbuffer)
{
    extent_ = ci.extent;

    std::array<VkFormat, GBufferPass::kGBufferCount> formats{};
    for (size_t i = 0; i < GBufferPass::kGBufferCount; ++i)
    {
        formats[i] = gbuffer.color(0, static_cast<GBufferPass::Slot>(i)).format;
    }

    createRenderPass(ctx, gbuffer.depthFormat(), formats);
    createFramebuffers(ctx, gbuffer);
}

void VoxelGBufferPass::destroy(VulkanContext& ctx)
{
    destroyFramebuffers(ctx);
    destroyRenderPass(ctx);
    extent_ = {};
}

void VoxelGBufferPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci,
                                const GBufferPass& gbuffer)
{
    extent_ = ci.extent;
    destroyFramebuffers(ctx);
    createFramebuffers(ctx, gbuffer);
}

void VoxelGBufferPass::record(VulkanContext& ctx, const FrameContext& fc,
                              VkDescriptorSet frameSet, const ExtraDrawCall& extraDraw)
{
    (void)ctx;
    if (!renderPass_)
    {
        return;
    }
    if (fc.frameIndex >= frames_.size())
    {
        return;
    }

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_.get();
    rp.framebuffer = frames_[fc.frameIndex].framebuffer.get();
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 0;
    rp.pClearValues = nullptr;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(extent_.width);
    vp.height = static_cast<float>(extent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(fc.cmd, 0, 1, &vp);
    vkCmdSetScissor(fc.cmd, 0, 1, &scissor);

    if (extraDraw.fn != nullptr)
    {
        extraDraw.fn(fc.cmd, frameSet, extraDraw.user);
    }

    vkCmdEndRenderPass(fc.cmd);
}

void VoxelGBufferPass::createRenderPass(VulkanContext& ctx, VkFormat depthFormat,
                                        const std::array<VkFormat, GBufferPass::kGBufferCount>&
                                            colorFormats)
{
    if (renderPass_)
    {
        return;
    }
    renderPass_ =
        engine::render::UniqueRenderPass(ctx.device,
                                         createLoadRenderPass(ctx.device, depthFormat,
                                                              colorFormats));
}

void VoxelGBufferPass::destroyRenderPass(VulkanContext& ctx)
{
    (void)ctx;
    renderPass_.reset();
}

void VoxelGBufferPass::createFramebuffers(VulkanContext& ctx, const GBufferPass& gbuffer)
{
    frames_.resize(kMaxFramesInFlight);

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        std::array<VkImageView, GBufferPass::kGBufferCount + 1> attachments{};
        for (size_t j = 0; j < GBufferPass::kGBufferCount; ++j)
        {
            attachments[j] =
                gbuffer.color(i, static_cast<GBufferPass::Slot>(j)).view;
        }
        attachments[GBufferPass::kGBufferCount] = gbuffer.depth(i).view;

        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = renderPass_.get();
        fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbci.pAttachments = attachments.data();
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &framebuffer) != VK_SUCCESS)
        {
            die("vkCreateFramebuffer (voxel gbuffer) failed");
        }
        frames_[i].framebuffer = engine::render::UniqueFramebuffer(ctx.device, framebuffer);
    }
}

void VoxelGBufferPass::destroyFramebuffers(VulkanContext& ctx)
{
    (void)ctx;
    for (auto& frame : frames_)
    {
        frame.framebuffer.reset();
    }
    frames_.clear();
}
