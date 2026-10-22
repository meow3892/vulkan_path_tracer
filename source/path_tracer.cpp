#include "path_tracer.hpp"

#include "shaderio.h"

static constexpr vk::Format HDR_TARGET_FORMAT = vk::Format::eR32G32B32A32Sfloat;

static vma::raii::Buffer createCameraBuffer(const Gpu &gpu)
{
  vk::BufferCreateInfo bufferCreateInfo{
      .size = sizeof(Shader::Camera),
      .usage = vk::BufferUsageFlagBits::eStorageBuffer,
  };

  vma::AllocationCreateInfo allocationCreateInfo{
      .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
      .usage = vma::MemoryUsage::eAutoPreferDevice,
  };

  return gpu.getAllocator().createBuffer(bufferCreateInfo, allocationCreateInfo);
}

static vk::raii::Sampler createHdrTargetSampler(const Gpu &gpu)
{
  vk::SamplerCreateInfo samplerCreateInfo{
      .magFilter = vk::Filter::eNearest,
      .minFilter = vk::Filter::eNearest,
      .mipmapMode = vk::SamplerMipmapMode::eNearest,
      .addressModeU = vk::SamplerAddressMode::eClampToEdge,
      .addressModeV = vk::SamplerAddressMode::eClampToEdge,
      .addressModeW = vk::SamplerAddressMode::eClampToEdge,
  };

  return gpu.getDevice().createSampler(samplerCreateInfo);
}

PathTracer::PathTracer(SDL_Window *window, const Gpu &gpu) : window_(window),
                                                             gpu_(gpu),
                                                             cameraBuffer_(createCameraBuffer(gpu)),
                                                             hdrTargetSampler_(createHdrTargetSampler(gpu))
{
  createHdrTarget();
}

void PathTracer::createHdrTarget()
{
  int width, height;
  SDL_GetWindowSizeInPixels(window_, &width, &height);

  vk::ImageCreateInfo imageCreateInfo{
      .imageType = vk::ImageType::e2D,
      .format = HDR_TARGET_FORMAT,
      .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .tiling = vk::ImageTiling::eOptimal,
      .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
  };
  vma::AllocationCreateInfo allocationCreateInfo{
      .flags = vma::AllocationCreateFlagBits::eDedicatedMemory,
      .usage = vma::MemoryUsage::eAutoPreferDevice,
  };
  hdrTargetImage_ = gpu_.getAllocator().createImage(imageCreateInfo, allocationCreateInfo);

  vk::ImageViewCreateInfo imageViewCreateInfo{
      .image = *hdrTargetImage_,
      .viewType = vk::ImageViewType::e2D,
      .format = HDR_TARGET_FORMAT,
      .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
  };
  hdrTargetImageView_ = gpu_.getDevice().createImageView(imageViewCreateInfo);
}