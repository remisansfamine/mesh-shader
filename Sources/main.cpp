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


// Resource Loading
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


// Windowing
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


// Renderer
constexpr uint32_t bufferingCount = 3;

/**
* Vulkan is a C library: objects are passed as function arguments.
* pseudo-code:
* VkPipeline myPipeline;
* vkCreateGraphicsPipelines(device, ..., &createInfo, &myPipeline);
* ...
* vkCmdBindPipeline(cmd, ..., myPipeline)
* ..
* vkDestroyPipeline(_device, myPipeline, nullptr)
* 
* DirectX12 is a C++ object oriented API: functions are called directly from the object.
* pseudo-code:
* ID3D12PipelineState* myPipeline;
* device->CreateGraphicsPipelineState(&createInfo, myPipeline);
* ...
* cmdList->SetPipelineState(myPipeline)
* ...
* myPipeline = nullptr; // WARNING: Memory leak.
* 
* In order to avoid memory management, DirectX12 uses Microsoft's ComPtr (SmartPtr).
* ComPtr will automatically decrement the object counter and call the interface destroy function on last reference destroy.
* There is no device->Destroy<object>() functions.
*/
#include <wrl.h>
template <typename T>
using MComPtr = Microsoft::WRL::ComPtr<T>;


/**
* Header specific to create a Factory (required to create a Device).
* Requires `dxgi.lib`
*/
#include <dxgi1_6.h>
// VkInstance -> IDXGIFactory
MComPtr<IDXGIFactory6> factory;


/**
* DX12 additionnal Debug tools:
* Report live objects after uninitialization to track leak and understroyed objects.
* Requires `dxguid.lib`
*/
#include <DXGIDebug.h>


/**
* Base DirectX12 header.
* vulkan.h -> d3d12.h
* Requires `d3d12.lib`
*/
#include <d3d12.h>
// VkDevice -> ID3D12Device
MComPtr<ID3D12Device> device;

// Validation Layers
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

// VkQueue -> ID3D12CommandQueue
MComPtr<ID3D12CommandQueue> graphicsQueue;

/**
* Vulkan has native WaitForGPU method (vkDeviceWaitIdle).
* 
* DirectX12 must implement it manually using fences.
* For more details on Fence/Event implementation, check Swapchain synchronization implementation below.
*/
HANDLE deviceFenceEvent;
MComPtr<ID3D12Fence> deviceFence;
uint32_t deviceFenceValue = 0u;


// VkSwapchainKHR -> IDXGISwapChain
MComPtr<IDXGISwapChain3> swapchain;
// VkImage -> ID3D12Resource
std::array<MComPtr<ID3D12Resource>, bufferingCount> swapchainImages;

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
HANDLE swapchainFenceEvent;
MComPtr<ID3D12Fence> swapchainFence;
std::array<uint32_t, bufferingCount> swapchainFenceValues;


/**
* VkCommandPool -> ID3D12CommandAllocator
* Like for Vulkan, it is better to create 1 command allocator (pool) per frame.
*/
std::array<MComPtr<ID3D12CommandAllocator>, bufferingCount> cmdAllocs;
/**
* VkCommandBuffer -> ID3D12CommandList
* /!\ with DirectX12, for graphics operations, the 'CommandList' type is not enough. ID3D12GraphicsCommandList must be used.
* Like for Vulkan, we allocate 1 command buffer per frame (using the current frame command allocator (pool)).
*/
std::array<MComPtr<ID3D12GraphicsCommandList1>, bufferingCount> cmdLists;

// Color Scene Texture
constexpr DXGI_FORMAT sceneColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr float sceneClearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
// Use Swapchain backbuffer texture as color output.
MComPtr<ID3D12DescriptorHeap> sceneRTViewHeap;


// Depth Scene Texture
constexpr DXGI_FORMAT sceneDepthFormat = DXGI_FORMAT_D16_UNORM;
constexpr D3D12_CLEAR_VALUE depthClearValue{ .Format = sceneDepthFormat, .DepthStencil = { 1.0f, 0 } };
MComPtr<ID3D12Resource> sceneDepthTexture;
MComPtr<ID3D12DescriptorHeap> sceneDepthRTViewHeap;


/**
* VkViewport -> D3D12_VIEWPORT
* Vulkan must specify viewport on pipeline creation by default (or use dynamic viewport).
* DirectX12 only uses dynamic viewport (set by commandlist).
*/
D3D12_VIEWPORT viewport;

/**
* VkRect2D -> D3D12_RECT
* Vulkan must specify scissor rectangle on pipeline creation by default (or use dynamic scissor).
* DirectX12 only uses dynamic scissor rectangle (set by commandlist).
*/
D3D12_RECT scissorRect;

/**
* Basic helper shader compiler header.
* For more advanced features use DirectXShaderCompiler library.
* Requires `d3dcompiler.lib`
*/
#include <d3dcompiler.h>

/**
* VkShaderModule -> ID3DBlob
*/
MComPtr<ID3DBlob> litVertexShader;
MComPtr<ID3DBlob> litPixelShader;

/**
* VkPipelineLayout -> ID3D12RootSignature
*/
MComPtr<ID3D12RootSignature> litRootSign;

/**
* VkPipeline -> ID3D12PipelineState
*/
MComPtr<ID3D12PipelineState> litPipelineState;


// VkBuffer -> ID3D12Resource
std::array<MComPtr<ID3D12Resource>, 4> sphereVertexBuffers;
/**
* Vulkan binds the buffer directly
* DirectX12 create 'views' (aka. how to read the memory) of buffers and use them for binding.
*/
std::array<D3D12_VERTEX_BUFFER_VIEW, 4> sphereVertexBufferViews;
uint32_t sphereIndexCount = 0u;
MComPtr<ID3D12Resource> sphereIndexBuffer;
D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView;

// PBR textures.
MComPtr<ID3D12Resource> rustedIron2AlbedoTexture;
MComPtr<ID3D12Resource> rustedIron2NormalTexture;
MComPtr<ID3D12Resource> rustedIron2MetallicTexture;
MComPtr<ID3D12Resource> rustedIron2RoughnessTexture;


// -------------------- Helper Functions --------------------
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


bool SubmitBufferToGPU(MComPtr<ID3D12Resource> _gpuBuffer, uint64_t _size, const void* _data, D3D12_RESOURCE_STATES _stateAfter)
{
	// Create temp upload buffer.
	MComPtr<ID3D12Resource> stagingBuffer;

	const D3D12_HEAP_PROPERTIES heap{
		.Type = D3D12_HEAP_TYPE_UPLOAD,
	};

	const D3D12_RESOURCE_DESC desc{
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Alignment = 0,
		.Width = _size,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc = {.Count = 1, .Quality = 0 },
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};

	const HRESULT hrStagBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&stagingBuffer));
	if (FAILED(hrStagBufferCreated))
	{
		SA_LOG(L"Create Staging Buffer failed!", Error, DX12);
		return false;
	}


	// Memory mapping and Upload (CPU to GPU transfer).
	const D3D12_RANGE range{ .Begin = 0, .End = 0 };
	void* data = nullptr;

	// vkMapMemory -> buffer->Map
	stagingBuffer->Map(0, &range, reinterpret_cast<void**>(&data));

	std::memcpy(data, _data, _size);

	// vkUnmapMemory -> buffer->Unmap
	stagingBuffer->Unmap(0, nullptr);


	// Copy GPU temp staging buffer to final GPU-only buffer.
	cmdLists[0]->CopyBufferRegion(_gpuBuffer.Get(), 0, stagingBuffer.Get(), 0, _size);


	// Resource transition to final state.
	const D3D12_RESOURCE_BARRIER barrier{
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
		.Transition = {
			.pResource = _gpuBuffer.Get(),
			.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
			.StateAfter = _stateAfter,
		},
	};

	cmdLists[0]->ResourceBarrier(1, &barrier);

	cmdLists[0]->Close();

	/**
	* Instant command submit execution (easy implementation)
	* Better code would parallelize resources loading in staging buffer and submit only once at the end to execute all GPU copies.
	*/

	ID3D12CommandList* cmdListsArr[] = { cmdLists[0].Get() };
	graphicsQueue->ExecuteCommandLists(1, cmdListsArr);

	WaitDeviceIdle();

	cmdAllocs[0]->Reset();
	cmdLists[0]->Reset(cmdAllocs[0].Get(), nullptr);

	return true;
}

bool SubmitTextureToGPU(MComPtr<ID3D12Resource> _gpuTexture, int _width, int _height, int _channelNum, DXGI_FORMAT _format, const void* _data, D3D12_RESOURCE_STATES _stateAfter)
{
	const UINT64 size = static_cast<UINT64>(_width * _height * _channelNum);

	// Create temp upload buffer.
	MComPtr<ID3D12Resource> stagingBuffer;

	const D3D12_HEAP_PROPERTIES heap{
		.Type = D3D12_HEAP_TYPE_UPLOAD,
	};

	const D3D12_RESOURCE_DESC desc{
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Alignment = 0,
		.Width = size,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc = {.Count = 1, .Quality = 0 },
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};

	const HRESULT hrStagBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&stagingBuffer));
	if (FAILED(hrStagBufferCreated))
	{
		SA_LOG(L"Create Staging Buffer failed!", Error, DX12);
		return false;
	}


	// Memory mapping and Upload (CPU to GPU transfer).
	const D3D12_RANGE range{ .Begin = 0, .End = 0 };
	void* data = nullptr;

	stagingBuffer->Map(0, &range, reinterpret_cast<void**>(&data));
	std::memcpy(data, _data, size);
	stagingBuffer->Unmap(0, nullptr);


	// Copy Buffer to texture
	/**
	* Copy Buffer to Texture
	* Mipmaps are not handled here. For mipmapping:
	*   - Generate Mipmaps
	*   - Record one copy for each mips using src.Offset and dst.SubresourceIndex
	*/
	const D3D12_TEXTURE_COPY_LOCATION src{
		.pResource = stagingBuffer.Get(),
		.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
		.PlacedFootprint = D3D12_PLACED_SUBRESOURCE_FOOTPRINT{
			.Offset = 0, // currMipLevel
			.Footprint = D3D12_SUBRESOURCE_FOOTPRINT{
				.Format = _format,
				.Width = static_cast<UINT>(_width),
				.Height = static_cast<UINT>(_height),
				.Depth = 1,
				.RowPitch = static_cast<UINT>(_width * _channelNum),
			}
		}
	};

	const D3D12_TEXTURE_COPY_LOCATION dst{
		.pResource = _gpuTexture.Get(),
		.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		.SubresourceIndex = 0 // currMipLevel
	};

	cmdLists[0]->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);


	// Resource transition to final state.
	const D3D12_RESOURCE_BARRIER barrier{
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
		.Transition = {
			.pResource = _gpuTexture.Get(),
			.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
			.StateAfter = _stateAfter,
		},
	};

	cmdLists[0]->ResourceBarrier(1, &barrier);

	cmdLists[0]->Close();

	/**
	* Instant command submit execution (easy implementation)
	* Better code would parallelize resources loading in staging buffer and submit only once at the end to execute all GPU copies.
	*/

	ID3D12CommandList* cmdListsArr[] = { cmdLists[0].Get() };
	graphicsQueue->ExecuteCommandLists(1, cmdListsArr);

	WaitDeviceIdle();

	cmdAllocs[0]->Reset();
	cmdLists[0]->Reset(cmdAllocs[0].Get(), nullptr);

	return true;
}


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
			window = glfwCreateWindow(windowSize.x, windowSize.y, "From Vulkan to DirectX12", nullptr, nullptr);

			if (!window)
			{
				SA_LOG(L"GLFW create window failed!", Error, GLFW);
				return EXIT_FAILURE;
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
			}
			

			// Device
			{
				MComPtr<IDXGIAdapter3> physicalDevice;

				// Select first prefered GPU, listed by HIGH_PERFORMANCE. No need to manually sort GPU like Vulkan.
				const HRESULT hrQueryGPU = factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&physicalDevice));
				if (FAILED(hrQueryGPU))
				{
					SA_LOG(L"Physical Device not found!", Error, DX12);
					return EXIT_FAILURE;
				}

				const HRESULT hrDeviceCreated = D3D12CreateDevice(physicalDevice.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
				if (FAILED(hrDeviceCreated))
				{
					SA_LOG(L"Create Device failed!", Error, DX12);
					return EXIT_FAILURE;
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
					// This renderer example only use 1 graphics queue.

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

					const HRESULT hrDeviceFenceCreated = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&deviceFence));
					if (FAILED(hrDeviceFenceCreated))
					{
						SA_LOG(L"Create Device Fence failed!", Error, DX12);
						return EXIT_FAILURE;
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
				}
			}


			// Commands
			{
				for (uint32_t i = 0; i < bufferingCount; ++i)
				{
					const HRESULT hrCmdAllocCreated = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocs[i]));
					if (FAILED(hrCmdAllocCreated))
					{
						SA_LOG((L"Create Command Allocator [%1] failed!", i), Error, DX12);
						return EXIT_FAILURE;
					}

					const HRESULT hrCmdListCreated = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocs[i].Get(), nullptr, IID_PPV_ARGS(&cmdLists[i]));
					if (FAILED(hrCmdListCreated))
					{
						SA_LOG((L"Create Command List [%1] failed!", i), Error, DX12);
						return EXIT_FAILURE;
					}

					// Command list must be closed because we will start the frame by Reset()
					cmdLists[i]->Close();
				}
			}


			// Scene Resources
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
						SA_LOG(L"Create RenderTarget ViewHeap failed.", Error, DX12);
						return EXIT_FAILURE;
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

					const HRESULT hrCreateDepthTexture = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&sceneDepthTexture));
					if (FAILED(hrCreateDepthTexture))
					{
						SA_LOG(L"Create Scene Depth Texture failed.", Error, DX12);
						return EXIT_FAILURE;
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
						SA_LOG(L"Create Depth ViewHeap failed.", Error, DX12);
						return EXIT_FAILURE;
					}

					/**
					* Create Depth View to use sceneDepthTexture as a render target.
					*/
					device->CreateDepthStencilView(sceneDepthTexture.Get(), nullptr, sceneDepthRTViewHeap->GetCPUDescriptorHandleForHeapStart());
				}
			}


			// Pipeline
			{
#if SA_DEBUG
				// Enable better shader debugging with the graphics debugging tools.
				const UINT shaderCompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
				const UINT shaderCompileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

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

				// Lit
				{
					// RootSignature
					{
						/**
						* Root signature is the Pipeline Layout.
						* Describe the shader bindings.
						*/

						// Use Descriptor Table to bind all the textures at once.
						const D3D12_DESCRIPTOR_RANGE1 pbrTextureRange[]{
							// Albedo
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 1,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
							},
							// Normal
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 2,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
							},
							// Metallic
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 3,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
							},
							// Roughness
							{
								.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
								.NumDescriptors = 1,
								.BaseShaderRegister = 4,
								.RegisterSpace = 0,
								.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
							},
						};

						const D3D12_ROOT_PARAMETER1 params[]{
							// Object Constant buffer
							{
								.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
								.Descriptor = {
									.ShaderRegister = 0,
									.RegisterSpace = 0,
									.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
								},
								.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
							},
							// Camera Constant buffer
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
								.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
								.Descriptor {
									.ShaderRegister = 0,
									.RegisterSpace = 0,
									.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
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
							.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
							.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
							.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
							.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
							.MipLODBias = 0,
							.MaxAnisotropy = 0,
							.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
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
							SA_LOG(L"Serialized Lit RootSignature failed.", Error, DX12, errorStr);

							return EXIT_FAILURE;
						}


						const HRESULT hrCreateRootSign = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&litRootSign));
						if (FAILED(hrCreateRootSign))
						{
							SA_LOG(L"Create Lit RootSignature failed.", Error, DX12);
							return EXIT_FAILURE;
						}
					}


					// Vertex Shader
					{
						MComPtr<ID3DBlob> errors;

						const HRESULT hrCompileShader = D3DCompileFromFile(L"Resources/Shaders/LitShader.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", shaderCompileFlags, 0, &litVertexShader, &errors);

						if (FAILED(hrCompileShader))
						{
							std::string errorStr(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
							SA_LOG(L"Shader {LitShader.hlsl, mainVS} compilation failed.", Error, DX12, errorStr);

							return EXIT_FAILURE;
						}
					}

					// Fragment Shader
					{
						MComPtr<ID3DBlob> errors;

						const HRESULT hrCompileShader = D3DCompileFromFile(L"Resources/Shaders/LitShader.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", shaderCompileFlags, 0, &litPixelShader, &errors);

						if (FAILED(hrCompileShader))
						{
							std::string errorStr(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
							SA_LOG(L"Shader {LitShader.hlsl, mainPS} compilation failed.", Error, DX12, errorStr);

							return EXIT_FAILURE;
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
							SA_LOG(L"Create Lit PipelineState failed.", Error, DX12);
							return EXIT_FAILURE;
						}
					}
				}
			}


			// Loaded Resources
			{
				cmdLists[0]->Reset(cmdAllocs[0].Get(), nullptr);

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
							/**
							* VkMemoryPropertyFlagBits -> D3D12_HEAP_PROPERTIES.Type
							* Defines if a buffer is GPU only, CPU-GPU, ...
							* In Vulkan, a buffer can be GPU only or CPU-GPU for data transfer (read and write).
							* In DirectX12, a buffer is either GPU only, 'Upload' for data transfer from CPU to GPU, or 'Readback' for data transfer from GPU to CPU.
							* 'Upload' and 'Readback' at the same time is NOT possible.
							*/
							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT, // Type Default is GPU only.
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
								.Alignment = 0,
								.Width = sizeof(SA::Vec3f) * inMesh->mNumVertices,
								.Height = 1,
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_UNKNOWN,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sphereVertexBuffers[0]));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create Sphere Vertex Position Buffer failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							sphereVertexBufferViews[0] = D3D12_VERTEX_BUFFER_VIEW{
								.BufferLocation = sphereVertexBuffers[0]->GetGPUVirtualAddress(),
								.SizeInBytes = static_cast<UINT>(desc.Width),
								.StrideInBytes = sizeof(SA::Vec3f),
							};

							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[0], desc.Width, inMesh->mVertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex Position Buffer submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}
						}

						// Normal
						{
							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
								.Alignment = 0,
								.Width = sizeof(SA::Vec3f) * inMesh->mNumVertices,
								.Height = 1,
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_UNKNOWN,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sphereVertexBuffers[1]));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create Sphere Vertex Normal Buffer failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							sphereVertexBufferViews[1] = D3D12_VERTEX_BUFFER_VIEW{
								.BufferLocation = sphereVertexBuffers[1]->GetGPUVirtualAddress(),
								.SizeInBytes = static_cast<UINT>(desc.Width),
								.StrideInBytes = sizeof(SA::Vec3f),
							};

							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[1], desc.Width, inMesh->mNormals, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex Normal Buffer submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}
						}

						// Tangent
						{
							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
								.Alignment = 0,
								.Width = sizeof(SA::Vec3f) * inMesh->mNumVertices,
								.Height = 1,
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_UNKNOWN,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sphereVertexBuffers[2]));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create Sphere Vertex Tangent Buffer failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							sphereVertexBufferViews[2] = D3D12_VERTEX_BUFFER_VIEW{
								.BufferLocation = sphereVertexBuffers[2]->GetGPUVirtualAddress(),
								.SizeInBytes = static_cast<UINT>(desc.Width),
								.StrideInBytes = sizeof(SA::Vec3f),
							};

							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[2], desc.Width, inMesh->mTangents, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex Tangent Buffer submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}
						}

						// UV
						{
							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
								.Alignment = 0,
								.Width = sizeof(SA::Vec2f) * inMesh->mNumVertices,
								.Height = 1,
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_UNKNOWN,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sphereVertexBuffers[3]));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create Sphere Vertex UV Buffer failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							sphereVertexBufferViews[3] = D3D12_VERTEX_BUFFER_VIEW{
								.BufferLocation = sphereVertexBuffers[3]->GetGPUVirtualAddress(),
								.SizeInBytes = static_cast<UINT>(desc.Width),
								.StrideInBytes = sizeof(SA::Vec2f),
							};

							std::vector<SA::Vec2f> uvs;
							uvs.reserve(inMesh->mNumVertices);

							for (uint32_t i = 0; i < inMesh->mNumVertices; ++i)
							{
								uvs.push_back(SA::Vec2f{ inMesh->mTextureCoords[0][i].x, inMesh->mTextureCoords[0][i].y });
							}

							const bool bSubmitSuccess = SubmitBufferToGPU(sphereVertexBuffers[3], desc.Width, uvs.data(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Vertex UV Buffer submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}
						}

						// Index
						{
							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
								.Alignment = 0,
								.Width = sizeof(uint16_t) * inMesh->mNumFaces * 3,
								.Height = 1,
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_UNKNOWN,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sphereIndexBuffer));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create Sphere Index Buffer failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							sphereIndexBufferView = D3D12_INDEX_BUFFER_VIEW{
								.BufferLocation = sphereIndexBuffer->GetGPUVirtualAddress(),
								.SizeInBytes = static_cast<UINT>(desc.Width),
								.Format = DXGI_FORMAT_R16_UINT, // This model's indices are lower than 65535.
							};


							std::vector<uint16_t> indices;
							indices.resize(inMesh->mNumFaces * 3);
							sphereIndexCount = inMesh->mNumFaces * 3;

							for (unsigned int i = 0; i < inMesh->mNumFaces; ++i)
							{
								indices[i * 3] = static_cast<uint16_t>(inMesh->mFaces[i].mIndices[0]);
								indices[i * 3 + 1] = static_cast<uint16_t>(inMesh->mFaces[i].mIndices[1]);
								indices[i * 3 + 2] = static_cast<uint16_t>(inMesh->mFaces[i].mIndices[2]);
							}

							const bool bSubmitSuccess = SubmitBufferToGPU(sphereIndexBuffer, desc.Width, indices.data(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"Sphere Index Buffer submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}
						}
					}
				}


				// Textures
				{
					stbi_set_flip_vertically_on_load(true);

					// RustedIron2 PBR
					{
						// Albedo
						{
							const char* path = "Resources/Textures/RustedIron2/rustediron2_basecolor.png";

							int width, height, channels;
							uint8_t* inData = stbi_load(path, &width, &height, &channels, 4);
							if (!inData)
							{
								SA_LOG(L"STBI Texture Loading failed", Error, STB, path);
								return EXIT_FAILURE;
							}

							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
								.Alignment = 0,
								.Width = static_cast<uint32_t>(width),
								.Height = static_cast<uint32_t>(height),
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rustedIron2AlbedoTexture));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create RustedIron2 Albedo Texture failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2AlbedoTexture, width, height, channels, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, inData, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"RustedIron2 Albedo Texture submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							stbi_image_free(inData);
						}

						// Normal Map
						{
							const char* path = "Resources/Textures/RustedIron2/rustediron2_normal.png";

							int width, height, channels;
							uint8_t* inData = stbi_load(path, &width, &height, &channels, 4);
							if (!inData)
							{
								SA_LOG(L"STBI Texture Loading failed", Error, STB, path);
								return EXIT_FAILURE;
							}

							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
								.Alignment = 0,
								.Width = static_cast<uint32_t>(width),
								.Height = static_cast<uint32_t>(height),
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rustedIron2NormalTexture));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create RustedIron2 Normal Texture failed!", Error, DX12);
								return 1;
							}

							const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2NormalTexture, width, height, 4, DXGI_FORMAT_R8G8B8A8_UNORM, inData, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"RustedIron2 Normal Texture submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							stbi_image_free(inData);
						}

						// Metallic
						{
							const char* path = "Resources/Textures/RustedIron2/rustediron2_metallic.png";

							int width, height, channels;
							uint8_t* inData = stbi_load(path, &width, &height, &channels, 1);
							if (!inData)
							{
								SA_LOG(L"STBI Texture Loading failed", Error, STB, path);
								return EXIT_FAILURE;
							}

							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
								.Alignment = 0,
								.Width = static_cast<uint32_t>(width),
								.Height = static_cast<uint32_t>(height),
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_R8_UNORM,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rustedIron2MetallicTexture));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create RustedIron2 Metallic Texture failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2MetallicTexture, width, height, channels, DXGI_FORMAT_R8_UNORM, inData, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"RustedIron2 Metallic Texture submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							stbi_image_free(inData);
						}

						// Roughness
						{
							const char* path = "Resources/Textures/RustedIron2/rustediron2_roughness.png";

							int width, height, channels;
							uint8_t* inData = stbi_load(path, &width, &height, &channels, 1);
							if (!inData)
							{
								SA_LOG(L"STBI Texture Loading failed", Error, STB, path);
								return EXIT_FAILURE;
							}

							const D3D12_HEAP_PROPERTIES heap{
								.Type = D3D12_HEAP_TYPE_DEFAULT,
							};

							const D3D12_RESOURCE_DESC desc{
								.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
								.Alignment = 0,
								.Width = static_cast<uint32_t>(width),
								.Height = static_cast<uint32_t>(height),
								.DepthOrArraySize = 1,
								.MipLevels = 1,
								.Format = DXGI_FORMAT_R8_UNORM,
								.SampleDesc = {.Count = 1, .Quality = 0 },
								.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
								.Flags = D3D12_RESOURCE_FLAG_NONE,
							};

							const HRESULT hrBufferCreated = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rustedIron2RoughnessTexture));
							if (FAILED(hrBufferCreated))
							{
								SA_LOG(L"Create RustedIron2 Roughness Texture failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							const bool bSubmitSuccess = SubmitTextureToGPU(rustedIron2RoughnessTexture, width, height, channels, DXGI_FORMAT_R8_UNORM, inData, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
							if (!bSubmitSuccess)
							{
								SA_LOG(L"RustedIron2 Roughness Texture submit failed!", Error, DX12);
								return EXIT_FAILURE;
							}

							stbi_image_free(inData);
						}
					}
				}

				cmdLists[0]->Close();
			}
		}
	}


	// Loop
	{
		while (!glfwWindowShouldClose(window))
		{
			// Inputs
			{
				glfwPollEvents();

				if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
					glfwSetWindowShouldClose(window, true);
			}

			// Render
			{
				// Swapchain Begin
				{
					const UINT32 prevFenceValue = swapchainFenceValues[swapchainFrameIndex];

					// Update frame index.
					swapchainFrameIndex = swapchain->GetCurrentBackBufferIndex();

					const UINT32 currFenceValue = swapchainFenceValues[swapchainFrameIndex];


					// If the next frame is not ready to be rendered yet, wait until it is ready.
					if (swapchainFence->GetCompletedValue() < currFenceValue)
					{
						const HRESULT hrSetEvent = swapchainFence->SetEventOnCompletion(currFenceValue, swapchainFenceEvent);
						if (FAILED(hrSetEvent))
						{
							SA_LOG(L"Fence SetEventOnCompletion failed.", Error, DX12);
							return EXIT_FAILURE;
						}

						WaitForSingleObjectEx(swapchainFenceEvent, INFINITE, FALSE);
					}

					// Set the fence value for the next frame.
					swapchainFenceValues[swapchainFrameIndex] = prevFenceValue + 1;
				}


				// Register Commands
				{
					auto cmdAlloc = cmdAllocs[swapchainFrameIndex];
					auto cmd = cmdLists[swapchainFrameIndex];

					cmdAlloc->Reset();
					cmd->Reset(cmdAlloc.Get(), nullptr);

					auto sceneColorRT = swapchainImages[swapchainFrameIndex];

					/**
					* Manage Render Targets for render:
					* Vulkan uses vkRenderPass and vkFramebuffers to describe in advance how the render targets should be managed through passes and subpasses.
					* DirectX12 doesn't have such system and must manage RenderTargets manually.
					*/
					{
						// Color Transition to RenderTarget.
						const D3D12_RESOURCE_BARRIER barrier{
							.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
							.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
							.Transition = {
								.pResource = sceneColorRT.Get(),
								.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
								.StateBefore = D3D12_RESOURCE_STATE_COMMON,
								.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
							},
						};

						cmd->ResourceBarrier(1, &barrier);


						// Clear
						{
							// Access current frame allocated view.
							D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = sceneRTViewHeap->GetCPUDescriptorHandleForHeapStart();
							const UINT rtvOffset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
							rtvHandle.ptr += rtvOffset * swapchainFrameIndex;
							
							const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = sceneDepthRTViewHeap->GetCPUDescriptorHandleForHeapStart();

							cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
							cmd->ClearRenderTargetView(rtvHandle, sceneClearColor, 0, nullptr);
							cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, depthClearValue.DepthStencil.Depth, depthClearValue.DepthStencil.Stencil, 0, nullptr);
						}
					}


					// Pipeline commons
					cmd->RSSetViewports(1, &viewport);
					cmd->RSSetScissorRects(1, &scissorRect);





					// Manage RenderTargets for present.
					{
						// Color Transition to Present.
						const D3D12_RESOURCE_BARRIER barrier{
							.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
							.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
							.Transition = {
								.pResource = sceneColorRT.Get(),
								.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
								.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
								.StateAfter = D3D12_RESOURCE_STATE_PRESENT,
							},
						};

						cmd->ResourceBarrier(1, &barrier);
					}


					cmd->Close();

					// Execute the command list.
					ID3D12CommandList* ppCommandLists[] = { cmd.Get() };
					graphicsQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
				}


				// Swapchain End
				{
					// Automatically present using internal present Queue if possible.
					const HRESULT hrPresent = swapchain->Present(1, 0);
					if (FAILED(hrPresent))
					{
						SA_LOG(L"Swapchain Present failed", Error, DX12);
						return EXIT_FAILURE;
					}

					// Schedule a Signal command in the queue.
					const UINT64 currFenceValue = swapchainFenceValues[swapchainFrameIndex];
					const HRESULT hrFenceSignal = graphicsQueue->Signal(swapchainFence.Get(), currFenceValue);
					if (FAILED(hrFenceSignal))
					{
						SA_LOG(L"Swapchain Fence Signal failed", Error, DX12);
						return EXIT_FAILURE;
					}
				}
			}

			SA_LOG_END_OF_FRAME();
		}
	}


	// Uninitialization
	{
		// Renderer
		{
			WaitDeviceIdle();

			// Resources
			{
				// Meshes
				{
					// Sphere
					{
						/**
						* Buffer Views do NOT need to be destroyed.
						* Views are not resources, they are just descriptors about how to read a resource.
						*/

						sphereVertexBuffers.fill(nullptr);
						sphereIndexBuffer = nullptr;
					}
				}

				// Textures
				{
					// RustedIron2
					{
						rustedIron2AlbedoTexture = nullptr;
						rustedIron2NormalTexture = nullptr;
						rustedIron2MetallicTexture = nullptr;
						rustedIron2RoughnessTexture = nullptr;
					}
				}
			}

			
			// Pipeline
			{
				// Lit
				{
					litPipelineState = nullptr;
					litVertexShader = nullptr;
					litPixelShader = nullptr;
					litRootSign = nullptr;
				}
			}


			// Scene Resources
			{
				sceneRTViewHeap = nullptr;
				sceneDepthRTViewHeap = nullptr;
				sceneDepthTexture = nullptr;
			}

			// Commands
			{
				cmdLists.fill(nullptr);
				cmdAllocs.fill(nullptr);
			}


			// Swapchain
			{
				// Synchronization
				{
					CloseHandle(swapchainFenceEvent);
					swapchainFence = nullptr;
				}

				swapchainImages.fill(nullptr);
				swapchain = nullptr;
			}


			// Device
			{
				// Synchronization
				{
					CloseHandle(deviceFenceEvent);
					deviceFence = nullptr;
				}

				// Queue
				{
					// GFX
					{
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

				device = nullptr;
			}


			// Factory
			{
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
			glfwTerminate();
		}
	}

	return 0;
}
