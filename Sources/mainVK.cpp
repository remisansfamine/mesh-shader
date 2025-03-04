#include <array>
#include <fstream>
#include <sstream>

/**
* Sapphire Suite Debugger:
* Maxime's custom Log and Assert macros for easy debug.
*/
#include <SA/Collections/Debug>

/**
* Sapphire Suite Maths library:
* Maxime's custom Maths library.
*/
#include <SA/Collections/Maths>
#include <SA/Collections/Transform>



// ========== Windowing ==========

#define GLFW_INCLUDE_VULKAN 1
#include <GLFW/glfw3.h>

GLFWwindow* window = nullptr;
constexpr SA::Vec2ui windowSize = { 1200, 900 };

void GLFWErrorCallback(int32_t error, const char* description)
{
	SA_LOG((L"GLFW Error [%1]: %2", error, description), Error, GLFW.API);
}



// ========== Renderer ==========

#include <vulkan/vulkan.h> // vulkan.h -> d3d12.h

// === Validation Layers ===

#if SA_DEBUG
std::vector<const char*> validationLayers{
	"VK_LAYER_KHRONOS_validation"
};

VKAPI_ATTR VkBool32 VKAPI_CALL ValidationLayersDebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	(void)pUserData;

	std::wstring msgTypeStr;

	switch (messageType)
	{
	case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
	{
		msgTypeStr = L"[General]";
		break;
	}
	case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
	{
		msgTypeStr = L"[Validation]";
		break;
	}
	case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
	{
		msgTypeStr = L"[Performance]";
		break;
	}
	default:
	{
		msgTypeStr = L"[Unknown]";
		break;
	}
	}

	switch (messageSeverity)
	{
		//case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
		//	break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
	{
		SA_LOG(pCallbackData->pMessage, Info, VK.ValidationLayers, (L"Vulkan Validation Layers %1", msgTypeStr));
		break;
	}
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
	{
		SA_LOG(pCallbackData->pMessage, Warning, VK.ValidationLayers, (L"Vulkan Validation Layers %1", msgTypeStr));
		break;
	}
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
	{
		SA_LOG(pCallbackData->pMessage, Error, VK.ValidationLayers, (L"Vulkan Validation Layers %1", msgTypeStr));
		break;
	}
	default:
	{
		SA_LOG(pCallbackData->pMessage, Normal, VK.ValidationLayers, (L"Vulkan Validation Layers %1", msgTypeStr));
		break;
	}
	}

	return VK_FALSE;
}
#endif


// === Instance ===

std::vector<const char*> vkInstanceExts{
#if SA_DEBUG
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
};

VkInstance instance = VK_NULL_HANDLE;


// === Surface ===

VkSurfaceKHR windowSurface = VK_NULL_HANDLE;


// === Device ===

struct QueueFamilyIndices
{
	uint32_t graphicsFamily = uint32_t(-1);
	//uint32_t computeFamily = uint32_t(-1);
	uint32_t presentFamily = uint32_t(-1);
};

VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
QueueFamilyIndices deviceQueueFamilyIndices;

const std::vector<const char*> vkDeviceReqExts{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VkDevice device = VK_NULL_HANDLE;

VkQueue graphicsQueue = VK_NULL_HANDLE;
//VkQueue computeQueue = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;


// === Swapchain ===

constexpr uint32_t bufferingCount = 3;

VkSwapchainKHR swapchain = VK_NULL_HANDLE;
std::array<VkImage, bufferingCount> swapchainImages{ VK_NULL_HANDLE };
std::array<VkImageView, bufferingCount> swapchainImageViews{ VK_NULL_HANDLE };
uint32_t swapchainFrameIndex = 0u;
uint32_t swapchainImageIndex = 0u;

struct SwapchainSynchronisation
{
	VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE;
	VkFence		fence = VK_NULL_HANDLE;
};
std::array<SwapchainSynchronisation, bufferingCount> swapchainSyncs{};


// === Commands ===

VkCommandPool cmdPool = VK_NULL_HANDLE;
std::array<VkCommandBuffer, bufferingCount> cmdBuffers{ VK_NULL_HANDLE };


// === Scene Textures ===

// = Color =
VkFormat sceneColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
const VkClearValue sceneClearColor{
	.color = { 0.0f, 0.1f, 0.2f, 1.0f },
};
// Use Swapchain backbuffer texture as color output.

// = Depth =
const VkFormat sceneDepthFormat = VK_FORMAT_D16_UNORM;
const VkClearValue sceneClearDepth{
	.depthStencil = { 1.0f, 0u },
};

VkImage sceneDepthImage = VK_NULL_HANDLE;
VkDeviceMemory sceneDepthImageMemory = VK_NULL_HANDLE;
VkImageView sceneDepthImageView = VK_NULL_HANDLE;

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}

	SA_LOG(L"Failed to find suitable memory type!", Error, VK);
	return uint32_t(-1);
}


// === RenderPass ===

VkRenderPass renderPass = VK_NULL_HANDLE;


// === Frame Buffer ===

std::array<VkFramebuffer, bufferingCount> framebuffers{ VK_NULL_HANDLE };


// === Pipeline ===

// = Viewport & Scissor =
VkViewport viewport{};
VkRect2D scissorRect{};

// = Lit =
VkDescriptorSetLayout litDescSetLayout = VK_NULL_HANDLE;

VkShaderModule litVertexShader = VK_NULL_HANDLE;
VkShaderModule litFragmentShader = VK_NULL_HANDLE;

VkPipelineLayout litPipelineLayout = VK_NULL_HANDLE;
VkPipeline litPipeline = VK_NULL_HANDLE;


// === Scene Objects ===

// = DescriptorSets =
VkDescriptorPool pbrSphereDescPool = VK_NULL_HANDLE;
std::array<VkDescriptorSet, bufferingCount> pbrSphereDescSets{ VK_NULL_HANDLE };

// = Camera Buffer =
struct CameraUBO
{
	SA::CMat4f view;
	SA::CMat4f invViewProj;
};
SA::TransformPRf cameraTr;
constexpr float cameraMoveSpeed = 1.0f;
constexpr float cameraRotSpeed = 12.0f;
constexpr float cameraNear = 0.1f;
constexpr float cameraFar = 1000.0f;
constexpr float cameraFOV = 90.0f;
std::array<VkBuffer, bufferingCount> cameraBuffers;
std::array<VkDeviceMemory, bufferingCount> cameraBufferMemories;

// = Object Buffer =
struct ObjectUBO
{
	SA::Mat4f transform;
};
constexpr SA::Vec3f spherePosition(0.5f, 0.0f, 2.0f);
VkBuffer sphereObjectBuffer;
VkDeviceMemory sphereObjectBufferMemory;

// = PointLights Buffer =
struct PointLightUBO
{
	SA::Vec3f position;

	float intensity = 0.0f;

	SA::Vec3f color;

	float radius = 0.0f;
};
constexpr uint32_t pointLightNum = 2;
VkBuffer pointLightBuffer;
VkDeviceMemory pointLightBufferMemory;


// === Resources ===

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#pragma warning(disable : 4505)
#include <stb_image_resize2.h>
#pragma warning(default : 4505)

bool SubmitBufferToGPU(VkBuffer _gpuBuffer, uint64_t _size, const void* _data)
{
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

	const VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0u,
		.size = _size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0u,
		.pQueueFamilyIndices = nullptr,
	};

	const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
	if (vrBufferCreated != VK_SUCCESS)
	{
		SA_LOG(L"Create Staging Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
		return false;
	}

	// Memory
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

	const VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // CPU-GPU.

	const VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties),
	};

	const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory);
	if (vrBufferAlloc != VK_SUCCESS)
	{
		SA_LOG(L"Create Staging Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
		return false;
	}


	const VkResult vrBindBufferMem = vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
	if (vrBindBufferMem != VK_SUCCESS)
	{
		SA_LOG(L"Bind Staging Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
		return false;
	}


	// Memory mapping
	void* data = nullptr;
	vkMapMemory(device, stagingBufferMemory, 0, _size, 0, &data);

	std::memcpy(data, _data, _size);

	vkUnmapMemory(device, stagingBufferMemory);


	// Copy GPU temp staging buffer to final GPU-only buffer.
	const VkBufferCopy copyRegion{
		.srcOffset = 0u,
		.dstOffset = 0u,
		.size = _size,
	};
	vkCmdCopyBuffer(cmdBuffers[0], stagingBuffer, _gpuBuffer, 1, &copyRegion);


	// Submit
	vkEndCommandBuffer(cmdBuffers[0]);

	const VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0u,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmdBuffers[0],
		.signalSemaphoreCount = 0u,
		.pSignalSemaphores = nullptr,
	};

	vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);


	// Ready for new submit.
	const VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	vkBeginCommandBuffer(cmdBuffers[0], &beginInfo);


	// Destroy
	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);

	return true;
}

bool SubmitTextureToGPU(VkImage _gpuTexture, const std::vector<SA::Vec2ui>& _extents, uint64_t _totalSize, uint32_t _channelNum, const void* _data)
{
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

	const VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0u,
		.size = _totalSize,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0u,
		.pQueueFamilyIndices = nullptr,
	};

	const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
	if (vrBufferCreated != VK_SUCCESS)
	{
		SA_LOG(L"Create Staging Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
		return false;
	}

	// Memory
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

	const VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; // CPU-GPU.

	const VkMemoryAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = memRequirements.size,
		.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties),
	};

	const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory);
	if (vrBufferAlloc != VK_SUCCESS)
	{
		SA_LOG(L"Create Staging Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
		return false;
	}


	const VkResult vrBindBufferMem = vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
	if (vrBindBufferMem != VK_SUCCESS)
	{
		SA_LOG(L"Bind Staging Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
		return false;
	}


	// Memory mapping
	void* data = nullptr;
	vkMapMemory(device, stagingBufferMemory, 0, _totalSize, 0, &data);

	std::memcpy(data, _data, _totalSize);

	vkUnmapMemory(device, stagingBufferMemory);


	// Transition Underfined -> Transfer
	const VkImageMemoryBarrier barrier1{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = 0u,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = _gpuTexture,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = static_cast<uint32_t>(_extents.size()),
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(
		cmdBuffers[0],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier1
	);


	// Copy Buffer to texture
	uint32_t offset = 0u;
	std::vector<VkBufferImageCopy> regions(_extents.size());

	for (uint32_t i = 0; i < _extents.size(); ++i)
	{
		regions[i] = VkBufferImageCopy{
			.bufferOffset = offset,
			.bufferRowLength = 0u,
			.bufferImageHeight = 0,
			.imageSubresource{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageOffset = {0, 0, 0},
			.imageExtent = {_extents[i].x, _extents[i].y, 1u },
		};

		offset += _extents[i].x * _extents[i].y * _channelNum;
	}

	vkCmdCopyBufferToImage(cmdBuffers[0],
		stagingBuffer,
		_gpuTexture,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		static_cast<uint32_t>(regions.size()),
		regions.data());


	// Transition Transfer -> Shader Read
	const VkImageMemoryBarrier barrier2{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = _gpuTexture,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = static_cast<uint32_t>(_extents.size()),
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(
		cmdBuffers[0],
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier2
	);


	// Submit
	vkEndCommandBuffer(cmdBuffers[0]);

	const VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0u,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmdBuffers[0],
		.signalSemaphoreCount = 0u,
		.pSignalSemaphores = nullptr,
	};

	vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);


	// Ready for new submit.
	const VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	vkBeginCommandBuffer(cmdBuffers[0], &beginInfo);


	// Destroy
	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);

	return true;
}

void GenerateMipMapsCPU(SA::Vec2ui _extent, std::vector<char>& _data, uint32_t& _outMipLevels, uint32_t& _outTotalSize, std::vector<SA::Vec2ui>& _outExtents, uint32_t _channelNum, uint32_t _layerNum = 1u)
{
	_outMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(_extent.x, _extent.y)))) + 1;

	_outExtents.resize(_outMipLevels);

	// Compute Total Size & extents
	{
		for (uint32_t i = 0u; i < _outMipLevels; ++i)
		{
			_outExtents[i] = _extent;

			_outTotalSize += _extent.x * _extent.y * _channelNum * _layerNum * sizeof(stbi_uc);

			if (_extent.x > 1)
				_extent.x >>= 1;

			if (_extent.y > 1)
				_extent.y >>= 1;
		}
	}


	_data.resize(_outTotalSize);


	// Generate
	{
		unsigned char* src = reinterpret_cast<unsigned char*>(_data.data());

		for (uint32_t i = 1u; i < _outMipLevels; ++i)
		{
			uint64_t srcLayerOffset = _outExtents[i - 1].x * _outExtents[i - 1].y * _channelNum * sizeof(stbi_uc);
			uint64_t currLayerOffset = _outExtents[i].x * _outExtents[i].y * _channelNum * sizeof(stbi_uc);
			unsigned char* dst = src + srcLayerOffset * _layerNum;

			for (uint32_t j = 0; j < _layerNum; ++j)
			{
				bool res = stbir_resize_uint8_linear(
					src,
					static_cast<int32_t>(_outExtents[i - 1].x),
					static_cast<int32_t>(_outExtents[i - 1].y),
					0,
					dst,
					static_cast<int32_t>(_outExtents[i].x),
					static_cast<int32_t>(_outExtents[i].y),
					0,
					static_cast<stbir_pixel_layout>(_channelNum)
				);

				if (!res)
				{
					SA_LOG(L"Mip map creation failed!", Error, STB);
					return;
				}

				dst += currLayerOffset;
				src += srcLayerOffset;
			}
		}
	}
}

#include <shaderc/shaderc.hpp>

bool CompileShaderFromFile(const std::string& _path, shaderc_shader_kind _stage, std::vector<uint32_t>& _out)
{
	// Read File
	std::string code;
	{
		std::fstream fStream(_path, std::ios_base::in);

		if (!fStream.is_open())
		{
			SA_LOG((L"Failed to open shader file {%1}", _path), Error, VK.Shader);
			return false;
		}


		std::stringstream sstream;
		sstream << fStream.rdbuf();

		fStream.close();

		code = sstream.str();
	}

	// Compile
	static shaderc::Compiler compiler;

	shaderc::CompileOptions options;

#if SA_DEBUG
	options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
	options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

	const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(code, _stage, _path.c_str(), options);

	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		SA_LOG((L"Compile Shader {%1} failed!", _path), Error, VK.Shader, (L"Errors: %1\tWarnings: %2\n%3", result.GetNumErrors(), result.GetNumWarnings(), result.GetErrorMessage()));
		return false;
	}
	else if (result.GetNumWarnings())
	{
		SA_LOG((L"Compile Shader {%1} success with %2 warnings.", _path, result.GetNumWarnings()), Warning, VK.Shadar, result.GetErrorMessage());
	}
	else
	{
		SA_LOG((L"Compile Shader {%1} success.", _path), Info, VK.Shader);
	}

	_out = { result.cbegin(), result.cend() };

	return true;
}

// = Sphere =
std::array<VkBuffer, 4> sphereVertexBuffers { VK_NULL_HANDLE };
std::array<VkDeviceMemory, 4> sphereVertexBufferMemories{ VK_NULL_HANDLE };

uint32_t sphereIndexCount = 0u;
VkBuffer sphereIndexBuffer = VK_NULL_HANDLE;
VkDeviceMemory sphereIndexBufferMemory = VK_NULL_HANDLE;

// = RustedIron2 PBR =
VkSampler rustedIron2Sampler = VK_NULL_HANDLE;

VkImage rustedIron2AlbedoImage = VK_NULL_HANDLE;
VkDeviceMemory rustedIron2AlbedoImageMemory = VK_NULL_HANDLE;
VkImageView rustedIron2AlbedoImageView = VK_NULL_HANDLE;

VkImage rustedIron2NormalImage = VK_NULL_HANDLE;
VkDeviceMemory rustedIron2NormalImageMemory = VK_NULL_HANDLE;
VkImageView rustedIron2NormalImageView = VK_NULL_HANDLE;

VkImage rustedIron2MetallicImage = VK_NULL_HANDLE;
VkDeviceMemory rustedIron2MetallicImageMemory = VK_NULL_HANDLE;
VkImageView rustedIron2MetallicImageView = VK_NULL_HANDLE;

VkImage rustedIron2RoughnessImage = VK_NULL_HANDLE;
VkDeviceMemory rustedIron2RoughnessImageMemory = VK_NULL_HANDLE;
VkImageView rustedIron2RoughnessImageView = VK_NULL_HANDLE;


int main()
{
	// Initialization
	{
		SA::Debug::InitDefaultLogger();

		// GLFW
		{
			glfwSetErrorCallback(GLFWErrorCallback);
			glfwInit();

			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			window = glfwCreateWindow(windowSize.x, windowSize.y, "FVTDX12_VK-Window", nullptr, nullptr);

			if (!window)
			{
				SA_LOG(L"GLFW create window failed!", Error, GLFW);
				return EXIT_FAILURE;
			}
			else
			{
				SA_LOG("GLFW create window success.", Info, GLFW, window);
			}

			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);


			// Add GLFW required Extensions for present support
			{
				uint32_t glfwExtensionCount = 0;
				const char** glfwExtensions = nullptr;

				glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

				vkInstanceExts.reserve(glfwExtensionCount);
				vkInstanceExts.insert(vkInstanceExts.end(), glfwExtensions, glfwExtensions + glfwExtensionCount);
			}
		}


		// Renderer
		{
			// Instance
			if (true)
			{
				const VkApplicationInfo appInfo{
					.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
					.pNext = nullptr,
					.pApplicationName = "FVTDX12_VK-App",
					.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
					.pEngineName = "No Engine",
					.engineVersion = VK_MAKE_VERSION(1, 0, 0),
					.apiVersion = VK_API_VERSION_1_2,
				};

				VkInstanceCreateInfo instanceCreateInfo{
					.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
					.pNext = nullptr,
					.flags = 0u,
					.pApplicationInfo = &appInfo,
					.enabledLayerCount = 0u,
					.ppEnabledLayerNames = nullptr,
					.enabledExtensionCount = static_cast<uint32_t>(vkInstanceExts.size()),
					.ppEnabledExtensionNames = vkInstanceExts.data(),
				};

#if SA_DEBUG
				// Validation Layers
				// Check Validation Layers Support
				{
					// Query currently supported layers.
					uint32_t layerCount;
					vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

					std::vector<VkLayerProperties> availableLayers(layerCount);
					vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());


					// Check each asked supported.
					for (uint32_t i = 0; i < validationLayers.size(); ++i)
					{
						bool layerFound = false;

						for (uint32_t j = 0; j < availableLayers.size(); ++j)
						{
							// Layer found.
							if (std::strcmp(validationLayers[i], availableLayers[j].layerName) == 0)
							{
								layerFound = true;
								break;
							}
						}

						// Layer not found.
						if (!layerFound)
						{
							SA_LOG((L"Validation Layers [%1] not supported!", validationLayers[i]), Error, VK.ValidationLayers);
							return EXIT_FAILURE;
						}
					}
				}

				const VkDebugUtilsMessengerCreateInfoEXT debugUtilsInfo{
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
					.pNext = nullptr,
					.flags = 0u,
					.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
										VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
										VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
					.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
									VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
									VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
					.pfnUserCallback = ValidationLayersDebugCallback,
					.pUserData = nullptr,
				};

				instanceCreateInfo.pNext = &debugUtilsInfo;

				instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();
#endif

				const VkResult vrInstanceCreated = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
				if (vrInstanceCreated != VK_SUCCESS)
				{
					SA_LOG(L"Create VkInstance failed!", Error, VK, (L"Error Code: %1", vrInstanceCreated));
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG("Create VkInstance success.", Info, GLFW, instance);
				}
			}


			// Surface
			if (true)
			{
				/**
				* Create Vulkan Surface from GLFW window.
				* Required to create PresentQueue in Device.
				*/
				const VkResult vrWindowSurfaceCreated = glfwCreateWindowSurface(instance, window, nullptr, &windowSurface);
				if (vrWindowSurfaceCreated != VK_SUCCESS)
				{
					SA_LOG(L"Create Window Surafce failed!", Error, VK, (L"Error Code: %1", vrWindowSurfaceCreated));
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG("Create Window Surafce success.", Info, GLFW, windowSurface);
				}
			}


			// Device
			if (true)
			{
				// Query physical devices
				uint32_t deviceCount = 0;
				const VkResult vrEnumPhysDeviceCount = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
				if (vrEnumPhysDeviceCount != VK_SUCCESS)
				{
					SA_LOG(L"Enumerate Physical Devices Count failed!", Error, VK, (L"Error Code: %1", vrEnumPhysDeviceCount));
					return EXIT_FAILURE;
				}
				if (deviceCount == 0)
				{
					SA_LOG(L"No GPU with Vulkan support found!", Error, VK);
					return EXIT_FAILURE;
				}

				std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
				const VkResult vrEnumPhysDevices = vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
				if (vrEnumPhysDevices != VK_SUCCESS)
				{
					SA_LOG(L"Enumerate Physical Devices failed!", Error, VK, (L"Error Code: %1", vrEnumPhysDevices));
					return EXIT_FAILURE;
				}


				// Find first suitable device (no scoring).
				for (auto& currPhysicalDevice : physicalDevices)
				{
					// Check extensions support
					{
						// Query extensions.
						uint32_t extensionCount = 0u;
						const VkResult vrEnumDeviceExtsCount = vkEnumerateDeviceExtensionProperties(currPhysicalDevice, nullptr, &extensionCount, nullptr);
						if (vrEnumDeviceExtsCount != VK_SUCCESS)
						{
							SA_LOG(L"Enumerate Devices extensions count failed!", Error, VK, (L"Error Code: %1", vrEnumDeviceExtsCount));
							return EXIT_FAILURE;
						}

						std::vector<VkExtensionProperties> supportedExts(extensionCount);
						const VkResult vrEnumDeviceExts = vkEnumerateDeviceExtensionProperties(currPhysicalDevice, nullptr, &extensionCount, supportedExts.data());
						if (vrEnumDeviceExts != VK_SUCCESS)
						{
							SA_LOG(L"Enumerate Devices extensions failed!", Error, VK, (L"Error Code: %1", vrEnumDeviceExts));
							return EXIT_FAILURE;
						}


						// Check support
						bool bAllReqExtSupported = true;

						for (auto reqExt : vkDeviceReqExts)
						{
							bool bExtFound = false;

							for (const auto& suppExt : supportedExts)
							{
								// Extension found.
								if (std::strcmp(reqExt, suppExt.extensionName) == 0)
								{
									bExtFound = true;
									break;
								}
							}

							if (!bExtFound)
							{
								bAllReqExtSupported = false;
								break;
							}
						}

						if (!bAllReqExtSupported)
							continue; // go to next device.
					}

					// Find Queue Families
					{
						QueueFamilyIndices currPhysicalDeviceQueueFamilies;

						uint32_t queueFamilyCount = 0;
						vkGetPhysicalDeviceQueueFamilyProperties(currPhysicalDevice, &queueFamilyCount, nullptr);

						std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
						vkGetPhysicalDeviceQueueFamilyProperties(currPhysicalDevice, &queueFamilyCount, queueFamilies.data());

						for (uint32_t i = 0; i < queueFamilyCount; ++i)
						{
							const auto& currFamily = queueFamilies[i];

							// Graphics family.
							if (currPhysicalDeviceQueueFamilies.graphicsFamily == uint32_t(-1) && (currFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
								currPhysicalDeviceQueueFamilies.graphicsFamily = i;

							//// Compute family.
							//if (currPhysicalDeviceQueueFamilies.computeFamily == uint32_t(-1) && (currFamily.queueFlags & VK_QUEUE_COMPUTE_BIT))
							//	currPhysicalDeviceQueueFamilies.computeFamily = i;

							// Present family.
							if (currPhysicalDeviceQueueFamilies.presentFamily == uint32_t(-1))
							{
								VkBool32 presentSupport = false;
								const VkResult vrSupportKHR = vkGetPhysicalDeviceSurfaceSupportKHR(currPhysicalDevice, i, windowSurface, &presentSupport);
								if (vrSupportKHR != VK_SUCCESS)
								{
									SA_LOG(L"Physical Device Surface Support failed.", Error, VK, (L"Error Code: %1", vrSupportKHR));
									return EXIT_SUCCESS;
								}

								if (presentSupport == VK_SUCCESS)
									currPhysicalDeviceQueueFamilies.presentFamily = i;
							}
						}

						// Check all queues can be created
						if (currPhysicalDeviceQueueFamilies.graphicsFamily == uint32_t(-1) ||
							//currPhysicalDeviceQueueFamilies.computeFamily == uint32_t(-1) ||
							currPhysicalDeviceQueueFamilies.presentFamily == uint32_t(-1))
							continue;  // go to next device.

						deviceQueueFamilyIndices = currPhysicalDeviceQueueFamilies;
					}

					physicalDevice = currPhysicalDevice;
					break;
				}

				if (physicalDevice == VK_NULL_HANDLE)
				{
					SA_LOG(L"No suitable PhysicalDevice found.", Error, VK);
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG(L"Create Physical Device success", Info, VK, physicalDevice);
				}


				// Create Logical Device.
				const VkPhysicalDeviceFeatures deviceFeatures{};

				const float queuePriority = 1.0f;
				const std::array<VkDeviceQueueCreateInfo, 2> queueCreateInfo{
					VkDeviceQueueCreateInfo{
						.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0,
						.queueFamilyIndex = deviceQueueFamilyIndices.graphicsFamily,
						.queueCount = 1,
						.pQueuePriorities = &queuePriority,
					},
					//VkDeviceQueueCreateInfo{
					//	.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					//	.pNext = nullptr,
					//	.flags = 0,
					//	.queueFamilyIndex = deviceQueueFamilyIndices.computeFamily,
					//	.queueCount = 1,
					//	.pQueuePriorities = &queuePriority,
					//},
					VkDeviceQueueCreateInfo{
						.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0,
						.queueFamilyIndex = deviceQueueFamilyIndices.presentFamily,
						.queueCount = 1,
						.pQueuePriorities = &queuePriority,
					}
				};

				VkDeviceCreateInfo deviceCreateInfo{
					.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
					.pNext = nullptr,
					.flags = 0,
					.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfo.size()),
					.pQueueCreateInfos = queueCreateInfo.data(),
					.enabledLayerCount = 0,
					.ppEnabledLayerNames = nullptr,
					.enabledExtensionCount = static_cast<uint32_t>(vkDeviceReqExts.size()),
					.ppEnabledExtensionNames = vkDeviceReqExts.data(),
					.pEnabledFeatures = &deviceFeatures,
				};

#if SA_DEBUG

				deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				deviceCreateInfo.ppEnabledLayerNames = validationLayers.data();

#endif

				const VkResult vrDeviceCreated = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
				if (vrDeviceCreated != VK_SUCCESS)
				{
					SA_LOG(L"Create Logical Device failed.", Error, VK, (L"Error Code: %1", vrDeviceCreated));
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG(L"Create Logical Device success.", Info, VK, device);
				}


				// Create Queues
				vkGetDeviceQueue(device, deviceQueueFamilyIndices.graphicsFamily, 0, &graphicsQueue);
				SA_LOG(L"Create Graphics Queue success.", Info, VK, graphicsQueue);

				//vkGetDeviceQueue(device, deviceQueueFamilyIndices.computeFamily, 0, &computeQueue);
				//SA_LOG(L"Create Compute Queue success.", Info, VK, computeQueue);

				vkGetDeviceQueue(device, deviceQueueFamilyIndices.presentFamily, 0, &presentQueue);
				SA_LOG(L"Create Present Queue success.", Info, VK, presentQueue);
			}


			// Swapchain
			if (true)
			{
				// Query Support Details
				VkSurfaceCapabilitiesKHR capabilities;
				std::vector<VkSurfaceFormatKHR> formats;
				std::vector<VkPresentModeKHR> presentModes;
				{
					// Capabilities
					const VkResult vrGetSurfaceCapabilities = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, windowSurface, &capabilities);
					if (vrGetSurfaceCapabilities != VK_SUCCESS)
					{
						SA_LOG(L"Get Physical Device Surface Capabilities failed!", Error, VK);
						return EXIT_FAILURE;
					}


					// Formats
					uint32_t formatCount = 0u;
					const VkResult vrGetSurfaceFormatsCount = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, windowSurface, &formatCount, nullptr);
					if (vrGetSurfaceFormatsCount != VK_SUCCESS)
					{
						SA_LOG(L"Get Physical Device Surface Formats Count failed!", Error, VK, (L"Error Code: %1", vrGetSurfaceFormatsCount));
						return EXIT_FAILURE;
					}
					if (formatCount == 0)
					{
						SA_LOG(L"No physical device surface formats found!", Error, VK);
						return EXIT_FAILURE;
					}

					formats.resize(formatCount);
					const VkResult vrGetSurfaceFormats = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, windowSurface, &formatCount, formats.data());
					if (vrGetSurfaceFormats != VK_SUCCESS)
					{
						SA_LOG(L"Get Physical Device Surface Formats failed!", Error, VK, (L"Error Code: %1", vrGetSurfaceFormats));
						return EXIT_FAILURE;
					}


					// Present modes
					uint32_t presentModeCount = 0u;
					const VkResult vrGetSurfacePresentCount = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, windowSurface, &presentModeCount, nullptr);
					if (vrGetSurfacePresentCount != VK_SUCCESS)
					{
						SA_LOG(L"Get Physical Device Surface PresentModes Count failed!", Error, VK, (L"Error Code: %1", vrGetSurfacePresentCount));
						return EXIT_FAILURE;
					}
					if (presentModeCount == 0)
					{
						SA_LOG(L"No physical device present modes found!", Error, VK);
						return EXIT_FAILURE;
					}

					presentModes.resize(presentModeCount);
					const VkResult vrGetSurfacePresent = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, windowSurface, &presentModeCount, presentModes.data());
					if (vrGetSurfacePresent != VK_SUCCESS)
					{
						SA_LOG(L"Get Physical Device Surface present modes failed!", Error, VK, (L"Error Code: %1", vrGetSurfacePresent));
						return EXIT_FAILURE;
					}
				}

				// ChooseSwapSurfaceFormat
				VkSurfaceFormatKHR swapchainFormat = formats[0];
				{
					// Find prefered
					for (uint32_t i = 0; i < formats.size(); ++i)
					{
						if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
						{
							swapchainFormat = formats[i];
							break;
						}
					}

					sceneColorFormat = swapchainFormat.format;
				}

				// ChooseSwapPresentMode
				VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR; // Default FIFO always supported.
				{
					// Find prefered.
					for (uint32_t i = 0; i < presentModes.size(); ++i)
					{
						if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
						{
							swapchainPresentMode = presentModes[i];
							break;
						}
					}
				}

				
				VkSharingMode swapchainImageSharingMode = VK_SHARING_MODE_EXCLUSIVE;


				// Provide queue Family indices.
				std::array<uint32_t, 2> queueFamilyIndices{
					deviceQueueFamilyIndices.graphicsFamily,
					deviceQueueFamilyIndices.presentFamily,
				};

				// Graphic and present familiy are different.
				if (queueFamilyIndices[0] != queueFamilyIndices[1])
					swapchainImageSharingMode = VK_SHARING_MODE_CONCURRENT;


				const VkSwapchainCreateInfoKHR swapchainCreateInfo{
					.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
					.pNext = nullptr,
					.flags = 0u,
					.surface = windowSurface,
					.minImageCount = bufferingCount,
					.imageFormat = sceneColorFormat,
					.imageColorSpace = swapchainFormat.colorSpace,
					.imageExtent = VkExtent2D{ windowSize.x, windowSize.y },
					.imageArrayLayers = 1u,
					.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					.imageSharingMode = swapchainImageSharingMode,
					.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size()),
					.pQueueFamilyIndices = queueFamilyIndices.data(),
					.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
					.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
					.presentMode = swapchainPresentMode,
					.clipped = VK_TRUE,
					.oldSwapchain = VK_NULL_HANDLE,
				};

				const VkResult vrSwapchainCreated = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
				if (vrSwapchainCreated != VK_SUCCESS)
				{
					SA_LOG("Create Swapchain failed!", Error, VK, (L"Error Code: %1", vrSwapchainCreated));
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG("Create Swapchain success.", Info, VK, swapchain);
				}


				// Query backbuffer images.
				uint32_t swapchainImageNum = bufferingCount;
				const VkResult vrGetSwapchainImages = vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageNum, swapchainImages.data());
				if (vrGetSwapchainImages != VK_SUCCESS || swapchainImageNum != bufferingCount)
				{
					SA_LOG(L"Get Swapchain Images failed!", Error, VK, (L"Error Code: %1", vrGetSwapchainImages));
					return EXIT_FAILURE;
				}
				else
				{
					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						SA_LOG(L"Created Swapchain backbuffer images success.", Info, VK, swapchainImages[i]);
					}
				}


				// Image Views
				{
					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						const VkImageViewCreateInfo imgViewCreateInfo{
							.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0,
							.image = swapchainImages[i],
							.viewType = VK_IMAGE_VIEW_TYPE_2D,
							.format = sceneColorFormat,
							.components{
								.r = VK_COMPONENT_SWIZZLE_IDENTITY,
								.g = VK_COMPONENT_SWIZZLE_IDENTITY,
								.b = VK_COMPONENT_SWIZZLE_IDENTITY,
								.a = VK_COMPONENT_SWIZZLE_IDENTITY
							},
							.subresourceRange{
								.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
								.baseMipLevel = 0,
								.levelCount = 1,
								.baseArrayLayer = 0,
								.layerCount = 1,
							}
						};

						const VkResult vrImageViewCreated = vkCreateImageView(device, &imgViewCreateInfo, nullptr, &swapchainImageViews[i]);
						if (vrImageViewCreated != VK_SUCCESS)
						{
							SA_LOG(L"Create Swapchain ImageView failed!", Error, VK, (L"Error Code: %1", vrImageViewCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Swapchain ImageView success.", Info, VK, swapchainImageViews[i]);
						}
					}
				}


				// Synchronization
				{
					VkSemaphoreCreateInfo semaphoreCreateInfo{};
					semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
					semaphoreCreateInfo.pNext = nullptr;
					semaphoreCreateInfo.flags = 0u;

					VkFenceCreateInfo fenceCreateInfo{};
					fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
					fenceCreateInfo.pNext = nullptr;
					fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						// Acquire Semaphore
						const VkResult vrAcqSemaphoreCreated = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &swapchainSyncs[i].acquireSemaphore);
						if (vrAcqSemaphoreCreated != VK_SUCCESS)
						{
							SA_LOG((L"Create Swapchain Acquire Semaphore [%1] failed!", i), Error, VK, (L"Error Code: %1", vrAcqSemaphoreCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG((L"Create Swapchain Acquire Semaphore [%1] success", i), Info, VK, swapchainSyncs[i].acquireSemaphore);
						}


						// Present Semaphore
						const VkResult vrPresSemaphoreCreated = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &swapchainSyncs[i].presentSemaphore);
						if (vrPresSemaphoreCreated != VK_SUCCESS)
						{
							SA_LOG((L"Create Swapchain Present Semaphore [%1] failed!", i), Error, VK, (L"Error Code: %1", vrPresSemaphoreCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG((L"Create Swapchain Present Semaphore [%1] success", i), Info, VK, swapchainSyncs[i].presentSemaphore);
						}


						// Fence
						const VkResult vrFenceCreated = vkCreateFence(device, &fenceCreateInfo, nullptr, &swapchainSyncs[i].fence);
						if (vrFenceCreated != VK_SUCCESS)
						{
							SA_LOG((L"Create Swapchain Fence [%1] failed!", i), Error, VK, (L"Error Code: %1", vrFenceCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG((L"Create Swapchain Fence [%1] success", i), Info, VK, swapchainSyncs[i].fence);
						}
					}
				}
			}


			// Commands
			if (true)
			{
				// Pool
				{
					const VkCommandPoolCreateInfo createInfo{
						.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
						.pNext = nullptr,
						.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
						.queueFamilyIndex = deviceQueueFamilyIndices.graphicsFamily,
					};

					const VkResult vrCmdPoolCreated = vkCreateCommandPool(device, &createInfo, nullptr, &cmdPool);
					if (vrCmdPoolCreated != VK_SUCCESS)
					{
						SA_LOG(L"Create Command Pool failed!", Error, VK, (L"Error Code: %1", vrCmdPoolCreated));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create Command Pool success.", Info, VK, cmdPool);
					}
				}


				// CmdBuffers
				{
					VkCommandBufferAllocateInfo allocInfo{
						.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
						.pNext = nullptr,
						.commandPool = cmdPool,
						.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
						.commandBufferCount = bufferingCount,
					};

					const VkResult vrAllocCmdBuffers = vkAllocateCommandBuffers(device, &allocInfo, cmdBuffers.data());
					if (vrAllocCmdBuffers != VK_SUCCESS)
					{
						SA_LOG(L"Allocate Command buffers failed!", Error, VK, (L"Error Code: %1", vrAllocCmdBuffers));
						return EXIT_FAILURE;
					}
					else
					{
						for (uint32_t i = 0; i < bufferingCount; ++i)
						{
							SA_LOG((L"Allocate Command buffer [%1] success.", i), Info, VK, cmdBuffers[i]);
						}
					}
				}
			}


			// Scene Resources
			if (true)
			{
				// Depth Texture
				{
					// Image
					{
						const VkImageCreateInfo imageCreateInfo{
							.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0,
							.imageType = VK_IMAGE_TYPE_2D,
							.format = sceneDepthFormat,
							.extent = VkExtent3D{ windowSize.x, windowSize.y, 1u },
							.mipLevels = 1,
							.arrayLayers = 1,
							.samples = VK_SAMPLE_COUNT_1_BIT,
							.tiling = VK_IMAGE_TILING_OPTIMAL,
							.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
							.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
							.queueFamilyIndexCount = 0,
							.pQueueFamilyIndices = nullptr,
							.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						};

						const VkResult vrImageCreated = vkCreateImage(device, &imageCreateInfo, nullptr, &sceneDepthImage);
						if (vrImageCreated != VK_SUCCESS)
						{
							SA_LOG(L"Create Scene Depth Image failed!", Error, Vk, (L"Error Code: %1", vrImageCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Scene Depth Image success.", Info, VK, sceneDepthImage);
						}
					}


					// Image Memory
					{
						VkMemoryRequirements memRequirements;
						vkGetImageMemoryRequirements(device, sceneDepthImage, &memRequirements);

						const VkMemoryAllocateInfo allocInfo{
							.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
							.pNext = nullptr,
							.allocationSize = memRequirements.size,
							.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
						};

						const VkResult vrImgAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sceneDepthImageMemory);
						if (vrImgAlloc != VK_SUCCESS)
						{
							SA_LOG(L"Create Scene Depth Image Memory failed!", Error, Vk, (L"Error Code: %1", vrImgAlloc));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Scene Depth Image Memory success.", Info, VK, sceneDepthImageMemory);
						}


						const VkResult vrImgMemBind = vkBindImageMemory(device, sceneDepthImage, sceneDepthImageMemory, 0);
						if (vrImgMemBind != VK_SUCCESS)
						{
							SA_LOG(L"Bind Scene Depth Image Memory failed!", Error, Vk, (L"Error Code: %1", vrImgMemBind));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Bind Scene Depth Image Memory success.", Info, VK);
						}
					}


					// Image View
					{
						const VkImageViewCreateInfo viewInfo{
							.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0,
							.image = sceneDepthImage,
							.viewType = VK_IMAGE_VIEW_TYPE_2D,
							.format = sceneDepthFormat,
							.components{
								.r = VK_COMPONENT_SWIZZLE_IDENTITY,
								.g = VK_COMPONENT_SWIZZLE_IDENTITY,
								.b = VK_COMPONENT_SWIZZLE_IDENTITY,
								.a = VK_COMPONENT_SWIZZLE_IDENTITY
							},
							.subresourceRange{
								.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
								.baseMipLevel = 0,
								.levelCount = 1,
								.baseArrayLayer = 0,
								.layerCount = 1,
							}
						};

						const VkResult vrImgViewCreated = vkCreateImageView(device, &viewInfo, nullptr, &sceneDepthImageView);
						if (vrImgViewCreated != VK_SUCCESS)
						{
							SA_LOG(L"Create Scene Depth Image View failed!", Error, Vk, (L"Error Code: %1", vrImgViewCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Scene Depth Image View success.", Info, VK, sceneDepthImageView);
						}
					}
				}
			}

			// Render Pass
			if (true)
			{
				const std::array<VkAttachmentDescription, 2> attachments{
					VkAttachmentDescription{
						.flags = 0,
						.format = sceneColorFormat,
						.samples = VK_SAMPLE_COUNT_1_BIT,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
						.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
						.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					},
					VkAttachmentDescription{
						.flags = 0,
						.format = sceneDepthFormat,
						.samples = VK_SAMPLE_COUNT_1_BIT,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
						.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
						.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					},
				};

				const VkAttachmentReference colorAttachmentRef{
					.attachment = 0,
					.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				};

				const VkAttachmentReference depthAttachmentRef{
					.attachment = 1,
					.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				};

				const VkSubpassDescription subpass{
					.flags = 0,
					.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
					.inputAttachmentCount = 0,
					.pInputAttachments = nullptr,
					.colorAttachmentCount = 1,
					.pColorAttachments = &colorAttachmentRef,
					.pResolveAttachments = nullptr,
					.pDepthStencilAttachment = &depthAttachmentRef,
					.preserveAttachmentCount = 0,
					.pPreserveAttachments = nullptr,
				};

				const VkSubpassDependency dependency{
					.srcSubpass = VK_SUBPASS_EXTERNAL,
					.dstSubpass = 0,
					.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dependencyFlags = 0u,
				};

				const VkRenderPassCreateInfo renderPassInfo{
					.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
					.pNext = nullptr,
					.flags = 0,
					.attachmentCount = static_cast<uint32_t>(attachments.size()),
					.pAttachments = attachments.data(),
					.subpassCount = 1,
					.pSubpasses = &subpass,
					.dependencyCount = 1,
					.pDependencies = &dependency,
				};

				const VkResult vrRenderPassCreated = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
				if (vrRenderPassCreated != VK_SUCCESS)
				{
					SA_LOG(L"Create RenderPass failed!", Error, VK, (L"Error Code: %1", vrRenderPassCreated));
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG(L"Create RenderPass success", Info, VK, renderPass);
				}
			}


			// Framebuffers
			if (true)
			{
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					std::array<VkImageView, 2u> attachments{
						swapchainImageViews[i],
						sceneDepthImageView
					};

					const VkFramebufferCreateInfo framebufferInfo{
						.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0,
						.renderPass = renderPass,
						.attachmentCount = static_cast<uint32_t>(attachments.size()),
						.pAttachments = attachments.data(),
						.width = windowSize.x,
						.height = windowSize.y,
						.layers = 1,
					};

					const VkResult vrFrameBuffCreated = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]);
					if (vrFrameBuffCreated != VK_SUCCESS)
					{
						SA_LOG((L"Create FrameBuffer [%1] failed!", i), Error, VK, (L"Error Code: %1", vrFrameBuffCreated));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG((L"Create FrameBuffer [%1] success", i), Info, VK, framebuffers[i]);
					}
				}
			}


			// Pipeline
			if (true)
			{
				// Viewport & Scissor
				{
					viewport = VkViewport{
						.x = 0,
						.y = static_cast<float>(windowSize.y),			//
						.width = static_cast<float>(windowSize.x),		// Flipping the Y-axis on viewport to match DX12
						.height = -static_cast<float>(windowSize.y),	//
						.minDepth = 0.0f,
						.maxDepth = 1.0f,
					};

					scissorRect = VkRect2D{
						.offset = VkOffset2D{ 0, 0 },
						.extent = VkExtent2D{ windowSize.x, windowSize.y },
					};
				}


				// Lit
				{
					// DescriptorSetLayout
					{
						std::array<VkDescriptorSetLayoutBinding, 7> bindings{
							VkDescriptorSetLayoutBinding{ // Camera buffer
								.binding = 0,
								.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
								.pImmutableSamplers = nullptr,
							},
							VkDescriptorSetLayoutBinding{ // Object buffer
								.binding = 1,
								.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
								.pImmutableSamplers = nullptr,
							},
							VkDescriptorSetLayoutBinding{ // PBR Albedo
								.binding = 2,
								.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
								.pImmutableSamplers = nullptr,
							},
							VkDescriptorSetLayoutBinding{ // PBR NormalMap
								.binding = 3,
								.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
								.pImmutableSamplers = nullptr,
							},
							VkDescriptorSetLayoutBinding{ // PBR MetallicMap
								.binding = 4,
								.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
								.pImmutableSamplers = nullptr,
							},
							VkDescriptorSetLayoutBinding{ // PBR RoughnessMap
								.binding = 5,
								.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
								.pImmutableSamplers = nullptr,
							},
							VkDescriptorSetLayoutBinding{ // PointLights buffer
								.binding = 6,
								.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
								.descriptorCount = 1,
								.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
								.pImmutableSamplers = nullptr,
							},
						};

						const VkDescriptorSetLayoutCreateInfo layoutInfo{
							.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.bindingCount = static_cast<uint32_t>(bindings.size()),
							.pBindings = bindings.data(),
						};

						const VkResult vrDescLayoutCreated = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &litDescSetLayout);
						if (vrDescLayoutCreated != VK_SUCCESS)
						{
							SA_LOG(L"Create Lit DescriptorSet Layout failed!", Error, VK, (L"Error Code: %1", vrDescLayoutCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit DescriptorSet Layout success.", Info, VK, litDescSetLayout);
						}
					}


					// Pipeline Layout
					{
						const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0,
							.setLayoutCount = 1u,
							.pSetLayouts = &litDescSetLayout,
							.pushConstantRangeCount = 0u,
							.pPushConstantRanges = nullptr,
						};

						const VkResult vrPipLayoutCreated = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &litPipelineLayout);
						if (vrPipLayoutCreated != VK_SUCCESS)
						{
							SA_LOG(L"Create Lit Pipeline Layout failed!", Error, VK, (L"Error Code: %1", vrPipLayoutCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit Pipeline Layout success", Info, VK, litPipelineLayout);
						}
					}


					// Vertex Shader
					{
						std::vector<uint32_t> shCode;

						CompileShaderFromFile("Resources/Shaders/GLSL/LitShader.vert", shaderc_vertex_shader, shCode);

						const VkShaderModuleCreateInfo createInfo{
							.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.codeSize = static_cast<uint32_t>(shCode.size()) * sizeof(uint32_t),
							.pCode = shCode.data(),
						};

						const VkResult vrShaderCompile = vkCreateShaderModule(device, &createInfo, nullptr, &litVertexShader);
						if (vrShaderCompile != VK_SUCCESS)
						{
							SA_LOG(L"Create Lit Vertex Shader failed!", Info, VK, (L"Error code: %1", vrShaderCompile));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit Vertex Shader success", Info, VK, litVertexShader);
						}
					}

					// Fragment Shader
					{
						std::vector<uint32_t> shCode;

						CompileShaderFromFile("Resources/Shaders/GLSL/LitShader.frag", shaderc_fragment_shader, shCode);

						const VkShaderModuleCreateInfo createInfo{
							.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.codeSize = static_cast<uint32_t>(shCode.size()) * sizeof(uint32_t),
							.pCode = shCode.data(),
						};

						const VkResult vrShaderCompile = vkCreateShaderModule(device, &createInfo, nullptr, &litFragmentShader);
						if (vrShaderCompile != VK_SUCCESS)
						{
							SA_LOG(L"Create Lit Fragment Shader failed!", Info, VK, (L"Error code: %1", vrShaderCompile));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit Fragment Shader success", Info, VK, litFragmentShader);
						}
					}


					// Pipeline
					{
						const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
							VkPipelineShaderStageCreateInfo{
								.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.stage = VK_SHADER_STAGE_VERTEX_BIT,
								.module = litVertexShader,
								.pName = "main",
								.pSpecializationInfo = nullptr,
							},
							VkPipelineShaderStageCreateInfo{
								.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
								.module = litFragmentShader,
								.pName = "main",
								.pSpecializationInfo = nullptr,
							},
						};

						const std::array<VkVertexInputBindingDescription, 4> vertexInputBindings{
							VkVertexInputBindingDescription{ // Position buffer
								.binding = 0,
								.stride = sizeof(SA::Vec3f),
								.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
							},
							VkVertexInputBindingDescription{ // Normal buffer
								.binding = 1,
								.stride = sizeof(SA::Vec3f),
								.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
							},
							VkVertexInputBindingDescription{ // Tangent buffer
								.binding = 2,
								.stride = sizeof(SA::Vec3f),
								.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
							},
							VkVertexInputBindingDescription{ // UV buffer
								.binding = 3,
								.stride = sizeof(SA::Vec2f),
								.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
							},
						};

						const std::array<VkVertexInputAttributeDescription, 4> vertexInputAttribs{
							VkVertexInputAttributeDescription{ // Position Input
								.location = 0u,
								.binding = 0u,
								.format = VK_FORMAT_R32G32B32_SFLOAT,
								.offset = 0u,
							},
							VkVertexInputAttributeDescription{ // Normal Input
								.location = 1u,
								.binding = 1u,
								.format = VK_FORMAT_R32G32B32_SFLOAT,
								.offset = 0u,
							},
							VkVertexInputAttributeDescription{ // Tangent Input
								.location = 2u,
								.binding = 2u,
								.format = VK_FORMAT_R32G32B32_SFLOAT,
								.offset = 0u,
							},
							VkVertexInputAttributeDescription{ // UV Input
								.location = 3u,
								.binding = 3u,
								.format = VK_FORMAT_R32G32_SFLOAT,
								.offset = 0u,
							},
						};

						const VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
							.primitiveRestartEnable = VK_FALSE,
						};

						const VkPipelineVertexInputStateCreateInfo vertexInputInfo{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size()),
							.pVertexBindingDescriptions = vertexInputBindings.data(),
							.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttribs.size()),
							.pVertexAttributeDescriptions = vertexInputAttribs.data(),
						};

						const VkPipelineViewportStateCreateInfo viewportInfo{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.viewportCount = 1,
							.pViewports = &viewport,
							.scissorCount = 1,
							.pScissors = &scissorRect,
						};

						const VkPipelineRasterizationStateCreateInfo rasterInfo{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.depthClampEnable = VK_FALSE,
							.rasterizerDiscardEnable = VK_FALSE,
							.polygonMode = VK_POLYGON_MODE_FILL,
							.cullMode = VK_CULL_MODE_BACK_BIT,
							.frontFace = VK_FRONT_FACE_CLOCKWISE,
							.depthBiasEnable = VK_FALSE,
							.depthBiasConstantFactor = 0.0f,
							.depthBiasClamp = 0.0f,
							.depthBiasSlopeFactor = 0.0f,
							.lineWidth = 1.0f,
						};

						const VkPipelineMultisampleStateCreateInfo multisampleInfo{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
							.sampleShadingEnable = VK_FALSE,
							.minSampleShading = 1.0f,
							.pSampleMask = nullptr,
							.alphaToCoverageEnable = VK_FALSE,
							.alphaToOneEnable = VK_FALSE,
						};

						const VkPipelineDepthStencilStateCreateInfo depthStencilState{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.depthTestEnable = VK_TRUE,
							.depthWriteEnable = VK_TRUE,
							.depthCompareOp = VK_COMPARE_OP_LESS,
							.depthBoundsTestEnable = VK_FALSE,
							.stencilTestEnable = VK_FALSE,
							.front = {},
							.back = {},
							.minDepthBounds = 0.0f,
							.maxDepthBounds = 1.0f,
						};

						const std::array<VkPipelineColorBlendAttachmentState, 1> colorBlendAttachs{
							VkPipelineColorBlendAttachmentState{
								.blendEnable = VK_FALSE,
								.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
								.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
								.colorBlendOp = VK_BLEND_OP_ADD,
								.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
								.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
								.alphaBlendOp = VK_BLEND_OP_ADD,
								.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
													VK_COLOR_COMPONENT_G_BIT |
													VK_COLOR_COMPONENT_B_BIT |
													VK_COLOR_COMPONENT_A_BIT,
							},
						};

						const VkPipelineColorBlendStateCreateInfo colorBlendState{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.logicOpEnable = VK_FALSE,
							.logicOp = VK_LOGIC_OP_COPY,
							.attachmentCount = static_cast<uint32_t>(colorBlendAttachs.size()),
							.pAttachments = colorBlendAttachs.data(),
							.blendConstants = { 0.0f },
						};

						const std::array<VkDynamicState, 2> dynamicStates{
							VK_DYNAMIC_STATE_VIEWPORT,
							VK_DYNAMIC_STATE_SCISSOR
						};

						const VkPipelineDynamicStateCreateInfo dynamicStateInfo{
							.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
							.pDynamicStates = dynamicStates.data(),
						};


						const VkGraphicsPipelineCreateInfo pipelineInfo{
							.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.stageCount = static_cast<uint32_t>(shaderStages.size()),
							.pStages = shaderStages.data(),
							.pVertexInputState = &vertexInputInfo,
							.pInputAssemblyState = &inputAssemblyState,
							.pTessellationState = nullptr,
							.pViewportState = &viewportInfo,
							.pRasterizationState = &rasterInfo,
							.pMultisampleState = &multisampleInfo,
							.pDepthStencilState = &depthStencilState,
							.pColorBlendState = &colorBlendState,
							.pDynamicState = &dynamicStateInfo,
							.layout = litPipelineLayout,
							.renderPass = renderPass,
							.subpass = 0u,
							.basePipelineHandle = VK_NULL_HANDLE,
							.basePipelineIndex = -1,
						};

						const VkResult vrCreatePipeline = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &litPipeline);
						if (vrCreatePipeline != VK_SUCCESS)
						{
							SA_LOG(L"Create Lit Pipeline failed!", Error, VK, (L"Error Code: %1", vrCreatePipeline));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit Pipeline success", Info, VK, litPipeline);
						}
					}
				}
			}


			const VkCommandBufferBeginInfo beginInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.pNext = nullptr,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
				.pInheritanceInfo = nullptr,
			};
			vkBeginCommandBuffer(cmdBuffers[0], &beginInfo);


			// Resources
			if (true)
			{
				Assimp::Importer importer;

				// Meshes
				{
					// Sphere
					{
						const char* path = "Resources/Models/Shapes/sphere.obj";
						const aiScene* scene = importer.ReadFile(path, aiProcess_CalcTangentSpace | aiProcess_ConvertToLeftHanded);
						if (!scene)
						{
							SA_LOG(L"Assimp loading failed!", Error, Assimp, path);
							return EXIT_FAILURE;
						}

						const aiMesh* inMesh = scene->mMeshes[0];

						// Position
						{
							const VkBufferCreateInfo bufferInfo{
								.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.size = sizeof(SA::Vec3f) * inMesh->mNumVertices,
								.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
							};

							const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &sphereVertexBuffers[0]);
							if (vrBufferCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex Position Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex Position Buffer success", Info, VK, sphereVertexBuffers[0]);
							}


							// Memory
							VkMemoryRequirements memRequirements;
							vkGetBufferMemoryRequirements(device, sphereVertexBuffers[0], &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sphereVertexBufferMemories[0]);
							if (vrBufferAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex Position Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex Position Buffer Memory success", Info, VK, sphereVertexBufferMemories[0]);
							}


							const VkResult vrBindBufferMem = vkBindBufferMemory(device, sphereVertexBuffers[0], sphereVertexBufferMemories[0], 0);
							if (vrBindBufferMem != VK_SUCCESS)
							{
								SA_LOG(L"Bind Sphere Vertex Position Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Bind Sphere Vertex Position Buffer Memory success", Info, VK);
							}


							// Submit
							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[0], bufferInfo.size, inMesh->mVertices);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex Position Buffer submit failed!", Error, VK);
								return EXIT_FAILURE;
							}
						}

						// Normal
						{
							const VkBufferCreateInfo bufferInfo{
								.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.size = sizeof(SA::Vec3f) * inMesh->mNumVertices,
								.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
							};

							const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &sphereVertexBuffers[1]);
							if (vrBufferCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex Normal Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex Normal Buffer success", Info, VK, sphereVertexBuffers[1]);
							}


							// Memory
							VkMemoryRequirements memRequirements;
							vkGetBufferMemoryRequirements(device, sphereVertexBuffers[1], &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sphereVertexBufferMemories[1]);
							if (vrBufferAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex Normal Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex Normal Buffer Memory success", Info, VK, sphereVertexBufferMemories[1]);
							}


							const VkResult vrBindBufferMem = vkBindBufferMemory(device, sphereVertexBuffers[1], sphereVertexBufferMemories[1], 0);
							if (vrBindBufferMem != VK_SUCCESS)
							{
								SA_LOG(L"Bind Sphere Vertex Normal Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Bind Sphere Vertex Normal Buffer Memory success", Info, VK);
							}


							// Submit
							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[1], bufferInfo.size, inMesh->mNormals);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex Normal Buffer submit failed!", Error, VK);
								return EXIT_FAILURE;
							}
						}

						// Tangent
						{
							const VkBufferCreateInfo bufferInfo{
								.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.size = sizeof(SA::Vec3f) * inMesh->mNumVertices,
								.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
							};

							const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &sphereVertexBuffers[2]);
							if (vrBufferCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex Tangent Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex Tangent Buffer success", Info, VK, sphereVertexBuffers[2]);
							}


							// Memory
							VkMemoryRequirements memRequirements;
							vkGetBufferMemoryRequirements(device, sphereVertexBuffers[2], &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sphereVertexBufferMemories[2]);
							if (vrBufferAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex Tangent Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex Tangent Buffer Memory success", Info, VK, sphereVertexBufferMemories[2]);
							}


							const VkResult vrBindBufferMem = vkBindBufferMemory(device, sphereVertexBuffers[2], sphereVertexBufferMemories[2], 0);
							if (vrBindBufferMem != VK_SUCCESS)
							{
								SA_LOG(L"Bind Sphere Vertex Tangent Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Bind Sphere Vertex Tangent Buffer Memory success", Info, VK);
							}


							// Submit
							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[2], bufferInfo.size, inMesh->mTangents);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex Tangent Buffer submit failed!", Error, VK);
								return EXIT_FAILURE;
							}
						}

						// UV
						{
							const VkBufferCreateInfo bufferInfo{
								.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.size = sizeof(SA::Vec2f) * inMesh->mNumVertices,
								.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
							};

							const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &sphereVertexBuffers[3]);
							if (vrBufferCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex UV Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex UV Buffer success", Info, VK, sphereVertexBuffers[3]);
							}


							// Memory
							VkMemoryRequirements memRequirements;
							vkGetBufferMemoryRequirements(device, sphereVertexBuffers[3], &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sphereVertexBufferMemories[3]);
							if (vrBufferAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Vertex UV Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Vertex UV Buffer Memory success", Info, VK, sphereVertexBufferMemories[3]);
							}


							const VkResult vrBindBufferMem = vkBindBufferMemory(device, sphereVertexBuffers[3], sphereVertexBufferMemories[3], 0);
							if (vrBindBufferMem != VK_SUCCESS)
							{
								SA_LOG(L"Bind Sphere Vertex UV Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Bind Sphere Vertex UV Buffer Memory success", Info, VK);
							}


							// Pack
							std::vector<SA::Vec2f> uvs;
							uvs.reserve(inMesh->mNumVertices);

							for (uint32_t i = 0; i < inMesh->mNumVertices; ++i)
							{
								uvs.push_back(SA::Vec2f{ inMesh->mTextureCoords[0][i].x, inMesh->mTextureCoords[0][i].y });
							}

							// Submit
							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[3], bufferInfo.size, uvs.data());
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex UV Buffer submit failed!", Error, VK);
								return EXIT_FAILURE;
							}
						}

						// Index
						{
							const VkBufferCreateInfo bufferInfo{
								.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.size = sizeof(uint16_t) * inMesh->mNumFaces * 3,
								.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
							};

							const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &sphereIndexBuffer);
							if (vrBufferCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Index Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Index Buffer success", Info, VK, sphereIndexBuffer);
							}


							// Memory
							VkMemoryRequirements memRequirements;
							vkGetBufferMemoryRequirements(device, sphereIndexBuffer, &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sphereIndexBufferMemory);
							if (vrBufferAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create Sphere Index Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create Sphere Index Buffer Memory success", Info, VK, sphereIndexBufferMemory);
							}


							const VkResult vrBindBufferMem = vkBindBufferMemory(device, sphereIndexBuffer, sphereIndexBufferMemory, 0);
							if (vrBindBufferMem != VK_SUCCESS)
							{
								SA_LOG(L"Bind Sphere Index Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Bind Sphere Index Buffer Memory success", Info, VK);
							}


							// Pack indices into uint16_t since max index < 65535.
							std::vector<uint16_t> indices;
							indices.resize(inMesh->mNumFaces * 3);
							sphereIndexCount = inMesh->mNumFaces * 3;

							for (unsigned int i = 0; i < inMesh->mNumFaces; ++i)
							{
								indices[i * 3] = static_cast<uint16_t>(inMesh->mFaces[i].mIndices[0]);
								indices[i * 3 + 1] = static_cast<uint16_t>(inMesh->mFaces[i].mIndices[1]);
								indices[i * 3 + 2] = static_cast<uint16_t>(inMesh->mFaces[i].mIndices[2]);
							}


							// Submit
							const bool bSubmitSuccess = SubmitBufferToGPU(sphereIndexBuffer, bufferInfo.size, indices.data());
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Index Buffer submit failed!", Error, VK);
								return EXIT_FAILURE;
							}
						}
					}
				}

				// Textures
				if (true)
				{
					stbi_set_flip_vertically_on_load(true);

					// Albedo
					if (true)
					{
						const char* path = "Resources/Textures/RustedIron2/rustediron2_basecolor.png";

						int width, height, channels;
						char* inData = reinterpret_cast<char*>(stbi_load(path, &width, &height, &channels, 4));
						if (!inData)
						{
							SA_LOG((L"STBI Texture Loading {%1} failed", path), Error, STB, stbi_failure_reason());
							return EXIT_FAILURE;
						}

						std::vector<char> data(inData, inData + width * height * channels);

						uint32_t mipLevels = 0u;
						uint32_t totalSize = 0u;
						std::vector<SA::Vec2ui> mipExtents;
						GenerateMipMapsCPU(
							SA::Vec2ui{
								static_cast<uint32_t>(width),
								static_cast<uint32_t>(height)
							},
							data,
							mipLevels,
							totalSize,
							mipExtents,
							channels);


						// Image
						{
							const VkImageCreateInfo imageInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.imageType = VK_IMAGE_TYPE_2D,
								.format = VK_FORMAT_R8G8B8A8_UNORM,
								.extent{
									.width = static_cast<uint32_t>(width),
									.height = static_cast<uint32_t>(height),
									.depth = 1u,
								},
								.mipLevels = mipLevels,
								.arrayLayers = 1u,
								.samples = VK_SAMPLE_COUNT_1_BIT,
								.tiling = VK_IMAGE_TILING_OPTIMAL,
								.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
								.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
							};

							const VkResult vrImageCreated = vkCreateImage(device, &imageInfo, nullptr, &rustedIron2AlbedoImage);
							if (vrImageCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture failed!", Error, VK, (L"Error code: %1", vrImageCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture success", Info, VK, rustedIron2AlbedoImage);
							}
						}


						// ImageMemory
						{
							VkMemoryRequirements memRequirements;
							vkGetImageMemoryRequirements(device, rustedIron2AlbedoImage, &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrImageAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &rustedIron2AlbedoImageMemory);
							if (vrImageAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture Alloc failed!", Error, VK, (L"Error code: %1", vrImageAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture Alloc success", Info, VK, rustedIron2AlbedoImageMemory);
							}
						}

						// Bind
						{
							const VkResult vrImageBindMem = vkBindImageMemory(device, rustedIron2AlbedoImage, rustedIron2AlbedoImageMemory, 0);
							if (vrImageBindMem != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture Memory bind failed!", Error, VK, (L"Error code: %1", vrImageBindMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture Memory bind success", Info, VK);
							}
						}

						// Image View
						{
							const VkImageViewCreateInfo viewInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.image = rustedIron2AlbedoImage,
								.viewType = VK_IMAGE_VIEW_TYPE_2D,
								.format = VK_FORMAT_R8G8B8A8_UNORM,
								.components{
									.r = VK_COMPONENT_SWIZZLE_IDENTITY,
									.g = VK_COMPONENT_SWIZZLE_IDENTITY,
									.b = VK_COMPONENT_SWIZZLE_IDENTITY,
									.a = VK_COMPONENT_SWIZZLE_IDENTITY
								},
								.subresourceRange{
									.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
									.baseMipLevel = 0,
									.levelCount = mipLevels,
									.baseArrayLayer = 0,
									.layerCount = 1,
								},
							};

							const VkResult vrImageViewCreated = vkCreateImageView(device, &viewInfo, nullptr, &rustedIron2AlbedoImageView);
							if (vrImageViewCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron Albedo ImageView failed!", Error, VK, (L"Error Code: %1", vrImageViewCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron Albedo ImageView success.", Info, VK, rustedIron2AlbedoImageView);
							}
						}

						const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2AlbedoImage, mipExtents, totalSize, channels, data.data());
						if (!bSubmitSuccess)
						{
							SA_LOG(L"RustedIron2 Albedo Texture submit failed!", Error, VK);
							return EXIT_FAILURE;
						}

						stbi_image_free(inData);
					}

					// Normal
					if (true)
					{
						const char* path = "Resources/Textures/RustedIron2/rustediron2_normal.png";

						int width, height, channels;
						char* inData = reinterpret_cast<char*>(stbi_load(path, &width, &height, &channels, 4));
						if (!inData)
						{
							SA_LOG((L"STBI Texture Loading {%1} failed", path), Error, STB, stbi_failure_reason());
							return EXIT_FAILURE;
						}
						channels = 4; // must force channels to 4 (format is RGBA).

						std::vector<char> data(inData, inData + width * height * channels);

						uint32_t mipLevels = 0u;
						uint32_t totalSize = 0u;
						std::vector<SA::Vec2ui> mipExtents;
						GenerateMipMapsCPU(
							SA::Vec2ui{
								static_cast<uint32_t>(width),
								static_cast<uint32_t>(height)
							},
							data,
							mipLevels,
							totalSize,
							mipExtents,
							channels);


						// Image
						{
							const VkImageCreateInfo imageInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.imageType = VK_IMAGE_TYPE_2D,
								.format = VK_FORMAT_R8G8B8A8_UNORM,
								.extent{
									.width = static_cast<uint32_t>(width),
									.height = static_cast<uint32_t>(height),
									.depth = 1u,
								},
								.mipLevels = mipLevels,
								.arrayLayers = 1u,
								.samples = VK_SAMPLE_COUNT_1_BIT,
								.tiling = VK_IMAGE_TILING_OPTIMAL,
								.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
								.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
							};

							const VkResult vrImageCreated = vkCreateImage(device, &imageInfo, nullptr, &rustedIron2NormalImage);
							if (vrImageCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Normal Texture failed!", Error, VK, (L"Error code: %1", vrImageCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Normal Texture success", Info, VK, rustedIron2NormalImage);
							}
						}


						// ImageMemory
						{
							VkMemoryRequirements memRequirements;
							vkGetImageMemoryRequirements(device, rustedIron2NormalImage, &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrImageAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &rustedIron2NormalImageMemory);
							if (vrImageAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Normal Texture Alloc failed!", Error, VK, (L"Error code: %1", vrImageAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Normal Texture Alloc success", Info, VK, rustedIron2NormalImageMemory);
							}
						}

						// Bind
						{
							const VkResult vrImageBindMem = vkBindImageMemory(device, rustedIron2NormalImage, rustedIron2NormalImageMemory, 0);
							if (vrImageBindMem != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Normal Texture Memory bind failed!", Error, VK, (L"Error code: %1", vrImageBindMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Normal Texture Memory bind success", Info, VK);
							}
						}

						// Image View
						{
							const VkImageViewCreateInfo viewInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.image = rustedIron2NormalImage,
								.viewType = VK_IMAGE_VIEW_TYPE_2D,
								.format = VK_FORMAT_R8G8B8A8_UNORM,
								.components{
									.r = VK_COMPONENT_SWIZZLE_IDENTITY,
									.g = VK_COMPONENT_SWIZZLE_IDENTITY,
									.b = VK_COMPONENT_SWIZZLE_IDENTITY,
									.a = VK_COMPONENT_SWIZZLE_IDENTITY
								},
								.subresourceRange{
									.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
									.baseMipLevel = 0,
									.levelCount = mipLevels,
									.baseArrayLayer = 0,
									.layerCount = 1,
								},
							};

							const VkResult vrImageViewCreated = vkCreateImageView(device, &viewInfo, nullptr, &rustedIron2NormalImageView);
							if (vrImageViewCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron Normal ImageView failed!", Error, VK, (L"Error Code: %1", vrImageViewCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron Normal ImageView success.", Info, VK, rustedIron2NormalImageView);
							}
						}

						const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2NormalImage, mipExtents, totalSize, channels, data.data());
						if (!bSubmitSuccess)
						{
							SA_LOG(L"RustedIron2 Normal Texture submit failed!", Error, VK);
							return EXIT_FAILURE;
						}

						stbi_image_free(inData);
					}

					// Metallic
					if(true)
					{
						const char* path = "Resources/Textures/RustedIron2/rustediron2_metallic.png";

						int width, height, channels;
						char* inData = reinterpret_cast<char*>(stbi_load(path, &width, &height, &channels, 1));
						if (!inData)
						{
							SA_LOG((L"STBI Texture Loading {%1} failed", path), Error, STB, stbi_failure_reason());
							return EXIT_FAILURE;
						}

						std::vector<char> data(inData, inData + width * height * channels);

						uint32_t mipLevels = 0u;
						uint32_t totalSize = 0u;
						std::vector<SA::Vec2ui> mipExtents;
						GenerateMipMapsCPU(
							SA::Vec2ui{
								static_cast<uint32_t>(width),
								static_cast<uint32_t>(height)
							},
							data,
							mipLevels,
							totalSize,
							mipExtents,
							channels);


						// Image
						{
							const VkImageCreateInfo imageInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.imageType = VK_IMAGE_TYPE_2D,
								.format = VK_FORMAT_R8_UNORM,
								.extent{
									.width = static_cast<uint32_t>(width),
									.height = static_cast<uint32_t>(height),
									.depth = 1u,
								},
								.mipLevels = mipLevels,
								.arrayLayers = 1u,
								.samples = VK_SAMPLE_COUNT_1_BIT,
								.tiling = VK_IMAGE_TILING_OPTIMAL,
								.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
								.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
							};

							const VkResult vrImageCreated = vkCreateImage(device, &imageInfo, nullptr, &rustedIron2MetallicImage);
							if (vrImageCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture failed!", Error, VK, (L"Error code: %1", vrImageCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture success", Info, VK, rustedIron2MetallicImage);
							}
						}


						// ImageMemory
						{
							VkMemoryRequirements memRequirements;
							vkGetImageMemoryRequirements(device, rustedIron2MetallicImage, &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrImageAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &rustedIron2MetallicImageMemory);
							if (vrImageAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture Alloc failed!", Error, VK, (L"Error code: %1", vrImageAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture Alloc success", Info, VK, rustedIron2MetallicImageMemory);
							}
						}

						// Bind
						{
							const VkResult vrImageBindMem = vkBindImageMemory(device, rustedIron2MetallicImage, rustedIron2MetallicImageMemory, 0);
							if (vrImageBindMem != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture Memory bind failed!", Error, VK, (L"Error code: %1", vrImageBindMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture Memory bind success", Info, VK);
							}
						}

						// Image View
						{
							const VkImageViewCreateInfo viewInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.image = rustedIron2MetallicImage,
								.viewType = VK_IMAGE_VIEW_TYPE_2D,
								.format = VK_FORMAT_R8_UNORM,
								.components{
									.r = VK_COMPONENT_SWIZZLE_IDENTITY,
									.g = VK_COMPONENT_SWIZZLE_IDENTITY,
									.b = VK_COMPONENT_SWIZZLE_IDENTITY,
									.a = VK_COMPONENT_SWIZZLE_IDENTITY
								},
								.subresourceRange{
									.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
									.baseMipLevel = 0,
									.levelCount = mipLevels,
									.baseArrayLayer = 0,
									.layerCount = 1,
								},
							};

							const VkResult vrImageViewCreated = vkCreateImageView(device, &viewInfo, nullptr, &rustedIron2MetallicImageView);
							if (vrImageViewCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron Metallic ImageView failed!", Error, VK, (L"Error Code: %1", vrImageViewCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron Metallic ImageView success.", Info, VK, rustedIron2MetallicImageView);
							}
						}

						const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2MetallicImage, mipExtents, totalSize, channels, data.data());
						if (!bSubmitSuccess)
						{
							SA_LOG(L"RustedIron2 Metallic Texture submit failed!", Error, VK);
							return EXIT_FAILURE;
						}

						stbi_image_free(inData);
					}

					// Roughness
					if (true)
					{
						const char* path = "Resources/Textures/RustedIron2/rustediron2_roughness.png";

						int width, height, channels;
						char* inData = reinterpret_cast<char*>(stbi_load(path, &width, &height, &channels, 1));
						if (!inData)
						{
							SA_LOG((L"STBI Texture Loading {%1} failed", path), Error, STB, stbi_failure_reason());
							return EXIT_FAILURE;
						}

						std::vector<char> data(inData, inData + width * height * channels);

						uint32_t mipLevels = 0u;
						uint32_t totalSize = 0u;
						std::vector<SA::Vec2ui> mipExtents;
						GenerateMipMapsCPU(
							SA::Vec2ui{
								static_cast<uint32_t>(width),
								static_cast<uint32_t>(height)
							},
							data,
							mipLevels,
							totalSize,
							mipExtents,
							channels);


						// Image
						{
							const VkImageCreateInfo imageInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.imageType = VK_IMAGE_TYPE_2D,
								.format = VK_FORMAT_R8_UNORM,
								.extent{
									.width = static_cast<uint32_t>(width),
									.height = static_cast<uint32_t>(height),
									.depth = 1u,
								},
								.mipLevels = mipLevels,
								.arrayLayers = 1u,
								.samples = VK_SAMPLE_COUNT_1_BIT,
								.tiling = VK_IMAGE_TILING_OPTIMAL,
								.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
								.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
								.queueFamilyIndexCount = 0u,
								.pQueueFamilyIndices = nullptr,
								.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
							};

							const VkResult vrImageCreated = vkCreateImage(device, &imageInfo, nullptr, &rustedIron2RoughnessImage);
							if (vrImageCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture failed!", Error, VK, (L"Error code: %1", vrImageCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture success", Info, VK, rustedIron2RoughnessImage);
							}
						}


						// ImageMemory
						{
							VkMemoryRequirements memRequirements;
							vkGetImageMemoryRequirements(device, rustedIron2RoughnessImage, &memRequirements);

							const VkMemoryAllocateInfo allocInfo{
								.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
								.pNext = nullptr,
								.allocationSize = memRequirements.size,
								.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
							};

							const VkResult vrImageAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &rustedIron2RoughnessImageMemory);
							if (vrImageAlloc != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture Alloc failed!", Error, VK, (L"Error code: %1", vrImageAlloc));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture Alloc success", Info, VK, rustedIron2RoughnessImageMemory);
							}
						}

						// Bind
						{
							const VkResult vrImageBindMem = vkBindImageMemory(device, rustedIron2RoughnessImage, rustedIron2RoughnessImageMemory, 0);
							if (vrImageBindMem != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture Memory bind failed!", Error, VK, (L"Error code: %1", vrImageBindMem));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture Memory bind success", Info, VK);
							}
						}

						// Image View
						{
							const VkImageViewCreateInfo viewInfo{
								.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								.pNext = nullptr,
								.flags = 0u,
								.image = rustedIron2RoughnessImage,
								.viewType = VK_IMAGE_VIEW_TYPE_2D,
								.format = VK_FORMAT_R8_UNORM,
								.components{
									.r = VK_COMPONENT_SWIZZLE_IDENTITY,
									.g = VK_COMPONENT_SWIZZLE_IDENTITY,
									.b = VK_COMPONENT_SWIZZLE_IDENTITY,
									.a = VK_COMPONENT_SWIZZLE_IDENTITY
								},
								.subresourceRange{
									.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
									.baseMipLevel = 0,
									.levelCount = mipLevels,
									.baseArrayLayer = 0,
									.layerCount = 1,
								},
							};

							const VkResult vrImageViewCreated = vkCreateImageView(device, &viewInfo, nullptr, &rustedIron2RoughnessImageView);
							if (vrImageViewCreated != VK_SUCCESS)
							{
								SA_LOG(L"Create RustedIron Roughness ImageView failed!", Error, VK, (L"Error Code: %1", vrImageViewCreated));
								return EXIT_FAILURE;
							}
							else
							{
								SA_LOG(L"Create RustedIron Roughness ImageView success.", Info, VK, rustedIron2RoughnessImageView);
							}
						}

						const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2RoughnessImage, mipExtents, totalSize, channels, data.data());
						if (!bSubmitSuccess)
						{
							SA_LOG(L"RustedIron2 Roughness Texture submit failed!", Error, VK);
							return EXIT_FAILURE;
						}

						stbi_image_free(inData);
					}
				}

				// Samplers
				if (true)
				{
					const VkSamplerCreateInfo samplerInfo{
						.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0u,
						.magFilter = VK_FILTER_LINEAR,
						.minFilter = VK_FILTER_LINEAR,
						.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
						.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
						.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
						.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
						.mipLodBias = 0.0f,
						.anisotropyEnable = VK_FALSE,
						.maxAnisotropy = 1.0f,
						.compareEnable = VK_FALSE,
						.compareOp = VK_COMPARE_OP_ALWAYS,
						.minLod = 0.0f,
						.maxLod = 12.0f, // RustedIron PBR textures have 12 mips.
						.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
						.unnormalizedCoordinates = VK_FALSE,
					};

					const VkResult vrSamplerCreater = vkCreateSampler(device, &samplerInfo, nullptr, &rustedIron2Sampler);
					if (vrSamplerCreater != VK_SUCCESS)
					{
						SA_LOG(L"Create RustedIron2 Sampler failed!", Error, VK, (L"Error code: %1", rustedIron2Sampler));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create RustedIron2 Sampler success", Info, VK, rustedIron2Sampler);
					}
				}
			}

			// Scene Objects
			if (true)
			{
				// Camera Buffers
				{
					const VkBufferCreateInfo bufferInfo{
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0u,
						.size = sizeof(CameraUBO),
						.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
						.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
						.queueFamilyIndexCount = 0u,
						.pQueueFamilyIndices = nullptr,
					};

					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &cameraBuffers[i]);
						if (vrBufferCreated != VK_SUCCESS)
						{
							SA_LOG((L"Create Camera Buffer [%1] failed!", i), Error, VK, (L"Error code: %1", vrBufferCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG((L"Create Camera Buffer [%1] success", i), Info, VK, cameraBuffers[i]);
						}


						// Memory
						VkMemoryRequirements memRequirements;
						vkGetBufferMemoryRequirements(device, cameraBuffers[i], &memRequirements);

						const VkMemoryAllocateInfo allocInfo{
							.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
							.pNext = nullptr,
							.allocationSize = memRequirements.size,
							.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
						};

						const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &cameraBufferMemories[i]);
						if (vrBufferAlloc != VK_SUCCESS)
						{
							SA_LOG((L"Create Camera Buffer Memory [%1] failed!", i), Error, VK, (L"Error code: %1", vrBufferAlloc));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG((L"Create Camera Buffer Memory [%1] success", i), Info, VK, cameraBufferMemories[i]);
						}


						const VkResult vrBindBufferMem = vkBindBufferMemory(device, cameraBuffers[i], cameraBufferMemories[i], 0);
						if (vrBindBufferMem != VK_SUCCESS)
						{
							SA_LOG((L"Bind Camera Buffer Memory [%1] failed!", i), Error, VK, (L"Error code: %1", vrBindBufferMem));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG((L"Bind Camera Buffer Memory [%1] success", i), Info, VK);
						}
					}
				}

				// Sphere Object Buffer
				{
					const VkBufferCreateInfo bufferInfo{
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0u,
						.size = sizeof(ObjectUBO),
						.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
						.queueFamilyIndexCount = 0u,
						.pQueueFamilyIndices = nullptr,
					};

					const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &sphereObjectBuffer);
					if (vrBufferCreated != VK_SUCCESS)
					{
						SA_LOG(L"Create Sphere Object Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create Sphere Object Buffer success", Info, VK, sphereObjectBuffer);
					}


					// Memory
					VkMemoryRequirements memRequirements;
					vkGetBufferMemoryRequirements(device, sphereObjectBuffer, &memRequirements);

					const VkMemoryAllocateInfo allocInfo{
						.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
						.pNext = nullptr,
						.allocationSize = memRequirements.size,
						.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
					};

					const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &sphereObjectBufferMemory);
					if (vrBufferAlloc != VK_SUCCESS)
					{
						SA_LOG(L"Create Object Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create Object Buffer Memory success", Info, VK, sphereObjectBufferMemory);
					}


					const VkResult vrBindBufferMem = vkBindBufferMemory(device, sphereObjectBuffer, sphereObjectBufferMemory, 0);
					if (vrBindBufferMem != VK_SUCCESS)
					{
						SA_LOG(L"Bind Object Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Bind Object Buffer Memory success", Info, VK);
					}

					// Submit
					const SA::CMat4f transform = SA::CMat4f::MakeTranslation(spherePosition);
					const bool bSubmitSuccess = SubmitBufferToGPU(sphereObjectBuffer, bufferInfo.size, &transform);
					if (!bSubmitSuccess)
					{
						SA_LOG(L"Sphere Object Buffer submit failed!", Error, DX12);
						return EXIT_FAILURE;
					}
				}

				// PointLights Buffer
				{
					const VkBufferCreateInfo bufferInfo{
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0u,
						.size = pointLightNum * sizeof(PointLightUBO),
						.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
						.queueFamilyIndexCount = 0u,
						.pQueueFamilyIndices = nullptr,
					};

					const VkResult vrBufferCreated = vkCreateBuffer(device, &bufferInfo, nullptr, &pointLightBuffer);
					if (vrBufferCreated != VK_SUCCESS)
					{
						SA_LOG(L"Create PointLights Buffer failed!", Error, VK, (L"Error code: %1", vrBufferCreated));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create PointLights Buffer success", Info, VK, pointLightBuffer);
					}


					// Memory
					VkMemoryRequirements memRequirements;
					vkGetBufferMemoryRequirements(device, pointLightBuffer, &memRequirements);

					const VkMemoryAllocateInfo allocInfo{
						.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
						.pNext = nullptr,
						.allocationSize = memRequirements.size,
						.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
					};

					const VkResult vrBufferAlloc = vkAllocateMemory(device, &allocInfo, nullptr, &pointLightBufferMemory);
					if (vrBufferAlloc != VK_SUCCESS)
					{
						SA_LOG(L"Create PointLights Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBufferAlloc));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create PointLights Buffer Memory success", Info, VK, pointLightBufferMemory);
					}


					const VkResult vrBindBufferMem = vkBindBufferMemory(device, pointLightBuffer, pointLightBufferMemory, 0);
					if (vrBindBufferMem != VK_SUCCESS)
					{
						SA_LOG(L"Bind PointLights Buffer Memory failed!", Error, VK, (L"Error code: %1", vrBindBufferMem));
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Bind PointLights Buffer Memory success", Info, VK);
					}

					// Submit
					std::array<PointLightUBO, pointLightNum> pointlightsUBO{
						PointLightUBO{
							.position = SA::Vec3f(-0.25f, -1.0f, 0.0f),
							.intensity = 4.0f,
							.color = SA::Vec3f(1.0f, 1.0f, 0.0f),
							.radius = 3.0f
						},
						PointLightUBO{
							.position = SA::Vec3f(1.75f, 2.0f, 1.0f),
							.intensity = 7.0f,
							.color = SA::Vec3f(0.0f, 1.0f, 1.0f),
							.radius = 4.0f
						}
					};
					const bool bSubmitSuccess = SubmitBufferToGPU(pointLightBuffer, bufferInfo.size, pointlightsUBO.data());
					if (!bSubmitSuccess)
					{
						SA_LOG(L"PointLights Buffer submit failed!", Error, DX12);
						return EXIT_FAILURE;
					}
				}


				// PBR Sphere Descriptor Sets
				{
					// Desc Pool
					{
						std::array<VkDescriptorPoolSize, 3> poolSize{
							VkDescriptorPoolSize{
								.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
								.descriptorCount = 2u,
							},
							VkDescriptorPoolSize{
								.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
								.descriptorCount = 1u,
							},
							VkDescriptorPoolSize{
								.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
								.descriptorCount = 4u,
							},
						};

						const VkDescriptorPoolCreateInfo poolInfo{
							.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
							.pNext = nullptr,
							.flags = 0u,
							.maxSets = bufferingCount,
							.poolSizeCount = static_cast<uint32_t>(poolSize.size()),
							.pPoolSizes = poolSize.data(),
						};

						const VkResult vrDescPooolCreated = vkCreateDescriptorPool(device, &poolInfo, nullptr, &pbrSphereDescPool);
						if (vrDescPooolCreated != VK_SUCCESS)
						{
							SA_LOG(L"Create PBR Sphere Descriptor Pool failed!", Error, VK, (L"Error code: %1", vrDescPooolCreated));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create PBR Sphere Descriptor Pool success", Info, VK, pbrSphereDescPool);
						}
					}

					// Alloc sets
					std::array<VkDescriptorSetLayout, bufferingCount> layouts;
					layouts.fill(litDescSetLayout);

					const VkDescriptorSetAllocateInfo allocInfo{
						.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
						.pNext = nullptr,
						.descriptorPool = pbrSphereDescPool,
						.descriptorSetCount = bufferingCount,
						.pSetLayouts = layouts.data(),
					};

					vkAllocateDescriptorSets(device, &allocInfo, pbrSphereDescSets.data());
					
					// Write sets
					std::array<VkWriteDescriptorSet, 7> writes;

					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						// Camera
						const VkDescriptorBufferInfo cameraBufferInfo{
							.buffer = cameraBuffers[i],
							.offset = 0,
							.range = sizeof(CameraUBO),
						};
						writes[0] =  VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 0,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
							.pImageInfo = nullptr,
							.pBufferInfo = &cameraBufferInfo,
							.pTexelBufferView = nullptr,
						};

						// Object
						const VkDescriptorBufferInfo objectBufferInfo{
							.buffer = sphereObjectBuffer,
							.offset = 0,
							.range = sizeof(ObjectUBO),
						};
						writes[1] = VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 1,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
							.pImageInfo = nullptr,
							.pBufferInfo = &objectBufferInfo,
							.pTexelBufferView = nullptr,
						};

						// PBR RustedIron Albedo
						const VkDescriptorImageInfo albedoImageInfo{
							.sampler = rustedIron2Sampler,
							.imageView = rustedIron2AlbedoImageView,
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						};
						writes[2] = VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 2,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &albedoImageInfo,
							.pBufferInfo = nullptr,
							.pTexelBufferView = nullptr,
						};

						// PBR RustedIron Normal
						const VkDescriptorImageInfo normalImageInfo{
							.sampler = rustedIron2Sampler,
							.imageView = rustedIron2NormalImageView,
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						};
						writes[3] = VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 3,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &normalImageInfo,
							.pBufferInfo = nullptr,
							.pTexelBufferView = nullptr,
						};

						// PBR RustedIron Metallic
						const VkDescriptorImageInfo metallicImageInfo{
							.sampler = rustedIron2Sampler,
							.imageView = rustedIron2MetallicImageView,
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						};
						writes[4] = VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 4,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &metallicImageInfo,
							.pBufferInfo = nullptr,
							.pTexelBufferView = nullptr,
						};

						// PBR RustedIron Roughness
						const VkDescriptorImageInfo roughnessImageInfo{
							.sampler = rustedIron2Sampler,
							.imageView = rustedIron2RoughnessImageView,
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						};
						writes[5] = VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 5,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &roughnessImageInfo,
							.pBufferInfo = nullptr,
							.pTexelBufferView = nullptr,
						};

						// PointLights buffer
						const VkDescriptorBufferInfo pointLightsBufferInfo{
							.buffer = pointLightBuffer,
							.offset = 0,
							.range = pointLightNum * sizeof(PointLightUBO),
						};
						writes[6] = VkWriteDescriptorSet{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = nullptr,
							.dstSet = pbrSphereDescSets[i],
							.dstBinding = 6,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
							.pImageInfo = nullptr,
							.pBufferInfo = &pointLightsBufferInfo,
							.pTexelBufferView = nullptr,
						};


						// Update
						vkUpdateDescriptorSets(device,
							static_cast<uint32_t>(writes.size()),
							writes.data(),
							0,
							nullptr);
					}
				}
			}


			vkEndCommandBuffer(cmdBuffers[0]);
		}
	}



	// Loop
	{
		double oldMouseX = 0.0f;
		double oldMouseY = 0.0f;
		float dx = 0.0f;
		float dy = 0.0f;

		glfwGetCursorPos(window, &oldMouseX, &oldMouseY);

		const float fixedTime = 0.0025f;
		float accumulateTime = 0.0f;
		auto start = std::chrono::steady_clock::now();

		while (!glfwWindowShouldClose(window))
		{
			auto end = std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(end - start).count();
			accumulateTime += deltaTime;
			start = end;

			// Fixed Update
			if (accumulateTime >= fixedTime)
			{
				accumulateTime -= fixedTime;

				glfwPollEvents();

				// Process input
				{
					if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
						glfwSetWindowShouldClose(window, true);
					if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
						cameraTr.position += fixedTime * cameraMoveSpeed * cameraTr.Right();
					if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
						cameraTr.position -= fixedTime * cameraMoveSpeed * cameraTr.Right();
					if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
						cameraTr.position += fixedTime * cameraMoveSpeed * cameraTr.Up();
					if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
						cameraTr.position -= fixedTime * cameraMoveSpeed * cameraTr.Up();
					if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
						cameraTr.position += fixedTime * cameraMoveSpeed * cameraTr.Forward();
					if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
						cameraTr.position -= fixedTime * cameraMoveSpeed * cameraTr.Forward();

					double mouseX = 0.0f;
					double mouseY = 0.0f;

					glfwGetCursorPos(window, &mouseX, &mouseY);

					if (mouseX != oldMouseX || mouseY != oldMouseY)
					{
						dx += static_cast<float>(mouseX - oldMouseX) * fixedTime * cameraRotSpeed * SA::Maths::DegToRad<float>;
						dy += static_cast<float>(mouseY - oldMouseY) * fixedTime * cameraRotSpeed * SA::Maths::DegToRad<float>;

						oldMouseX = mouseX;
						oldMouseY = mouseY;

						dx = dx > SA::Maths::Pi<float> ?
							dx - SA::Maths::Pi<float> :
							dx < -SA::Maths::Pi<float> ? dx + SA::Maths::Pi<float> : dx;
						dy = dy > SA::Maths::Pi<float> ?
							dy - SA::Maths::Pi<float> :
							dy < -SA::Maths::Pi<float> ? dy + SA::Maths::Pi<float> : dy;

						cameraTr.rotation = SA::Quatf(cos(dx), 0, sin(dx), 0) * SA::Quatf(cos(dy), sin(dy), 0, 0);
					}
				}
			}


			// Render
			{
				// Swapchain Begin
				{
					// Wait current Fence.
					vkWaitForFences(device, 1, &swapchainSyncs[swapchainFrameIndex].fence, true, UINT64_MAX);

					// Reset current Fence.
					vkResetFences(device, 1, &swapchainSyncs[swapchainFrameIndex].fence);

					const VkResult vrAcqImage = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapchainSyncs[swapchainFrameIndex].acquireSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);
					if (vrAcqImage != VK_SUCCESS)
					{
						SA_LOG(L"Swapchain Acquire Next Image failed!", Error, VK, (L"Error code: %1", vrAcqImage));
						return EXIT_FAILURE;
					}
				}


				// Update camera.
				{

					// Fill Data with updated values.
					CameraUBO cameraUBO;
					cameraUBO.view = cameraTr.Matrix();
					const SA::CMat4f perspective = SA::CMat4f::MakePerspective(cameraFOV, float(windowSize.x) / float(windowSize.y), cameraNear, cameraFar);
					cameraUBO.invViewProj = perspective * cameraUBO.view.GetInversed();

					// Memory mapping and Upload (CPU to GPU transfer).
					void* data = nullptr;
					vkMapMemory(device, cameraBufferMemories[swapchainFrameIndex], 0u, sizeof(CameraUBO), 0u, &data);

					std::memcpy(data, &cameraUBO, sizeof(CameraUBO));
					
					vkUnmapMemory(device, cameraBufferMemories[swapchainFrameIndex]);
				}


				// Register Commands
				auto cmd = cmdBuffers[swapchainFrameIndex];
				{
					vkResetCommandBuffer(cmd, 0);

					const VkCommandBufferBeginInfo beginInfo{
						.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
						.pNext = nullptr,
						.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
						.pInheritanceInfo = nullptr,
					};
					vkBeginCommandBuffer(cmd, &beginInfo);


					// RenderPass Begin
					std::array<VkClearValue, 2> clears{
						sceneClearColor,
						sceneClearDepth
					};

					const VkRenderPassBeginInfo renderPassInfo{
						.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
						.pNext = nullptr,
						.renderPass = renderPass,
						.framebuffer = framebuffers[swapchainImageIndex],
						.renderArea{
							.offset = { 0, 0 },
							.extent = { windowSize.x, windowSize.y },
						},
						.clearValueCount = static_cast<uint32_t>(clears.size()),
						.pClearValues = clears.data(),
					};

					vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);


					// Lit Pipeline
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litPipeline);

					vkCmdSetViewport(cmd, 0, 1, &viewport);
					vkCmdSetScissor(cmd, 0, 1, &scissorRect);


					// Draw Sphere
					std::array<VkDeviceSize, 4> offsets{ 0 };
					vkCmdBindVertexBuffers(cmd, 0,
						static_cast<uint32_t>(sphereVertexBuffers.size()),
						sphereVertexBuffers.data(),
						offsets.data());
					vkCmdBindIndexBuffer(cmd, sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

					vkCmdBindDescriptorSets(cmd,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						litPipelineLayout, 0, 1,
						&pbrSphereDescSets[swapchainFrameIndex],
						0, nullptr);

					vkCmdDrawIndexed(cmd, sphereIndexCount, 1, 0, 0, 0);


					// End Renderpass
					vkCmdEndRenderPass(cmd);

					vkEndCommandBuffer(cmd);
				}


				// Swapchain End
				{
					// Submit graphics.
					{
						const VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

						const VkSubmitInfo submitInfo{
							.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
							.pNext = nullptr,
							.waitSemaphoreCount = 1u,
							.pWaitSemaphores = &swapchainSyncs[swapchainFrameIndex].acquireSemaphore,
							.pWaitDstStageMask = &waitStages,
							.commandBufferCount = 1,
							.pCommandBuffers = &cmd,
							.signalSemaphoreCount = 1u,
							.pSignalSemaphores = &swapchainSyncs[swapchainFrameIndex].presentSemaphore,
						};

						const VkResult vrQueueSubmit = vkQueueSubmit(graphicsQueue, 1, &submitInfo, swapchainSyncs[swapchainFrameIndex].fence);
						if (vrQueueSubmit != VK_SUCCESS)
						{
							SA_LOG(L"Failed to submit graphics queue!", Error, VK, (L"Error code: %1", vrQueueSubmit));
							return EXIT_FAILURE;
						}
					}


					// Submit present.
					{
						const VkPresentInfoKHR presentInfo{
							.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
							.pNext = nullptr,
							.waitSemaphoreCount = 1u,
							.pWaitSemaphores = &swapchainSyncs[swapchainFrameIndex].presentSemaphore,
							.swapchainCount = 1u,
							.pSwapchains = &swapchain,
							.pImageIndices = &swapchainImageIndex,
							.pResults = nullptr,
						};

						const VkResult vrQueuePresent = vkQueuePresentKHR(presentQueue, &presentInfo);
						if (vrQueuePresent != VK_SUCCESS)
						{
							SA_LOG(L"Failed to submit present queue!", Error, VK, (L"Error code: %1", vrQueuePresent));
							return EXIT_FAILURE;
						}
					}

					// Increment next frame.
					swapchainFrameIndex = (swapchainFrameIndex + 1) % bufferingCount;
				}
			}
		}
	}



	// Uninitialization
	if(true)
	{
		// Renderer
		{
			vkDeviceWaitIdle(device);


			// Resources
			{
				// Samplers
				{
					vkDestroySampler(device, rustedIron2Sampler, nullptr);
					SA_LOG(L"Destroy RustedIron2 Sampler success.", Info, VK, rustedIron2RoughnessImage);
					rustedIron2Sampler = VK_NULL_HANDLE;
				}

				// Textures
				{
					// RustedIron2
					{
						// Roughness
						vkDestroyImage(device, rustedIron2RoughnessImage, nullptr);
						SA_LOG(L"Destroy RustedIron2 Roughness Image success.", Info, VK, rustedIron2RoughnessImage);
						rustedIron2RoughnessImage = VK_NULL_HANDLE;
						vkDestroyImageView(device, rustedIron2RoughnessImageView, nullptr);
						SA_LOG(L"Destroy RustedIron2 Roughness Image View success.", Info, VK, rustedIron2RoughnessImageView);
						rustedIron2RoughnessImageView = VK_NULL_HANDLE;
						vkFreeMemory(device, rustedIron2RoughnessImageMemory, nullptr);
						SA_LOG(L"Destroy RustedIron2 Roughness Image Memory success.", Info, VK, rustedIron2RoughnessImageMemory);
						rustedIron2RoughnessImageMemory = VK_NULL_HANDLE;

						// Metallic
						vkDestroyImage(device, rustedIron2MetallicImage, nullptr);
						SA_LOG(L"Destroy RustedIron2 Metallic Image success.", Info, VK, rustedIron2MetallicImage);
						rustedIron2MetallicImage = VK_NULL_HANDLE;
						vkDestroyImageView(device, rustedIron2MetallicImageView, nullptr);
						SA_LOG(L"Destroy RustedIron2 Metallic Image View success.", Info, VK, rustedIron2MetallicImageView);
						rustedIron2MetallicImageView = VK_NULL_HANDLE;
						vkFreeMemory(device, rustedIron2MetallicImageMemory, nullptr);
						SA_LOG(L"Destroy RustedIron2 Metallic Image Memory success.", Info, VK, rustedIron2MetallicImageMemory);
						rustedIron2MetallicImageMemory = VK_NULL_HANDLE;

						// Normal
						vkDestroyImage(device, rustedIron2NormalImage, nullptr);
						SA_LOG(L"Destroy RustedIron2 Normal Image success.", Info, VK, rustedIron2NormalImage);
						rustedIron2NormalImage = VK_NULL_HANDLE;
						vkDestroyImageView(device, rustedIron2NormalImageView, nullptr);
						SA_LOG(L"Destroy RustedIron2 Normal Image View success.", Info, VK, rustedIron2NormalImageView);
						rustedIron2NormalImageView = VK_NULL_HANDLE;
						vkFreeMemory(device, rustedIron2NormalImageMemory, nullptr);
						SA_LOG(L"Destroy RustedIron2 Normal Image Memory success.", Info, VK, rustedIron2NormalImageMemory);
						rustedIron2NormalImageMemory = VK_NULL_HANDLE;

						// Albedo
						vkDestroyImage(device, rustedIron2AlbedoImage, nullptr);
						SA_LOG(L"Destroy RustedIron2 Albedo Image success.", Info, VK, rustedIron2AlbedoImage);
						rustedIron2AlbedoImage = VK_NULL_HANDLE;
						vkDestroyImageView(device, rustedIron2AlbedoImageView, nullptr);
						SA_LOG(L"Destroy RustedIron2 Albedo Image View success.", Info, VK, rustedIron2AlbedoImageView);
						rustedIron2AlbedoImageView = VK_NULL_HANDLE;
						vkFreeMemory(device, rustedIron2AlbedoImageMemory, nullptr);
						SA_LOG(L"Destroy RustedIron2 Albedo Image Memory success.", Info, VK, rustedIron2AlbedoImageMemory);
						rustedIron2AlbedoImageMemory = VK_NULL_HANDLE;
					}
				}

				// Meshes
				{
					// Sphere
					{
						// Index
						vkDestroyBuffer(device, sphereIndexBuffer, nullptr);
						SA_LOG(L"Destroy Sphere Index Buffer success.", Info, VK, sphereIndexBuffer);
						sphereIndexBuffer = VK_NULL_HANDLE;
						vkFreeMemory(device, sphereIndexBufferMemory, nullptr);
						SA_LOG(L"Destroy Sphere Index Buffer Memory success.", Info, VK, sphereIndexBufferMemory);
						sphereIndexBufferMemory = VK_NULL_HANDLE;

						// UV
						vkDestroyBuffer(device, sphereVertexBuffers[3], nullptr);
						SA_LOG(L"Destroy Sphere Vertex UV Buffer success.", Info, VK, sphereVertexBuffers[3]);
						sphereVertexBuffers[3] = VK_NULL_HANDLE;
						vkFreeMemory(device, sphereVertexBufferMemories[3], nullptr);
						SA_LOG(L"Destroy Sphere Vertex UV Buffer Memory success.", Info, VK, sphereVertexBufferMemories[3]);
						sphereVertexBufferMemories[3] = VK_NULL_HANDLE;

						// Tangent
						vkDestroyBuffer(device, sphereVertexBuffers[2], nullptr);
						SA_LOG(L"Destroy Sphere Vertex Tangent Buffer success.", Info, VK, sphereVertexBuffers[2]);
						sphereVertexBuffers[2] = VK_NULL_HANDLE;
						vkFreeMemory(device, sphereVertexBufferMemories[2], nullptr);
						SA_LOG(L"Destroy Sphere Vertex Tangent Buffer Memory success.", Info, VK, sphereVertexBufferMemories[2]);
						sphereVertexBufferMemories[2] = VK_NULL_HANDLE;

						// Normal
						vkDestroyBuffer(device, sphereVertexBuffers[1], nullptr);
						SA_LOG(L"Destroy Sphere Vertex Normal Buffer success.", Info, VK, sphereVertexBuffers[1]);
						sphereVertexBuffers[1] = VK_NULL_HANDLE;
						vkFreeMemory(device, sphereVertexBufferMemories[1], nullptr);
						SA_LOG(L"Destroy Sphere Vertex Normal Buffer Memory success.", Info, VK, sphereVertexBufferMemories[1]);
						sphereVertexBufferMemories[1] = VK_NULL_HANDLE;

						// Position
						vkDestroyBuffer(device, sphereVertexBuffers[0], nullptr);
						SA_LOG(L"Destroy Sphere Vertex Position Buffer success.", Info, VK, sphereVertexBuffers[0]);
						sphereVertexBuffers[0] = VK_NULL_HANDLE;
						vkFreeMemory(device, sphereVertexBufferMemories[0], nullptr);
						SA_LOG(L"Destroy Sphere Vertex Position Buffer Memory success.", Info, VK, sphereVertexBufferMemories[0]);
						sphereVertexBufferMemories[0] = VK_NULL_HANDLE;
					}
				}
			}


			// Scene Objects
			if (true)
			{
				// PointLights
				vkDestroyBuffer(device, pointLightBuffer, nullptr);
				SA_LOG(L"Destroy PointLights Buffer success.", Info, VK, pointLightBuffer);
				pointLightBuffer = VK_NULL_HANDLE;
				vkFreeMemory(device, pointLightBufferMemory, nullptr);
				SA_LOG(L"Destroy PointLights Buffer Memory success.", Info, VK, pointLightBufferMemory);
				pointLightBufferMemory = VK_NULL_HANDLE;

				// Object
				vkDestroyBuffer(device, sphereObjectBuffer, nullptr);
				SA_LOG(L"Destroy Sphere Object Buffer success.", Info, VK, sphereObjectBuffer);
				sphereObjectBuffer = VK_NULL_HANDLE;
				vkFreeMemory(device, sphereObjectBufferMemory, nullptr);
				SA_LOG(L"Destroy Sphere Object Buffer Memory success.", Info, VK, sphereObjectBufferMemory);
				sphereObjectBufferMemory = VK_NULL_HANDLE;

				// Camera
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					vkDestroyBuffer(device, cameraBuffers[i], nullptr);
					SA_LOG((L"Destroy Camera Buffer [%1] success.", i), Info, VK, cameraBuffers[i]);
					cameraBuffers[i] = VK_NULL_HANDLE;
					vkFreeMemory(device, cameraBufferMemories[i], nullptr);
					SA_LOG((L"Destroy Camera Buffer Memory [%1] success.", i), Info, VK, cameraBufferMemories[i]);
					cameraBufferMemories[i] = VK_NULL_HANDLE;
				}

				// Descriptor Sets
				{
					// PBR Sphere Descriptor Sets
					{
						// Automatically freed when destroying DescriptorPool
						//vkFreeDescriptorSets(device, pbrSphereDescPool, bufferingCount, pbrSphereDescSets.data());
						//SA_LOG(L"Destroy PBR Sphere Descriptor Sets success.", Info, VK);
						//pbrSphereDescSets.fill(VK_NULL_HANDLE);

						vkDestroyDescriptorPool(device, pbrSphereDescPool, nullptr);
						SA_LOG(L"Destroy PBR Sphere Descriptor Sets Pool success.", Info, VK, pbrSphereDescPool);
						pbrSphereDescPool = VK_NULL_HANDLE;
					}
				}
			}

			// Pipeline
			{
				// Lit
				{
					// Pipeline
					{
						vkDestroyPipeline(device, litPipeline, nullptr);
						SA_LOG(L"Destroy Lit Pipeline success.", Info, VK, litPipeline);
						litPipeline = VK_NULL_HANDLE;
					}

					// Fragment Shader
					{
						vkDestroyShaderModule(device, litFragmentShader, nullptr);
						SA_LOG(L"Destroy Lit Fragment Shader success.", Info, VK, litFragmentShader);
						litFragmentShader = VK_NULL_HANDLE;
					}

					// Vertex Shader
					{
						vkDestroyShaderModule(device, litVertexShader, nullptr);
						SA_LOG(L"Destroy Lit Vertex Shader success.", Info, VK, litVertexShader);
						litVertexShader = VK_NULL_HANDLE;
					}

					// Pipeline Layout
					{
						vkDestroyPipelineLayout(device, litPipelineLayout, nullptr);
						SA_LOG(L"Destroy Lit PipelineLayout success.", Info, VK, litPipelineLayout);
						litPipelineLayout = VK_NULL_HANDLE;
					}
				}
			}


			// DescriptorSet
			{
				// Lit
				{
					vkDestroyDescriptorSetLayout(device, litDescSetLayout, nullptr);
					SA_LOG(L"Destroy Lit DescriptorSetLayout success.", Info, VK, litDescSetLayout);
					litDescSetLayout = VK_NULL_HANDLE;
				}
			}


			// Framebuffers
			{
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					vkDestroyFramebuffer(device, framebuffers[i], nullptr);
					SA_LOG((L"Destroy FrameBuffer [%1] success.", i), Info, VK, framebuffers[i]);
					framebuffers[i] = VK_NULL_HANDLE;
				}
			}


			// RenderPass
			{
				vkDestroyRenderPass(device, renderPass, nullptr);
				SA_LOG(L"Destroy RenderPass success.", Info, VK, renderPass);
				renderPass = VK_NULL_HANDLE;
			}


			// Scene Resources
			{
				// Depth Texture
				{
					// Image View
					{
						vkDestroyImageView(device, sceneDepthImageView, nullptr);
						SA_LOG(L"Destroy Scene Depth ImageView success", Info, VK, sceneDepthImageView);
						sceneDepthImageView = VK_NULL_HANDLE;
					}

					// Image Memory
					{
						vkFreeMemory(device, sceneDepthImageMemory, nullptr);
						SA_LOG(L"Free Scene Depth Image Memory success", Info, VK, sceneDepthImageMemory);
						sceneDepthImageMemory = VK_NULL_HANDLE;
					}

					// Image
					{
						vkDestroyImage(device, sceneDepthImage, nullptr);
						SA_LOG(L"Destroy Scene Depth Image success", Info, VK, sceneDepthImage);
						sceneDepthImage = VK_NULL_HANDLE;
					}
				}
			}


			// Commands
			{
				// CmdBuffers
				{
					/**
					* Can be skipped: will be automatically be freed when destroying VkCommandPool.
					* Kept for logging purpose.
					*/

					vkFreeCommandBuffers(device, cmdPool, bufferingCount, cmdBuffers.data());
					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						SA_LOG((L"Free Command buffer [%1] success.", i), Info, VK, cmdBuffers[i]);
						cmdBuffers[i] = VK_NULL_HANDLE;
					}
				}

				// Pool
				{
					vkDestroyCommandPool(device, cmdPool, nullptr);
					SA_LOG(L"Destroy Command Pool [%1] success.", Info, VK, cmdPool);
					cmdPool = VK_NULL_HANDLE;
				}
			}


			// Swapchain
			{
				// Synchronization
				{
					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						vkDestroySemaphore(device, swapchainSyncs[i].acquireSemaphore, nullptr);
						SA_LOG((L"Destroy Swapchain Acquire Semaphore [%1] success", i), Info, VK, swapchainSyncs[i].acquireSemaphore);
						swapchainSyncs[i].acquireSemaphore = VK_NULL_HANDLE;

						vkDestroySemaphore(device, swapchainSyncs[i].presentSemaphore, nullptr);
						SA_LOG((L"Destroy Swapchain Present Semaphore [%1] success", i), Info, VK, swapchainSyncs[i].presentSemaphore);
						swapchainSyncs[i].presentSemaphore = VK_NULL_HANDLE;

						vkDestroyFence(device, swapchainSyncs[i].fence, nullptr);
						SA_LOG((L"Destroy Swapchain Fence [%1] success", i), Info, VK, swapchainSyncs[i].fence);
						swapchainSyncs[i].fence = VK_NULL_HANDLE;
					}
				}


				// ImageViews
				{
					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						vkDestroyImageView(device, swapchainImageViews[i], nullptr);
						SA_LOG(L"Destroy Swapchain ImageView success", Info, VK, swapchainImageViews[i]);
						swapchainImageViews[i] = VK_NULL_HANDLE;
					}
				}


				// Backbuffers
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					// Do not destroy swapchain images manually, they are already attached to VkSwapchain lifetime.

					SA_LOG((L"Destroy Swapchain backbuffer image [%1] success", i), Info, VK, swapchainImages[i]);
					swapchainImages[i] = VK_NULL_HANDLE;
				}

				vkDestroySwapchainKHR(device, swapchain, nullptr);
				SA_LOG(L"Destroy Swapchain success", Info, VK, swapchain);
				swapchain = VK_NULL_HANDLE;
			}


			// Device
			{
				SA_LOG(L"Destroy Graphics Queue success", Info, VK, graphicsQueue);
				graphicsQueue = VK_NULL_HANDLE;

				//SA_LOG(L"Destroy Compute Queue success", Info, VK, computeQueue);
				//computeQueue = VK_NULL_HANDLE;

				SA_LOG(L"Destroy Present Queue success", Info, VK, presentQueue);
				presentQueue = VK_NULL_HANDLE;

				vkDestroyDevice(device, nullptr);
				SA_LOG(L"Destroy Logical Device success", Info, VK, device);
				device = VK_NULL_HANDLE;

				SA_LOG(L"Destroy Physical Device success", Info, VK, physicalDevice);
				physicalDevice = VK_NULL_HANDLE;
			}


			// Surface
			{
				vkDestroySurfaceKHR(instance, windowSurface, nullptr);
				SA_LOG(L"Destroy Window Surface success", Info, VK, windowSurface);
				windowSurface = VK_NULL_HANDLE;
			}


			// Instance
			{
				vkDestroyInstance(instance, nullptr);
				SA_LOG(L"Destroy Instance success", Info, VK, instance);
				instance = VK_NULL_HANDLE;
			}
		}


		// GLFW
		{
			glfwDestroyWindow(window);
			glfwTerminate();
		}
	}

	return 0;
}
