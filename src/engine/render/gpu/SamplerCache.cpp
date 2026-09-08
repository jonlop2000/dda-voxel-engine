#include "engine/render/gpu/SamplerCache.h"

namespace engine::render
{

namespace
{

void applyClampToEdge(VkSamplerCreateInfo& info)
{
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

void applyRepeat(VkSamplerCreateInfo& info)
{
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

void applyClampToBorder(VkSamplerCreateInfo& info)
{
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
}

} // namespace

VkSamplerCreateInfo samplerCreateInfo(SamplerPreset preset)
{
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.anisotropyEnable = VK_FALSE;

    switch (preset)
    {
    case SamplerPreset::ClampLinear:
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.maxAnisotropy = 1.0f;
        applyClampToEdge(info);
        break;
    case SamplerPreset::ClampNearest:
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.maxAnisotropy = 1.0f;
        applyClampToEdge(info);
        break;
    case SamplerPreset::ClampLinearNearestMip:
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        applyClampToEdge(info);
        break;
    case SamplerPreset::RepeatLinear:
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.maxAnisotropy = 1.0f;
        applyRepeat(info);
        break;
    case SamplerPreset::RepeatNearest:
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.maxAnisotropy = 1.0f;
        applyRepeat(info);
        break;
    case SamplerPreset::ShadowCompareNearest:
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        applyClampToBorder(info);
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        info.compareEnable = VK_TRUE;
        info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    case SamplerPreset::Count:
        break;
    }

    return info;
}

bool SamplerCache::get(VkDevice device, SamplerPreset preset, VkSampler& outSampler)
{
    outSampler = VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE || preset == SamplerPreset::Count)
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

    UniqueSampler& sampler = samplers_[indexOf(preset)];
    if (!sampler)
    {
        VkSampler rawSampler = VK_NULL_HANDLE;
        const VkSamplerCreateInfo createInfo = samplerCreateInfo(preset);
        const VkResult result = vkCreateSampler(device_, &createInfo, nullptr, &rawSampler);
        if (result != VK_SUCCESS)
        {
            return false;
        }
        sampler = UniqueSampler(device_, rawSampler);
    }

    outSampler = sampler.get();
    return true;
}

VkSampler SamplerCache::find(SamplerPreset preset) const
{
    if (preset == SamplerPreset::Count)
    {
        return VK_NULL_HANDLE;
    }
    return samplers_[indexOf(preset)].get();
}

void SamplerCache::destroy()
{
    for (auto& sampler : samplers_)
    {
        sampler.reset();
    }
    device_ = VK_NULL_HANDLE;
}

std::size_t SamplerCache::indexOf(SamplerPreset preset)
{
    return static_cast<std::size_t>(preset);
}

} // namespace engine::render
