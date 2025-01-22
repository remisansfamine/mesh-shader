cmake_minimum_required(VERSION 3.16)

project(FromVulkanToDirectX12)


set(MY_BIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/Build/bin/${CMAKE_CXX_COMPILER_ID}_$<CONFIG>")

# Log directory at configuration-time
message("[${CMAKE_PROJECT_NAME}]: Output directory: ${MY_BIN_OUTPUT_DIR}")

# Set output directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${MY_BIN_OUTPUT_DIR}/libs")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${MY_BIN_OUTPUT_DIR}/libs")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${MY_BIN_OUTPUT_DIR}")


# Create executable target.
file(GLOB_RECURSE SOURCES_PRIVATE "Sources/*.hpp" "Sources/*.cpp")
add_executable(FromVulkanToDirectX12 ${SOURCES_PRIVATE})

target_compile_features(FromVulkanToDirectX12 PUBLIC c_std_11)
target_compile_features(FromVulkanToDirectX12 PUBLIC cxx_std_20)
target_compile_options(FromVulkanToDirectX12 PRIVATE /W4 /WX)


# Link libraries
target_link_libraries(FromVulkanToDirectX12 PUBLIC d3d12.lib dxgi.lib dxguid.lib)
target_link_libraries(FromVulkanToDirectX12 PUBLIC glfw assimp stb SA_Logger SA_Maths)


# Copy resources dir command.
add_custom_command(
    TARGET FromVulkanToDirectX12
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:FromVulkanToDirectX12>/Resources
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
