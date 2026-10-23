#include "path_tracer.hpp"

#include "shaderio.h"

static constexpr vk::Format HDR_TARGET_FORMAT = vk::Format::eR32G32B32A32Sfloat;

static constexpr std::array<vk::DescriptorSetLayoutBinding, 3> BINDINGS{{
    {.binding = 0, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
    {.binding = 1, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
    {.binding = 2, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
}};

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

static vk::raii::DescriptorSetLayout createDescriptorSetLayout(const Gpu &gpu)
{
  vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
      .bindingCount = static_cast<uint32_t>(BINDINGS.size()),
      .pBindings = BINDINGS.data(),
  };

  return gpu.getDevice().createDescriptorSetLayout(descriptorSetLayoutCreateInfo);
}

static vk::raii::DescriptorPool createDescriptorPool(const Gpu &gpu)
{
  std::vector<vk::DescriptorPoolSize> poolSizes;
  for (const auto &binding : BINDINGS)
  {
    poolSizes.push_back({
        .type = binding.descriptorType,
        .descriptorCount = binding.descriptorCount,
    });
  }

  vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo{
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
  };

  return gpu.getDevice().createDescriptorPool(descriptorPoolCreateInfo);
}

PathTracer::PathTracer(SDL_Window *window, const Gpu &gpu) : window_(window),
                                                             gpu_(gpu),
                                                             descriptorSetLayout_(createDescriptorSetLayout(gpu)),
                                                             descriptorPool_(createDescriptorPool(gpu)),
                                                             cameraBuffer_(createCameraBuffer(gpu)),
                                                             hdrTargetSampler_(createHdrTargetSampler(gpu))
{
  vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo{
      .descriptorPool = *descriptorPool_,
      .descriptorSetCount = 1,
      .pSetLayouts = &*descriptorSetLayout_,
  };
  descriptorSet_ = (*gpu.getDevice()).allocateDescriptorSets(descriptorSetAllocateInfo).front();

  vk::DescriptorBufferInfo cameraBufferInfo{
      .buffer = *cameraBuffer_,
      .range = vk::WholeSize,
  };
  vk::WriteDescriptorSet write{
      .dstSet = descriptorSet_,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = vk::DescriptorType::eStorageBuffer,
      .pBufferInfo = &cameraBufferInfo,
  };
  gpu.getDevice().updateDescriptorSets(write, nullptr);

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

  vk::DescriptorImageInfo storageImageInfo{
      .imageView = *hdrTargetImageView_,
      .imageLayout = vk::ImageLayout::eGeneral,
  };
  vk::DescriptorImageInfo sampledImageInfo{
      .sampler = *hdrTargetSampler_,
      .imageView = *hdrTargetImageView_,
      .imageLayout = vk::ImageLayout::eGeneral,
  };

  std::array writes{
      vk::WriteDescriptorSet{
          .dstSet = descriptorSet_,
          .dstBinding = 1,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eStorageImage,
          .pImageInfo = &storageImageInfo,
      },
      vk::WriteDescriptorSet{
          .dstSet = descriptorSet_,
          .dstBinding = 2,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
          .pImageInfo = &sampledImageInfo,
      },
  };
  gpu_.getDevice().updateDescriptorSets(writes, nullptr);
}