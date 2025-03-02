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
MComPtr<ID3D12Fence> swapchainFence; // VkFence -> ID3D12Fence
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
constexpr DXGI_FORMAT sceneColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // VkFormat -> DXGI_FORMAT
constexpr float sceneClearColor[] = { 0.0f, 0.1f, 0.2f, 1.0f };
// Use Swapchain backbuffer texture as color output.
MComPtr<ID3D12DescriptorHeap> sceneRTViewHeap;

// = Depth =
constexpr DXGI_FORMAT sceneDepthFormat = DXGI_FORMAT_D16_UNORM; // VkFormat -> DXGI_FORMAT
constexpr D3D12_CLEAR_VALUE sceneDepthClearValue{ .Format = sceneDepthFormat, .DepthStencil = { 1.0f, 0 } };
MComPtr<ID3D12Resource> sceneDepthTexture; // VkImage -> ID3D12Resource
MComPtr<ID3D12DescriptorHeap> sceneDepthRTViewHeap;


// === Pipeline ===

// = Viewport & Scissor =
/**
* Vulkan must specify viewport and scissor on pipeline creation by default (or use dynamic).
* DirectX12 only uses dynamic viewport and scissor (set by commandlist).
*/
D3D12_VIEWPORT viewport; // VkViewport -> D3D12_VIEWPORT
D3D12_RECT scissorRect; // VkRect2D -> D3D12_RECT

// = Lit =

/**
* Basic helper shader compiler header.
* For more advanced features use DirectXShaderCompiler library.
* Requires `d3dcompiler.lib`
*/
#include <d3dcompiler.h>

MComPtr<ID3DBlob> litVertexShader; // VkShaderModule -> ID3DBlob
MComPtr<ID3DBlob> litPixelShader;

MComPtr<ID3D12RootSignature> litRootSign; // VkPipelineLayout -> ID3D12RootSignature
MComPtr<ID3D12PipelineState> litPipelineState; // VkPipeline -> ID3D12PipelineState



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
						SA_LOG(L"Validation layer initialization failed.", Error, DX12, (L"Error Code: %1", hrDebugInterface));
					}
				}

				// Enable additional debug layers.
				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
				
				const HRESULT hrFactoryCreated = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));
				if (FAILED(hrFactoryCreated))
				{
					SA_LOG(L"Create Factory failed!", Error, DX12, (L"Error Code: %1", hrFactoryCreated));
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
					SA_LOG(L"Adapter not found!", Error, DX12, (L"Error Code: %1", hrQueryGPU));
					return EXIT_FAILURE;
				}

				const HRESULT hrDeviceCreated = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
				if (FAILED(hrDeviceCreated))
				{
					SA_LOG(L"Create Device failed!", Error, DX12, (L"Error Code: %1", hrDeviceCreated));
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
						SA_LOG(L"Device query info queue to enable validation layers failed.", Error, DX12, (L"Error Code: %1", hrQueryInfoQueue));
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
							SA_LOG(L"Create Graphics Queue failed!", Error, DX12, (L"Error Code: %1", hrGFXCmdQueueCreated));
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
						SA_LOG(L"Create Device Fence failed!", Error, DX12, (L"Error Code: %1", hrDeviceFenceCreated));
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
					SA_LOG(L"Create Swapchain failed!", Error, DX12, (L"Error Code: %1", hrSwapChainCreated));
					return EXIT_FAILURE;
				}
				else
				{
					SA_LOG(L"Create Swapchain success.", Info, DX12, swapchain1.Get());
				}

				const HRESULT hrSwapChainCast = swapchain1.As(&swapchain);
				if (FAILED(hrSwapChainCast))
				{
					SA_LOG(L"Swapchain cast failed!", Error, DX12, (L"Error Code: %1", hrSwapChainCast));
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
						SA_LOG(L"Create Swapchain Fence failed!", Error, DX12, (L"Error Code: %1", hrSwapChainFenceCreated));
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
						SA_LOG((L"Get Swapchain Buffer [%1] failed!", i), Error, DX12, (L"Error Code: %1", hrSwapChainGetBuffer));
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
						SA_LOG(L"Create Command Allocator failed!", Error, DX12, (L"Error Code: %1", hrCmdAllocCreated));
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
						SA_LOG((L"Create Command List [%1] failed!", i), Error, DX12, (L"Error Code: %1", hrCmdListCreated));
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


			// Scene Textures
			{
				// Color RT View Heap
				{
					/**
					* Create a Render Target typed heap to allocate views.
					*/
					const D3D12_DESCRIPTOR_HEAP_DESC desc{
						.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
						.NumDescriptors = bufferingCount,
						.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
						.NodeMask = 0,
					};

					const HRESULT hrCreateHeap = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&sceneRTViewHeap));
					if (FAILED(hrCreateHeap))
					{
						SA_LOG(L"Create Color RenderTarget ViewHeap failed!", Error, DX12, (L"Error Code: %1", hrCreateHeap));
						return EXIT_FAILURE;
					}
					else
					{
						const LPCWSTR name = L"SceneRTViewHeap";
						sceneRTViewHeap->SetName(name);

						SA_LOG(L"Create Color RenderTarget ViewHeap success.", Info, DX12, (L"\"%1\" [%2]", name, sceneRTViewHeap.Get()));
					}


					// Create RT Views (for each frame)
					D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = sceneRTViewHeap->GetCPUDescriptorHandleForHeapStart();
					const UINT rtvOffset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

					for (uint32_t i = 0; i < bufferingCount; ++i)
					{
						device->CreateRenderTargetView(swapchainImages[i].Get(), nullptr, rtvHandle);
						rtvHandle.ptr += rtvOffset;
					}
				}


				// Depth Scene Texture
				{
					const D3D12_RESOURCE_DESC desc{
						.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
						.Alignment = 0u,
						.Width = windowSize.x,
						.Height = windowSize.y,
						.DepthOrArraySize = 1,
						.MipLevels = 1,
						.Format = sceneDepthFormat,
						.SampleDesc = DXGI_SAMPLE_DESC{
							.Count = 1,
							.Quality = 0,
						},
						.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
						.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
					};

					const D3D12_HEAP_PROPERTIES heap{
						.Type = D3D12_HEAP_TYPE_DEFAULT,
						.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
						.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
						.CreationNodeMask = 1,
						.VisibleNodeMask = 1,
					};

					const HRESULT hrCreateDepthTexture = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &sceneDepthClearValue, IID_PPV_ARGS(&sceneDepthTexture));
					if (FAILED(hrCreateDepthTexture))
					{
						SA_LOG(L"Create Scene Depth Texture failed!", Error, DX12, (L"Error Code: %1", hrCreateDepthTexture));
						return EXIT_FAILURE;
					}
					else
					{
						const LPCWSTR name = L"SceneDepthTexture";
						sceneDepthTexture->SetName(name);

						SA_LOG(L"Create Scene Depth Texture success.", Info, DX12, (L"\"%1\" [%2]", name, sceneDepthTexture.Get()));
					}
				}

				// Depth Scene RT View Heap
				{
					/**
					* Create a 'Depth' typed heap to allocate views.
					*/
					const D3D12_DESCRIPTOR_HEAP_DESC desc{
						.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
						.NumDescriptors = 1,
						.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
						.NodeMask = 0,
					};

					const HRESULT hrCreateHeap = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&sceneDepthRTViewHeap));
					if (FAILED(hrCreateHeap))
					{
						SA_LOG(L"Create Depth ViewHeap failed!", Error, DX12, (L"Error Code: %1", hrCreateHeap));
						return EXIT_FAILURE;
					}
					else
					{
						const LPCWSTR name = L"SceneDepthViewHeap";
						sceneDepthRTViewHeap->SetName(name);

						SA_LOG(L"Create Depth ViewHeap success", Info, DX12, (L"\"%1\" [%2]", name, sceneDepthRTViewHeap.Get()));
					}

					/**
					* Create Depth View to use sceneDepthTexture as a render target.
					*/
					device->CreateDepthStencilView(sceneDepthTexture.Get(), nullptr, sceneDepthRTViewHeap->GetCPUDescriptorHandleForHeapStart());
				}
			}


			// Pipeline
			{
				// Viewport & Scissor
				{
					viewport = D3D12_VIEWPORT{
						.TopLeftX = 0,
						.TopLeftY = 0,
						.Width = float(windowSize.x),
						.Height = float(windowSize.y),
						.MinDepth = 0.0f,
						.MaxDepth = 1.0f,
					};

					scissorRect = D3D12_RECT{
						.left = 0,
						.top = 0,
						.right = static_cast<LONG>(windowSize.x),
						.bottom = static_cast<LONG>(windowSize.y),
					};
				}

#if SA_DEBUG
				// Enable better shader debugging with the graphics debugging tools.
				const UINT shaderCompileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
				const UINT shaderCompileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

				// Lit
				{
					// RootSignature
					{
						/**
						* Root signature is the Pipeline Layout.
						* Since there is no 'DescriptorSetLayout' in DX12, we have to manually specify all the shader bindings here.
						*/

						const D3D12_DESCRIPTOR_RANGE1 pointLightSRVRange{
							.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
							.NumDescriptors = 1,
							.BaseShaderRegister = 0,
							.RegisterSpace = 0,
							.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
							.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
						};

						// Use Descriptor Table to bind all the textures at once.
						const D3D12_DESCRIPTOR_RANGE1 pbrTextureRange[]{
							// Albedo
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 1,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
								.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
							},
							// Normal
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 2,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
								.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
							},
							// Metallic
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 3,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
								.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
							},
							// Roughness
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 4,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
								.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
							},
						};

						const D3D12_ROOT_PARAMETER1 params[]{
							// Camera Constant buffer
							{
								.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
								.Descriptor = {
									.ShaderRegister = 0,
									.RegisterSpace = 0,
									.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
								},
								.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
							},
							// Object Constant buffer
							{
								.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
								.Descriptor = {
									.ShaderRegister = 1,
									.RegisterSpace = 0,
									.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
								},
								.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
							},
							// Point Lights Structured buffer
							{
								/**
								* Use DescriptorTable with SRV type instead of direct SRV binding to create BufferView in SRV Heap.
								* This allows use to correctly call pointLights.GetDimensions() in HLSL.
								*/
								.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
								.DescriptorTable {
									.NumDescriptorRanges = 1,
									.pDescriptorRanges = &pointLightSRVRange
								},
								.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
							},
							// PBR texture table
							{
								.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
								.DescriptorTable {
									.NumDescriptorRanges = _countof(pbrTextureRange),
									.pDescriptorRanges = pbrTextureRange
								},
								.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
							}
						};

						const D3D12_STATIC_SAMPLER_DESC sampler{
							.Filter = D3D12_FILTER_ANISOTROPIC,
							.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
							.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
							.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
							.MipLODBias = 0,
							.MaxAnisotropy = 16,
							.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
							.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
							.MinLOD = 0.0f,
							.MaxLOD = D3D12_FLOAT32_MAX,
							.ShaderRegister = 0,
							.RegisterSpace = 0,
							.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
						};

						const D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{
							.Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
							.Desc_1_1{
								.NumParameters = _countof(params),
								.pParameters = params,
								.NumStaticSamplers = 1,
								.pStaticSamplers = &sampler,
								.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
							}
						};


						MComPtr<ID3DBlob> signature;
						MComPtr<ID3DBlob> error;

						// RootSignature description must be serialized before creating object.
						const HRESULT hrSerRootSign = D3D12SerializeVersionedRootSignature(&desc, &signature, &error);
						if (FAILED(hrSerRootSign))
						{
							std::string errorStr(static_cast<char*>(error->GetBufferPointer()), error->GetBufferSize());
							SA_LOG(L"Serialized Lit RootSignature failed!", Error, DX12, errorStr);

							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Serialized Lit RootSignature success.", Info, DX12, signature.Get());
						}


						const HRESULT hrCreateRootSign = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&litRootSign));
						if (FAILED(hrCreateRootSign))
						{
							SA_LOG(L"Create Lit RootSignature failed!", Error, DX12, (L"Error Code: %1", hrCreateRootSign));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit RootSignature success.", Info, DX12, litRootSign.Get());
						}
					}


					// Vertex Shader
					{
						MComPtr<ID3DBlob> errors;

						const HRESULT hrCompileShader = D3DCompileFromFile(L"Resources/Shaders/HLSL/LitShader.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", shaderCompileFlags, 0, &litVertexShader, &errors);

						if (FAILED(hrCompileShader))
						{
							std::string errorStr(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
							SA_LOG(L"Shader {LitShader.hlsl, mainVS} compilation failed!", Error, DX12, errorStr);

							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Shader {LitShader.hlsl, mainVS} compilation success.", Info, DX12, litVertexShader.Get());
						}
					}

					// Fragment Shader
					{
						MComPtr<ID3DBlob> errors;

						const HRESULT hrCompileShader = D3DCompileFromFile(L"Resources/Shaders/HLSL/LitShader.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", shaderCompileFlags, 0, &litPixelShader, &errors);

						if (FAILED(hrCompileShader))
						{
							std::string errorStr(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
							SA_LOG(L"Shader {LitShader.hlsl, mainPS} compilation failed.", Error, DX12, errorStr);

							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Shader {LitShader.hlsl, mainPS} compilation success.", Info, DX12, litPixelShader.Get());
						}
					}


					// PipelineState
					{
						const D3D12_RENDER_TARGET_BLEND_DESC rtBlend{
							.BlendEnable = false,
							.LogicOpEnable = false,

							.SrcBlend = D3D12_BLEND_ONE,
							.DestBlend = D3D12_BLEND_ZERO,
							.BlendOp = D3D12_BLEND_OP_ADD,

							.SrcBlendAlpha = D3D12_BLEND_ONE,
							.DestBlendAlpha = D3D12_BLEND_ZERO,
							.BlendOpAlpha = D3D12_BLEND_OP_ADD,

							.LogicOp = D3D12_LOGIC_OP_NOOP,

							.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
						};

						const D3D12_BLEND_DESC blendState{
							.AlphaToCoverageEnable = false,
							.IndependentBlendEnable = false,

							.RenderTarget
							{
								rtBlend,
								rtBlend,
								rtBlend,
								rtBlend,
								rtBlend,
								rtBlend,
								rtBlend,
								rtBlend
							}
						};

						const D3D12_RASTERIZER_DESC raster{
							.FillMode = D3D12_FILL_MODE_SOLID,
							.CullMode = D3D12_CULL_MODE_BACK,
							.FrontCounterClockwise = FALSE,
							.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
							.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
							.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
							.DepthClipEnable = TRUE,
							.MultisampleEnable = FALSE,
							.AntialiasedLineEnable = FALSE,
							.ForcedSampleCount = 0,
							.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
						};

						const D3D12_DEPTH_STENCIL_DESC depthStencilState{
							.DepthEnable = TRUE,
							.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
							.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
							.StencilEnable = FALSE,
							.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
							.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
							.FrontFace
							{
								.StencilFailOp = D3D12_STENCIL_OP_KEEP,
								.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
								.StencilPassOp = D3D12_STENCIL_OP_KEEP,
								.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS,
							},
							.BackFace
							{
								.StencilFailOp = D3D12_STENCIL_OP_KEEP,
								.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
								.StencilPassOp = D3D12_STENCIL_OP_KEEP,
								.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS,
							}
						};

						D3D12_INPUT_ELEMENT_DESC inputElems[]{
							{
								.SemanticName = "POSITION",
								.SemanticIndex = 0,
								.Format = DXGI_FORMAT_R32G32B32_FLOAT,
								.InputSlot = 0,
								.AlignedByteOffset = 0,
								.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
								.InstanceDataStepRate = 0
							},
							{
								.SemanticName = "NORMAL",
								.SemanticIndex = 0,
								.Format = DXGI_FORMAT_R32G32B32_FLOAT,
								.InputSlot = 1,
								.AlignedByteOffset = 0,
								.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
								.InstanceDataStepRate = 0
							},
							{
								.SemanticName = "TANGENT",
								.SemanticIndex = 0,
								.Format = DXGI_FORMAT_R32G32B32_FLOAT,
								.InputSlot = 2,
								.AlignedByteOffset = 0,
								.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
								.InstanceDataStepRate = 0
							},
							{
								.SemanticName = "TEXCOORD",
								.SemanticIndex = 0,
								.Format = DXGI_FORMAT_R32G32_FLOAT,
								.InputSlot = 3,
								.AlignedByteOffset = 0,
								.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
								.InstanceDataStepRate = 0
							}
						};
						const D3D12_INPUT_LAYOUT_DESC inputLayout{
							.pInputElementDescs = inputElems,
							.NumElements = _countof(inputElems)
						};


						const D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{
							.pRootSignature = litRootSign.Get(),

							.VS{
								.pShaderBytecode = litVertexShader->GetBufferPointer(),
								.BytecodeLength = litVertexShader->GetBufferSize()
							},
							.PS{
								.pShaderBytecode = litPixelShader->GetBufferPointer(),
								.BytecodeLength = litPixelShader->GetBufferSize()
							},

							.StreamOutput = {},
							.BlendState = blendState,
							.SampleMask = UINT_MAX,

							.RasterizerState = raster,
							.InputLayout = inputLayout,

							.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,

							.NumRenderTargets = 1,
							.RTVFormats{
								sceneColorFormat
							},
							.DSVFormat = sceneDepthFormat,

							.SampleDesc
							{
								.Count = 1,
								.Quality = 0,
							},

							.NodeMask = 0,

							.CachedPSO
							{
								.pCachedBlob = nullptr,
								.CachedBlobSizeInBytes = 0,
							},

							.Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
						};

						const HRESULT hrCreatePipeline = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&litPipelineState));
						if (FAILED(hrCreatePipeline))
						{
							SA_LOG(L"Create Lit PipelineState failed!", Error, DX12, (L"Error Code: %1", hrCreatePipeline));
							return EXIT_FAILURE;
						}
						else
						{
							SA_LOG(L"Create Lit PipelineState success.", Error, DX12, litPipelineState.Get());
						}
					}
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
			// Pipeline
			{
				// Lit
				{
					// PipelineState
					{
						SA_LOG(L"Destroying Lit PipelineState...", Info, DX12, litPipelineState.Get());
						litPipelineState = nullptr;
					}

					// Pixel Shader
					{
						SA_LOG(L"Destroying Lit Vertex Shader...", Info, DX12, litPixelShader.Get());
						litPixelShader = nullptr;
					}

					// Vertex Shader
					{
						SA_LOG(L"Destroying Lit Vertex Shader...", Info, DX12, litVertexShader.Get());
						litVertexShader = nullptr;
					}

					// RootSignature
					{
						SA_LOG(L"Destroying Lit RootSignature...", Info, DX12, litRootSign.Get());
						litRootSign = nullptr;
					}
				}
			}


			// Scene Resources
			{
				SA_LOG(L"Destroying Scene Color RT ViewHeap...", Info, DX12, sceneRTViewHeap.Get());
				sceneRTViewHeap = nullptr;

				SA_LOG(L"Destroying Scene Depth RT ViewHeap...", Info, DX12, sceneDepthRTViewHeap.Get());
				sceneDepthRTViewHeap = nullptr;

				SA_LOG(L"Destroying Scene Depth Texture...", Info, DX12, sceneDepthTexture.Get());
				sceneDepthTexture = nullptr;
			}


			// Commands
			{
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					SA_LOG((L"Destroying Command List [%1]...", i), Info, DX12, cmdLists[i].Get());
					cmdLists[i] = nullptr;
				}

				SA_LOG(L"Destroying Command Allocator...", Info, DX12, cmdAlloc.Get());
				cmdAlloc = nullptr;
			}


			// Swapchain
			{
				// Synchronization
				{
					CloseHandle(swapchainFenceEvent);
					SA_LOG(L"Destroy Swapchain Fence Event success", Info, DX12, swapchainFenceEvent);
					swapchainFenceEvent = nullptr;
					
					SA_LOG(L"Destroying Swapchain Fence...", Info, DX12, swapchainFence.Get());
					swapchainFence = nullptr;
				}

				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					SA_LOG((L"Destroying Swapchain image [%1]...", i), Info, DX12, swapchainImages[i].Get());
					swapchainImages[i] = nullptr;
				}

				SA_LOG(L"Destroying Swapchain...", Info, DX12, swapchain.Get());
				swapchain = nullptr;
			}


			// Device
			{
				// Synchronization
				{
					CloseHandle(deviceFenceEvent);
					SA_LOG(L"Destroy Device Fence Event success", Info, DX12, deviceFenceEvent);
					deviceFenceEvent = nullptr;

					SA_LOG(L"Destroying Device Fence...", Info, DX12, deviceFence.Get());
					deviceFence = nullptr;
				}

				// Queue
				{
					// GFX
					{
						SA_LOG(L"Destroying Graphics Queue...", Info, DX12, graphicsQueue.Get());
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

				SA_LOG(L"Destroying Device...", Info, DX12, device.Get());
				device = nullptr;
			}


			// Factory
			{
				SA_LOG(L"Destroying Factory...", Info, DX12, factory.Get());
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
