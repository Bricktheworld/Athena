#include "../root_signature.hlsli"
#include "../interlop.hlsli"

struct PSInput
{
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

ConstantBuffer<interlop::PostProcessingRenderResources> render_resources : register(b0);

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 aces_film(float3 x)
{
  float a = 2.51f;
  float b = 0.03f;
  float c = 2.43f;
  float d = 0.59f;
  float e = 0.14f;
  return saturate((x*(a*x + b)) / (x*(c*x + d) + e));
}

float3 less_than(float3 f, float value)
{
    return float3(
        (f.x < value) ? 1.f : 0.f,
        (f.y < value) ? 1.f : 0.f,
        (f.z < value) ? 1.f : 0.f);
}

float3 linear_to_srgb(float3 rgb)
{
  rgb = clamp(rgb, 0.0f, 1.0f);
  return lerp(
      pow(rgb * 1.055f, 1.0f / 2.4f) - 0.055f,
      rgb * 12.92f,
      less_than(rgb, 0.0031308f)
  );
}


[RootSignature(BINDLESS_ROOT_SIGNATURE)]
float4 main(PSInput IN) : SV_TARGET
{
  Texture2D<float4> input = ResourceDescriptorHeap[render_resources.texture];
  float3 color = input.Sample(g_ClampSampler, IN.uv).rgb;
  color = aces_film(color);
  color = linear_to_srgb(color);
  return float4(color, 1.0f);
}