#include "image.h"

namespace scvk
{
	void destroyImage(VkDevice device, const Image& image)
	{
		vkDestroyImage(device, image.mImage, nullptr);
		vkDestroyImageView(device, image.mImageView, nullptr);
	}
}