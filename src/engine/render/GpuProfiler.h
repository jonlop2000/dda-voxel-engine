#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"

struct VulkanContext;

struct GpuProfilerSample
{
    std::string label;
    double ms = 0.0;
};

class GpuProfiler
{
public:
    bool create(VulkanContext& ctx, uint32_t maxScopes);
    void destroy(VulkanContext& ctx);

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    bool isSupported() const { return supported_; }

    void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex);
    uint32_t beginScope(VkCommandBuffer cmd, const char* label);
    void endScope(VkCommandBuffer cmd, uint32_t scopeId);
    void resolve(VulkanContext& ctx, uint32_t frameIndex);

    const std::vector<GpuProfilerSample>& samples() const { return samples_; }
    double totalMs() const;

private:
    struct Scope
    {
        const char* label = nullptr;
        uint32_t startQuery = 0;
        uint32_t endQuery = 0;
    };

    static constexpr uint32_t kInvalidScope = 0xffffffffu;

    uint32_t maxScopes_ = 0;
    uint32_t queriesPerFrame_ = 0;
    uint32_t currentFrame_ = 0;
    float timestampPeriod_ = 0.0f;

    bool supported_ = false;
    bool enabled_ = false;

    VkQueryPool queryPool_ = VK_NULL_HANDLE;

    std::array<uint32_t, kMaxFramesInFlight> scopeCounts_{};
    std::array<uint32_t, kMaxFramesInFlight> queryCounts_{};
    std::vector<Scope> scopes_{};
    std::vector<uint64_t> queryResults_{};
    std::vector<GpuProfilerSample> samples_{};
};
