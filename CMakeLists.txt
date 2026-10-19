cmake_minimum_required(VERSION 4.0)
project(vulkan_path_tracer LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

find_package(Vulkan REQUIRED)

add_subdirectory("vendor/SDL" EXCLUDE_FROM_ALL)
add_subdirectory("vendor/VulkanMemoryAllocator-Hpp" EXCLUDE_FROM_ALL)

add_executable(vulkan_path_tracer)

target_link_libraries(vulkan_path_tracer
PRIVATE
  SDL3::SDL3
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
  VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
)

target_include_directories(vulkan_path_tracer
PRIVATE
  "source/"
)

target_sources(vulkan_path_tracer
PRIVATE
  "source/main.cpp"
  "source/app.cpp"
  "source/gpu.cpp"
  "source/vma.cpp"
)

target_compile_definitions(vulkan_path_tracer
PRIVATE
  VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)