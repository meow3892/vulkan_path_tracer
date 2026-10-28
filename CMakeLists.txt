cmake_minimum_required(VERSION 4.0)
project(vulkan_path_tracer LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

find_package(Vulkan REQUIRED)

add_subdirectory("vendor/SDL" EXCLUDE_FROM_ALL)
add_subdirectory("vendor/VulkanMemoryAllocator-Hpp" EXCLUDE_FROM_ALL)
add_subdirectory("vendor/glm" EXCLUDE_FROM_ALL)
add_subdirectory("vendor/fastgltf" EXCLUDE_FROM_ALL)

add_executable(vulkan_path_tracer)

target_link_libraries(vulkan_path_tracer
PRIVATE
  SDL3::SDL3
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
  VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
  glm::glm
  fastgltf::fastgltf
)

target_include_directories(vulkan_path_tracer
PRIVATE
  "source/"
  "shader/"
)

target_sources(vulkan_path_tracer
PRIVATE
  "source/main.cpp"
  "source/app.cpp"
  "source/gpu.cpp"
  "source/vma.cpp"
  "source/asset_manager.cpp"
  "source/path_tracer.cpp"
)

target_compile_definitions(vulkan_path_tracer
PRIVATE
  VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)

find_program(SLANGC slangc REQUIRED)

set(SHADER_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shader)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIRECTORY})

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIRECTORY}/path_tracing.spv
  COMMAND ${SLANGC} ${CMAKE_SOURCE_DIR}/shader/path_tracing.slang
    -target spirv
    -fvk-use-scalar-layout
    -o ${SHADER_OUTPUT_DIRECTORY}/path_tracing.spv
    -depfile ${CMAKE_CURRENT_BINARY_DIR}/path_tracing.d
  DEPENDS shader/path_tracing.slang
  DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/path_tracing.d
)

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIRECTORY}/tonemapping_pbr_neutral.spv
  COMMAND ${SLANGC} ${CMAKE_SOURCE_DIR}/shader/tonemapping_pbr_neutral.slang
    -target spirv
    -fvk-use-scalar-layout
    -fvk-use-entrypoint-name
    -o ${SHADER_OUTPUT_DIRECTORY}/tonemapping_pbr_neutral.spv
    -depfile ${CMAKE_CURRENT_BINARY_DIR}/tonemapping_pbr_neutral.d
  DEPENDS shader/tonemapping_pbr_neutral.slang
  DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/tonemapping_pbr_neutral.d
)

add_custom_target(shaders
  DEPENDS
    ${SHADER_OUTPUT_DIRECTORY}/path_tracing.spv
    ${SHADER_OUTPUT_DIRECTORY}/tonemapping_pbr_neutral.spv
)
add_dependencies(vulkan_path_tracer shaders)