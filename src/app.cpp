#include <iostream>

#include <volk.h>

#include "app.h"
#include <timer.h>
#include "mesh.h"
#include "mesh_loader.h"
#include "pipelines.h"

#include <GLFW/glfw3.h>

#include <vk_initializers.h>
#include "vk_types.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


#include <tracy/tracy.hpp>
#include <tracy/TracyVulkan.hpp>



void VulkanApp::init()
{
    #ifdef NDEBUG
        constexpr bool validation = false;
    #else
        constexpr bool validation = true;
    #endif

    initGlfw();
    initContext(validation);
    initSwapchain();
    initFrameResources();
    initGlobalResources();
    initGlobalDescriptors();
    initMeshPipeline();


    initTracy();

    loadGltfFromFile(this, "../../assets/sponza/sponza.gltf", mMesh);
    mMesh.mBuffers = uploadMeshData(mMesh.mIndices, mMesh.mVertices);

    //delete the mesh data on engine shutdown
    mDeletionQueue.push_function([&]() {
        vmaDestroyBuffer(mVmaAllocator, mMesh.mBuffers.mVertexBuffer.mBuffer, mMesh.mBuffers.mVertexBuffer.mAllocation);
        vmaDestroyBuffer(mVmaAllocator, mMesh.mBuffers.mIndexBuffer.mBuffer, mMesh.mBuffers.mIndexBuffer.mAllocation);
        });

    mTexture = uploadTexture("../../assets/statue.jpg");
    mDeletionQueue.push_function([&]() {
        vkDestroyImageView(mDevice, mTexture.mImage.mView, nullptr);
        vmaDestroyImage(mVmaAllocator, mTexture.mImage.mImage, mTexture.mImage.mAllocation);
        vkDestroySampler(mDevice, mTexture.mSampler, nullptr);
        //scvk::destroyTexture(mDevice, mVmaAllocator, mTexture);
        });
}

void VulkanApp::initGlfw()
{
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

void VulkanApp::initSwapchain()
{
    createSwapchain(mWindowExtents.width, mWindowExtents.height);
}

void VulkanApp::initTracy()
{
    //VkCommandBufferAllocateInfo info = {
    //    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    //    .commandBufferCount  = 1,
    //    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    //    .commandPool = 

    //}

    //#if defined(VK_EXT_calibrated_timestamps)
    //    TracyVkCtx tracyCtx_ = TracyVkContextCalibrated(
    //        mPhysicalDevice, mDebugMessenger, mGraphicsQueueFamily,
    //        commandBuffer,
    //        vkGetPhysicalDeviceCalibrateableTimeDomainsKHR,
    //        vkGetCalibratedTimestampsKHR);
    //#else
    //    TracyVkCtx tracyCtx_ = TracyVkContext(
    //        physicalDevice, device, graphicsQueueIndex,
    //        commandBuffer);
    //#endif
}

void VulkanApp::initFrameResources()
{
    //create a command pool for commands submitted to the graphics queue.
    //we also want the pool to allow for resetting of individual command buffers
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(mGraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);


    for (int i = 0; i < FRAME_OVERLAP; i++) {
        /// Create command pool for each frame
        VK_CHECK(vkCreateCommandPool(mDevice, &commandPoolInfo, nullptr, &mFrames[i].mCommandPool));


        /// Allocate a command buffer per frame for frame submission.
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(mFrames[i].mCommandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &cmdAllocInfo, &mFrames[i].mMainCommandBuffer));

        /// Create sync primitives needed for frame submission.
        VkSemaphoreCreateInfo semCreateInfo = vkinit::semaphoreCreateInfo(0);
        VK_CHECK(vkCreateSemaphore(mDevice, &semCreateInfo, nullptr, &mFrames[i].mImageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(mDevice, &semCreateInfo, nullptr, &mFrames[i].mRenderFinishedSemaphore));
        VkFenceCreateInfo fncCreateInfo = vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(mDevice, &fncCreateInfo, nullptr, &mFrames[i].mRenderFence));

        /// Create UBOs for camera matrices.
        VkBufferCreateInfo uboInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        uboInfo.size = sizeof(FrameData);
        uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocationCreateInfo uboAllocInfo = {};
        uboAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        uboAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        uboAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &uboInfo, &uboAllocInfo, &mFrames[i].mFrameDataBuffer.mBuffer, &mFrames[i].mFrameDataBuffer.mAllocation, &mFrames[i].mFrameDataBuffer.mAllocInfo));
    }
}

void VulkanApp::initGlobalResources()
{
    // Create a command pool for immediate submits.
    const VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(mGraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK(vkCreateCommandPool(mDevice, &commandPoolInfo, nullptr, &mImmCommandPool));

    // Allocate a command buffer for immediate submits.
    const VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(mImmCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cmdAllocInfo, &mImmCommandBuffer));
    mDeletionQueue.push_function([=]() {vkDestroyCommandPool(mDevice, mImmCommandPool, nullptr);});

    // Create a fence for immediate submits.
    const VkFenceCreateInfo fncCreateInfo = vkinit::fenceCreateInfo();
    VK_CHECK(vkCreateFence(mDevice, &fncCreateInfo, nullptr, &mImmFence));
    mDeletionQueue.push_function([=]() { vkDestroyFence(mDevice, mImmFence, nullptr); });
}

void VulkanApp::initGlobalDescriptors()
{

    const VkDescriptorPoolSize size = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2 };
    const VkDescriptorPoolCreateInfo info = { 
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2,
        .poolSizeCount = 1,
        .pPoolSizes = &size
    };
    VK_CHECK(vkCreateDescriptorPool(mDevice, &info, nullptr, &mGlobalDescriptorPool));
    mDeletionQueue.push_function([&]() {vkDestroyDescriptorPool(mDevice, mGlobalDescriptorPool, nullptr);});

    DescriptorLayoutBuilder builder;
    builder.clear();
    builder.addBinding(0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    mFrameDataDescriptorSetLayout = builder.build(mDevice, VK_SHADER_STAGE_ALL);
    mDeletionQueue.push_function([&]() {vkDestroyDescriptorSetLayout(mDevice, mFrameDataDescriptorSetLayout, nullptr);});

    // The descriptors for the frame ubo's aren't updated per-frame, so we can bind them once outside the main loop.
    for (int i = 0; i < FRAME_OVERLAP; ++i)
    {
        VkDescriptorSetAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = mGlobalDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &mFrameDataDescriptorSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(mDevice, &allocInfo, &mFrames[i].mFrameDataDescriptorSet));

        const VkDescriptorBufferInfo  bufferInfo = {
            .buffer = mFrames[i].mFrameDataBuffer.mBuffer,
            .range = sizeof(FrameData)
        };
        const VkWriteDescriptorSet bufferWrite = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mFrames[i].mFrameDataDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1, // Remember that a single descriptor can refer to an array of resources.
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo
        };
        vkUpdateDescriptorSets(mDevice, 1, &bufferWrite, 0, nullptr);
    }



    // create a descriptor pool that will hold a large number of sets sets with 1 image each
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    { 
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
    };
    mGlobalDescriptorAllocator.initPool(mDevice, 4000, sizes);
    mDeletionQueue.push_function([&]() {mGlobalDescriptorAllocator.destroyPool(mDevice);});

    // Build the descriptor layout.
    builder.clear();
    builder.addBinding(0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    mMeshDescriptorSetLayout = builder.build(mDevice, VK_SHADER_STAGE_FRAGMENT_BIT);
    mDeletionQueue.push_function([&]() {vkDestroyDescriptorSetLayout(mDevice, mMeshDescriptorSetLayout, nullptr);});

}

void VulkanApp::initMeshPipeline()
{
    VkShaderModule triangleVertexShader;
    if (!loadShaderModule("../../shaders/mesh.vert.spv", mDevice, &triangleVertexShader)) {
        fmt::print("Error when building the fragment shader module");
    }

    VkShaderModule triangleFragShader;
    if (!loadShaderModule("../../shaders/mesh.frag.spv", mDevice, &triangleFragShader)) {
        fmt::print("Error when building the vertex shader module");
    }
    std::vector<VkPipelineShaderStageCreateInfo> shaders;
    shaders.resize(2);
    shaders[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaders[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaders[0].module = triangleVertexShader;
    shaders[0].pName = "main";

    shaders[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaders[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaders[1].module = triangleFragShader;
    shaders[1].pName = "main";

    // The pipeline layout defines an interface for shader resources used by the pipeline.
    const std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { mFrameDataDescriptorSetLayout, mMeshDescriptorSetLayout };
    const VkPushConstantRange bufferRange = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(GPUDrawPushConstants) };
    const VkPipelineLayoutCreateInfo pipeline_layout_info = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &bufferRange
    };
    VK_CHECK(vkCreatePipelineLayout(mDevice, &pipeline_layout_info, nullptr, &mMeshPipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.stageCount = uint32_t(shaders.size());
    pipelineInfo.pStages = shaders.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology                  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable    = VK_FALSE;
    pipelineInfo.pInputAssemblyState        = &inputAssembly;

    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;
    pipelineInfo.pViewportState = &viewportState;

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    pipelineInfo.pRasterizationState = &rasterizer;

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pMultisampleState = &multisampling;

    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE; //TODO: check
    depthStencil.depthCompareOp         = VK_COMPARE_OP_LESS_OR_EQUAL; //TODO: check
    depthStencil.depthBoundsTestEnable  = VK_FALSE;
    depthStencil.stencilTestEnable      = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {}; 
    depthStencil.minDepthBounds = 0.f;
    depthStencil.maxDepthBounds = 1.f;
    pipelineInfo.pDepthStencilState = &depthStencil;


    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    pipelineInfo.pColorBlendState = &colorBlending;

    pipelineInfo.layout = mMeshPipelineLayout;

    std::vector<VkDynamicState> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    pipelineInfo.pDynamicState = &dynamicState;

    VkPipelineRenderingCreateInfo rInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rInfo.colorAttachmentCount      = 1;
    rInfo.pColorAttachmentFormats   = &mSwapchainImageFormat;
    rInfo.depthAttachmentFormat     = mDepthImage.mFormat;
    pipelineInfo.pNext = &rInfo;

    if (VkResult err = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pipelineInfo,
        nullptr, &mMeshPipeline)
        )
    {
        fmt::println("failed to create pipeline, {}", string_VkResult(err));
    }

    //PipelineBuilder pipelineBuilder;
    //pipelineBuilder.clear();
    //pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
    //pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    //pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //pipelineBuilder.set_multisampling_none();
    //pipelineBuilder.disable_blending();
    //pipelineBuilder.disable_depthtest();

    ////connect the image format we will draw into, from draw image
    //pipelineBuilder.set_color_attachment_format(mSwapchainImageFormat);
    //pipelineBuilder.set_depth_format(VK_FORMAT_UNDEFINED);

    //_trianglePipeline = pipelineBuilder.build_pipeline(mDevice);

    //clean structures
    vkDestroyShaderModule(mDevice, triangleFragShader, nullptr);
    vkDestroyShaderModule(mDevice, triangleVertexShader, nullptr);

    mDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(mDevice, mMeshPipelineLayout, nullptr);
        vkDestroyPipeline(mDevice, mMeshPipeline, nullptr);
        });
}



void VulkanApp::run()
{

    //Allocate a descriptor set for each primitive.
    for (int i = 0; i < mMesh.mPrimitives.size(); ++i)
    {
        mMeshDescriptorSets.emplace_back(mGlobalDescriptorAllocator.allocate(mDevice, mMeshDescriptorSetLayout));
        const auto tex = mMesh.mPrimitives[i].textureID != UINT32_MAX ? mMesh.mPrimitives[i].textureID : 0;
        const VkDescriptorImageInfo textureDescriptor = {
            .sampler = mTexture.mSampler,
            .imageView = mMesh.mTextures[tex].mImage.mView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        const VkWriteDescriptorSet imageWrite = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mMeshDescriptorSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1, // Remember that a single descriptor can refer to an array of resources.
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &textureDescriptor
        };
        vkUpdateDescriptorSets(mDevice, 1, &imageWrite, 0, nullptr);
    }

    static auto lastFrameTime = std::chrono::high_resolution_clock::now();
    static auto elapsed = 0.f;
    static auto elapsedFrames = 0u;
    // Main loop
    while (!glfwWindowShouldClose(mWindow)) {

        // Record the current frame's start time
        const auto currentFrameTime = std::chrono::high_resolution_clock::now();
        // Compute delta time in seconds
        const auto deltaTime = std::chrono::duration<float, std::ratio<1>>(currentFrameTime - lastFrameTime).count();
        elapsed += deltaTime;
        ++elapsedFrames;
        if (elapsed >= 1.0f) {
            auto fps = elapsedFrames/ elapsed;
            // Reset counters
            elapsedFrames = 0;
            elapsed = 0.0f;
            glfwSetWindowTitle(mWindow, std::to_string(fps).c_str());
        }
        
        lastFrameTime = currentFrameTime;

        static glm::vec3 camPos     = glm::vec3(0.f, 0.f, 2.f);
        static glm::vec3 forward    = glm::vec3(0.f,0.f,-1.f);

        if (glfwGetKey(mWindow, GLFW_KEY_W) == GLFW_PRESS)
            camPos += glm::vec3(0.f, 0.1f, 0.f) ;
            //camPos = glm::translate(glm::mat4(1.f), { 0.f,0.1f,0.f }) * camPos;
        //camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(mWindow, GLFW_KEY_S) == GLFW_PRESS)
            //camera.ProcessKeyboard(BACKWARD, deltaTime);
            camPos += glm::vec3(0.f, -0.1f, 0.f) ;
        //camPos = glm::translate(camPos, { 0.f,-0.01f,0.f });
        if (glfwGetKey(mWindow, GLFW_KEY_A) == GLFW_PRESS)
            //camera.ProcessKeyboard(LEFT, deltaTime);
            camPos += glm::vec3(-0.1f, 0.f, 0.f) ;
        //camPos = glm::translate(camPos, { -0.01f,0.f,0.f });
        if (glfwGetKey(mWindow, GLFW_KEY_D) == GLFW_PRESS)
            camPos += glm::vec3(0.1f, 0.f, 0.f) ;
        //camPos = glm::translate(camPos, { 0.01f,0.f,0.f });
        //camera.ProcessKeyboard(RIGHT, deltaTime);
        if (glfwGetKey(mWindow, GLFW_KEY_Q) == GLFW_PRESS)
            forward = glm::rotate(glm::mat4(1.f), glm::radians(1.f), { 0.f,1.f,0.f }) * glm::vec4(forward, 0.f);
        if (glfwGetKey(mWindow, GLFW_KEY_E) == GLFW_PRESS)
            forward = glm::rotate(glm::mat4(1.f), glm::radians(-1.f), { 0.f,1.f,0.f }) * glm::vec4(forward, 0.f);
    
        // Wait for the other frame to finish by waiting on it's fence.
        VK_CHECK(vkWaitForFences(mDevice, 1, &getCurrentFrame().mRenderFence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(mDevice, 1, &getCurrentFrame().mRenderFence));

        /// Acquire an image to render to from the swap chain.
        uint32_t swapchainImageIndex;
        vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, getCurrentFrame().mImageAvailableSemaphore, nullptr, &swapchainImageIndex);

        // Update the uniform buffer for the next frame
        //auto view = glm::translate(glm::mat4(1.f), { 0.f, 0.f, -2.f });
        auto view       = glm::lookAt(camPos, camPos + forward/*glm::vec3(0.f)*/, { 0.f,1.f,0.f });
        auto proj       = glm::perspective(glm::radians(70.f), float(mSwapchainExtent.width) / mSwapchainExtent.height, 0.01f, 1000.f);
        proj[1][1]      *= -1;
        const auto viewProj =  proj * view;
        FrameData frameData = { .view = view, .proj = proj, .viewProj = viewProj};
        // Copy data to UBO. Note that we specified the memory to be host coherent, so the write is immediately visible to the GPU.
        memcpy(getCurrentFrame().mFrameDataBuffer.mAllocInfo.pMappedData, &frameData, sizeof(FrameData));

        // Build the command buffer for this frame's render commands.
        VkCommandBuffer cmd = getCurrentFrame().mMainCommandBuffer;
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
        {

            //TODO: Fix masks.
            // Transition swapchain color and depth images layouts for output.
            std::array<VkImageMemoryBarrier2, 2> swapchainOutputBarriers;
            // Swapchain color image
            swapchainOutputBarriers[0] = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask       = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask      = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask       = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask      = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout          = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout          = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image              = mSwapchainImages[swapchainImageIndex],
                .subresourceRange   = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            // Swapchain depth image.
            swapchainOutputBarriers[1] = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .image = mDepthImage.mImage,
                .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
            };
            const VkDependencyInfo depInfo = { 
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 2,
                .pImageMemoryBarriers = swapchainOutputBarriers.data()
            };
            vkCmdPipelineBarrier2(cmd, &depInfo);
            

            // Define the attachments to render to.
            const VkRenderingAttachmentInfo colorAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = mSwapchainImageViews[swapchainImageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE
            };
            const VkRenderingAttachmentInfo depthAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = mDepthImage.mView,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .clearValue = {.depthStencil = {.depth = 1.f}}
            };
            const VkRenderingInfo renderInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = VkRect2D{ VkOffset2D { 0, 0 }, mSwapchainExtent },
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = &depthAttachment
            };
            // Begin render pass instance.
            vkCmdBeginRendering(cmd, &renderInfo);

            // Update viewport state.
            const VkViewport viewport = {
                .x = 0.f, .y = 0.f,
                .width = static_cast<float>(mSwapchainExtent.width), .height = static_cast<float>(mSwapchainExtent.height),
                .minDepth = 0.f, .maxDepth = 1.f };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            // Update scissor state.
            const VkRect2D scissor = {
                .offset = {.x = 0, .y = 0},
                .extent = {.width = mSwapchainExtent.width, .height = mSwapchainExtent.height}, };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Bind mesh pipeline
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMeshPipeline);

            // Bind descriptors
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMeshPipelineLayout, 0, 1, &getCurrentFrame().mFrameDataDescriptorSet, 0, nullptr); 
            //vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMeshPipelineLayout, 1, 1, &mMeshDescriptorSets[0], 0, nullptr);

            // Bind mesh index buffer.
            vkCmdBindIndexBuffer(cmd, mMesh.mBuffers.mIndexBuffer.mBuffer, 0, VK_INDEX_TYPE_UINT32);
            
            for (int i =0; i <mMesh.mPrimitives.size(); ++i)
            {
                auto& prim = mMesh.mPrimitives[i];

                // Push push constants.
                const glm::mat4 model = glm::mat4(1.f);
                GPUDrawPushConstants push_constants = {
                    .mWorldMatrix = glm::scale(model, glm::vec3(0.1f)),
                    .mVertexBufferAddress = mMesh.mBuffers.mVertexBufferAddress
                };
                vkCmdPushConstants(cmd, mMeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
                // Bind the descriptor set for the primitive's resources.
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMeshPipelineLayout, 1, 1, &mMeshDescriptorSets[i], 0, nullptr);
               
                // Draw primitive.
                vkCmdDrawIndexed(cmd, prim.indexCount, 1, prim.firstIndex, 0, 0);
            }
            // End render pass.
            vkCmdEndRendering(cmd);

            // Transition swapchain color image into one suitable for presentation.
            // Note that the depth image doesn't need another transition as it is not presented.
            const VkImageMemoryBarrier2 colorPresentBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .image = mSwapchainImages[swapchainImageIndex],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            const VkDependencyInfo presentDepInfo= { 
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &colorPresentBarrier
            };
            vkCmdPipelineBarrier2(cmd, &presentDepInfo);
            
        }
        VK_CHECK(vkEndCommandBuffer(cmd));
       
        // Submit the command buffer.
        const VkCommandBufferSubmitInfo cInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
            .deviceMask = 0
        };
        const VkSemaphoreSubmitInfo acquireCompleteInfo = { 
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = getCurrentFrame().mImageAvailableSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            .deviceIndex = 0
        };
        const VkSemaphoreSubmitInfo renderingCompleteInfo = { 
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = getCurrentFrame().mRenderFinishedSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            .deviceIndex = 0
        };
        const VkSubmitInfo2 submitInfo = { 
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, 
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &acquireCompleteInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &renderingCompleteInfo
        };
        VK_CHECK(vkQueueSubmit2(mGraphicsQueue, 1, &submitInfo, getCurrentFrame().mRenderFence));

        // Queue presentation. The GPU will wait on the semaphore before presenting. We can then immediately start working on the next frame.
        const VkPresentInfoKHR info = { 
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &getCurrentFrame().mRenderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &mSwapchain,
            .pImageIndices = &swapchainImageIndex
        };
        VK_CHECK(vkQueuePresentKHR(mGraphicsQueue, &info));

        glfwPollEvents();
        // Set the index of the next frame to render to
        ++mFrameNumber;
    }

    VK_CHECK(vkDeviceWaitIdle(mDevice));

    for (int i = 0; i < FRAME_OVERLAP; ++i)
    {
        vkDestroyFence(mDevice, mFrames[i].mRenderFence, nullptr);
        vkDestroySemaphore(mDevice, mFrames[i].mImageAvailableSemaphore, nullptr);
        vkDestroySemaphore(mDevice, mFrames[i].mRenderFinishedSemaphore, nullptr);

        vkDestroyCommandPool(mDevice, mFrames[i].mCommandPool, nullptr);

        vmaDestroyBuffer(mVmaAllocator, mFrames[i].mFrameDataBuffer.mBuffer, mFrames[i].mFrameDataBuffer.mAllocation);
    }
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

// Submit operations to the queue, and wait for them to complete.
void VulkanApp::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    // Reset fence so it can be re-signalled.
    VK_CHECK(vkResetFences(mDevice, 1, &mImmFence));
    // Reset command buffer.
    VK_CHECK(vkResetCommandBuffer(mImmCommandBuffer, 0));
    // Begin recording.
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(mImmCommandBuffer, &cmdBeginInfo));
    // Call function
    function(mImmCommandBuffer);
    // End recording
    VK_CHECK(vkEndCommandBuffer(mImmCommandBuffer));
    // Submit to queue, passing a fence
    const  VkCommandBufferSubmitInfo cmdinfo = { 
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = mImmCommandBuffer,
        .deviceMask = 0
    };
    const VkSubmitInfo2 submit = {
        .sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos    = &cmdinfo
    };
    VK_CHECK(vkQueueSubmit2(mGraphicsQueue, 1, &submit, mImmFence));
    // Wait on the fence.
    VK_CHECK(vkWaitForFences(mDevice, 1, &mImmFence, true, 9999999999));
}

void VulkanApp::createSwapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ mPhysicalDevice, mDevice, mSurface };

    mSwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = mSwapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(/*VK_PRESENT_MODE_IMMEDIATE_KHR*/VK_PRESENT_MODE_FIFO_KHR) // TODO: read.
        .set_desired_extent(width, height) // Set resolution of swapchain images (should be window resolution).
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    //store swapchain and its related images
    mSwapchain              = vkbSwapchain.swapchain;
    mSwapchainImages        = vkbSwapchain.get_images().value();
    mSwapchainImageViews    = vkbSwapchain.get_image_views().value();
    mSwapchainExtent        = vkbSwapchain.extent;


    // Create depth buffer for swapchain.
    mDepthImage.mFormat = VK_FORMAT_D32_SFLOAT;
    mDepthImage.mExtents = { mSwapchainExtent.width, mSwapchainExtent.height, 1 };

    // allocate and create the image for the depth buffer
    VkImageCreateInfo info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType      = VK_IMAGE_TYPE_2D;
    info.format         = mDepthImage.mFormat;
    info.extent         = mDepthImage.mExtents;
    info.mipLevels      = 1;
    info.arrayLayers    = 1;
    info.samples        = VK_SAMPLE_COUNT_1_BIT;
    info.tiling         = VK_IMAGE_TILING_OPTIMAL;
    info.usage          = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo rimg_allocinfo = {
        .usage          = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags  = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    VK_CHECK(vmaCreateImage(mVmaAllocator, &info, &rimg_allocinfo, &mDepthImage.mImage, &mDepthImage.mAllocation, nullptr));

    // create the image view for the depth buffer.
    VkImageViewCreateInfo viewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.viewType                           = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.image                              = mDepthImage.mImage;
    viewInfo.format                             = mDepthImage.mFormat;
    viewInfo.subresourceRange.baseMipLevel      = 0;
    viewInfo.subresourceRange.levelCount        = 1;
    viewInfo.subresourceRange.baseArrayLayer    = 0;
    viewInfo.subresourceRange.layerCount        = 1;
    viewInfo.subresourceRange.aspectMask        = VK_IMAGE_ASPECT_DEPTH_BIT;
    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthImage.mView));
    mDeletionQueue.push_function([=]() {
        // TODO: Figure out why destroyImage doesnt work...
        //scvk::destroyImage(mDevice, mVmaAllocator, mDepthImage);
        vkDestroyImageView(mDevice, mDepthImage.mView, nullptr);
        vmaDestroyImage(mVmaAllocator, mDepthImage.mImage, mDepthImage.mAllocation);
        });
}


// TODO: ()
/// Note that this pattern is not very efficient, as we are waiting for the GPU command to fully execute before continuing with our CPU side logic.
/// This is something people generally put on a background thread, whose sole job is to execute uploads like this one, and deleting/reusing the staging buffers.
/// TODO: Profile.

// Uploads the vertices and indices of a mesh to the GPU
// and returns the associated GPU buffers needed for rendering.

GPUMeshBuffers VulkanApp::uploadMeshData(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    GPUMeshBuffers newSurface;
    newSurface.mVertexBuffer.mSizeBytes = vertices.size() * sizeof(Vertex);
    newSurface.mIndexBuffer.mSizeBytes = indices.size() * sizeof(uint32_t);

    //create vertex buffer & get it's address.
    VkBufferCreateInfo deviceBufferCreateInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    deviceBufferCreateInfo.size           = newSurface.mVertexBuffer.mSizeBytes;
    deviceBufferCreateInfo.usage          = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    deviceBufferCreateInfo.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
    const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &newSurface.mVertexBuffer.mBuffer, &newSurface.mVertexBuffer.mAllocation, &newSurface.mVertexBuffer.mAllocInfo));

    newSurface.mVertexBufferAddress = scvk::GetBufferDeviceAddress(mDevice, newSurface.mVertexBuffer);

    // Create index buffer
    deviceBufferCreateInfo.size = newSurface.mIndexBuffer.mSizeBytes;
    deviceBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    deviceBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &newSurface.mIndexBuffer.mBuffer, &newSurface.mIndexBuffer.mAllocation, &newSurface.mIndexBuffer.mAllocInfo));
   
    // Create staging buffer to hold both vertices and indices.
    scvk::Buffer staging_buffer;

    VkBufferCreateInfo stagingBufferCreateInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingBufferCreateInfo.size = newSurface.mVertexBuffer.mSizeBytes + newSurface.mIndexBuffer.mSizeBytes;
    stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VmaAllocationCreateInfo stagingBufferAllocInfo = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingBufferCreateInfo, &stagingBufferAllocInfo, &staging_buffer.mBuffer, &staging_buffer.mAllocation, &staging_buffer.mAllocInfo));


    // Copy data into staging buffer.
    void* data;
    vmaMapMemory(mVmaAllocator, staging_buffer.mAllocation, (void**)&data);
    memcpy(data, vertices.data(), newSurface.mVertexBuffer.mSizeBytes);
    memcpy((char*)data + newSurface.mVertexBuffer.mSizeBytes, indices.data(), newSurface.mIndexBuffer.mSizeBytes);
    vmaUnmapMemory(mVmaAllocator, staging_buffer.mAllocation);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.dstOffset    = 0;
        vertexCopy.srcOffset    = 0;
        vertexCopy.size         = newSurface.mVertexBuffer.mSizeBytes;

        vkCmdCopyBuffer(cmd, staging_buffer.mBuffer, newSurface.mVertexBuffer.mBuffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = newSurface.mVertexBuffer.mSizeBytes;
        indexCopy.size      = newSurface.mIndexBuffer.mSizeBytes;

        vkCmdCopyBuffer(cmd, staging_buffer.mBuffer, newSurface.mIndexBuffer.mBuffer, 1, &indexCopy);
    });

    // No longer need the staging buffer.
    vmaDestroyBuffer(mVmaAllocator, staging_buffer.mBuffer, staging_buffer.mAllocation);

    return newSurface;

}

scvk::Texture VulkanApp::uploadTexture(const char* path)
{
    int width, height;
    unsigned char* imageData = stbi_load(path, &width, &height, nullptr, 4);
    return uploadTexture(imageData, width, height);
}

scvk::Texture VulkanApp::uploadTexture(unsigned char* imageData, int width, int height)
{

    scvk::Image image;
    image.mFormat = VK_FORMAT_R8G8B8A8_SRGB;
    image.mExtents = { static_cast<uint32_t>(width),  static_cast<uint32_t>(height), 1 };

    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = image.mExtents;
    imageInfo.format = image.mFormat;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo imageCreateInfo = {};
    imageCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    imageCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(mVmaAllocator, &imageInfo, &imageCreateInfo, &image.mImage, &image.mAllocation, nullptr));

    VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image.mImage;
    viewInfo.format = image.mFormat;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;

    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &image.mView));

    // Create staging buffer and copy image data to it.
    scvk::Buffer staging = scvk::createHostVisibleStagingBuffer(mVmaAllocator, image.mExtents.width * image.mExtents.height * 4);

    void* data;
    vmaMapMemory(mVmaAllocator, staging.mAllocation, (void**)&data);
    memcpy(data, imageData, image.mExtents.width * image.mExtents.height * 4);
    vmaUnmapMemory(mVmaAllocator, staging.mAllocation);

    // Transfer data from staging buffer to image.

    immediateSubmit([&](VkCommandBuffer cmd) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT,
        0,1,
        0,1 };

    VkImageMemoryBarrier2 preCopyMemoryBarrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    preCopyMemoryBarrier.image = image.mImage;
    preCopyMemoryBarrier.srcAccessMask = 0;
    preCopyMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    preCopyMemoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
    preCopyMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
    preCopyMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    preCopyMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    preCopyMemoryBarrier.subresourceRange = range;

    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &preCopyMemoryBarrier;

    vkCmdPipelineBarrier2(cmd, &dep);

    // Copy data from staging buffer to image.
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { image.mExtents.width, image.mExtents.height, 1 };


    vkCmdCopyBufferToImage(cmd, staging.mBuffer, image.mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier2 postCopyMemoryBarrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    postCopyMemoryBarrier.image = image.mImage;
    postCopyMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    postCopyMemoryBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    postCopyMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
    postCopyMemoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
    postCopyMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    postCopyMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    postCopyMemoryBarrier.subresourceRange = range;

    VkDependencyInfo depPost = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depPost.imageMemoryBarrierCount = 1;
    depPost.pImageMemoryBarriers = &postCopyMemoryBarrier;

    vkCmdPipelineBarrier2(cmd, &depPost);

        });

    vmaDestroyBuffer(mVmaAllocator, staging.mBuffer, staging.mAllocation);


    // Create a sampler for the texture
    VkSampler sampler;
    VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    //VkSamplerMipmapMode     mipmapMode;
    //VkSamplerAddressMode    addressModeU;
    //VkSamplerAddressMode    addressModeV;
    //VkSamplerAddressMode    addressModeW;
    //float                   mipLodBias;
    //VkBool32                anisotropyEnable;
    //float                   maxAnisotropy;
    //VkBool32                compareEnable;
    //VkCompareOp             compareOp;
    //float                   minLod;
    //float                   maxLod;
    //VkBorderColor           borderColor;
    //VkBool32                unnormalizedCoordinates;

    VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &sampler));

    scvk::Texture texture;
    texture.mImage = image;
    texture.mSampler = sampler;
    texture.mMipLevels = 1;

    return texture;
}