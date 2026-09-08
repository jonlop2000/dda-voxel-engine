#include "engine/render/GpuProfiler.h"

#include "engine/render/VulkanContext.h"

#include <algorithm>
#include <vector>

bool GpuProfiler::create(VulkanContext& ctx, uint32_t maxScopes)
{
    maxScopes_ = std::max(1u, maxScopes);
    queriesPerFrame_ = maxScopes_ * 2u;
    currentFrame_ = 0;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.gpu, &props);
    timestampPeriod_ = props.limits.timestampPeriod;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.gpu, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.gpu, &familyCount, families.data());

    supported_ = false;
    if (ctx.queueFamilies.graphics.has_value())
    {
        const uint32_t gfxIndex = ctx.queueFamilies.graphics.value();
        if (gfxIndex < familyCount)
        {
            supported_ = families[gfxIndex].timestampValidBits > 0;
        }
    }

    if (!supported_)
    {
        enabled_ = false;
        return true;
    }

    VkQueryPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = queriesPerFrame_ * kMaxFramesInFlight;

    if (vkCreateQueryPool(ctx.device, &poolInfo, nullptr, &queryPool_) != VK_SUCCESS)
    {
        return false;
    }

    scopes_.resize(static_cast<size_t>(maxScopes_) * kMaxFramesInFlight);
    samples_.reserve(maxScopes_);
    queryResults_.resize(queriesPerFrame_);
    scopeCounts_.fill(0);
    queryCounts_.fill(0);

    return true;
}

void GpuProfiler::destroy(VulkanContext& ctx)
{
    if (queryPool_ != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(ctx.device, queryPool_, nullptr);
        queryPool_ = VK_NULL_HANDLE;
    }
    samples_.clear();
    scopes_.clear();
    queryResults_.clear();
    maxScopes_ = 0;
    queriesPerFrame_ = 0;
    enabled_ = false;
}

void GpuProfiler::setEnabled(bool enabled)
{
    enabled_ = supported_ && enabled;
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!enabled_ || queryPool_ == VK_NULL_HANDLE)
    {
        return;
    }

    currentFrame_ = frameIndex % kMaxFramesInFlight;
    scopeCounts_[currentFrame_] = 0;
    queryCounts_[currentFrame_] = 0;

    const uint32_t baseQuery = currentFrame_ * queriesPerFrame_;
    vkCmdResetQueryPool(cmd, queryPool_, baseQuery, queriesPerFrame_);
}

uint32_t GpuProfiler::beginScope(VkCommandBuffer cmd, const char* label)
{
    if (!enabled_ || queryPool_ == VK_NULL_HANDLE || label == nullptr)
    {
        return kInvalidScope;
    }

    if (scopeCounts_[currentFrame_] >= maxScopes_)
    {
        return kInvalidScope;
    }

    if (queryCounts_[currentFrame_] + 2 > queriesPerFrame_)
    {
        return kInvalidScope;
    }

    const uint32_t scopeIndex = scopeCounts_[currentFrame_]++;
    const uint32_t queryOffset = queryCounts_[currentFrame_];
    queryCounts_[currentFrame_] += 2;

    const uint32_t baseQuery = currentFrame_ * queriesPerFrame_;
    const uint32_t startQuery = baseQuery + queryOffset;
    const uint32_t endQuery = startQuery + 1;

    Scope& scope = scopes_[currentFrame_ * maxScopes_ + scopeIndex];
    scope.label = label;
    scope.startQuery = startQuery;
    scope.endQuery = endQuery;

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool_, startQuery);

    return scopeIndex;
}

void GpuProfiler::endScope(VkCommandBuffer cmd, uint32_t scopeId)
{
    if (!enabled_ || queryPool_ == VK_NULL_HANDLE || scopeId == kInvalidScope)
    {
        return;
    }

    if (scopeId >= maxScopes_)
    {
        return;
    }

    const Scope& scope = scopes_[currentFrame_ * maxScopes_ + scopeId];
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_, scope.endQuery);
}

void GpuProfiler::resolve(VulkanContext& ctx, uint32_t frameIndex)
{
    samples_.clear();

    if (!enabled_ || queryPool_ == VK_NULL_HANDLE)
    {
        return;
    }

    const uint32_t frame = frameIndex % kMaxFramesInFlight;
    const uint32_t queryCount = queryCounts_[frame];
    const uint32_t scopeCount = scopeCounts_[frame];

    if (queryCount == 0 || scopeCount == 0)
    {
        return;
    }

    if (queryResults_.size() < queryCount)
    {
        queryResults_.resize(queryCount);
    }

    const uint32_t baseQuery = frame * queriesPerFrame_;
    const VkResult res = vkGetQueryPoolResults(ctx.device, queryPool_, baseQuery, queryCount,
                                               queryCount * sizeof(uint64_t),
                                               queryResults_.data(), sizeof(uint64_t),
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (res != VK_SUCCESS)
    {
        return;
    }

    samples_.reserve(scopeCount);

    for (uint32_t i = 0; i < scopeCount; ++i)
    {
        const Scope& scope = scopes_[frame * maxScopes_ + i];
        const uint32_t startIndex = scope.startQuery - baseQuery;
        const uint32_t endIndex = scope.endQuery - baseQuery;
        if (startIndex >= queryCount || endIndex >= queryCount)
        {
            continue;
        }

        const uint64_t start = queryResults_[startIndex];
        const uint64_t end = queryResults_[endIndex];
        const double deltaNs = static_cast<double>(end - start) * timestampPeriod_;
        const double ms = deltaNs / 1.0e6;

        GpuProfilerSample sample{};
        sample.label = scope.label != nullptr ? scope.label : "(unnamed)";
        sample.ms = ms;
        samples_.push_back(sample);
    }
}

double GpuProfiler::totalMs() const
{
    for (const auto& sample : samples_)
    {
        if (sample.label == "Frame")
        {
            return sample.ms;
        }
    }

    double maxMs = 0.0;
    for (const auto& sample : samples_)
    {
        maxMs = std::max(maxMs, sample.ms);
    }
    return maxMs;
}
