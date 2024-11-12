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
}

