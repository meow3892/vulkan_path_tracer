#pragma once

#include <vk_mem_alloc_raii.hpp>
#include <vulkan/vulkan_raii.hpp>

class Gpu
{
public:
  Gpu();

  vk::PhysicalDevice getPhysicalDevice() const;

  uint32_t getGraphicsQueueIndex() const;

  const vma::raii::Allocator &getAllocator() const;

private:
  vk::raii::Context context_;
  vk::raii::Instance instance_{nullptr};
  vk::raii::PhysicalDevice physicalDevice_{nullptr};
  vk::raii::Device device_{nullptr};
  uint32_t graphicsQueueIndex_{~0u};
  vk::raii::Queue graphicsQueue_{nullptr};
  vma::raii::Allocator allocator_{nullptr};

  void createInstance();
  void selectPhysicalDevice();
  void createDeviceAndQueues();
  void createAllocator();
};