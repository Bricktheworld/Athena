#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<TextureCopySrt> g_Srt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TextureCopy(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   src = DEREF(g_Srt.src);
  RWTexture2D<float4> dst = DEREF(g_Srt.dst);

  dst[thread_id] = src[thread_id];
}


[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TextureDownsampleHalf(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   src = DEREF(g_Srt.src);
  RWTexture2D<float4> dst = DEREF(g_Srt.dst);

  float2 resolution;
  src.GetDimensions(resolution.x, resolution.y);

  float2 src_uv = float2(thread_id * 2) / resolution;

  dst[thread_id] = src.Sample(g_BilinearSamplerClamp, src_uv + float2(1.0f, 1.0f));
}
