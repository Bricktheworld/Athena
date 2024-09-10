#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<DebugVisualizerResources> g_Resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_VisibilityBufferVisualize( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<uint>     input  = ResourceDescriptorHeap[g_Resources.input];
  RWTexture2D<float4> output = ResourceDescriptorHeap[g_Resources.output];

  uint prim_id = input[thread_id.xy];

  output[thread_id.xy] = float4(float(prim_id & 1), float(prim_id & 3) / 4.0f, float(prim_id & 7) / 8.0f, 1.0f);
}