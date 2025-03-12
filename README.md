# FromVulkanToDirectX12

Learn **DirectX12** graphics API from **Vulkan**.\
This repository implements a basic **Forward PBR renderer** in both Vulkan and DirectX12 to **compare** the two API and be able to quickly **make links** their implementation.
 - mainDX.cpp: DirectX12 implementation
 - mainVK.cpp: Vulkan implementation

## Required components

### CMake
The **CMake build-tools** are used to allow using any compiler and IDE.\
Install the **CMake build-tools** [here](https://cmake.org/download/).

The **CMake integration tools** for your **IDE** are also required to be able to directly open the root directory and work from there.\
\
Ex: Install Visual Studio's **CMake integration tools** from the Visual Studio Installer:\
![VS_installCMake](Doc/Pictures/VS_install_CMakeIntegration.png)

### Vulkan SDK
In order to link and compile the Vulkan example, the Vulkan SDK is required.\
/!\ When installing the Vulkan SDK, be sure to select the **Shader Toolchain Debug Symbols** to be able to link the GLSL shader compiler library (_libshaderc_combined_d.lib_) in debug:
![VkSDK_install](Doc/Pictures/VulkanSDKInstall.png)

### DirectX12
Windows already pack all the required DirectX12 library, no SDK installation is required.


## Initialization

Start by **cloning** the **repository**.
```
git clone https://github.com/mrouffet/FromVulkanToDirectX12.git
cd FromVulkanToDirectX12
```

Then init the **submodules** to fetch the **dependencies** using the command:
```
git submodule update --init --recursive
```

Now the project is ready to be open directly from the **root directory** using your IDE in "_CMake mode_".
![VS_openDir](Doc/Pictures/win_VSOpenDir.jpg)


## Authors

**Maxime "mrouffet" ROUFFET** - main developer (maximerouffet@gmail.com)