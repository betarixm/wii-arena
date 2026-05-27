cmake_minimum_required(VERSION 3.20)
project(vulkan_frame_layer LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

set(VULKAN_LAYER_NAME "VK_LAYER_WII_arena_frame_capture")
set(VULKAN_LAYER_LIB_PATH "${CMAKE_INSTALL_PREFIX}/lib/libvulkan_frame_layer.so")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/VkLayer_wii_arena_frame_capture.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_wii_arena_frame_capture.json
    @ONLY
)

add_library(vulkan_frame_layer SHARED src/layer.cpp)
target_compile_features(vulkan_frame_layer PRIVATE cxx_std_20)

target_link_libraries(vulkan_frame_layer PRIVATE Vulkan::Vulkan Threads::Threads)

set_target_properties(vulkan_frame_layer PROPERTIES
    OUTPUT_NAME "vulkan_frame_layer"
)

install(
    TARGETS vulkan_frame_layer
    LIBRARY DESTINATION lib
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_wii_arena_frame_capture.json
    DESTINATION share/vulkan/explicit_layer.d
)
