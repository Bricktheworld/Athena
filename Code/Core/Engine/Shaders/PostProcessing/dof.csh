#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"

static const int kKernelRadius = 8;

static const int kDilateSize = 2;

static const float kMaxCoC = 4.0;

ConstantBuffer<DofCocSrt> g_CoCSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFCoC( uint2 thread_id : SV_DispatchThreadID )
{
  Texture2D<float>    depth_buffer = DEREF(g_CoCSrt.depth_buffer);

  RWTexture2D<float2> coc_buffer   = DEREF(g_CoCSrt.coc_buffer);

  float2 resolution;
  depth_buffer.GetDimensions(resolution.x, resolution.y);

  float2 full_res_uv = float2(thread_id * kDoFResolutionScale) / resolution;

  float z_near       = g_CoCSrt.z_near;
  float aperture     = g_CoCSrt.aperture;
  float focal_dist   = g_CoCSrt.focal_dist;
  float focal_range  = g_CoCSrt.focal_range;
  float z            = z_near / depth_buffer.Sample(g_BilinearSampler, full_res_uv);

  if (z > focal_dist)
  {
    z = focal_dist + max(0, z - focal_dist - focal_range);
  }

  float coc = (1.0f - focal_dist / z) * 0.7f * aperture;

  coc_buffer[thread_id.xy] = float2(clamp(-coc, 0.0f, kMaxCoC), clamp(coc, 0.0f, kMaxCoC));
}

ConstantBuffer<DofCoCDilateSrt> g_CoCDilateSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFCoCDilate(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float2>   coc_buffer        = DEREF(g_CoCDilateSrt.coc_buffer);
  RWTexture2D<float2> coc_dilate_buffer = DEREF(g_CoCDilateSrt.coc_dilate_buffer);

  float2 resolution;
  coc_dilate_buffer.GetDimensions(resolution.x, resolution.y);

  float max_near_coc = 0.0;
  for (int x = -kDilateSize; x <= kDilateSize; x++)
  {
    for (int y = -kDilateSize; y <= kDilateSize; y++)
    {
      int2 sample      = thread_id + int2(x, y);
      sample           = clamp(sample, 0, int2(resolution));
      float sample_coc = coc_buffer[(uint2)sample].x;
      max_near_coc     = max(max_near_coc, sample_coc);
    }
  }

  coc_dilate_buffer[thread_id] = float2(max_near_coc, coc_buffer[thread_id].y);
}

ConstantBuffer<DoFBokehBlurSrt> g_BokehBlurSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFBokehBlur(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float>    depth_buffer      = DEREF(g_BokehBlurSrt.depth_buffer);
  Texture2D<float2>   coc_dilate_buffer = DEREF(g_BokehBlurSrt.coc_dilate_buffer);
  Texture2D<float4>   hdr_buffer        = DEREF(g_BokehBlurSrt.hdr_buffer);
  RWTexture2D<float4> blurred           = DEREF(g_BokehBlurSrt.blurred);

  float z_near = g_BokehBlurSrt.z_near;

  float2 half_res;
  blurred.GetDimensions(half_res.x, half_res.y);

  float2 uv = float2(thread_id.xy) / half_res;

  float2 full_res;
  hdr_buffer.GetDimensions(full_res.x, full_res.y);

  float2 full_res_uv = float2(thread_id.xy * kDoFResolutionScale) / full_res;

  uint sample_count = g_BokehBlurSrt.sample_count;
  f32  blur_radius  = g_BokehBlurSrt.blur_radius;

  static const float2x2 kGoldenRotation =
  {
     cos(kGoldenAngle), sin(kGoldenAngle),
    -sin(kGoldenAngle), cos(kGoldenAngle),
  };

  float near_coc     = max(coc_dilate_buffer[thread_id].r, 0.0f) / 1.5f;
  // float depth        = z_near / depth_buffer.Sample(full_res_uv);

  float r = 1.0f;

  float3 accum  = hdr_buffer[thread_id * kDoFResolutionScale].rgb;
  float3 weight = pow(accum, 4.0f);
  accum *= weight;

  float2 sample_dir = float2(0.0f, blur_radius * near_coc * 0.01f / sqrt((float)sample_count));


  for (uint i = 0; i < sample_count; i++)
  {
    r += 1.0f / r;
    sample_dir = mul(kGoldenRotation, sample_dir);

    float2 sample_uv     = uv + (r - 1.0f) * sample_dir;

    float3 sample_color  = hdr_buffer.Sample(g_BilinearSampler, sample_uv).rgb;
    float3 sample_bokeh  = pow(sample_color, 4.0f);

    float  sample_dist     = length(sample_uv - uv);
    float  near_sample_coc = coc_dilate_buffer.Sample(g_BilinearSampler, sample_uv).r;
    float  sample_weight   = sample_dist < near_sample_coc ? 1.0f - (sample_dist / blur_radius) : 0.0f;
    accum  += sample_weight * sample_color * sample_bokeh;
    weight += sample_weight * sample_bokeh;
  }

#if 0
  float4 unblurred = hdr_buffer.Sample(g_BilinearSampler, full_res_uv + 1.0f);
  blurred[thread_id] = near_coc < 0.01f ? unblurred : lerp(unblurred, float4(accum / weight, 1.0f), near_coc);
#endif
  blurred[thread_id] = float4(accum / weight, 1.0f);
}


ConstantBuffer<DoFCompositeSrt> g_CompositeSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFComposite(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   hdr_buffer       = DEREF(g_CompositeSrt.hdr_buffer);
            
  Texture2D<float2>   coc_buffer       = DEREF(g_CompositeSrt.coc_dilate_buffer);
  Texture2D<float4>   near_blur_buffer = DEREF(g_CompositeSrt.near_blur_buffer);

  RWTexture2D<float4> render_target    = DEREF(g_CompositeSrt.render_target);

  float2 full_res;
  render_target.GetDimensions(full_res.x, full_res.y);

  float2 uv = float2(thread_id) / full_res;

  // NOTE(Brandon): The color buffer is expected to be the same dimensions as the render target
  float3 unblurred = hdr_buffer[thread_id].rgb;

  float2 coc       = coc_buffer[thread_id / kDoFResolutionScale];

#if 1
  float2 half_res;
  near_blur_buffer.GetDimensions(half_res.x, half_res.y);

  float2 uv_step_half = 1.0f / half_res;
  float2 near_uv0   = uv - uv_step_half / 2.0f;
  float2 near_uv1   = float2(near_uv0.x + uv_step_half.x, near_uv0.y                 );
  float2 near_uv2   = float2(near_uv0.x,                  near_uv0.y + uv_step_half.y);
  float2 near_uv3   = float2(near_uv0.x + uv_step_half.x, near_uv0.y + uv_step_half.y);

  float3 near = 0.0f;
  near       += near_blur_buffer.Sample(g_BilinearSampler, near_uv0).rgb;
  near       += near_blur_buffer.Sample(g_BilinearSampler, near_uv1).rgb;
  near       += near_blur_buffer.Sample(g_BilinearSampler, near_uv2).rgb;
  near       += near_blur_buffer.Sample(g_BilinearSampler, near_uv3).rgb;

  near /= 4.0f;
#endif

  float3 near_unfiltered = near_blur_buffer.Sample(g_BilinearSampler, uv).rgb;

  if (uv.x > 0.5f)
  {
    near = near_unfiltered;
  }

  // float3 far_blend = lerp(unblurred, far, clamp(coc.y, 0.0h, 1.0h));
  render_target[thread_id] = float4(lerp(unblurred, near, clamp(coc.x, 0.0f, 1.0f)), 1.0f);
}
