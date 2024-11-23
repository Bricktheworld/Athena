#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<TextureCopyResources> resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TextureCopy(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   src = ResourceDescriptorHeap[resources.src];
  RWTexture2D<float4> dst = ResourceDescriptorHeap[resources.dst];

  dst[thread_id] = src[thread_id];
}