#pragma once

#include <SDL3/SDL_video.h>
#include <vk_mem_alloc_raii.hpp>
#include <vulkan/vulkan_raii.hpp>

class Gpu
{
public:
  Gpu(SDL_Window *window);

  vk::PhysicalDevice getPhysicalDevice() const;

  uint32_t getGraphicsQueueIndex() const;

  vk::Queue getGraphicsQueue() const;

  const vk::raii::Device &getDevice() const;

  const vma::raii::Allocator &getAllocator() const;

  vk::PhysicalDeviceAccelerationStructurePropertiesKHR getAccelerationStructureProperties() const;

  vk::SurfaceKHR getSurface() const;

private:
  vk::raii::Context context_;
  vk::raii::Instance instance_{nullptr};
  vk::raii::SurfaceKHR surface_{nullptr};
  vk::raii::PhysicalDevice physicalDevice_{nullptr};
  vk::raii::Device device_{nullptr};
  uint32_t graphicsQueueIndex_{~0u};
  vk::raii::Queue graphicsQueue_{nullptr};
  vma::raii::Allocator allocator_{nullptr};
  vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties_;

  void createInstance();
  void createSurface(SDL_Window *window);
  void selectPhysicalDevice();
  void createDeviceAndQueues();
  void createAllocator();
  void queryProperties();
};