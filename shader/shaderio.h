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
} // namespace Shader