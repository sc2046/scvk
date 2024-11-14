#pragma once

#include <vk_types.h>

namespace scvk
{

    struct Buffer
    {
        VkBuffer			mBuffer;
        VmaAllocation		mAllocation;
		VmaAllocationInfo	mAllocInfo;
    };

    //Buffer createBuffer(uint32_t byteSize, VkBufferUsageFlags flags)
   // {
		//// allocate buffer
		//VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		//bufferInfo.pNext = nullptr;
		//bufferInfo.size = byteSize;

		//bufferInfo.usage = flags;

		//VmaAllocationCreateInfo vmaallocInfo = {};
		////vmaallocInfo.usage = memoryUsage;
		//vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
		//
		//Buffer newBuffer;

		//vmaCreateBuffer()

		//// allocate the buffer
		//VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
		//	&newBuffer.info));
    //}

	inline VkDeviceAddress GetBufferDeviceAddress(VkDevice device, const Buffer& buffer)
	{
		VkBufferDeviceAddressInfo addressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer .mBuffer};
		return vkGetBufferDeviceAddress(device, &addressInfo);
	}

	inline Buffer createHostVisibleStagingBuffer(VmaAllocator allocator, uint32_t size_bytes,
		VkBufferUsageFlags usage = 0, VkMemoryPropertyFlags alloc_flags = 0)
	{
		Buffer buf;
		VkBufferCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size			= size_bytes,
			.usage			= usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sharingMode	= VK_SHARING_MODE_EXCLUSIVE
		};
		const VmaAllocationCreateInfo allocCreateInfo{
			.flags			= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
			.usage			= VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			.requiredFlags	= alloc_flags | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};
		VK_CHECK(vmaCreateBuffer(allocator, &createInfo, &allocCreateInfo, &buf.mBuffer, &buf.mAllocation, &buf.mAllocInfo));
		return buf;
	}
}

