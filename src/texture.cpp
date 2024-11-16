
#include "texture.h"

namespace scvk
{
	void destroyTexture(VkDevice device, VmaAllocator allocator, Texture& texture)
	{
		vkDestroySampler(device, texture.mSampler, nullptr);
		destroyImage(device, allocator, texture.mImage);
	}
}