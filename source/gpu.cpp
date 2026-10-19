#include "gpu.hpp"

#include <SDL3/SDL_vulkan.h>

#include <iostream>

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

Gpu::Gpu()
{
  createInstance();
  selectPhysicalDevice();
  createDeviceAndQueues();
  createAllocator();
}

vk::PhysicalDevice Gpu::getPhysicalDevice() const
{
  return *physicalDevice_;
}

uint32_t Gpu::getGraphicsQueueIndex() const
{
  return graphicsQueueIndex_;
}

const vma::raii::Allocator &Gpu::getAllocator() const
{
  return allocator_;
}

void Gpu::createInstance()
{
  const vk::ApplicationInfo applicationInfo{
      .apiVersion = vk::ApiVersion14,
  };

  std::vector<const char *> enabledLayers;
  const char *validationLayer = "VK_LAYER_KHRONOS_validation";
  if (ENABLE_VALIDATION_LAYERS)
  {
    const auto instanceLayerProperties = vk::enumerateInstanceLayerProperties();

    bool isValidationLayerAvailable = std::ranges::any_of(
        instanceLayerProperties,
        [validationLayer](const auto &property)
        { return std::strcmp(validationLayer, property.layerName) == 0; });

    if (isValidationLayerAvailable)
    {
      enabledLayers.push_back(validationLayer);
    }
    else
    {
      std::cerr << "Vulkan validation layer not available\n";
    }
  }
  const char *debugAction = "VK_DBG_LAYER_ACTION_DEBUG_OUTPUT";
  std::array<const vk::LayerSettingEXT, 1> layerSettings{
      {validationLayer, "debug_action", vk::LayerSettingTypeEXT::eString, 1, &debugAction},
  };
  const vk::LayerSettingsCreateInfoEXT layerSettingsCreateInfo{
      .settingCount = layerSettings.size(),
      .pSettings = layerSettings.data(),
  };

  Uint32 sdlInstanceExtensionCount = 0;
  auto sdlInstanceExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlInstanceExtensionCount);
  std::vector instanceExtensions(sdlInstanceExtensions, sdlInstanceExtensions + sdlInstanceExtensionCount);

  vk::InstanceCreateInfo instanceCreateInfo{
      .pNext = &layerSettingsCreateInfo,
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
      .ppEnabledLayerNames = enabledLayers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
      .ppEnabledExtensionNames = instanceExtensions.data(),
  };

  instance_ = context_.createInstance(instanceCreateInfo);
}

void Gpu::selectPhysicalDevice()
{
  const auto physicalDevices = instance_.enumeratePhysicalDevices();

  for (const auto physicalDevice : physicalDevices)
  {
    const auto properties = physicalDevice.getProperties();
    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
    {
      physicalDevice_ = physicalDevice;
      break;
    }
  }

  if (!(*physicalDevice_))
  {
    throw std::runtime_error("No discrete Gpu found");
  }
}

void Gpu::createDeviceAndQueues()
{
  const auto queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();
  for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i)
  {
    if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics))
    {
      graphicsQueueIndex_ = i;
      break;
    }
  }
  if (graphicsQueueIndex_ == ~0u)
  {
    throw std::runtime_error("Failed to find graphics queue");
  }

  float queuePriority = 1.0f;
  std::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfos{
      {
          .queueFamilyIndex = graphicsQueueIndex_,
          .queueCount = 1,
          .pQueuePriorities = &queuePriority,
      },
  };

  vk::DeviceCreateInfo deviceCreateInfo{
      .queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCreateInfos.size()),
      .pQueueCreateInfos = deviceQueueCreateInfos.data(),
  };

  device_ = physicalDevice_.createDevice(deviceCreateInfo);
}

void Gpu::createAllocator()
{
  vma::AllocatorCreateInfo allocatorCreateInfo{
      .physicalDevice = *physicalDevice_,
      .vulkanApiVersion = vk::ApiVersion14,
  };

  allocator_ = vma::raii::Allocator(instance_, device_, allocatorCreateInfo);
}