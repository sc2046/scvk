#pragma once

#include "vk_types.h"

struct Image
{
	VkImage         mImage;
	VkImageView		mImageView;
	VmaAllocation   mAllocation;
	VkExtent3D		mExtents;
	VkFormat		mFormat;
};