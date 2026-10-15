cmake_minimum_required(VERSION 4.0)
project(vulkan_path_tracer LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_subdirectory("vendor/SDL" EXCLUDE_FROM_ALL)

add_executable(vulkan_path_tracer)

target_link_libraries(vulkan_path_tracer
PRIVATE
  SDL3::SDL3
)

target_include_directories(vulkan_path_tracer
PRIVATE
  "source/"
)

target_sources(vulkan_path_tracer
PRIVATE
  "source/main.cpp"
  "source/app.cpp"
)