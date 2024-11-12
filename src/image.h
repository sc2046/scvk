#pragma once

#include "vk_types.h"

struct Image
{
	VkImage         mImage;
	VmaAllocation   mAllocation;
};