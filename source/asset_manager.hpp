#pragma once

#include "gpu.hpp"

#include <vk_mem_alloc_raii.hpp>

#include <filesystem>

struct MeshHandle
{
  uint32_t index;
};

class AssetManager
{
public:
  AssetManager(const Gpu &gpu);

  MeshHandle uploadGltf(const std::filesystem::path &path);

  void buildBlases();

private:
  struct Primitive
  {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t vertexOffset;
    uint32_t vertexCount;
    uint32_t materialIndex;
  };

  struct Mesh
  {
    vma::raii::Buffer indexBuffer;
    vma::raii::Buffer positionBuffer;
    vma::raii::Buffer normalBuffer;
    vma::raii::Buffer tangentBuffer;
    vma::raii::Buffer texcoordBuffer;

    std::vector<Primitive> primitives;
    uint32_t firstPrimitive;

    vma::raii::Buffer blasBuffer{nullptr};
    vk::raii::AccelerationStructureKHR blas{nullptr};
  };

  struct Texture2D
  {
    vma::raii::Image image;
    vk::raii::ImageView imageView;
  };

  void uploadTexture2D(vk::Format format, vk::Extent2D extent, vk::ArrayProxy<const void *const> mipLevels);

  const Gpu &gpu_;
  vk::raii::CommandPool commandPool_;
  vma::raii::Buffer materialBuffer_;
  uint32_t materialCount_{0};
  vma::raii::Buffer primitiveBuffer_;
  uint32_t primitiveCount_{0};
  std::vector<Mesh> meshes_;
  std::vector<Texture2D> texture2Ds_;
};