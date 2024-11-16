#pragma once

#include <vk_types.h>
#include "image.h"
#include "image.h"

namespace scvk
{
	struct Texture
	{
		Image		mImage;
		VkSampler	mSampler;
		uint32_t	mMipLevels;
	};

	void destroyTexture(VkDevice device, VmaAllocator allocator, const Texture& texture);

}