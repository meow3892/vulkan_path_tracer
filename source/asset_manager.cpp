#include "asset_manager.hpp"

#include "shaderio.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/gtc/type_ptr.hpp>

static constexpr uint32_t MAX_MATERIALS = 1024;

static vma::raii::Buffer createMaterialBuffer(const Gpu &gpu)
{
  vk::BufferCreateInfo bufferCreateInfo{
      .size = sizeof(Shader::Material) * MAX_MATERIALS,
      .usage = vk::BufferUsageFlagBits::eStorageBuffer,
  };

  vma::AllocationCreateInfo allocationCreateInfo{
      .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
      .usage = vma::MemoryUsage::eAutoPreferDevice,
  };

  return gpu.getAllocator().createBuffer(bufferCreateInfo, allocationCreateInfo);
}

AssetManager::AssetManager(const Gpu &gpu) : gpu_(gpu), materialBuffer_(createMaterialBuffer(gpu))
{
  const std::array<uint8_t, 4> white{255, 255, 255, 255};
  const std::array<uint8_t, 4> flatNormal{128, 128, 255, 255};
  uploadTexture2D(vk::Format::eR8G8B8A8Unorm, {1, 1}, white.data());
  uploadTexture2D(vk::Format::eR8G8B8A8Unorm, {1, 1}, flatNormal.data());
}

void AssetManager::uploadTexture2D(vk::Format format, vk::Extent2D extent, vk::ArrayProxy<const void *const> mipLevels)
{
  auto &device = gpu_.getDevice();

  vk::ImageCreateInfo imageCreateInfo{
      .imageType = vk::ImageType::e2D,
      .format = format,
      .extent = {extent.width, extent.height, 1},
      .mipLevels = mipLevels.size(),
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .tiling = vk::ImageTiling::eOptimal,
      .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eHostTransfer,
  };
  vma::AllocationCreateInfo allocationCreateInfo{
      .usage = vma::MemoryUsage::eAutoPreferDevice,
  };
  auto image = gpu_.getAllocator().createImage(imageCreateInfo, allocationCreateInfo);

  vk::ImageSubresourceRange subresourceRange{
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .levelCount = mipLevels.size(),
      .layerCount = 1,
  };

  vk::HostImageLayoutTransitionInfo transitionInfo{
      .image = *image,
      .oldLayout = vk::ImageLayout::eUndefined,
      .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
      .subresourceRange = subresourceRange,
  };
  device.transitionImageLayout(transitionInfo);

  std::vector<vk::MemoryToImageCopy> regions;
  regions.reserve(mipLevels.size());
  for (uint32_t level = 0; level < mipLevels.size(); ++level)
  {
    regions.push_back({
        .pHostPointer = mipLevels.data()[level],
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = level, .layerCount = 1},
        .imageExtent = {std::max(1u, extent.width >> level), std::max(1u, extent.height >> level), 1},
    });
  }

  vk::CopyMemoryToImageInfo copyMemoryToImageInfo{
      .dstImage = *image,
      .dstImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
      .regionCount = static_cast<uint32_t>(regions.size()),
      .pRegions = regions.data(),
  };
  device.copyMemoryToImage(copyMemoryToImageInfo);

  vk::ImageViewCreateInfo imageViewCreateInfo{
      .image = *image,
      .viewType = vk::ImageViewType::e2D,
      .format = format,
      .subresourceRange = subresourceRange,
  };
  auto imageView = device.createImageView(imageViewCreateInfo);

  texture2Ds_.push_back(Texture2D{
      .image = std::move(image),
      .imageView = std::move(imageView),
  });
}

static const fastgltf::Attribute &findAttribute(const fastgltf::Primitive &primitive, std::string_view name)
{
  const auto attribute = primitive.findAttribute(name);
  if (attribute == primitive.attributes.end())
  {
    throw std::runtime_error(std::string(name) + " attribute not found");
  }
  return *attribute;
}

MeshHandle AssetManager::uploadGltf(const std::filesystem::path &path)
{
  fastgltf::Parser parser(fastgltf::Extensions::KHR_materials_emissive_strength);
  auto data = fastgltf::GltfDataBuffer::FromPath(path);
  if (data.error() != fastgltf::Error::None)
  {
    throw std::runtime_error(path.string() + ' ' + std::string(fastgltf::getErrorMessage(data.error())));
  }

  auto asset = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::LoadExternalBuffers);
  if (asset.error() != fastgltf::Error::None)
  {
    throw std::runtime_error(path.string() + ' ' + std::string(fastgltf::getErrorMessage(asset.error())));
  }

  if (asset->meshes.size() != 1)
  {
    throw std::runtime_error(path.string() + " does not have exactly 1 mesh");
  }

  const auto &mesh = asset->meshes.front();

  if (materialCount_ + asset->materials.size() > MAX_MATERIALS)
  {
    throw std::runtime_error(path.string() + " material buffer is full");
  }

  const uint32_t materialBase = materialCount_;
  const uint32_t textureBase = textureCount_;

  try
  {
    // first pass gather mesh size
    size_t vertexCount = 0, indexCount = 0;
    for (const auto &primitive : mesh.primitives)
    {
      vertexCount += asset->accessors[findAttribute(primitive, "POSITION").accessorIndex].count;
      indexCount += asset->accessors[primitive.indicesAccessor.value()].count;
    }

    auto &allocator = gpu_.getAllocator();

    vk::BufferCreateInfo bufferCreateInfo{
        .usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
    };
    vma::AllocationCreateInfo allocationCreateInfo{
        .flags = vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
        .usage = vma::MemoryUsage::eAutoPreferDevice,
    };

    bufferCreateInfo.size = sizeof(uint32_t) * indexCount;
    auto indexBuffer = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo);
    bufferCreateInfo.size = sizeof(glm::vec3) * vertexCount;
    auto positionBuffer = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo);

    bufferCreateInfo.usage = vk::BufferUsageFlagBits::eShaderDeviceAddress;
    auto normalBuffer = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo);
    bufferCreateInfo.size = sizeof(glm::vec4) * vertexCount;
    auto tangentBuffer = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo);
    bufferCreateInfo.size = sizeof(glm::vec2) * vertexCount;
    auto texcoordBuffer = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo);

    auto *indices = static_cast<uint32_t *>(indexBuffer.getAllocation().map());
    auto *positions = static_cast<glm::vec3 *>(positionBuffer.getAllocation().map());
    auto *normals = static_cast<glm::vec3 *>(normalBuffer.getAllocation().map());
    auto *tangents = static_cast<glm::vec4 *>(tangentBuffer.getAllocation().map());
    auto *texcoords = static_cast<glm::vec2 *>(texcoordBuffer.getAllocation().map());

    // second pass fill mesh buffers
    std::vector<Primitive> primitives;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    for (const auto &primitive : mesh.primitives)
    {
      auto &indexAccessor = asset->accessors[primitive.indicesAccessor.value()];
      auto &positionAccessor = asset->accessors[findAttribute(primitive, "POSITION").accessorIndex];
      auto &normalAccessor = asset->accessors[findAttribute(primitive, "NORMAL").accessorIndex];
      auto &tangentAccessor = asset->accessors[findAttribute(primitive, "TANGENT").accessorIndex];
      auto &texcoordAccessor = asset->accessors[findAttribute(primitive, "TEXCOORD_0").accessorIndex];

      fastgltf::copyFromAccessor<uint32_t>(asset.get(), indexAccessor, indices + firstIndex);
      fastgltf::copyFromAccessor<glm::vec3>(asset.get(), positionAccessor, positions + vertexOffset);
      fastgltf::copyFromAccessor<glm::vec3>(asset.get(), normalAccessor, normals + vertexOffset);
      fastgltf::copyFromAccessor<glm::vec4>(asset.get(), tangentAccessor, tangents + vertexOffset);
      fastgltf::copyFromAccessor<glm::vec2>(asset.get(), texcoordAccessor, texcoords + vertexOffset);

      primitives.push_back({
          .firstIndex = firstIndex,
          .indexCount = static_cast<uint32_t>(indexAccessor.count),
          .vertexOffset = vertexOffset,
          .vertexCount = static_cast<uint32_t>(positionAccessor.count),
          .materialIndex = materialBase + static_cast<uint32_t>(primitive.materialIndex.value()),
      });

      firstIndex += indexAccessor.count;
      vertexOffset += positionAccessor.count;
    }

    indexBuffer.getAllocation().unmap();
    positionBuffer.getAllocation().unmap();
    normalBuffer.getAllocation().unmap();
    tangentBuffer.getAllocation().unmap();
    texcoordBuffer.getAllocation().unmap();

    // fill material buffer
    auto *materials = static_cast<Shader::Material *>(materialBuffer_.getAllocation().map());
    for (const auto &material : asset->materials)
    {
      materials[materialCount_] = {
          .baseColorFactor = glm::make_vec4(material.pbrData.baseColorFactor.data()),
          .emissiveFactor = glm::make_vec3(material.emissiveFactor.data()) * material.emissiveStrength,
          .metallicFactor = material.pbrData.metallicFactor,
          .roughnessFactor = material.pbrData.roughnessFactor,
          .baseColorTextureIndex = material.pbrData.baseColorTexture ? textureBase + static_cast<uint32_t>(material.pbrData.baseColorTexture->textureIndex) : 0,
          .normalTextureIndex = material.normalTexture ? textureBase + static_cast<uint32_t>(material.normalTexture->textureIndex) : 1,
          .metallicRoughnessTextureIndex = material.pbrData.metallicRoughnessTexture ? textureBase + static_cast<uint32_t>(material.pbrData.metallicRoughnessTexture->textureIndex) : 0,
          .emissiveTextureIndex = material.emissiveTexture ? textureBase + static_cast<uint32_t>(material.emissiveTexture->textureIndex) : 0,
          .alphaCutoff = material.alphaMode == fastgltf::AlphaMode::Mask ? material.alphaCutoff : 0.0f,
      };
      ++materialCount_;
    }
    materialBuffer_.getAllocation().unmap();

    textureCount_ += asset->textures.size();

    meshes_.push_back({
        .indexBuffer = std::move(indexBuffer),
        .positionBuffer = std::move(positionBuffer),
        .normalBuffer = std::move(normalBuffer),
        .tangentBuffer = std::move(tangentBuffer),
        .texcoordBuffer = std::move(texcoordBuffer),
        .primitives = std::move(primitives),
    });

    return MeshHandle{static_cast<uint32_t>(meshes_.size() - 1)};
  }
  catch (const std::exception &exception)
  {
    // rethrow missing attribute or optional::value() with path
    throw std::runtime_error(path.string() + ' ' + exception.what());
  }
}