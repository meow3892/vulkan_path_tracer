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