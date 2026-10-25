#include "shaderio.h"

struct Asset
{
  StructuredBuffer<Shader::Material> materials;
  StructuredBuffer<Shader::Primitive> primitives;
  Sampler2D texture2Ds[];
};

struct PathTracing
{
  ConstantBuffer<Shader::Camera> camera;
  RWTexture2D<float4> hdrTargetStorage;
  Sampler2D<float4> hdrTargetSampled;
  RaytracingAccelerationStructure tlas;
};
