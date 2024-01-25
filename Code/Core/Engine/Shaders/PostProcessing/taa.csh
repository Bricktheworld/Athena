#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"


ConstantBuffer<interlop::TAAResources> g_Resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TAA( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   prev = ResourceDescriptorHeap[g_Resources.prev_hdr];
  Texture2D<float4>   curr = ResourceDescriptorHeap[g_Resources.curr_hdr];
  RWTexture2D<float4> taa  = ResourceDescriptorHeap[g_Resources.taa];


  if (g_SceneBuffer.disable_taa)
  {
    taa[thread_id.xy] = curr[thread_id.xy];
    return;
  }

  Texture2D<float2> curr_velocity_buffer = ResourceDescriptorHeap[g_Resources.curr_velocity];
  Texture2D<float2> prev_velocity_buffer = ResourceDescriptorHeap[g_Resources.prev_velocity];

  float width, height;
  curr.GetDimensions(width, height);

  float2 uv = (float2(thread_id.xy) + float2(0.5f, 0.5f)) / float2(width, height);


  float3 min_color = 9999.0f;
  float3 max_color = -9999.0f;

  for (int y = -1; y <= 1; y++)
  {
    for (int x = -1; x <= 1; x++)
    {
      float2 offset = float2(x, y) / float2(width, height);
      float3 color  = curr.Sample(g_ClampSampler, uv + offset).rgb;
      min_color     = min(min_color, color);
      max_color     = max(max_color, color);
    }
  }


  float2 curr_velocity         = curr_velocity_buffer[thread_id.xy].xy;

  float2 reproj_uv             = uv + curr_velocity;
  float2 prev_velocity         = prev_velocity_buffer.Sample(g_ClampSampler, reproj_uv).xy;

  float  acceleration          = length(prev_velocity - curr_velocity);
  float  velocity_disocclusion = saturate((acceleration - 0.001f) * 10.0f);


  float3 prev_color            = clamp(prev.Sample(g_ClampSampler, reproj_uv).rgb, min_color, max_color);
  float3 curr_color            = curr[thread_id.xy].rgb;
  float3 accumulation          = float3(0.9f * prev_color + 0.1f * curr_color);

  taa[thread_id.xy]            = float4(lerp(accumulation, curr_color, velocity_disocclusion), 1.0f);
}
