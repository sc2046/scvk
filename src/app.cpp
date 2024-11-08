#include <iostream>

#include <volk.h>

#include "app.h"

#include <GLFW/glfw3.h>

#include "vk_types.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

void VulkanApp::init()
{
    #ifdef NDEBUG
        constexpr bool validation = false;
    #else
        constexpr bool validation = true;
    #endif

    if (!glfwInit()) {
        fmt::println("Failed to initialize GLFW");
    }

    // Tell GLFW to not create an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Create the GLFW window
    mWindow = glfwCreateWindow(mWindowExtents.width, mWindowExtents.height, "Vulkan Window", nullptr, nullptr);
    if (!mWindow) {
        glfwTerminate();
        fmt::println("Failed to create GLFW window");
    }

    initContext(validation);
    initSwapchain();


}

void VulkanApp::initContext(bool validation)
{

    // Create instance
    vkb::InstanceBuilder builder;

    const auto inst_ret = builder.set_app_name("Vulkan Engine")
        .request_validation_layers(validation)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    if (!inst_ret) {
        fmt::print("Failed to create Vulkan instance: {}\n", inst_ret.error().message());
    }

    vkb::Instance vkb_instance = inst_ret.value();
    mInstance = vkb_instance.instance;
    mDebugMessenger = vkb_instance.debug_messenger;
    
    // Create a surface to present to.
    if (glfwCreateWindowSurface(mInstance, mWindow, nullptr, &mSurface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    // Initialise volk
    VK_CHECK(volkInitialize());
    volkLoadInstance(mInstance);

    // features from Vulkan 1.2.
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.scalarBlockLayout = true;

    // features from Vulkan 1.3.
    VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    // Require these features for the extensions this app uses.
    VkPhysicalDeviceAccelerationStructureFeaturesKHR    asFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    asFeatures.accelerationStructure = true;
    
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    rayQueryFeatures.rayQuery = true;


    // Select a physical device that supports the required extensions 
    vkb::PhysicalDeviceSelector physDeviceSelector{ vkb_instance };
    const auto physDevice_ret = physDeviceSelector
        .set_minimum_version(1, 3)
        .set_surface(mSurface)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
        .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        .add_required_extension_features(asFeatures)
        .add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
        .add_required_extension_features(rayQueryFeatures)
        .select();
    if (!physDevice_ret) {
        fmt::print("Failed to create Vulkan physical device: {}\n", physDevice_ret.error().message());
    }
    vkb::PhysicalDevice physicalDevice = physDevice_ret.value();
    mPhysicalDevice = physicalDevice.physical_device;

    // create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    const auto dev_ret = deviceBuilder.build();
    if (!dev_ret) {
        fmt::print("Failed to create Vulkan logical device: {}\n", dev_ret.error().message());
    }
    vkb::Device vkbDevice = dev_ret.value();

    mDevice = vkbDevice.device;
    volkLoadDevice(mDevice);

    // Search device for a compute queue.
    const auto queue_ret = vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!queue_ret) {
        fmt::print("Failed to find a graphics queue: {}\n", queue_ret.error().message());
    }
    mGraphicsQueue = queue_ret.value();
    mGraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // Add destroy functions to deletion queue.
    mDeletionQueue.push_function([&]() {    vkDestroyInstance(mInstance, nullptr);});
    mDeletionQueue.push_function([&]() {    vkDestroySurfaceKHR(mInstance,mSurface,nullptr);});
    mDeletionQueue.push_function([&]() {    vkb::destroy_debug_utils_messenger(mInstance, mDebugMessenger);});
    mDeletionQueue.push_function([&]() {    vkDestroyDevice(mDevice, nullptr);});

    // Initialize VMA.
    VmaVulkanFunctions f{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr
    };
    VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = mPhysicalDevice,
        .device = mDevice,
        .pVulkanFunctions = &f,
        .instance = mInstance,
    };
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &mVmaAllocator));
    mDeletionQueue.push_function([&]() {    vmaDestroyAllocator(mVmaAllocator);});

}

void VulkanApp::createSwapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ mPhysicalDevice, mDevice, mSurface};

    mSwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = mSwapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // TODO: read.
        .set_desired_extent(width, height) // Set resolution of swapchain images (should be window resolution).
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    mSwapchainExtent = vkbSwapchain.extent;
    //store swapchain and its related images
    mSwapchain = vkbSwapchain.swapchain;
    mSwapchainImages = vkbSwapchain.get_images().value();
    mSwapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanApp::initSwapchain()
{
    createSwapchain(mWindowExtents.width, mWindowExtents.height);
}

void VulkanApp::initAllocators()
{
    // Create a single-use command pool.
    // Command buffers allocated from this pool will not be re-used.
    VkCommandPoolCreateInfo commanddPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, // Most command buffers allocated will be short-lived, single-use
    .queueFamilyIndex = mGraphicsQueueFamily,
    };
    VK_CHECK(vkCreateCommandPool(mDevice, &commanddPoolCreateInfo, nullptr, &mCommandPool));
    mDeletionQueue.push_function([&]() { vkDestroyCommandPool(mDevice, mCommandPool, nullptr);});
}


VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags = 0)
{
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;

    info.flags = flags;

    return info;
}

VkSemaphoreCreateInfo semaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0)
{
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;
    return info;
}

void VulkanApp::draw()
{

}

void VulkanApp::run()
{

    static int frameNumber = 0;

    VkSemaphoreCreateInfo semCreateInfo = semaphoreCreateInfo(0);
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VK_CHECK(vkCreateSemaphore(mDevice, &semCreateInfo, nullptr, &imageAvailableSemaphore));
    VK_CHECK(vkCreateSemaphore(mDevice, &semCreateInfo, nullptr, &renderFinishedSemaphore));

    // Create a fence in the signalled state.
    VkFenceCreateInfo fncCreateInfo = fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
    VkFence fence;
    VK_CHECK(vkCreateFence(mDevice, &fncCreateInfo, nullptr, &fence));


    // Main loop
    while (!glfwWindowShouldClose(mWindow)) {
        glfwPollEvents();

        ///
        /// Wait for the previous frame to finish by waiting on the fence.
        /// 
        VK_CHECK(vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(mDevice, 1, &fence));

        ///
        /// Acquire an image from the swap chain
        /// 
        uint32_t imageIndex = 0;
        vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, imageAvailableSemaphore, nullptr, &imageIndex);


        ///
        /// Record drawing commands in a command buffer
        /// 
        VkCommandBufferAllocateInfo cmdBufAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdBufAllocInfo.commandBufferCount = 1;
        cmdBufAllocInfo.commandPool = mCommandPool;
        cmdBufAllocInfo.level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VkCommandBuffer cmd;
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &cmdBufAllocInfo, &cmd));

        VkCommandBufferBeginInfo cmdBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
        {

            VkClearColorValue color = (frameNumber == 0) ? VkClearColorValue{ 1, 0, 1, 1 } : VkClearColorValue{0, 1, 1, 1};
            frameNumber = (frameNumber + 1) % 2;

            VkImageSubresourceRange range = {};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.levelCount = 1;
            range.layerCount = 1;

            vkCmdClearColorImage(cmd, mSwapchainImages[imageIndex], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
        }
        VK_CHECK(vkEndCommandBuffer(cmd));
       
        ///
        /// Submit the command buffer.
        /// 
        VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; 
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

        vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, fence );

        ///
        /// Present the swapchain image.
        /// 
        VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &renderFinishedSemaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &mSwapchain;
        info.pImageIndices = &imageIndex;
        VK_CHECK(vkQueuePresentKHR(mGraphicsQueue, &info));
    }
    VK_CHECK(vkDeviceWaitIdle(mDevice));

    vkDestroyFence(mDevice, fence, nullptr);
    vkDestroySemaphore(mDevice, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(mDevice, renderFinishedSemaphore, nullptr);
}


void VulkanApp::destroySwapchain()
{
    vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
    for (int i = 0; i < mSwapchainImageViews.size(); i++) {

        vkDestroyImageView(mDevice, mSwapchainImageViews[i], nullptr);
    }
}

void VulkanApp::cleanup()
{
    destroySwapchain();

    vkDeviceWaitIdle(mDevice);
    mDeletionQueue.flush();

    glfwDestroyWindow(mWindow);
    glfwTerminate();
}
