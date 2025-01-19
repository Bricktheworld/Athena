#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"


ConstantBuffer<TemporalAASrt> g_Srt : register(b0);

uint2 get_dilated_texel(int2 texel)
{
  Texture2D<float> depth_buffer = DEREF(g_Srt.gbuffer_depth);
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

float luma_rec709(float3 color)
{
  return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

float3 luma_weight_color_rec709(float3 color)
{
  return color / (1.0f + luma_rec709(color));
}

float3 inverse_luma_weight_color_rec709(float3 color)
{
  return color / (1.0f - luma_rec709(color));
}

void tap_curr_buffer(
  int2 texel_offset,
  float2 uv,
  float2 dimensions,
  inout float3 min_color,
  inout float3 max_color
) {
  Texture2D<float4> curr_buffer = DEREF(g_Srt.curr_hdr);

  float2 uv_offset = float2(texel_offset) / dimensions;
  float3 color     = luma_weight_color_rec709(curr_buffer.Sample(g_BilinearSamplerClamp, uv + uv_offset).rgb);
  min_color        = min(min_color, color);
  max_color        = max(max_color, color);
}

float3 clip_aabb(float3 color, float3 history_color, float3 min_color, float3 max_color)
{
  float3 p_clip = 0.5f * (max_color + min_color);
  float3 e_clip = 0.5f * (max_color - min_color);

  float3 v_clip  = history_color - p_clip;
  float3 v_unit  = v_clip.xyz / e_clip;
  float3 a_unit  = abs(v_unit);
  float  ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

  if (ma_unit > 1.0f)
  {
    return p_clip + v_clip / ma_unit;
  }
  else
  {
    return history_color;
  }
}

// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
float4 sample_texture_catmull_rom(Texture2D<float4> texture, float2 uv)
{
  float2 texture_size;
  texture.GetDimensions(texture_size.x, texture_size.y);

  // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
  // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
  // location [1, 1] in the grid, where [0, 0] is the top left corner.
  float2 sample_pos = uv * texture_size;
  float2 tex_pos1   = floor(sample_pos - 0.5f) + 0.5f;

  // Compute the fractional offset from our starting texel to our original sample location, which we'll
  // feed into the Catmull-Rom spline function to get our filter weights.
  float2 f  = sample_pos - tex_pos1;
  float2 f2 = f * f;
  float2 f3 = f2 * f;

  // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
  // These equations are pre-expanded based on our knowledge of where the texels will be located,
  // which lets us avoid having to evaluate a piece-wise function.
  float2 w0 = (1.0f / 6.0f) * (-3.0f * f3 + 6.0f * f2 - 3.0f * f);
  float2 w1 = (1.0f / 6.0f) * (9.0f * f3 - 15.0f * f2 + 6.0f);
  float2 w2 = (1.0f / 6.0f) * (-9.0f * f3 + 12.0f * f2 + 3.0f * f);
  float2 w3 = (1.0f / 6.0f) * (3.0f * f3 - 3.0f * f2);

  // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
  // simultaneously evaluate the middle 2 samples from the 4x4 grid.
  float2 w12      = w1 + w2;
  float2 offset12 = w2 / (w1 + w2);

  // Compute the final UV coordinates we'll use for sampling the texture
  float2 tex_pos0  = tex_pos1 - 1;
  float2 tex_pos3  = tex_pos1 + 2;
  float2 tex_pos12 = tex_pos1 + offset12;

  tex_pos0  /= texture_size;
  tex_pos3  /= texture_size;
  tex_pos12 /= texture_size;

  float4 result = 0.0f;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos0.x,  tex_pos0.y), 0.0f) * w0.x  * w0.y;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos12.x, tex_pos0.y), 0.0f) * w12.x * w0.y;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos3.x,  tex_pos0.y), 0.0f) * w3.x  * w0.y;

  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos0.x,  tex_pos12.y), 0.0f) * w0.x  * w12.y;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos12.x, tex_pos12.y), 0.0f) * w12.x * w12.y;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos3.x,  tex_pos12.y), 0.0f) * w3.x  * w12.y;

  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos0.x,  tex_pos3.y), 0.0f) * w0.x  * w3.y;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos12.x, tex_pos3.y), 0.0f) * w12.x * w3.y;
  result += texture.SampleLevel(g_BilinearSamplerClamp, float2(tex_pos3.x,  tex_pos3.y), 0.0f) * w3.x  * w3.y;

  return result;
}

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_TAA( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   prev_buffer = DEREF(g_Srt.prev_hdr);
  Texture2D<float4>   curr_buffer = DEREF(g_Srt.curr_hdr);

  RWTexture2D<float4> taa_buffer  = DEREF(g_Srt.taa);

  Texture2D<float2> curr_velocity_buffer = DEREF(g_Srt.curr_velocity);
  Texture2D<float2> prev_velocity_buffer = DEREF(g_Srt.prev_velocity);

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
  float2 prev_velocity         = prev_velocity_buffer.Sample(g_BilinearSamplerClamp, reproj_uv).xy;

  float  acceleration          = length(prev_velocity - curr_velocity);
  float  velocity_disocclusion = saturate((acceleration - 0.001f) * 10.0f);

  float3 prev_color            = luma_weight_color_rec709(sample_texture_catmull_rom(prev_buffer, reproj_uv).rgb);
  float3 curr_color            = luma_weight_color_rec709(curr_buffer[thread_id.xy].rgb);
  // NOTE(bshihabi): I see almost no difference between this and just regular clamping...
  prev_color                   = clip_aabb(curr_color, prev_color, min_color, max_color);
  float3 accumulation          = float3(0.9f * prev_color + 0.1f * curr_color);

  float3 resolve               = lerp(accumulation, curr_color, velocity_disocclusion);
  taa_buffer[thread_id.xy]     = float4(inverse_luma_weight_color_rec709(resolve), 1.0f);
}
