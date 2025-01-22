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
*/
#include <dxgi1_6.h>
// VkInstance -> IDXGIFactory
MComPtr<IDXGIFactory6> factory;


/**
* DX12 additionnal Debug tools:
* Report live objects after uninitialization to track leak and understroyed objects.
*/
#include <DXGIDebug.h>


/**
* Base DirectX12 header.
* vulkan.h -> d3d12.h
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


// VkBuffer -> ID3D12Resource
std::array<MComPtr<ID3D12Resource>, 4> sphereVertexBuffers;
/**
* Vulkan binds the buffer directly
* DirectX12 create 'views' (aka. how to read the memory) of buffers and use them for binding.
*/
std::array<D3D12_VERTEX_BUFFER_VIEW, 4> sphereVertexBufferViews;
MComPtr<ID3D12Resource> sphereIndexBuffer;
D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView;

// PBR textures.
MComPtr<ID3D12Resource> rustedIron2AlbedoTexture;
MComPtr<ID3D12Resource> rustedIron2NormalTexture;
MComPtr<ID3D12Resource> rustedIron2MetallicTexture;
MComPtr<ID3D12Resource> rustedIron2RoughnessTexture;


// -------------------- Helper Functions --------------------
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
	D3D12_RANGE range{ .Begin = 0, .End = 0 };
	void* data = nullptr;

	// vkMapMemory -> buffer->Map
	stagingBuffer->Map(0, &range, reinterpret_cast<void**>(&data));

	std::memcpy(data, _data, _size);

	// vkUnmapMemory -> buffer->Unmap
	stagingBuffer->Unmap(0, nullptr);


	// Copy GPU temp staging buffer to final GPU-only buffer.
	cmdLists[0]->CopyBufferRegion(_gpuBuffer.Get(), 0, stagingBuffer.Get(), 0, _size);


	// Resource transition to final state.
	D3D12_RESOURCE_BARRIER barrier{
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
						D3D12_COMMAND_QUEUE_DESC desc{
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
			}


			// Swapchain
			{
				DXGI_SWAP_CHAIN_DESC1 desc{
					.Width = windowSize.x,
					.Height = windowSize.y,
					.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
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


			// Resources
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
								.Format = DXGI_FORMAT_R16_UINT,
							};


							std::vector<uint16_t> indices;
							indices.resize(inMesh->mNumFaces * 3);
							sphereIndexCount = inMesh->mNumFaces * 3;

							for (int i = 0; i < inMesh->mNumFaces; ++i)
							{
								indices[i * 3] = inMesh->mFaces[i].mIndices[0];
								indices[i * 3 + 1] = inMesh->mFaces[i].mIndices[1];
								indices[i * 3 + 2] = inMesh->mFaces[i].mIndices[2];
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
					const UINT64 prevFenceValue = swapchainFenceValues[swapchainFrameIndex];

					// Update frame index.
					swapchainFrameIndex = swapchain->GetCurrentBackBufferIndex();

					const UINT64 currFenceValue = swapchainFenceValues[swapchainFrameIndex];


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


			// Commands
			{
				cmdLists.fill(nullptr);
				cmdAllocs.fill(nullptr);
			}


			// Swapchain
			{
				CloseHandle(swapchainFenceEvent);
				swapchainFence = nullptr;
				swapchain = nullptr;
			}


			// Device
			{
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
