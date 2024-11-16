#include "image.h"

namespace scvk
{
	void destroyImage(VkDevice device, VmaAllocator allocator, Image& image)
	{
		vkDestroyImageView(device, image.mView, nullptr);
		vmaDestroyImage(allocator, image.mImage, image.mAllocation);
	}
}