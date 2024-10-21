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

// Windowing
#include <GLFW/glfw3.h>
GLFWwindow* window = nullptr;
constexpr SA::Vec2ui windowSize = { 1200, 900 };

void GLFWErrorCallback(int32_t error, const char* description)
{
	SA_LOG((L"GLFW Error [%1]: %2", error, description), Error, GLFW.API);
}

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
* _device->CreateGraphicsPipelineState(&createInfo, myPipeline);
* ...
* _cmdList->SetPipelineState(myPipeline)
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
* VkInstance -> DX12 Factory
*/
#include <dxgi1_6.h>
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

			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}

		// Renderer
		{
			/**
			* Factory:
			* VkInstance -> DX Factory
			*/
			{
				UINT dxgiFactoryFlags = 0;

#if SA_DEBUG
				// Validation Layers
				{
					MComPtr<ID3D12Debug1> debugController = nullptr;

					if (D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) == S_OK)
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

				CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

				if (!factory)
				{
					SA_LOG(L"Create Factory failed!", Error, DX12);
					return 1;
				}
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

			SA_LOG_END_OF_FRAME();
		}
	}


	// Uninitialization
	{
		// Renderer
		{
			// Factory
			{
				factory = nullptr;

#if SA_DEBUG
				// Report live objects
				MComPtr<IDXGIDebug1> dxgiDebug = nullptr;

				if (DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)) == S_OK)
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
