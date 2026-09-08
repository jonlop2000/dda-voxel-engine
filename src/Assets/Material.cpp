#include "Assets/Material.h"

#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

void MaterialPool::create(VulkanContext& ctx, uint32_t maxMaterialsIn)
{
    destroy(ctx);
    maxMaterials = maxMaterialsIn == 0 ? 1u : maxMaterialsIn;

    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t i = 0; i < 3; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = static_cast<uint32_t>(sizeof(bindings) / sizeof(bindings[0]));
    dlci.pBindings = bindings;
    VkDescriptorSetLayout rawLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(ctx.device, &dlci, nullptr, &rawLayout) != VK_SUCCESS)
    {
        die("vkCreateDescriptorSetLayout (material) failed");
    }
    layout_ = engine::render::UniqueDescriptorSetLayout(ctx.device, rawLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxMaterials * 3u;

    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    dpci.maxSets = maxMaterials;
    VkDescriptorPool rawPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(ctx.device, &dpci, nullptr, &rawPool) != VK_SUCCESS)
    {
        die("vkCreateDescriptorPool (material) failed");
    }
    pool_ = engine::render::UniqueDescriptorPool(ctx.device, rawPool);
}

void MaterialPool::destroy(VulkanContext& ctx)
{
    (void)ctx;
    pool_.reset();
    layout_.reset();
    maxMaterials = 0;
}

VkDescriptorSet MaterialPool::allocate(VulkanContext& ctx)
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool_.get();
    alloc.descriptorSetCount = 1;
    const VkDescriptorSetLayout rawLayout = layout_.get();
    alloc.pSetLayouts = &rawLayout;

    if (vkAllocateDescriptorSets(ctx.device, &alloc, &set) != VK_SUCCESS)
    {
        die("vkAllocateDescriptorSets (material) failed");
    }
    return set;
}

void MaterialPool::updateDescriptorSet(VulkanContext& ctx, VkDescriptorSet set,
                                       const Texture2D& baseColor, const Texture2D& normal,
                                       const Texture2D& orm)
{
    VkDescriptorImageInfo infos[3]{};
    infos[0].sampler = baseColor.sampler;
    infos[0].imageView = baseColor.view;
    infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[1].sampler = normal.sampler;
    infos[1].imageView = normal.view;
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[2].sampler = orm.sampler;
    infos[2].imageView = orm.view;
    infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; i++)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &infos[i];
    }

    vkUpdateDescriptorSets(ctx.device,
                           static_cast<uint32_t>(sizeof(writes) / sizeof(writes[0])), writes, 0,
                           nullptr);
}
