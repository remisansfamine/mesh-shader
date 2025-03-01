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

#include <GLFW/glfw3.h>

/**
* Required to directly access HWND handle from GLFW window type.
* See glfwGetWin32Window()
*/
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

GLFWwindow* window = nullptr;
constexpr SA::Vec2ui windowSize = { 1200, 900 };

void GLFWErrorCallback(int32_t error, const char* description)
{
	SA_LOG((L"GLFW Error [%1]: %2", error, description), Error, GLFW.API);
}



// ========== Renderer ==========

/**
* DirectX12 uses Microsoft's ComPtr (SmartPtr) to automatically decrement the object counter and call the interface destroy function on last reference destroy.
* There is no device->Destroy<object>() functions, we MUST use ComPtr.
*/
#include <wrl.h>
template <typename T>
using MComPtr = Microsoft::WRL::ComPtr<T>;

/**
* Base DirectX12 header.
* Requires `d3d12.lib`
*/
#include <d3d12.h> // vulkan.h -> d3d12.h

/**
* DX12 additionnal Debug tools:
* Report live objects after uninitialization to track leak and understroyed objects.
* Requires `dxguid.lib`
*/
#include <DXGIDebug.h>

// === Validation Layers ===

#if SA_DEBUG
DWORD VLayerCallbackCookie = 0;

void ValidationLayersDebugCallback(D3D12_MESSAGE_CATEGORY _category,
	D3D12_MESSAGE_SEVERITY _severity,
	D3D12_MESSAGE_ID _ID,
	LPCSTR _description,
	void* _context)
{
	(void)_context;

	std::wstring categoryStr;

	switch (_category)
	{
	case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED:
		categoryStr = L"Application Defined";
		break;
	case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS:
		categoryStr = L"Miscellaneous";
		break;
	case D3D12_MESSAGE_CATEGORY_INITIALIZATION:
		categoryStr = L"Initialization";
		break;
	case D3D12_MESSAGE_CATEGORY_CLEANUP:
		categoryStr = L"Cleanup";
		break;
	case D3D12_MESSAGE_CATEGORY_COMPILATION:
		categoryStr = L"Compilation";
		break;
	case D3D12_MESSAGE_CATEGORY_STATE_CREATION:
		categoryStr = L"State Creation";
		break;
	case D3D12_MESSAGE_CATEGORY_STATE_SETTING:
		categoryStr = L"State Setting";
		break;
	case D3D12_MESSAGE_CATEGORY_STATE_GETTING:
		categoryStr = L"State Getting";
		break;
	case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION:
		categoryStr = L"Resource Manipulation";
		break;
	case D3D12_MESSAGE_CATEGORY_EXECUTION:
		categoryStr = L"Execution";
		break;
	case D3D12_MESSAGE_CATEGORY_SHADER:
		categoryStr = L"Shader";
		break;
	default:
		categoryStr = L"Unknown";
		break;
	}

	std::wstring dets = SA::StringFormat(L"ID [%1]\tCategory [%2]", static_cast<int>(_ID), categoryStr);

	switch (_severity)
	{
	case D3D12_MESSAGE_SEVERITY_CORRUPTION:
		SA_LOG(_description, AssertFailure, DX12.ValidationLayers, std::move(dets));
		break;
	case D3D12_MESSAGE_SEVERITY_ERROR:
		SA_LOG(_description, Error, DX12.ValidationLayers, std::move(dets));
		break;
	case D3D12_MESSAGE_SEVERITY_WARNING:
		SA_LOG(_description, Warning, DX12.ValidationLayers, std::move(dets));
		break;
	case D3D12_MESSAGE_SEVERITY_INFO:
		// Filter Info: too much logging on Resource create/destroy and Swapchain Present.
		// SA logging already tracking.
		return;
		//SA_LOG(_description, Info, DX12.ValidationLayers, std::move(dets));
		//break;
	case D3D12_MESSAGE_SEVERITY_MESSAGE:
	default:
		SA_LOG(_description, Normal, DX12.ValidationLayers, std::move(dets));
		break;
	}
}

#endif


// === Factory ===

/**
* Header specific to create a Factory (required to create a Device).
* Requires `dxgi.lib`
*/
#include <dxgi1_6.h>
MComPtr<IDXGIFactory6> factory; // VkInstance -> IDXGIFactory


// === Device ===

// Don't need to keep Adapter (physical Device) reference: only use Logical.
MComPtr<ID3D12Device> device; // VkDevice -> ID3D12Device

MComPtr<ID3D12CommandQueue> graphicsQueue; // VkQueue -> ID3D12CommandQueue
// No PresentQueue needed (already handleled by Swapchain).

/**
* Vulkan has native WaitForGPU method (vkDeviceWaitIdle).
*
* DirectX12 must implement it manually using fences.
* For more details on Fence/Event implementation, check Swapchain synchronization implementation below.
*/
HANDLE deviceFenceEvent;
MComPtr<ID3D12Fence> deviceFence;
uint32_t deviceFenceValue = 0u;

void WaitDeviceIdle()
{
	// Schedule a Signal command in the queue.
	graphicsQueue->Signal(deviceFence.Get(), deviceFenceValue);

	// Wait until the fence has been processed.
	deviceFence->SetEventOnCompletion(deviceFenceValue, deviceFenceEvent);
	WaitForSingleObjectEx(deviceFenceEvent, INFINITE, false);

	// Increment for next use.
	++deviceFenceValue;
}


// === Swapchain ===

constexpr uint32_t bufferingCount = 3;

MComPtr<IDXGISwapChain3> swapchain; // VkSwapchainKHR -> IDXGISwapChain
std::array<MComPtr<ID3D12Resource>, bufferingCount> swapchainImages{ nullptr }; // VkImage -> ID3D12Resource
uint32_t swapchainFrameIndex = 0u;

/**
* Vulkan uses Semaphores and Fences for swapchain synchronization
* 1 Fence and 1 Semaphore are used PER FRAME.
*
*
* DirectX12 uses fence and Windows event.
* Only 1 Frame and 1 Windows Event are used FOR ALL FRAME, with different Values (one PER FRAME).
* See DirectX-Graphics-Samples/D3D12HelloFrameBuffering Sample for reference.
*/
HANDLE swapchainFenceEvent = nullptr;
MComPtr<ID3D12Fence> swapchainFence;
std::array<uint32_t, bufferingCount> swapchainFenceValues{ 0u };


// === Commands ===
MComPtr<ID3D12CommandAllocator> cmdAlloc; // VkCommandPool -> ID3D12CommandAllocator

/**
* /!\ with DirectX12, for graphics operations, the 'CommandList' type is not enough. ID3D12GraphicsCommandList must be used.
* Like for Vulkan, we allocate 1 command buffer per frame.
*/
std::array<MComPtr<ID3D12GraphicsCommandList1>, bufferingCount> cmdLists{ nullptr }; // VkCommandBuffer -> ID3D12CommandList


// === Scene Textures ===

// = Color =
constexpr DXGI_FORMAT sceneColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr float sceneClearColor[] = { 0.0f, 0.1f, 0.2f, 1.0f };
// Use Swapchain backbuffer texture as color output.

// = Depth =
constexpr DXGI_FORMAT sceneDepthFormat = DXGI_FORMAT_D16_UNORM;
constexpr D3D12_CLEAR_VALUE sceneDepthClearValue{ .Format = sceneDepthFormat, .DepthStencil = { 1.0f, 0 } };
MComPtr<ID3D12Resource> sceneDepthTexture;



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
			window = glfwCreateWindow(windowSize.x, windowSize.y, "FVTDX12_DX12-Window", nullptr, nullptr);

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
		}


		// Renderer
		{
			// Factory
			{
				UINT dxgiFactoryFlags = 0;

#if SA_DEBUG
				// Validation Layers
				{
					MComPtr<ID3D12Debug1> debugController = nullptr;

					const HRESULT hrDebugInterface = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
					if (SUCCEEDED(hrDebugInterface))
					{
						debugController->EnableDebugLayer();
						debugController->SetEnableGPUBasedValidation(true);
					}
					else
					{
						SA_LOG(L"Validation layer initialization failed.", Error, DX12);
					}
				}

				// Enable additional debug layers.
				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
				
				const HRESULT hrFactoryCreated = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
				if (FAILED(hrFactoryCreated))
				{
					SA_LOG(L"Create Factory failed!", Error, DX12);
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG(L"Create Factory success.", Info, DX12, factory.Get());
				}
			}


			// Device
			{
				// Don't need to keep reference after creating logical device.
				MComPtr<IDXGIAdapter3> adapter; // VkPhysicalDevice -> IDXGIAdapter.

				// Select first prefered GPU, listed by HIGH_PERFORMANCE. No need to manually sort GPU like Vulkan.
				const HRESULT hrQueryGPU = factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
				if (FAILED(hrQueryGPU))
				{
					SA_LOG(L"Adapter not found!", Error, DX12);
					return EXIT_FAILURE;
				}

				const HRESULT hrDeviceCreated = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
				if (FAILED(hrDeviceCreated))
				{
					SA_LOG(L"Create Device failed!", Error, DX12);
					return EXIT_FAILURE;
				}
				else
				{
					const LPCWSTR name = L"Main Device";
					device->SetName(name);

					SA_LOG(L"Create Device success.", Info, DX12, (L"\"%1\" [%2]", name, device.Get()));
				}

#if SA_DEBUG
				// Validation Layers
				{
					MComPtr<ID3D12InfoQueue1> infoQueue = nullptr;

					const HRESULT hrQueryInfoQueue = device->QueryInterface(IID_PPV_ARGS(&infoQueue));
					if (SUCCEEDED(hrQueryInfoQueue))
					{
						/**
						* Cookie must be provided to properly register message callback (and unregister later).
						* Set nullptr as cookie will not crash (and no error) but won't work.
						*/
						infoQueue->RegisterMessageCallback(ValidationLayersDebugCallback,
							D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, nullptr, &VLayerCallbackCookie);

						infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
						infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
						infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
					}
					else
					{
						SA_LOG(L"Device query info queue to enable validation layers failed.", Error, DX12);
					}
				}
#endif

				// Queue
				{
					// This example renderer only uses 1 graphics queue.

					// GFX
					{
						const D3D12_COMMAND_QUEUE_DESC desc{
							.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
							.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
						};

						/**
						* DX12 can create queue 'on the fly' after device creation.
						* No need to specify in advance how many queues will be used by the device object.
						*/
						const HRESULT hrGFXCmdQueueCreated = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&graphicsQueue));
						if (FAILED(hrGFXCmdQueueCreated))
						{
							SA_LOG(L"Create Graphics Queue failed!", Error, DX12);
							return EXIT_FAILURE;
						}
						else
						{
							const LPCWSTR name = L"GraphicsQueue";
							graphicsQueue->SetName(name);

							SA_LOG(L"Create Graphics Queue success.", Info, DX12, (L"\"%1\" [%2]", name, graphicsQueue.Get()));
						}
					}
				}


				// Synchronization
				{
					deviceFenceEvent = CreateEvent(nullptr, false, false, nullptr);
					if (!deviceFenceEvent)
					{
						SA_LOG(L"Create Device Fence Event failed!", Error, DX12);
						return EXIT_FAILURE;
					}
					else
					{
						SA_LOG(L"Create Device Fence Event success.", Info, DX12);
					}

					const HRESULT hrDeviceFenceCreated = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&deviceFence));
					if (FAILED(hrDeviceFenceCreated))
					{
						SA_LOG(L"Create Device Fence failed!", Error, DX12);
						return EXIT_FAILURE;
					}
					else
					{
						const LPCWSTR name = L"DeviceFence";
						deviceFence->SetName(name);

						SA_LOG(L"Create Swapchain Fence success.", Info, DX12, (L"\"%1\" [%2]", name, deviceFence.Get()));
					}
				}
			}


			// Swapchain
			{
				const DXGI_SWAP_CHAIN_DESC1 desc{
					.Width = windowSize.x,
					.Height = windowSize.y,
					.Format = sceneColorFormat,
					.Stereo = false,
					.SampleDesc = {.Count = 1, .Quality = 0 },
					.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
					.BufferCount = bufferingCount,
					.Scaling = DXGI_SCALING_STRETCH,
					.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
					.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
					.Flags = 0,
				};

				MComPtr<IDXGISwapChain1> swapchain1;
				const HRESULT hrSwapChainCreated = factory->CreateSwapChainForHwnd(graphicsQueue.Get(), glfwGetWin32Window(window), &desc, nullptr, nullptr, &swapchain1);
				if (FAILED(hrSwapChainCreated))
				{
					SA_LOG(L"Create Swapchain failed!", Error, DX12);
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG(L"Create Swapchain success.", Info, DX12, swapchain1.Get());
				}

				const HRESULT hrSwapChainCast = swapchain1.As(&swapchain);
				if (FAILED(hrSwapChainCast))
				{
					SA_LOG(L"Swapchain cast failed!", Error, DX12);
					return EXIT_FAILURE;
				}


				// Synchronization
				{
					swapchainFenceEvent = CreateEvent(nullptr, false, false, nullptr);
					if (!swapchainFenceEvent)
					{
						SA_LOG(L"Create Swapchain Fence Event failed!", Error, DX12);
						return EXIT_FAILURE;
					}

					const HRESULT hrSwapChainFenceCreated = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&swapchainFence));
					if (FAILED(hrSwapChainFenceCreated))
					{
						SA_LOG(L"Create Swapchain Fence failed!", Error, DX12);
						return EXIT_FAILURE;
					}
					else
					{
						const LPCWSTR name = L"SwapchainFence";
						swapchainFence->SetName(name);

						SA_LOG(L"Create Swapchain Fence success.", Info, DX12, (L"\"%1\" [%2]", name, swapchainFence.Get()));
					}
				}

				// Query back-buffers
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					const HRESULT hrSwapChainGetBuffer = swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchainImages[i]));
					if (hrSwapChainGetBuffer)
					{
						SA_LOG((L"Get Swapchain Buffer [%1] failed!", i), Error, DX12);
						return EXIT_FAILURE;
					}
					else
					{
						const std::wstring name = L"SwapchainBackBuffer [" + std::to_wstring(i) + L"]";
						swapchainImages[i]->SetName(name.data());

						SA_LOG((L"Get Swapchain Buffer [%1] success.", i), Info, DX12, (L"\"%1\" [%2]", name, swapchainImages[i].Get()));
					}
				}
			}


			// Commands
			{
				// Allocator
				{
					const HRESULT hrCmdAllocCreated = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
					if (FAILED(hrCmdAllocCreated))
					{
						SA_LOG(L"Create Command Allocator failed!", Error, DX12);
						return EXIT_FAILURE;
					}
					else
					{
						LPCWSTR name = L"CommandAlloc";
						cmdAlloc->SetName(name);

						SA_LOG(L"Create Command Allocator success.", Info, DX12, (L"\"%1\" [%2]", name, cmdAlloc.Get()));
					}
				}

				// Lists
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					const HRESULT hrCmdListCreated = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdLists[i]));
					if (FAILED(hrCmdListCreated))
					{
						SA_LOG((L"Create Command List [%1] failed!", i), Error, DX12);
						return EXIT_FAILURE;
					}
					else
					{
						const std::wstring name = L"CommandList [" + std::to_wstring(i) + L"]";
						cmdLists[i]->SetName(name.data());

						SA_LOG((L"Create Command List [%1] success.", i), Info, DX12, (L"\"%1\" [%2]", name, cmdLists[i].Get()));
					}

					// Command list must be closed because we will start the frame by Reset()
					cmdLists[i]->Close();
				}
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
			// Commands
			{
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					SA_LOG((L"Destroy Command List [%1] success", i), Info, DX12, cmdLists[i]);
					cmdLists[i] = nullptr;
				}

				SA_LOG(L"Destroy Command Allocator success", Info, DX12, cmdAlloc);
				cmdAlloc = nullptr;
			}


			// Swapchain
			{
				// Synchronization
				{
					CloseHandle(swapchainFenceEvent);
					SA_LOG(L"Destroy Swapchain Fence Event success", Info, DX12, swapchainFenceEvent);
					swapchainFenceEvent = nullptr;
					
					SA_LOG(L"Destroy Swapchain Fence success", Info, DX12, swapchainFence);
					swapchainFence = nullptr;
				}

				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					SA_LOG((L"Destroy Swapchain image [%1] success", i), Info, DX12, swapchainImages[i]);
					swapchainImages[i] = nullptr;
				}

				SA_LOG(L"Destroy Swapchain success", Info, DX12, swapchain);
				swapchain = nullptr;
			}


			// Device
			{
				// Synchronization
				{
					CloseHandle(deviceFenceEvent);
					SA_LOG(L"Destroy Device Fence Event success", Info, DX12, deviceFenceEvent);
					deviceFenceEvent = nullptr;

					SA_LOG(L"Destroy Device Fence success", Info, DX12, deviceFence);
					deviceFence = nullptr;
				}

				// Queue
				{
					// GFX
					{
						SA_LOG(L"Destroy Graphics Queue success", Info, DX12, graphicsQueue);
						graphicsQueue = nullptr;
					}
				}

#if SA_DEBUG
				// Validation Layers
				if (VLayerCallbackCookie)
				{
					MComPtr<ID3D12InfoQueue1> infoQueue = nullptr;

					const HRESULT hrQueryInfoQueue = device->QueryInterface(IID_PPV_ARGS(&infoQueue));
					if (SUCCEEDED(hrQueryInfoQueue))
					{
						infoQueue->UnregisterMessageCallback(VLayerCallbackCookie);
						VLayerCallbackCookie = 0;
					}
				}
#endif

				SA_LOG(L"Destroy Device success", Info, DX12, device);
				device = nullptr;
			}


			// Factory
			{
				SA_LOG(L"Destroy Factory success", Info, DX12, factory);
				factory = nullptr;

#if SA_DEBUG
				// Report live objects
				MComPtr<IDXGIDebug1> dxgiDebug = nullptr;

				const HRESULT hrDebugInterface = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));
				if (SUCCEEDED(hrDebugInterface))
				{
					dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_ALL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
				}
				else
				{
					SA_LOG(L"Validation layer uninitialized failed.", Error, DX12);
				}
#endif
			}
		}


		// GLFW
		{
			glfwDestroyWindow(window);
			SA_LOG(L"Destroy Window success", Info, GLFW, window);
			window = nullptr;

			glfwTerminate();
		}
	}

	return 0;
}
