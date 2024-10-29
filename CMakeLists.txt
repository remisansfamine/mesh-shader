cmake_minimum_required(VERSION 3.16)

project(FromVulkanToDirectX12)


# Set output directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Build/bin/${CMAKE_CXX_COMPILER_ID}_$<CONFIG>/libs")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Build/bin/${CMAKE_CXX_COMPILER_ID}_$<CONFIG>/libs")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Build/bin/${CMAKE_CXX_COMPILER_ID}_$<CONFIG>")


# Create executable target.
file(GLOB_RECURSE SOURCES_PRIVATE "Sources/*.hpp" "Sources/*.cpp")
add_executable(FromVulkanToDirectX12 ${SOURCES_PRIVATE})

target_compile_features(FromVulkanToDirectX12 PUBLIC c_std_11)
target_compile_features(FromVulkanToDirectX12 PUBLIC cxx_std_20)


# Link libraries
target_link_libraries(FromVulkanToDirectX12 PUBLIC d3d12.lib dxgi.lib dxguid.lib)
target_link_libraries(FromVulkanToDirectX12 PUBLIC glfw SA_Logger SA_Maths)


# ThirdParty
add_subdirectory(ThirdParty/glfw)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Disable assimp built as shared libraries")
set(ASSIMP_INSTALL OFF CACHE BOOL "Disable assimp install")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable assimp build tests")
add_subdirectory(ThirdParty/assimp)


## SA libraries
add_subdirectory(ThirdParty/SA/Logger)
add_subdirectory(ThirdParty/SA/Maths)
