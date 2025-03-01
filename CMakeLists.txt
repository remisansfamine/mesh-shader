cmake_minimum_required(VERSION 3.16)

project(FromVulkanToDirectX12)


set(MY_BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/Build/bin/${CMAKE_CXX_COMPILER_ID}_$<CONFIG>")

# Log directory at configuration-time
message("[${CMAKE_PROJECT_NAME}]: Output directory: ${MY_BIN_OUTPUT_DIR}")

# Set output directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${MY_BIN_OUTPUT_DIR}/libs")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${MY_BIN_OUTPUT_DIR}/libs")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${MY_BIN_OUTPUT_DIR}")



# Target mainDX12
add_executable(FVTDX12_mainDX12 "Sources/mainDX12.cpp")

target_compile_features(FVTDX12_mainDX12 PRIVATE c_std_11 cxx_std_20)
target_compile_options(FVTDX12_mainDX12 PRIVATE /W4 /WX)

target_link_libraries(FVTDX12_mainDX12 PUBLIC d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib)
target_link_libraries(FVTDX12_mainDX12 PUBLIC glfw assimp stb SA_Logger SA_Maths)

add_custom_command(
    TARGET FVTDX12_mainDX12
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:FVTDX12_mainDX12>/Resources
)



# Target mainVK
add_executable(FVTDX12_mainVK "Sources/mainVK.cpp")

target_compile_features(FVTDX12_mainVK PRIVATE c_std_11 cxx_std_20)
target_compile_options(FVTDX12_mainVK PRIVATE /W4 /WX)

find_package(Vulkan REQUIRED)
target_link_libraries(FVTDX12_mainVK PUBLIC Vulkan::Vulkan)
target_link_libraries(FVTDX12_mainVK PUBLIC glfw assimp stb SA_Logger SA_Maths)

add_custom_command(
    TARGET FVTDX12_mainVK
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:FVTDX12_mainVK>/Resources
)



# ThirdParty
add_subdirectory(ThirdParty/glfw)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Disable assimp built as shared libraries")
set(ASSIMP_INSTALL OFF CACHE BOOL "Disable assimp install")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable assimp build tests")
add_subdirectory(ThirdParty/assimp)

add_subdirectory(ThirdParty/stb)


## SA libraries
add_subdirectory(ThirdParty/SA/Logger)
add_subdirectory(ThirdParty/SA/Maths)
