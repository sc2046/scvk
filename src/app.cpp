#include <iostream>

#include <volk.h>

#include "app.h"
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

uint32_t numIndices;

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
    initFrameResources();
    initGlobalResources();
    //initDescriptors();
    initMeshPipeline();


    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    loadMeshFromfile("../../assets/monkey_smooth.obj", vertices, indices);

    mesh        = uploadMesh(indices, vertices);
    numIndices  = indices.size();

    ////delete the rectangle data on engine shutdown
    mDeletionQueue.push_function([&]() {
        vmaDestroyBuffer(mVmaAllocator, mesh.mVertexBuffer.mBuffer, mesh.mVertexBuffer.mAllocation);
        vmaDestroyBuffer(mVmaAllocator, mesh.mIndexBuffer.mBuffer, mesh.mIndexBuffer.mAllocation);
        });
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

void VulkanApp::initFrameResources()
{
    //create a command pool for commands submitted to the graphics queue.
    //we also want the pool to allow for resetting of individual command buffers
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(mGraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        ///
        /// Create command pool for each frame
        /// 
        VK_CHECK(vkCreateCommandPool(mDevice, &commandPoolInfo, nullptr, &mFrames[i].mCommandPool));


        ///
        /// Allocate a command buffer per frame for frame submission.
        /// 
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(mFrames[i].mCommandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(mDevice, &cmdAllocInfo, &mFrames[i].mMainCommandBuffer));

        ///
        /// Create sync primitives needed for frame submission.
        /// 
        VkSemaphoreCreateInfo semCreateInfo = vkinit::semaphoreCreateInfo(0);
        VK_CHECK(vkCreateSemaphore(mDevice, &semCreateInfo, nullptr, &mFrames[i].mImageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(mDevice, &semCreateInfo, nullptr, &mFrames[i].mRenderFinishedSemaphore));
        VkFenceCreateInfo fncCreateInfo = vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK(vkCreateFence(mDevice, &fncCreateInfo, nullptr, &mFrames[i].mRenderFence));
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

void VulkanApp::initDescriptors()
{
    ////create a descriptor pool that will hold 10 sets with 1 image each
    //std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    //{
    //    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    //};

    //mGlobalDescriptorAllocator.initPool(mDevice, 10, sizes);

    ////make the descriptor set layout for our compute draw
    //{
    //    DescriptorLayoutBuilder builder;
    //    builder.addBinding(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    //    mDrawImageDescriptorLayout = builder.build(mDevice, VK_SHADER_STAGE_COMPUTE_BIT);
    //}
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


    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    //build the pipeline layout that controls the inputs/outputs of the shader
    // TODO: add descriptor support.
    VkPipelineLayoutCreateInfo pipeline_layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges    = &bufferRange;

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

void VulkanApp::draw(VkCommandBuffer cmd)
{
    // Define the attachments to render to.
    VkRenderingAttachmentInfo colorAttachment   = {.sType =  VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView                   = mSwapchainImageViews[mSwapchainImageIndex];
    colorAttachment.imageLayout                 = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp                      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.clearValue.color            = {0.f,0.f,0.f,0.f};

    // The storeOp specifies what to do with the data after rendering. In this case we want to store it in memory.
    // For example, the contents of a depth buffer could be discarded after rendering, unless is used in subsequent passes.
    colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment       = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView                       = mDepthImage.mImageView;
    depthAttachment.imageLayout                     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp                          = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp                         = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth   = 1.f;

    VkRenderingInfo renderInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, mSwapchainExtent };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;
    renderInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(cmd, &renderInfo);

    //set dynamic viewport and scissor
    const VkViewport viewport = {
        0,0,                                                // x,y
        mSwapchainExtent.width, mSwapchainExtent.height,    // width, height
        0.f,1.f                                             // min, max
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor = {
        0,0,                                                // x,y
        mSwapchainExtent.width, mSwapchainExtent.height,    // width, height
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind mesh pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMeshPipeline);

    // Push push constants.
    GPUDrawPushConstants push_constants = {
        .mWorldMatrix           = glm::mat4{ 1.f },
        .mVertexBufferAddress   = mesh.mVertexBufferAddress
    };

    //make a model view matrix for rendering the object
    glm::mat4 view          = glm::translate(glm::mat4(1.f), { 0.f,0.f,-2.f });
    glm::mat4 projection    = glm::perspective(glm::radians(70.f), float(mSwapchainExtent.width) / mSwapchainExtent.height, 0.01f, 1000.f);
    projection[1][1]        *= -1;
    glm::mat4 model         = glm::rotate(glm::mat4(1.f), glm::radians(10.f * float(glfwGetTime())), glm::vec3( 0.f,1.f,0.f ));
    push_constants.mWorldMatrix = projection * view * model;
    
    vkCmdPushConstants(cmd, mMeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);

    // Render indexed mesh.
    vkCmdBindIndexBuffer(cmd, mesh.mIndexBuffer.mBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, numIndices, 1, 0, 0, 0);

    vkCmdEndRendering(cmd);
}


void VulkanApp::run()
{

    // Main loop
    while (!glfwWindowShouldClose(mWindow)) {
        glfwPollEvents();

        ///
        /// Wait for the other frame to finish by waiting on it's fence.
        /// 
        VK_CHECK(vkWaitForFences(mDevice, 1, &getCurrentFrame().mRenderFence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(mDevice, 1, &getCurrentFrame().mRenderFence));

        ///
        /// Acquire an image to render to from the swap chain.
        /// 
        vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, getCurrentFrame().mImageAvailableSemaphore, nullptr, &mSwapchainImageIndex);


        ///
        /// Record draw commands in a command buffer
        /// 
        VkCommandBuffer cmd = getCurrentFrame().mMainCommandBuffer;
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
        {
            // Define a common range as both transitions use the fill image.
            VkImageSubresourceRange imageRange;
            imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageRange.baseMipLevel = 0;
            imageRange.levelCount = VK_REMAINING_MIP_LEVELS;
            imageRange.baseArrayLayer = 0;
            imageRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

            // Transition swapchain images into one suitable for drawing.
            //TODO: Fix masks.
            {
                std::array<VkImageMemoryBarrier2, 2> imageBarriers;

                // Swapchain color image
                imageBarriers[0] = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                imageBarriers[0].srcStageMask       = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarriers[0].srcAccessMask      = VK_ACCESS_2_MEMORY_WRITE_BIT;
                imageBarriers[0].dstStageMask       = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarriers[0].dstAccessMask      = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
                imageBarriers[0].oldLayout          = VK_IMAGE_LAYOUT_UNDEFINED;
                imageBarriers[0].newLayout          = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; 
                imageBarriers[0].subresourceRange   = imageRange;
                imageBarriers[0].image              = mSwapchainImages[mSwapchainImageIndex];

                // Swapchain depth image.
                VkImageSubresourceRange depthRange;
                depthRange.aspectMask       = VK_IMAGE_ASPECT_DEPTH_BIT;
                depthRange.baseMipLevel     = 0;
                depthRange.levelCount       = VK_REMAINING_MIP_LEVELS;
                depthRange.baseArrayLayer   = 0;
                depthRange.layerCount       = VK_REMAINING_ARRAY_LAYERS;
                imageBarriers[1] = { .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                imageBarriers[1].srcStageMask       = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarriers[1].srcAccessMask      = VK_ACCESS_2_MEMORY_WRITE_BIT;
                imageBarriers[1].dstStageMask       = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarriers[1].dstAccessMask      = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
                imageBarriers[1].oldLayout          = VK_IMAGE_LAYOUT_UNDEFINED;
                imageBarriers[1].newLayout          = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                imageBarriers[1].subresourceRange   = depthRange;
                imageBarriers[1].image              = mDepthImage.mImage;

                VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                depInfo.imageMemoryBarrierCount = 2;
                depInfo.pImageMemoryBarriers    = imageBarriers.data();

                vkCmdPipelineBarrier2(cmd, &depInfo);
            }

            draw(cmd);

            // Transition swapchain image into one suitable for presentation.
            {
                VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
                imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                imageBarrier.subresourceRange = imageRange;
                imageBarrier.image = mSwapchainImages[mSwapchainImageIndex];

                VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                depInfo.imageMemoryBarrierCount = 1;
                depInfo.pImageMemoryBarriers = &imageBarrier;

                vkCmdPipelineBarrier2(cmd, &depInfo);
            }
        }
        VK_CHECK(vkEndCommandBuffer(cmd));
       
        ///
        /// Submit the command buffer.
        /// 
        
        VkCommandBufferSubmitInfo cInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cInfo.commandBuffer = cmd;
        cInfo.deviceMask = 0;

        VkSemaphoreSubmitInfo acquireCompleteInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        acquireCompleteInfo.semaphore   = getCurrentFrame().mImageAvailableSemaphore;
        acquireCompleteInfo.deviceIndex = 0;
        acquireCompleteInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;

        VkSemaphoreSubmitInfo renderingCompleteInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        renderingCompleteInfo.semaphore    = getCurrentFrame().mRenderFinishedSemaphore;
        renderingCompleteInfo.deviceIndex  = 0;
        renderingCompleteInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
        
        VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submitInfo.waitSemaphoreInfoCount      = 1;
        submitInfo.pWaitSemaphoreInfos         = &acquireCompleteInfo;
        submitInfo.commandBufferInfoCount      = 1;
        submitInfo.pCommandBufferInfos         = &cInfo;
        submitInfo.signalSemaphoreInfoCount    = 1;
        submitInfo.pSignalSemaphoreInfos       = &renderingCompleteInfo;

        VK_CHECK(vkQueueSubmit2(mGraphicsQueue, 1, &submitInfo, getCurrentFrame().mRenderFence));

        ///
        /// Present the swapchain image.
        /// 
        VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &getCurrentFrame().mRenderFinishedSemaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &mSwapchain;
        info.pImageIndices = &mSwapchainImageIndex;
        VK_CHECK(vkQueuePresentKHR(mGraphicsQueue, &info));

        ++mFrameNumber;
    }

    VK_CHECK(vkDeviceWaitIdle(mDevice));

    for (int i = 0; i < FRAME_OVERLAP; ++i)
    {
        vkDestroyFence(mDevice, mFrames[i].mRenderFence, nullptr);
        vkDestroySemaphore(mDevice, mFrames[i].mImageAvailableSemaphore, nullptr);
        vkDestroySemaphore(mDevice, mFrames[i].mRenderFinishedSemaphore, nullptr);

        vkDestroyCommandPool(mDevice, mFrames[i].mCommandPool, nullptr);
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
    VK_CHECK(vkResetFences(mDevice, 1, &mImmFence));
    VK_CHECK(vkResetCommandBuffer(mImmCommandBuffer, 0));

    // Begin recording.
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(mImmCommandBuffer, &cmdBeginInfo));

    function(mImmCommandBuffer);

    VK_CHECK(vkEndCommandBuffer(mImmCommandBuffer));

    VkCommandBufferSubmitInfo cmdinfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmdinfo.commandBuffer = mImmCommandBuffer;
    cmdinfo.deviceMask = 0;

    VkSubmitInfo2 submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdinfo;

    // Submit command buffer to the queue and wait on the associated fence.
    VK_CHECK(vkQueueSubmit2(mGraphicsQueue, 1, &submit, mImmFence));
    VK_CHECK(vkWaitForFences(mDevice, 1, &mImmFence, true, 9999999999));
}

void VulkanApp::createSwapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ mPhysicalDevice, mDevice, mSurface };

    mSwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = mSwapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // TODO: read.
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
    vmaCreateImage(mVmaAllocator, &info, &rimg_allocinfo, &mDepthImage.mImage, &mDepthImage.mAllocation, nullptr);
    mDeletionQueue.push_function([&]() {vmaDestroyImage(mVmaAllocator, mDepthImage.mImage, mDepthImage.mAllocation);});

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

    VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthImage.mImageView));
    mDeletionQueue.push_function([&]() {vkDestroyImageView(mDevice, mDepthImage.mImageView, nullptr);});
}


// TODO: ()
/// Note that this pattern is not very efficient, as we are waiting for the GPU command to fully execute before continuing with our CPU side logic.
/// This is something people generally put on a background thread, whose sole job is to execute uploads like this one, and deleting/reusing the staging buffers.
///

// Uploads the vertices and indices of a mesh to the GPU
// and returns the associated GPU buffers needed for rendering.
GPUMeshBuffers VulkanApp::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    //create vertex buffer & get it's address.
    VkBufferCreateInfo deviceBufferCreateInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    deviceBufferCreateInfo.size           = vertexBufferSize;
    deviceBufferCreateInfo.usage          = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    deviceBufferCreateInfo.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
    const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &newSurface.mVertexBuffer.mBuffer, &newSurface.mVertexBuffer.mAllocation, &newSurface.mVertexBuffer.mAllocInfo));

    newSurface.mVertexBufferAddress = scvk::GetBufferDeviceAddress(mDevice, newSurface.mVertexBuffer);

    // Create index buffer
    deviceBufferCreateInfo.size = indexBufferSize;
    deviceBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    deviceBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &newSurface.mIndexBuffer.mBuffer, &newSurface.mIndexBuffer.mAllocation, &newSurface.mIndexBuffer.mAllocInfo));
   
    // Create staging buffer to hold both vertices and indices.
    scvk::Buffer staging_buffer;

    VkBufferCreateInfo stagingBufferCreateInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingBufferCreateInfo.size = vertexBufferSize + indexBufferSize;
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
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);
    vmaUnmapMemory(mVmaAllocator, staging_buffer.mAllocation);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.dstOffset    = 0;
        vertexCopy.srcOffset    = 0;
        vertexCopy.size         = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging_buffer.mBuffer, newSurface.mVertexBuffer.mBuffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size      = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging_buffer.mBuffer, newSurface.mIndexBuffer.mBuffer, 1, &indexCopy);
    });

    // No longer need the staging buffer.
    vmaDestroyBuffer(mVmaAllocator, staging_buffer.mBuffer, staging_buffer.mAllocation);

    return newSurface;

}

