#pragma once

#include "asset_manager.hpp"
#include "gpu.hpp"

#include <SDL3/SDL_video.h>
#include <glm/mat4x4.hpp>

struct Instance
{
  MeshHandle mesh;
  glm::mat4 transform;
};

class PathTracer
{
public:
  PathTracer(SDL_Window *window, const Gpu &gpu, const AssetManager &assetManager);

  void buildTlas(std::span<const Instance> instances);

private:
  SDL_Window *window_;
  const Gpu &gpu_;
  const AssetManager &assetManager_;
  vk::raii::CommandPool commandPool_;
  vk::raii::DescriptorSetLayout descriptorSetLayout_;
  vk::raii::DescriptorPool descriptorPool_;
  vk::DescriptorSet descriptorSet_;
  vma::raii::Buffer cameraBuffer_;
  vk::raii::Sampler hdrTargetSampler_;
  vma::raii::Image hdrTargetImage_{nullptr};
  vk::raii::ImageView hdrTargetImageView_{nullptr};
  vk::raii::SwapchainKHR swapchain_{nullptr};
  std::vector<vk::Image> swapchainImages_;
  std::vector<vk::raii::ImageView> swapchainImageViews_;
  vma::raii::Buffer instanceBuffer_;
  vma::raii::Buffer tlasBuffer_{nullptr};
  vk::raii::AccelerationStructureKHR tlas_{nullptr};
  vma::raii::Buffer tlasScratchBuffer_{nullptr};

  void createSwapchain();
  void createHdrTarget();
  void createTlas();
};