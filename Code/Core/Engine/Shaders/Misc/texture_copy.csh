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