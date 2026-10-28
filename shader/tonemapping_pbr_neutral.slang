#include "binding.slang"

ParameterBlock<Asset> asset;
ParameterBlock<PathTracing> pathTracing;

float3 PBRNeutralToneMapping(float3 color)
{
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression)
    return color;

  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return lerp(color, float3(newPeak), g);
}

struct VertexOutput
{
  float4 position : SV_Position;
  float2 texcoord : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex: SV_VulkanVertexID)
{
  VertexOutput output;
  output.texcoord = float2((vertexIndex << 1) & 2, vertexIndex & 2);
  output.position = float4(output.texcoord * 2.0 - 1.0, 0.0, 1.0);
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
  float3 color = pathTracing.hdrTargetSampled.SampleLevel(input.texcoord, 0.0).rgb;

  return float4(PBRNeutralToneMapping(max(color, float3(0.0))), 1.0);
}
