#include "binding.slang"

ParameterBlock<Asset> asset;
ParameterBlock<PathTracing> pathTracing;

static const float PI = 3.1415926535;
static const float EPSILON = 0x1p-23;
static const float RAY_EPSILON = 0.001;
static const float RAY_TMAX = 1e30;
static const float FIREFLY_CLAMP = 100.0;

// from Jarzynski and Olano 2020, "Hash Functions for GPU Rendering"
uint32_t3 pcg3d(uint32_t3 v)
{
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  return v;
}

float3 randomFloat3(inout uint32_t3 state)
{
  state = pcg3d(state);
  return float3(state >> 8u) / float(1u << 24u);
}

float smithLambda(float cosTheta, float alpha)
{
  float a2 = alpha * alpha;
  float c2 = cosTheta * cosTheta;
  return (-1.0 + sqrt(1.0 + a2 * (1.0 - c2) / max(c2, EPSILON))) * 0.5;
}

float smithG1(float cosTheta, float alpha)
{
  return 1.0 / (1.0 + smithLambda(cosTheta, alpha));
}

float smithG2(float ndotv, float ndotl, float alpha)
{
  return 1.0 / (1.0 + smithLambda(ndotv, alpha) + smithLambda(ndotl, alpha));
}

float3 schlickFresnel(float3 v, float3 h, float3 F0)
{
  float vdoth = max(dot(v, h), 0.0);
  return F0 + (1.0 - F0) * pow(1.0 - vdoth, 5.0);
}

// from Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
float3 hammonDiffuse(float3 n, float3 v, float3 l, float alpha, float3 albedo)
{
  float3 h = normalize(v + l);
  float ndotl = max(dot(n, l), 0.0);
  float ndotv = max(dot(n, v), 0.0);
  float ndoth = max(dot(n, h), EPSILON);
  float facing = 0.5 + 0.5 * dot(l, v);
  float rough = facing * (0.9 - 0.4 * facing) * ((0.5 + ndoth) / ndoth);
  float smooth = 1.05 * (1.0 - pow(1.0 - ndotl, 5.0)) * (1.0 - pow(1.0 - ndotv, 5.0));
  float single = lerp(smooth, rough, alpha) / PI;
  float multi = 0.1159 * alpha;
  float3 diffuse = albedo * (single + albedo * multi);
  return diffuse;
}

// from Heitz 2018, "Sampling the GGX Distribution of Visible Normals"
float3 sampleGGXVNDF(float3 Ve, float alphaX, float alphaY, float u1, float u2)
{
  float3 Vh = normalize(float3(alphaX * Ve.x, alphaY * Ve.y, Ve.z));
  float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
  float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
  float3 T2 = cross(Vh, T1);
  float r = sqrt(u1);
  float phi = 2.0 * PI * u2;
  float t1 = r * cos(phi);
  float t2 = r * sin(phi);
  float s = 0.5 * (1.0 + Vh.z);
  t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
  float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
  float3 Ne = normalize(float3(alphaX * Nh.x, alphaY * Nh.y, max(0.0, Nh.z)));
  return Ne;
}

float3 sampleCosineHemisphere(float2 random)
{
  float r = sqrt(random.x);
  float phi = 2.0 * PI * random.y;
  return float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - random.x)));
}

struct HitInfo
{
  float3 position;
  float3 normal;
  float3 tangent;
  float3 albedo;
  float metallic;
  float roughness;
  float3 emissive;
};

HitInfo getObjectHitInfo(RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> rayQuery)
{
  uint32_t primitiveIndex = rayQuery.CommittedInstanceID() + rayQuery.CommittedGeometryIndex();
  Shader::Primitive primitive = asset.primitives[primitiveIndex];
  Shader::Material material = asset.materials[primitive.materialIndex];

  float2 barycentrics = rayQuery.CommittedTriangleBarycentrics();
  float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
  uint32_t triangleIndex = rayQuery.CommittedPrimitiveIndex();

  uint32_t *indices = (uint32_t *)primitive.indexAddress;
  uint32_t index0 = indices[triangleIndex * 3 + 0];
  uint32_t index1 = indices[triangleIndex * 3 + 1];
  uint32_t index2 = indices[triangleIndex * 3 + 2];

  float3 *normals = (float3 *)primitive.normalAddress;
  float3 objectNormal = normals[index0] * weights.x + normals[index1] * weights.y + normals[index2] * weights.z;

  float4 *tangents = (float4 *)primitive.tangentAddress;
  float4 objectTangent = tangents[index0] * weights.x + tangents[index1] * weights.y + tangents[index2] * weights.z;

  float2 *texcoords = (float2 *)primitive.texcoordAddress;
  float2 uv = texcoords[index0] * weights.x + texcoords[index1] * weights.y + texcoords[index2] * weights.z;

  float3 normal = mul(objectNormal, (float3x3)rayQuery.CommittedWorldToObject3x4());
  float3 tangent = mul((float3x3)rayQuery.CommittedObjectToWorld3x4(), objectTangent.xyz);
  float3 bitangent = cross(normal, tangent) * objectTangent.w;
  float3x3 tangentBasis = float3x3(tangent, bitangent, normal);

  float3 textureNormal = asset.texture2Ds[NonUniformResourceIndex(material.normalTextureIndex)].SampleLevel(uv, 0.0).xyz * 2.0 - 1.0;
  textureNormal = normalize(mul(textureNormal, tangentBasis));

  float4 albedo = material.baseColorFactor;
  albedo *= asset.texture2Ds[NonUniformResourceIndex(material.baseColorTextureIndex)].SampleLevel(uv, 0.0);

  float2 metallicRoughness = asset.texture2Ds[NonUniformResourceIndex(material.metallicRoughnessTextureIndex)].SampleLevel(uv, 0.0).bg;

  float3 emissive = material.emissiveFactor;
  emissive *= asset.texture2Ds[NonUniformResourceIndex(material.emissiveTextureIndex)].SampleLevel(uv, 0.0).xyz;

  HitInfo hit;
  hit.position = rayQuery.WorldRayOrigin() + rayQuery.CommittedRayT() * rayQuery.WorldRayDirection();
  hit.normal = textureNormal;
  hit.tangent = tangent;
  hit.albedo = albedo.xyz;
  hit.metallic = material.metallicFactor * metallicRoughness.x;
  hit.roughness = material.roughnessFactor * metallicRoughness.y;
  hit.emissive = emissive;
  return hit;
}

float luminance(float3 color)
{
  return dot(color, float3(0.2126, 0.7152, 0.0722));
}

bool sampleBrdf(float3 n,
                float3 tangent,
                float3 v,
                float3 F0,
                float3 diffuseAlbedo,
                float roughness,
                inout uint32_t3 state,
                out float3 direction,
                out float3 weight)
{
  direction = float3(0.0);
  weight = float3(0.0);

  float alpha = roughness * roughness;
  float3 t = normalize(tangent - dot(tangent, n) * n);
  float3 b = cross(n, t);
  float3 viewTangent = float3(dot(v, t), dot(v, b), dot(v, n));

  float specularProbability = luminance(F0) / max(luminance(F0) + luminance(diffuseAlbedo), EPSILON);
  specularProbability = clamp(specularProbability, 0.1, 0.9);

  float3 random = randomFloat3(state);
  float3 localDirection;
  if (random.x < specularProbability)
  {
    float3 h = sampleGGXVNDF(viewTangent, alpha, alpha, random.y, random.z);
    localDirection = reflect(-viewTangent, h);
    if (localDirection.z <= 0.0)
    {
      return false;
    }
    float ndotv = viewTangent.z;
    float ndotl = localDirection.z;
    float3 F = schlickFresnel(viewTangent, h, F0);
    weight = F * (smithG2(ndotv, ndotl, alpha) / smithG1(ndotv, alpha)) / specularProbability;
  }
  else
  {
    localDirection = sampleCosineHemisphere(random.yz);
    float3 diffuse = hammonDiffuse(float3(0.0, 0.0, 1.0), viewTangent, localDirection, alpha, diffuseAlbedo);
    weight = diffuse * PI / (1.0 - specularProbability);
  }

  direction = normalize(localDirection.x * t + localDirection.y * b + localDirection.z * n);
  return true;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint32_t3 threadID: SV_DispatchThreadID)
{
  uint32_t width, height;
  pathTracing.hdrTargetStorage.GetDimensions(width, height);
  if (threadID.x >= width || threadID.y >= height)
  {
    return;
  }

  uint32_t2 pixelIndex = threadID.xy;
  uint32_t3 state = uint32_t3(pixelIndex, pathTracing.camera.frameIndex);

  float2 jitter = randomFloat3(state).xy;
  float2 ndc = (float2(pixelIndex) + jitter) / float2(width, height) * 2.0 - 1.0;

  float4 viewTarget = mul(pathTracing.camera.inverseProjectionMatrix, float4(ndc, 1.0, 1.0));
  float3 viewDirection = normalize(viewTarget.xyz / viewTarget.w);

  RayDesc ray;
  ray.Origin = mul(pathTracing.camera.inverseViewMatrix, float4(0.0, 0.0, 0.0, 1.0)).xyz;
  ray.Direction = normalize(mul((float3x3)pathTracing.camera.inverseViewMatrix, viewDirection));
  ray.TMin = 0;
  ray.TMax = RAY_TMAX;

  float3 radiance = float3(0.0);
  float3 throughput = float3(1.0);

  for (uint32_t bounce = 0; bounce < pathTracing.camera.bounceCount; ++bounce)
  {
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> rayQuery;
    rayQuery.TraceRayInline(pathTracing.tlas, RAY_FLAG_NONE, 0xFF, ray);
    rayQuery.Proceed();

    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
      break;
    }

    HitInfo hit = getObjectHitInfo(rayQuery);

    float3 v = -ray.Direction;
    float3 n = hit.normal;

    radiance += throughput * hit.emissive;

    if (dot(n, v) <= 0.0)
    {
      break;
    }

    float3 F0 = lerp(float3(0.04), hit.albedo, hit.metallic);
    float3 diffuseAlbedo = hit.albedo * (1.0 - hit.metallic);

    float3 direction;
    float3 weight;
    if (!sampleBrdf(n, hit.tangent, v, F0, diffuseAlbedo, hit.roughness, state, direction, weight))
    {
      break;
    }
    throughput *= weight;

    if (max(throughput.r, max(throughput.g, throughput.b)) <= 0.0)
    {
      break;
    }

    ray.Origin = hit.position + n * RAY_EPSILON;
    ray.Direction = direction;
  }

  if (any(isnan(radiance) || isinf(radiance)))
  {
    radiance = float3(0.0);
  }
  float radianceLuminance = luminance(radiance);
  if (radianceLuminance > FIREFLY_CLAMP)
  {
    radiance *= FIREFLY_CLAMP / radianceLuminance;
  }

  float3 accumulated;
  if (pathTracing.camera.frameIndex == 0)
  {
    accumulated = radiance;
  }
  else
  {
    float frameCount = float(pathTracing.camera.frameIndex);
    float3 previous = pathTracing.hdrTargetStorage[pixelIndex].rgb;
    accumulated = (previous * frameCount + radiance) / (frameCount + 1.0);
  }

  pathTracing.hdrTargetStorage[pixelIndex] = float4(accumulated, 1.0);
}
