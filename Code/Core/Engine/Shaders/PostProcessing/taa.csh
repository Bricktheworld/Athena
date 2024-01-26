#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"


ConstantBuffer<TAAResources> g_Resources : register(b0);

uint2 get_dilated_texel(int2 texel)
{
  Texture2D<float> depth_buffer = ResourceDescriptorHeap[g_Resources.gbuffer_depth];
  float  closest_depth     = 0.0f;
  float2 closest_texel_pos = texel;

  uint2 dimensions;
  depth_buffer.GetDimensions(dimensions.x, dimensions.y);

  for (int y = -1; y <= 1; y++)
  {
    for (int x = -1; x <= 1; x++)
    {
      uint2 pos                = clamp(texel + int2(x, y), 0, dimensions);
      float neighborhood_depth = depth_buffer[pos];

      if (neighborhood_depth > closest_depth)
      {
        closest_texel_pos = pos;
        closest_depth     = neighborhood_depth;
      }
    }
  }

  return closest_texel_pos;
}

void tap_curr_buffer(
  int2 texel_offset,
  float2 uv,
  float2 dimensions,
  inout float3 min_color,
  inout float3 max_color
) {
  Texture2D<float4> curr_buffer = ResourceDescriptorHeap[g_Resources.curr_hdr];

  float2 uv_offset = float2(texel_offset) / dimensions;
  float3 color     = curr_buffer.Sample(g_ClampSampler, uv + uv_offset).rgb;
  min_color        = min(min_color, color);
  max_color        = max(max_color, color);
}

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TAA( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   prev_buffer = ResourceDescriptorHeap[g_Resources.prev_hdr];
  Texture2D<float4>   curr_buffer = ResourceDescriptorHeap[g_Resources.curr_hdr];

  RWTexture2D<float4> taa_buffer  = ResourceDescriptorHeap[g_Resources.taa];


  if (g_ViewportBuffer.disable_taa)
  {
    taa_buffer[thread_id.xy] = curr_buffer[thread_id.xy];
    return;
  }

  Texture2D<float2> curr_velocity_buffer = ResourceDescriptorHeap[g_Resources.curr_velocity];
  Texture2D<float2> prev_velocity_buffer = ResourceDescriptorHeap[g_Resources.prev_velocity];

  float width, height;
  curr_buffer.GetDimensions(width, height);
  float2 dimensions = float2(width, height);

  uint2  dilated_texel = get_dilated_texel(thread_id.xy);
  float2 uv            = (float2(thread_id.xy) + 0.5f) / dimensions;

  float3 min_color_cross =  9999.0f;
  float3 max_color_cross = -9999.0f;

  tap_curr_buffer(int2( 0, -1), uv, dimensions, min_color_cross, max_color_cross);
  tap_curr_buffer(int2(-1,  0), uv, dimensions, min_color_cross, max_color_cross);
  tap_curr_buffer(int2( 0,  0), uv, dimensions, min_color_cross, max_color_cross);
  tap_curr_buffer(int2( 1,  0), uv, dimensions, min_color_cross, max_color_cross);
  tap_curr_buffer(int2( 0,  1), uv, dimensions, min_color_cross, max_color_cross);

  float3 min_color_3x3   = min_color_cross;
  float3 max_color_3x3   = max_color_cross;

  tap_curr_buffer(int2(-1, -1), uv, dimensions, min_color_3x3,   max_color_3x3);
  tap_curr_buffer(int2( 1, -1), uv, dimensions, min_color_3x3,   max_color_3x3);
  tap_curr_buffer(int2(-1,  1), uv, dimensions, min_color_3x3,   max_color_3x3);
  tap_curr_buffer(int2( 1,  1), uv, dimensions, min_color_3x3,   max_color_3x3);

  float3 min_color = min_color_3x3 * 0.5f + min_color_cross * 0.5f;
  float3 max_color = max_color_3x3 * 0.5f + max_color_cross * 0.5f;


  float2 curr_velocity         = curr_velocity_buffer[dilated_texel].xy;

  float2 reproj_uv             = uv + curr_velocity;
  float2 prev_velocity         = prev_velocity_buffer.Sample(g_ClampSampler, reproj_uv).xy;

  float  acceleration          = length(prev_velocity - curr_velocity);
  float  velocity_disocclusion = saturate((acceleration - 0.001f) * 10.0f);


  float3 prev_color            = clamp(prev_buffer.Sample(g_ClampSampler, reproj_uv).rgb, min_color, max_color);
  float3 curr_color            = curr_buffer[thread_id.xy].rgb;
  float3 accumulation          = float3(0.9f * prev_color + 0.1f * curr_color);

  taa_buffer[thread_id.xy]     = float4(lerp(accumulation, curr_color, velocity_disocclusion), 1.0f);
}
