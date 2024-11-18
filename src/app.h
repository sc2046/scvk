#pragma once

#include "vk_types.h"
#include "vk_mem_alloc.h"

#include "buffer.h"
#include "descriptors.h"
#include "image.h"
#include "mesh.h"
#include "texture.h"
#include "timer.h"

//struct SwapchainResources
//{
//	VkSwapchainKHR				mSwapchain;
//	std::vector<VkImage>		mSwapchainImages;
//	std::vector<VkImageView>	mSwapchainImageViews;
//	VkFormat					mSwapchainImageFormat;
//	VkExtent2D					mSwapchainExtent;
//};

constexpr unsigned int FRAME_OVERLAP = 2;
struct FrameResources {

	// Synchronisation primitives for frame submission.
	VkSemaphore		mImageAvailableSemaphore;
	VkSemaphore		mRenderFinishedSemaphore;
	VkFence			mRenderFence;

	VkCommandPool	mCommandPool;
	VkCommandBuffer mMainCommandBuffer;

	// Per-frame shader resources.
	VkDescriptorSet			mFrameDataDescriptorSet;
	scvk::Buffer			mFrameDataBuffer;
};

struct FrameData
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewProj;
};

class VulkanApp {
public:


	bool bUseValidationLayers{ true };

	VkSurfaceKHR		mSurface;
	struct GLFWwindow*	mWindow{ nullptr }; // Forward declaration.
	VkExtent2D			mWindowExtents{ 1024, 768 };

	int					mFrameNumber{ 0 };
	FrameResources		mFrames[FRAME_OVERLAP];
	FrameResources&		getCurrentFrame() { return mFrames[mFrameNumber % FRAME_OVERLAP]; };

	// Swapchain stuff.
	VkSwapchainKHR				mSwapchain;
	std::vector<VkImage>		mSwapchainImages;
	std::vector<VkImageView>	mSwapchainImageViews;
	VkFormat					mSwapchainImageFormat;
	VkExtent2D					mSwapchainExtent;

	scvk::Image					mDepthImage;


	VkDescriptorPool		mGlobalDescriptorPool;
	VkDescriptorSetLayout	mFrameDataDescriptorSetLayout;



	void init();
	void run();
	void cleanup();

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);


	GPUMeshBuffers uploadMeshData(std::span<uint32_t> indices, std::span<Vertex> vertices);
	LoadedMesh mMesh;

	scvk::Texture uploadTexture(const char* path);
	scvk::Texture uploadTexture(unsigned char* data, int width, int height);
	scvk::Texture mTexture;

	VkDescriptorSetLayout			mMeshDescriptorSetLayout;
	std::vector<VkDescriptorSet>	mMeshDescriptorSets;



private:

	void initGlfw();
	void initContext(bool validation);
	void initSwapchain();
	void initFrameResources();
	void initGlobalResources();
	void initGlobalDescriptors();
	void initMeshPipeline();
	

	void initTracy();
	
	void createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();
	
	scvk::Timer mTimer;


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
	VkPipeline			mMeshPipeline;
	VkPipelineLayout	mMeshPipelineLayout;
	
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