#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"


ConstantBuffer<interlop::TAAResources> g_Resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TAA( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   prev = ResourceDescriptorHeap[g_Resources.prev];

  RWTexture2D<float4> curr = ResourceDescriptorHeap[g_Resources.curr];


  Texture2D<float2> velocity_buffer = ResourceDescriptorHeap[g_Resources.velocity];

  float width, height;
  curr.GetDimensions(width, height);

  float2 uv = (float2(thread_id.xy) + float2(0.5f, 0.5f)) / float2(width, height);

  float2 velocity    = velocity_buffer[thread_id.xy].xy;
  float2 reproj_uv   = uv + velocity;

  float3 prev_color  = prev.Sample(g_ClampSampler, reproj_uv).rgb;
  float3 curr_color  = curr[thread_id.xy].rgb;
  curr[thread_id.xy] = float4(0.9f * prev_color + 0.1f * curr_color, 1.0f);
}
