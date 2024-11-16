#include "descriptors.h"

//void DescriptorLayoutBuilder::addBinding(uint32_t binding, uint32_t count, VkDescriptorType type)
//{
//    VkDescriptorSetLayoutBinding newbind{};
//    newbind.binding = binding;
//    newbind.descriptorCount = count;
//    newbind.descriptorType = type;
//
//    bindings.push_back(newbind);
//}
//
//void DescriptorLayoutBuilder::clear()
//{
//    bindings.clear();
//}
//
//
//// Note that stage flags will apply to the entire descriptor set.
//VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
//{
//    for (auto& b : bindings) {
//        b.stageFlags |= shaderStages;
//    }
//
//    VkDescriptorSetLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
//    info.pNext = pNext;
//    info.pBindings = bindings.data();
//    info.bindingCount = std::accumulate(bindings.cbegin(), bindings.cend(), 0u, [](uint32_t sum, VkDescriptorSetLayoutBinding b) {return sum + b.descriptorCount;});
//    info.flags = flags;
//
//    VkDescriptorSetLayout set;
//    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));
//
//    return set;
//}
