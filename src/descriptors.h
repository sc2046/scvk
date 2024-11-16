#pragma once

#include "vk_types.h"

struct DescriptorLayoutBuilder
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void addBinding(uint32_t binding, uint32_t count, VkDescriptorType type);
	void clear();
	VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocator {

    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio; // The number of descriptors of type type, PER set.
    };

    VkDescriptorPool pool;

    void initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clearDescriptors(VkDevice device);
    void destroyPool(VkDevice device);

    // Allocates one set from the pool.
    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};

// Responsible for binding a descriptor set to gpu resources.
//struct DescriptorSetWriter {
//    std::deque<VkDescriptorImageInfo> imageInfos;
//    std::deque<VkDescriptorBufferInfo> bufferInfos;
//    std::vector<VkWriteDescriptorSet> writes;
//
//    void writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
//    void writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);
//     
//    void clear();
//    void updateSet(VkDevice device, VkDescriptorSet set);
//};

/// ==================================================

inline void DescriptorLayoutBuilder::addBinding(uint32_t binding, uint32_t count,  VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding newbind{};
    newbind.binding = binding;
    newbind.descriptorCount = count;
    newbind.descriptorType = type;

    bindings.push_back(newbind);
}

inline void DescriptorLayoutBuilder::clear()
{
    bindings.clear();
}


// Note that stage flags will apply to the entire descriptor set.
inline VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
    for (auto& b : bindings) {
        b.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.pNext = pNext;
    info.pBindings = bindings.data();
    info.bindingCount = std::accumulate(bindings.cbegin(), bindings.cend(), 0u, [](uint32_t sum, VkDescriptorSetLayoutBinding b) {return sum + b.descriptorCount;});
    info.flags = flags;

    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

    return set;
}

/// ========================================================================

inline void  DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * maxSets)
            });
    }

    VkDescriptorPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_info.flags = 0;
    pool_info.maxSets = maxSets;
    pool_info.poolSizeCount = (uint32_t)poolSizes.size();
    pool_info.pPoolSizes = poolSizes.data();

    vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}

inline void DescriptorAllocator::clearDescriptors(VkDevice device)
{
    vkResetDescriptorPool(device, pool, 0);
}

inline void DescriptorAllocator::destroyPool(VkDevice device)
{
    vkDestroyDescriptorPool(device, pool, nullptr);
}

inline VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

    return ds;
}