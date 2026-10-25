#include "path_tracer.hpp"

#include "shaderio.h"

#include <glm/gtc/type_ptr.hpp>

static constexpr uint32_t MAX_INSTANCES = 1024;

static constexpr vk::Format HDR_TARGET_FORMAT = vk::Format::eR32G32B32A32Sfloat;

static constexpr vk::Format SWAPCHAIN_FORMAT = vk::Format::eB8G8R8A8Srgb;
static constexpr vk::ColorSpaceKHR SWAPCHAIN_COLOR_SPACE = vk::ColorSpaceKHR::eSrgbNonlinear;
static constexpr vk::PresentModeKHR SWAPCHAIN_PRESENT_MODE = vk::PresentModeKHR::eMailbox;

static constexpr std::array<vk::DescriptorSetLayoutBinding, 4> BINDINGS{{
    {.binding = 0, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
    {.binding = 1, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
    {.binding = 2, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
    {.binding = 3, .descriptorType = vk::DescriptorType::eAccelerationStructureKHR, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eAll},
}};

static vk::raii::CommandPool createCommandPool(const Gpu &gpu)
{
  vk::CommandPoolCreateInfo commandPoolCreateInfo{
      .flags = vk::CommandPoolCreateFlagBits::eTransient,
      .queueFamilyIndex = gpu.getGraphicsQueueIndex(),
  };

  return gpu.getDevice().createCommandPool(commandPoolCreateInfo);
}

static vma::raii::Buffer createInstanceBuffer(const Gpu &gpu)
{
  vk::BufferCreateInfo bufferCreateInfo{
      .size = sizeof(vk::AccelerationStructureInstanceKHR) * MAX_INSTANCES,
      .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
  };

  vma::AllocationCreateInfo allocationCreateInfo{
      .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
      .usage = vma::MemoryUsage::eAutoPreferDevice,
  };

  return gpu.getAllocator().createBuffer(bufferCreateInfo, allocationCreateInfo);
}

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

PathTracer::PathTracer(SDL_Window *window, const Gpu &gpu, const AssetManager &assetManager) : window_(window),
                                                                                               gpu_(gpu),
                                                                                               assetManager_(assetManager),
                                                                                               commandPool_(createCommandPool(gpu)),
                                                                                               descriptorSetLayout_(createDescriptorSetLayout(gpu)),
                                                                                               descriptorPool_(createDescriptorPool(gpu)),
                                                                                               cameraBuffer_(createCameraBuffer(gpu)),
                                                                                               hdrTargetSampler_(createHdrTargetSampler(gpu)),
                                                                                               instanceBuffer_(createInstanceBuffer(gpu))
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

  createSwapchain();
  createHdrTarget();
  createTlas();
}

void PathTracer::createSwapchain()
{
  auto &device = gpu_.getDevice();
  const auto surfaceCapabilities = gpu_.getPhysicalDevice().getSurfaceCapabilitiesKHR(gpu_.getSurface());

  vk::SwapchainCreateInfoKHR swapchainCreateInfo{
      .surface = gpu_.getSurface(),
      .minImageCount = surfaceCapabilities.minImageCount + 1,
      .imageFormat = SWAPCHAIN_FORMAT,
      .imageColorSpace = SWAPCHAIN_COLOR_SPACE,
      .imageExtent = surfaceCapabilities.currentExtent,
      .imageArrayLayers = 1,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
      .preTransform = surfaceCapabilities.currentTransform,
      .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode = SWAPCHAIN_PRESENT_MODE,
      .clipped = true,
  };
  swapchain_ = device.createSwapchainKHR(swapchainCreateInfo);
  swapchainImages_ = swapchain_.getImages();

  for (const auto image : swapchainImages_)
  {
    vk::ImageViewCreateInfo imageViewCreateInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = SWAPCHAIN_FORMAT,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
    };
    swapchainImageViews_.push_back(device.createImageView(imageViewCreateInfo));
  }
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

void PathTracer::createTlas()
{
  auto &device = gpu_.getDevice();
  auto &allocator = gpu_.getAllocator();

  vk::AccelerationStructureGeometryKHR geometry{
      .geometryType = vk::GeometryTypeKHR::eInstances,
      .geometry = vk::AccelerationStructureGeometryInstancesDataKHR{},
  };
  vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{
      .type = vk::AccelerationStructureTypeKHR::eTopLevel,
      .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
      .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
      .geometryCount = 1,
      .pGeometries = &geometry,
  };
  const auto buildSizesInfo = device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometryInfo, MAX_INSTANCES);

  vma::AllocationCreateInfo allocationCreateInfo{
      .usage = vma::MemoryUsage::eAutoPreferDevice,
  };

  vk::BufferCreateInfo tlasBufferCreateInfo{
      .size = buildSizesInfo.accelerationStructureSize,
      .usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
  };
  tlasBuffer_ = allocator.createBuffer(tlasBufferCreateInfo, allocationCreateInfo);

  vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{
      .buffer = *tlasBuffer_,
      .size = buildSizesInfo.accelerationStructureSize,
      .type = vk::AccelerationStructureTypeKHR::eTopLevel,
  };
  tlas_ = device.createAccelerationStructureKHR(tlasCreateInfo);

  vk::BufferCreateInfo tlasScratchBufferCreateInfo{
      .size = buildSizesInfo.buildScratchSize,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
  };
  tlasScratchBuffer_ = allocator.createBufferWithAlignment(tlasScratchBufferCreateInfo, allocationCreateInfo, gpu_.getAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment);

  vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo{
      .accelerationStructureCount = 1,
      .pAccelerationStructures = &*tlas_,
  };
  vk::WriteDescriptorSet write{
      .pNext = &accelerationStructureInfo,
      .dstSet = descriptorSet_,
      .dstBinding = 3,
      .descriptorCount = 1,
      .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
  };
  device.updateDescriptorSets(write, nullptr);
}

void PathTracer::buildTlas(std::span<const Instance> instances)
{
  if (instances.size() > MAX_INSTANCES)
  {
    throw std::runtime_error("Instance buffer is full");
  }

  auto &device = gpu_.getDevice();

  auto *tlasInstances = static_cast<vk::AccelerationStructureInstanceKHR *>(instanceBuffer_.getAllocation().map());
  for (size_t index = 0; index < instances.size(); ++index)
  {
    const auto &instance = instances[index];

    const auto transposed = glm::transpose(instance.transform);
    vk::TransformMatrixKHR transform;
    std::memcpy(&transform, glm::value_ptr(transposed), sizeof(transform));

    tlasInstances[index] = {
        .transform = transform,
        .instanceCustomIndex = assetManager_.getFirstPrimitive(instance.mesh),
        .mask = 0xFF,
        .instanceShaderBindingTableRecordOffset = 0,
        .accelerationStructureReference = assetManager_.getBlasAddress(instance.mesh),
    };
  }
  instanceBuffer_.getAllocation().unmap();

  vk::AccelerationStructureGeometryInstancesDataKHR instancesData{
      .data = device.getBufferAddress({.buffer = *instanceBuffer_}),
  };
  vk::AccelerationStructureGeometryKHR geometry{
      .geometryType = vk::GeometryTypeKHR::eInstances,
      .geometry = instancesData,
  };
  vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{
      .type = vk::AccelerationStructureTypeKHR::eTopLevel,
      .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
      .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
      .dstAccelerationStructure = *tlas_,
      .geometryCount = 1,
      .pGeometries = &geometry,
      .scratchData = device.getBufferAddress({.buffer = *tlasScratchBuffer_}),
  };
  vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo{
      .primitiveCount = static_cast<uint32_t>(instances.size()),
  };

  vk::CommandBufferAllocateInfo commandBufferAllocateInfo{
      .commandPool = *commandPool_,
      .level = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = 1,
  };
  auto commandBuffer = std::move(device.allocateCommandBuffers(commandBufferAllocateInfo).front());

  vk::CommandBufferBeginInfo commandBufferBeginInfo{
      .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
  };
  commandBuffer.begin(commandBufferBeginInfo);

  vk::MemoryBarrier2 barrier{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
  };
  commandBuffer.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});

  commandBuffer.buildAccelerationStructuresKHR(buildGeometryInfo, &buildRangeInfo);
  commandBuffer.end();

  vk::SubmitInfo submitInfo{
      .commandBufferCount = 1,
      .pCommandBuffers = &*commandBuffer,
  };
  auto graphicsQueue = gpu_.getGraphicsQueue();
  graphicsQueue.submit(submitInfo);
  graphicsQueue.waitIdle();
}