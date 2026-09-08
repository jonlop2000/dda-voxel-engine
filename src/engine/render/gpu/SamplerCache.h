#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

namespace engine::render
{

enum class SamplerPreset : uint8_t
{
    ClampLinear,
    ClampNearest,
    ClampLinearNearestMip,
    RepeatLinear,
    RepeatNearest,
    ShadowCompareNearest,
    Count
};

constexpr std::size_t kSamplerPresetCount = static_cast<std::size_t>(SamplerPreset::Count);

VkSamplerCreateInfo samplerCreateInfo(SamplerPreset preset);

class SamplerCache
{
public:
    SamplerCache() = default;
    ~SamplerCache() { destroy(); }

    SamplerCache(const SamplerCache&) = delete;
    SamplerCache& operator=(const SamplerCache&) = delete;
    SamplerCache(SamplerCache&&) = delete;
    SamplerCache& operator=(SamplerCache&&) = delete;

    bool get(VkDevice device, SamplerPreset preset, VkSampler& outSampler);
    VkSampler find(SamplerPreset preset) const;
    void destroy();

private:
    static std::size_t indexOf(SamplerPreset preset);

    VkDevice device_ = VK_NULL_HANDLE;
    std::array<UniqueSampler, kSamplerPresetCount> samplers_{};
};

} // namespace engine::render
