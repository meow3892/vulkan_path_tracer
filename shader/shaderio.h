#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <glm/glm.hpp>

using float3 = glm::vec3;
using float4 = glm::vec4;
using float4x4 = glm::mat4;
#endif

namespace Shader
{
struct Material
{
  float4 baseColorFactor;
  float3 emissiveFactor;
  float metallicFactor;
  float roughnessFactor;
  uint32_t baseColorTextureIndex;
  uint32_t normalTextureIndex;
  uint32_t metallicRoughnessTextureIndex;
  uint32_t emissiveTextureIndex;
  float alphaCutoff;
};

struct Primitive
{
  uint64_t indexAddress;
  uint64_t positionAddress;
  uint64_t normalAddress;
  uint64_t tangentAddress;
  uint64_t texcoordAddress;
  uint32_t materialIndex;
};

struct Camera
{
  float4x4 inverseViewMatrix;
  float4x4 inverseProjectionMatrix;
  uint32_t frameIndex;
  uint32_t sampleCount;
};
} // namespace Shader