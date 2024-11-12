#pragma once

#include "vk_types.h"
#include "vk_mem_alloc.h"

#include "buffer.h"
#include "descriptors.h"
#include "mesh.h"


constexpr unsigned int FRAME_OVERLAP = 2;
struct FrameResources {

	// Synchronisation primitives for frame submission.
	VkSemaphore mImageAvailableSemaphore;
	VkSemaphore mRenderFinishedSemaphore;
	VkFence		mRenderFence;

	VkCommandPool mCommandPool;
	VkCommandBuffer mMainCommandBuffer;
};


class VulkanApp {
public:


	bool bUseValidationLayers{ true };

	VkSurfaceKHR		mSurface;
	struct GLFWwindow*	mWindow{ nullptr }; // Forward declaration.
	VkExtent2D			mWindowExtents{ 1024, 768 };

	int					mFrameNumber{ 0 };
	FrameResources		mFrames[FRAME_OVERLAP];
	FrameResources& getCurrentFrame() { return mFrames[mFrameNumber % FRAME_OVERLAP]; };

	// Swapchain stuff.
	VkSwapchainKHR				mSwapchain;
	std::vector<VkImage>		mSwapchainImages;
	std::vector<VkImageView>	mSwapchainImageViews;
	VkFormat					mSwapchainImageFormat;
	VkExtent2D					mSwapchainExtent;


	uint32_t					mSwapchainImageIndex;


	void init();
	void run();
	void cleanup();

	void draw(VkCommandBuffer cmd);

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);


	GPUMeshBuffers mesh;

private:

	void initContext(bool validation);
	void initSwapchain();
	void initFrameResources();
	void initGlobalResources();
	void initDescriptors();


	void initMeshPipeline();
	
	
	void createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();
	
	//VkDescriptorSet			mDrawImageDescriptors;
	//VkDescriptorSetLayout	mDrawImageDescriptorLayout;

	// Vulkan context.
	//-----------------------------------------------
	VkInstance					mInstance;
	VkPhysicalDevice			mPhysicalDevice;
	VkDevice					mDevice;
	VkDebugUtilsMessengerEXT	mDebugMessenger;
	VkQueue						mGraphicsQueue;
	uint32_t					mGraphicsQueueFamily;
	VmaAllocator				mVmaAllocator;

	// immediate submit structures
	//-----------------------------------------------
	VkFence						mImmFence;
	VkCommandBuffer				mImmCommandBuffer;
	VkCommandPool				mImmCommandPool;

	// Descriptors
	//-----------------------------------------------
	DescriptorAllocator			mGlobalDescriptorAllocator;

	// Pipeline Data
	//-----------------------------------------------
	VkShaderModule		mVertexShader;
	VkShaderModule		mFragmentShader;
	VkPipelineLayout	mMeshPipelineLayout;
	VkPipeline			mMeshPipeline;
	
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