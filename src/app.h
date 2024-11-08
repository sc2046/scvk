#pragma once

#include "vk_types.h"
#include "vk_mem_alloc.h"



class VulkanApp {
public:


	bool bUseValidationLayers{ true };

	VkSurfaceKHR		mSurface;
	struct GLFWwindow*	mWindow{ nullptr }; // Forward declaration.
	VkExtent2D			mWindowExtents{ 1024, 768 };

	// Swapchain stuff.
	VkSwapchainKHR	mSwapchain;
	VkFormat		mSwapchainImageFormat;

	std::vector<VkImage>		mSwapchainImages;
	std::vector<VkImageView>	mSwapchainImageViews;
	VkExtent2D					mSwapchainExtent;


	void init();
	void initSwapchain();
	void initAllocators();


	void draw();


	void run();

	void cleanup();



private:

	void initContext(bool validation);
	void createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();
	
	// Vulkan context.
	//-----------------------------------------------
	VkInstance					mInstance;
	VkPhysicalDevice			mPhysicalDevice;
	VkDevice					mDevice;
	VkDebugUtilsMessengerEXT	mDebugMessenger;
	VkQueue						mGraphicsQueue;
	uint32_t					mGraphicsQueueFamily;
	VmaAllocator				mVmaAllocator;

	// Allocators.
	//-----------------------------------------------
	VkCommandPool				mCommandPool;

	// Descriptors
	//-----------------------------------------------
	VkDescriptorPool			mDescriptorPool;
	VkDescriptorSetLayout		mDescriptorSetLayout;
	VkDescriptorSet				mDescriptorSet;

	// Image
	//-----------------------------------------------
	Image						mImageLinear;
	Image						mImageRender;
	VkImageView					mImageView;

	// Texture
	//-----------------------------------------------
	Image		mTextureImage;
	uint32_t	mTextureByteSize;
	VkExtent2D	mTextureExtents;

	VkImageView mTextureImageView;
	VkSampler	mTextureSampler;

	// Acceleration structures
	//-----------------------------------------------
	AccelerationStructure		mAabbBlas;
	Buffer						mAabbGeometryBuffer;
	
	AccelerationStructure		mTlas;
	Buffer						mTlasInstanceBuffer;  // Stores the per-instance data (matrices, materialID etc...) 

	// Pipeline Data
	//-----------------------------------------------
	VkShaderModule				mComputeShader;
	VkPipelineLayout			mPipelineLayout;
	VkPipeline					mComputePipeline;
	
	//-----------------------------------------------
	struct DeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function) {
			deletors.push_back(function);
		}

		void flush() {
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
				(*it)(); //call functors
			}

			deletors.clear();
		}
	};
	DeletionQueue mDeletionQueue;


};