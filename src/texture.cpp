
#include "texture.h"

namespace scvk
{
	void destroyTexture(VkDevice device, const Texture& texture)
	{
		destroyImage(device, texture.mImage);
		vkDestroySampler(device, texture.mSampler, nullptr);
	}
}