#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "asset_importer.h"
#include "raster/raster_shaders.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define VK_CHECK(x)                                                                                \
	do {                                                                                           \
		VkResult err = (x);                                                                        \
		if (err) {                                                                                 \
			std::cerr << "Detected Vulkan error: " << err << " at " << __FILE__ << ":" << __LINE__ \
			          << std::endl;                                                                \
			std::abort();                                                                          \
		}                                                                                          \
	} while (0)

enum class RenderStyle {
	Raster,
	RayTrace,
};

constexpr uint32_t FRAME_OVERLAP = 2;
constexpr uint32_t DEFAULT_WIDTH = 1920;
constexpr uint32_t DEFAULT_HEIGHT = 1080;
constexpr bool USE_VALIDATION_LAYERS = false;

struct FrameData {
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkFence inFlightFence = VK_NULL_HANDLE;
	VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
};

struct GpuMesh {
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
	VkBuffer indexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory indexMemory = VK_NULL_HANDLE;
	uint32_t indexCount = 0;
};

struct PushConstants {
	glm::mat4 mvp{1.0f};
};

class VulkanApp {
public:
	RenderStyle renderStyle = RenderStyle::Raster;

	void run() {
		init();
		mainLoop();
		cleanup();
	}

private:
	// --- Window / SDL ---
	SDL_Window* window = nullptr;
	uint32_t windowWidth = DEFAULT_WIDTH;
	uint32_t windowHeight = DEFAULT_HEIGHT;

	// --- Core Vulkan ---
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	uint32_t graphicsQueueFamily = 0;

	// --- Swapchain ---
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D swapchainExtent{};
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	std::vector<VkSemaphore> renderFinishedSemaphores;

	// --- Depth ---
	VkImage depthImage = VK_NULL_HANDLE;
	VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
	VkImageView depthImageView = VK_NULL_HANDLE;
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

	// --- Commands & sync ---
	std::array<FrameData, FRAME_OVERLAP> frames{};
	uint32_t frameNumber = 0;

	// --- Pipeline ---
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline graphicsPipeline = VK_NULL_HANDLE;

	// --- Scene ---
	std::vector<GpuMesh> meshes;

	void init() {
		if (!SDL_Init(SDL_INIT_VIDEO)) {
			throw std::runtime_error(std::string("Failed to initialize SDL: ") + SDL_GetError());
		}

		window = SDL_CreateWindow("Vulkan Raster Renderer", static_cast<int>(windowWidth),
		                          static_cast<int>(windowHeight), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
		if (window == nullptr) {
			throw std::runtime_error(std::string("Failed to create SDL window: ") + SDL_GetError());
		}

		init_vulkan();
		create_swapchain();
		create_depth_resources();
		create_render_sync();
		create_commands();
		create_pipeline();
		load_assets();
	}

	void init_vulkan() {
		vkb::InstanceBuilder instanceBuilder;
		auto instRet = instanceBuilder.set_app_name("CS665 Vulkan Renderer")
		                   .request_validation_layers(USE_VALIDATION_LAYERS)
		                   .use_default_debug_messenger()
		                   .require_api_version(1, 3, 0)
		                   .build();
		if (!instRet) {
			throw std::runtime_error("Failed to create Vulkan instance: " + instRet.error().message());
		}
		vkb::Instance vkbInst = instRet.value();
		instance = vkbInst.instance;
		debugMessenger = vkbInst.debug_messenger;

		if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
			throw std::runtime_error(std::string("Failed to create Vulkan surface: ") + SDL_GetError());
		}

		VkPhysicalDeviceVulkan13Features features13{};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.bufferDeviceAddress = VK_TRUE;
		features12.descriptorIndexing = VK_TRUE;

		vkb::PhysicalDeviceSelector selector{vkbInst};
		auto physRet = selector.set_minimum_version(1, 3)
		                   .set_required_features_13(features13)
		                   .set_required_features_12(features12)
		                   .set_surface(surface)
		                   .select();
		if (!physRet) {
			throw std::runtime_error("Failed to select a suitable GPU: " + physRet.error().message());
		}
		vkb::PhysicalDevice vkbPhys = physRet.value();
		physicalDevice = vkbPhys.physical_device;

		vkb::DeviceBuilder deviceBuilder{vkbPhys};
		auto devRet = deviceBuilder.build();
		if (!devRet) {
			throw std::runtime_error("Failed to create logical device: " + devRet.error().message());
		}
		vkb::Device vkbDevice = devRet.value();
		device = vkbDevice.device;

		auto queueRet = vkbDevice.get_queue(vkb::QueueType::graphics);
		auto queueFamilyRet = vkbDevice.get_queue_index(vkb::QueueType::graphics);
		if (!queueRet || !queueFamilyRet) {
			throw std::runtime_error("Failed to get graphics queue");
		}
		graphicsQueue = queueRet.value();
		graphicsQueueFamily = queueFamilyRet.value();
	}

	void create_swapchain() {
		vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};

		const VkSurfaceFormatKHR desiredFormat{
			.format = VK_FORMAT_B8G8R8A8_UNORM,
			.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		};

		auto swapRet = swapchainBuilder.set_desired_format(desiredFormat)
		                   .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		                   .set_desired_extent(windowWidth, windowHeight)
		                   .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		                   .build();
		if (!swapRet) {
			throw std::runtime_error("Failed to create swapchain: " + swapRet.error().message());
		}

		vkb::Swapchain vkbSwapchain = swapRet.value();
		swapchain = vkbSwapchain.swapchain;
		swapchainImageFormat = vkbSwapchain.image_format;
		swapchainExtent = vkbSwapchain.extent;

		auto imagesRet = vkbSwapchain.get_images();
		auto viewsRet = vkbSwapchain.get_image_views();
		if (!imagesRet || !viewsRet) {
			throw std::runtime_error("Failed to retrieve swapchain images/views");
		}
		swapchainImages = imagesRet.value();
		swapchainImageViews = viewsRet.value();
	}

	void destroy_swapchain_resources() {
		if (device == VK_NULL_HANDLE) {
			return;
		}

		if (depthImageView != VK_NULL_HANDLE) {
			vkDestroyImageView(device, depthImageView, nullptr);
			depthImageView = VK_NULL_HANDLE;
		}
		if (depthImage != VK_NULL_HANDLE) {
			vkDestroyImage(device, depthImage, nullptr);
			depthImage = VK_NULL_HANDLE;
		}
		if (depthImageMemory != VK_NULL_HANDLE) {
			vkFreeMemory(device, depthImageMemory, nullptr);
			depthImageMemory = VK_NULL_HANDLE;
		}

		for (VkSemaphore semaphore : renderFinishedSemaphores) {
			if (semaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(device, semaphore, nullptr);
			}
		}
		renderFinishedSemaphores.clear();

		for (VkImageView view : swapchainImageViews) {
			if (view != VK_NULL_HANDLE) {
				vkDestroyImageView(device, view, nullptr);
			}
		}
		swapchainImageViews.clear();

		if (swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(device, swapchain, nullptr);
			swapchain = VK_NULL_HANDLE;
		}
	}

	uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
		VkPhysicalDeviceMemoryProperties memProperties{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
			if ((typeFilter & (1u << i)) &&
			    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}
		throw std::runtime_error("Failed to find suitable memory type");
	}

	void create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
	                  VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
	                  VkDeviceMemory& imageMemory) const {
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &image));

		VkMemoryRequirements memRequirements{};
		vkGetImageMemoryRequirements(device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

		VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));
		VK_CHECK(vkBindImageMemory(device, image, imageMemory, 0));
	}

	VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const {
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView imageView = VK_NULL_HANDLE;
		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
		return imageView;
	}

	void create_depth_resources() {
		create_image(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
		             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage,
		             depthImageMemory);
		depthImageView = create_image_view(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
	}

	void create_render_sync() {
		renderFinishedSemaphores.resize(swapchainImages.size());
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (size_t i = 0; i < swapchainImages.size(); ++i) {
			VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]));
		}
	}

	void create_commands() {
		for (auto& frame : frames) {
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			poolInfo.queueFamilyIndex = graphicsQueueFamily;
			VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool));

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = frame.commandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;
			VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer));

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlightFence));

			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailableSemaphore));
		}
	}

	VkShaderModule create_shader_module(const uint32_t* code, size_t sizeBytes) const {
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = sizeBytes;
		createInfo.pCode = code;

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
		return shaderModule;
	}

	void create_pipeline() {
		VkShaderModule vertModule = create_shader_module(raster_vert_spv, raster_vert_spv_size);
		VkShaderModule fragModule = create_shader_module(raster_frag_spv, raster_frag_spv_size);

		VkPipelineShaderStageCreateInfo vertStage{};
		vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertStage.module = vertModule;
		vertStage.pName = "main";

		VkPipelineShaderStageCreateInfo fragStage{};
		fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragStage.module = fragModule;
		fragStage.pName = "main";

		VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(AssetVertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = static_cast<uint32_t>(offsetof(AssetVertex, position));

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = static_cast<uint32_t>(offsetof(AssetVertex, normal));

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = static_cast<uint32_t>(offsetof(AssetVertex, uv));

		attributeDescriptions[3].binding = 0;
		attributeDescriptions[3].location = 3;
		attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[3].offset = static_cast<uint32_t>(offsetof(AssetVertex, color));

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 0;
		pipelineLayoutInfo.pSetLayouts = nullptr;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		VkPipelineRenderingCreateInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &swapchainImageFormat;
		renderingInfo.depthAttachmentFormat = depthFormat;

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;

		VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline));

		vkDestroyShaderModule(device, fragModule, nullptr);
		vkDestroyShaderModule(device, vertModule, nullptr);
	}

	void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
	                   VkDeviceMemory& bufferMemory) const {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

		VkMemoryRequirements memRequirements{};
		vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

		VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory));
		VK_CHECK(vkBindBufferMemory(device, buffer, bufferMemory, 0));
	}

	GpuMesh upload_mesh(const AssetMesh& mesh) {
		GpuMesh gpu{};

		const VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(mesh.vertices.size() * sizeof(AssetVertex));
		create_buffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, gpu.vertexBuffer,
		              gpu.vertexMemory);
		void* vertexData = nullptr;
		VK_CHECK(vkMapMemory(device, gpu.vertexMemory, 0, vertexBufferSize, 0, &vertexData));
		std::memcpy(vertexData, mesh.vertices.data(), static_cast<size_t>(vertexBufferSize));
		vkUnmapMemory(device, gpu.vertexMemory);

		const VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));
		create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, gpu.indexBuffer,
		              gpu.indexMemory);
		void* indexData = nullptr;
		VK_CHECK(vkMapMemory(device, gpu.indexMemory, 0, indexBufferSize, 0, &indexData));
		std::memcpy(indexData, mesh.indices.data(), static_cast<size_t>(indexBufferSize));
		vkUnmapMemory(device, gpu.indexMemory);

		gpu.indexCount = static_cast<uint32_t>(mesh.indices.size());
		return gpu;
	}

	void load_assets() {
		const std::vector<AssetMesh> loaded = load_assets_from_directory("assets");
		meshes.clear();
		meshes.reserve(loaded.size());
		for (const AssetMesh& mesh : loaded) {
			if (mesh.vertices.empty() || mesh.indices.empty()) {
				continue;
			}
			meshes.push_back(upload_mesh(mesh));
		}
		std::cout << "Uploaded " << meshes.size() << " mesh(es) to GPU buffers." << std::endl;
	}

	FrameData& current_frame() { return frames[frameNumber % FRAME_OVERLAP]; }

	void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
	                      VkImageAspectFlags aspectMask) const {
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = aspectMask;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = 0;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		} else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
		           newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			barrier.dstAccessMask = 0;
		} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
		           newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = 0;
			barrier.dstStageMask =
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		} else {
			// Generic fallback
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
		}

		VkDependencyInfo dependency{};
		dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &dependency);
	}

	void draw_raster() {
		FrameData& frame = current_frame();

		VK_CHECK(vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
		VK_CHECK(vkResetFences(device, 1, &frame.inFlightFence));

		uint32_t imageIndex = 0;
		VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<uint64_t>::max(),
		                                               frame.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
			return; // Swapchain recreation not implemented; skip frame.
		} else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("Failed to acquire swapchain image");
		}

		VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

		transition_image(frame.commandBuffer, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
		                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		transition_image(frame.commandBuffer, depthImage, VK_IMAGE_LAYOUT_UNDEFINED,
		                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

		VkRenderingAttachmentInfo colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = swapchainImageViews[imageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = {{0.02f, 0.02f, 0.03f, 1.0f}};

		VkRenderingAttachmentInfo depthAttachment{};
		depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.imageView = depthImageView;
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.clearValue.depthStencil = {1.0f, 0};

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.offset = {0, 0};
		renderingInfo.renderArea.extent = swapchainExtent;
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthAttachment;

		vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapchainExtent.width);
		viewport.height = static_cast<float>(swapchainExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = swapchainExtent;
		vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

		if (!meshes.empty()) {
			const float aspect = swapchainExtent.height > 0
			                         ? static_cast<float>(swapchainExtent.width) / static_cast<float>(swapchainExtent.height)
			                         : 1.0f;
			const float time = static_cast<float>(frameNumber) * 0.01f;
			glm::mat4 view = glm::lookAt(glm::vec3(2.5f * std::sin(time), 1.5f, 2.5f * std::cos(time)),
			                             glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f);
			proj[1][1] *= -1.0f; // Vulkan clip space has inverted Y compared to OpenGL conventions.
			glm::mat4 mvp = proj * view;

			for (const GpuMesh& mesh : meshes) {
				VkDeviceSize offset = 0;
				vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer, &offset);
				vkCmdBindIndexBuffer(frame.commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

				PushConstants pc{};
				pc.mvp = mvp;
				vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
				                   sizeof(PushConstants), &pc);

				vkCmdDrawIndexed(frame.commandBuffer, mesh.indexCount, 1, 0, 0, 0);
			}
		}

		vkCmdEndRendering(frame.commandBuffer);

		transition_image(frame.commandBuffer, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

		VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

		VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
		VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &frame.commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.inFlightFence));

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;

		VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
		if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR && presentResult != VK_ERROR_OUT_OF_DATE_KHR) {
			throw std::runtime_error("Failed to present swapchain image");
		}

		++frameNumber;
	}

	void draw_raytrace() {
		// Ray tracing path not implemented yet; keep the application responsive.
		SDL_Delay(16);
	}

	void mainLoop() {
		bool running = true;
		while (running) {
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_EVENT_QUIT) {
					running = false;
				}
				if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
					running = false;
				}
			}

			if (renderStyle == RenderStyle::Raster) {
				draw_raster();
			} else {
				draw_raytrace();
			}
		}

		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
		}
	}

	void cleanup() {
		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
		}

		for (GpuMesh& mesh : meshes) {
			if (mesh.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
			if (mesh.indexMemory != VK_NULL_HANDLE) vkFreeMemory(device, mesh.indexMemory, nullptr);
			if (mesh.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
			if (mesh.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(device, mesh.vertexMemory, nullptr);
		}
		meshes.clear();

		if (graphicsPipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(device, graphicsPipeline, nullptr);
			graphicsPipeline = VK_NULL_HANDLE;
		}
		if (pipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			pipelineLayout = VK_NULL_HANDLE;
		}

		for (auto& frame : frames) {
			if (frame.commandPool != VK_NULL_HANDLE) {
				vkDestroyCommandPool(device, frame.commandPool, nullptr);
				frame.commandPool = VK_NULL_HANDLE;
				frame.commandBuffer = VK_NULL_HANDLE; // freed with pool
			}
			if (frame.inFlightFence != VK_NULL_HANDLE) {
				vkDestroyFence(device, frame.inFlightFence, nullptr);
				frame.inFlightFence = VK_NULL_HANDLE;
			}
			if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
				frame.imageAvailableSemaphore = VK_NULL_HANDLE;
			}
		}

		destroy_swapchain_resources();

		if (device != VK_NULL_HANDLE) {
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}
		if (surface != VK_NULL_HANDLE) {
			vkDestroySurfaceKHR(instance, surface, nullptr);
			surface = VK_NULL_HANDLE;
		}
		if (debugMessenger != VK_NULL_HANDLE) {
			vkb::destroy_debug_utils_messenger(instance, debugMessenger, nullptr);
			debugMessenger = VK_NULL_HANDLE;
		}
		if (instance != VK_NULL_HANDLE) {
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
		if (window != nullptr) {
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		SDL_Quit();
	}
};

int main(int argc, char** argv) {
	VulkanApp app;
	if (argc > 1 && std::string(argv[1]) == "rt") {
		app.renderStyle = RenderStyle::RayTrace;
	} else {
		app.renderStyle = RenderStyle::Raster;
	}

	try {
		app.run();
	} catch (const std::exception& e) {
		std::cerr << "Fatal error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
