#pragma once

#include "vk_types.h"

namespace scvk
{
	struct Image
	{
		VkImage         mImage;
		VkImageView		mView;
		VmaAllocation   mAllocation;
		VkExtent3D		mExtents;
		VkFormat		mFormat;
	};

	void destroyImage(VkDevice device, VmaAllocator allocator, Image& image);

}