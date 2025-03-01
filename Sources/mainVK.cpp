#include <array>

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


// Resource Loading
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#pragma warning(disable : 4505)
#include <stb_image_resize2.h>
#pragma warning(default : 4505)


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

const std::vector<const char*> vkDeviceReqExts{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; // VkPhysicalDevice -> IDXGIAdapter
VkDevice device = VK_NULL_HANDLE; // VkDevice -> ID3D12Device

VkQueue graphicsQueue = VK_NULL_HANDLE;
//VkQueue computeQueue = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;



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

				VkInstanceCreateInfo instanceInfo{
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

				instanceInfo.pNext = &debugUtilsInfo;

				instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				instanceInfo.ppEnabledLayerNames = validationLayers.data();
#endif

				const VkResult vrInstanceCreated = vkCreateInstance(&instanceInfo, nullptr, &instance);
				if (vrInstanceCreated != VK_SUCCESS)
				{
					SA_LOG(L"Create VkInstance failed!", Error, VK, vrInstanceCreated);
					return EXIT_FAILURE;
				}
			}


			// Surface
			{
				/**
				* Create Vulkan Surface from GLFW window.
				* Required to create PresentQueue in Device.
				*/
				glfwCreateWindowSurface(instance, window, nullptr, &windowSurface);
			}

			// Device
			{
				struct QueueFamilyIndices
				{
					uint32_t graphicsFamily = uint32_t(-1);
					//uint32_t computeFamily = uint32_t(-1);
					uint32_t presentFamily = uint32_t(-1);
				};


				// Query physical devices
				uint32_t deviceCount = 0;
				const VkResult vrEnumPhysDeviceCount = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
				if (vrEnumPhysDeviceCount != VK_SUCCESS)
				{
					SA_LOG(L"Enumerate Physical Devices Count failed!", Error, VK, vrEnumPhysDeviceCount);
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
					SA_LOG(L"Enumerate Physical Devices failed!", Error, VK, vrEnumPhysDevices);
					return EXIT_FAILURE;
				}


				// Find first suitable device (no scoring).
				QueueFamilyIndices physicalDeviceQueueFamilies;
				for (auto& currPhysicalDevice : physicalDevices)
				{
					// Check extensions support
					{
						// Query extensions.
						uint32_t extensionCount = 0u;
						const VkResult vrEnumDeviceExtsCount = vkEnumerateDeviceExtensionProperties(currPhysicalDevice, nullptr, &extensionCount, nullptr);
						if (vrEnumDeviceExtsCount != VK_SUCCESS)
						{
							SA_LOG(L"Enumerate Devices extensions count failed!", Error, VK, vrEnumDeviceExtsCount);
							return EXIT_FAILURE;
						}

						std::vector<VkExtensionProperties> supportedExts(extensionCount);
						const VkResult vrEnumDeviceExts = vkEnumerateDeviceExtensionProperties(currPhysicalDevice, nullptr, &extensionCount, supportedExts.data());
						if (vrEnumDeviceExts != VK_SUCCESS)
						{
							SA_LOG(L"Enumerate Devices extensions failed!", Error, VK, vrEnumDeviceExts);
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
									SA_LOG(L"Physical Device Surface Support failed.", Error, VK, vrSupportKHR);
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

						physicalDeviceQueueFamilies = currPhysicalDeviceQueueFamilies;
					}

					physicalDevice = currPhysicalDevice;
					break;
				}

				if (physicalDevice == VK_NULL_HANDLE)
				{
					SA_LOG(L"No suitable PhysicalDevice found.", Error, VK);
					return EXIT_FAILURE;
				}


				// Create Logical Device.
				const VkPhysicalDeviceFeatures deviceFeatures{};

				const float queuePriority = 1.0f;
				const std::array<VkDeviceQueueCreateInfo, 2> queueCreateInfo{
					VkDeviceQueueCreateInfo{
						.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0,
						.queueFamilyIndex = physicalDeviceQueueFamilies.graphicsFamily,
						.queueCount = 1,
						.pQueuePriorities = &queuePriority,
					},
					//VkDeviceQueueCreateInfo{
					//	.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					//	.pNext = nullptr,
					//	.flags = 0,
					//	.queueFamilyIndex = physicalDeviceQueueFamilies.computeFamily,
					//	.queueCount = 1,
					//	.pQueuePriorities = &queuePriority,
					//},
					VkDeviceQueueCreateInfo{
						.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
						.pNext = nullptr,
						.flags = 0,
						.queueFamilyIndex = physicalDeviceQueueFamilies.presentFamily,
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
					SA_LOG(L"Create Logical Device failed.", Error, VK, vrDeviceCreated);
					return EXIT_FAILURE;
				}


				// Create Queues
				vkGetDeviceQueue(device, physicalDeviceQueueFamilies.graphicsFamily, 0, &graphicsQueue);
				//vkGetDeviceQueue(device, physicalDeviceQueueFamilies.presentFamily, 0, &computeQueue);
				vkGetDeviceQueue(device, physicalDeviceQueueFamilies.presentFamily, 0, &presentQueue);
			}
		}
	}



	// Loop
	{
	}



	// Uninitialization
	{
		// Renderer
		{
			// Device
			{
				graphicsQueue = VK_NULL_HANDLE;
				//computeQueue = VK_NULL_HANDLE;
				presentQueue = VK_NULL_HANDLE;

				vkDestroyDevice(device, nullptr);
				physicalDevice = VK_NULL_HANDLE;
			}


			// Surface
			{
				vkDestroySurfaceKHR(instance, windowSurface, nullptr);
			}


			// Instance
			{
				vkDestroyInstance(instance, nullptr);
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
