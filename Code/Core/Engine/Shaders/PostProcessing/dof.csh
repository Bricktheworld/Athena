#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"

static const int kKernelRadius = 8;

static const int kDilateSize = 3;

static const float kMaxCoC = 10.0f;

ConstantBuffer<DofCocSrt> g_CoCSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFCoC( uint2 thread_id : SV_DispatchThreadID )
{
  Texture2D<float>   depth_buffer = DEREF(g_CoCSrt.depth_buffer);

  RWTexture2D<float> coc_buffer   = DEREF(g_CoCSrt.coc_buffer);

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

  coc_buffer[thread_id] = min(abs(coc), kMaxCoC);
}

ConstantBuffer<DoFBokehBlurSrt> g_BokehBlurSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFBokehBlur(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float>    depth_buffer = DEREF(g_BokehBlurSrt.depth_buffer);
  Texture2D<float>    coc_buffer   = DEREF(g_BokehBlurSrt.coc_buffer);
  Texture2D<float4>   hdr_buffer   = DEREF(g_BokehBlurSrt.hdr_buffer);
  RWTexture2D<float4> blur_buffer  = DEREF(g_BokehBlurSrt.blur_buffer);

  float z_near = g_BokehBlurSrt.z_near;

  float2 half_res;
  blur_buffer.GetDimensions(half_res.x, half_res.y);

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

  float coc         = max(coc_buffer[thread_id], 0.0f);

  float depth       = z_near / depth_buffer.Sample(g_BilinearSampler, full_res_uv);

  float radius_step = (blur_radius * blur_radius) / (2.0f * sample_count);

  float radius      = radius_step;

  float3 accum  = hdr_buffer[thread_id * kDoFResolutionScale].rgb;
  float3 total = 1.0f;
  accum *= total;

  float2 sample_dir       = float2(1.0f, 0.0f);
  float2 full_res_uv_step = 1.0f / full_res;

  for (uint i = 0; i < sample_count; i++)
  {
    // Sample in a sunflower pattern using the golden ratio (creates a disk sampling kernel)
    sample_dir            = mul(kGoldenRotation, sample_dir);
    float2 sample_uv      = clamp(uv + sample_dir * full_res_uv_step * radius, 0.0f, 1.0f);

    // Sample the color/depth at this location
    float3 sample_color   = hdr_buffer.Sample(g_BilinearSampler, sample_uv).rgb;
    float  sample_depth   = z_near / depth_buffer.Sample(g_BilinearSampler, sample_uv);

    float  sample_coc     = coc_buffer.Sample(g_BilinearSampler, sample_uv);


    float  sample_weight  = sample_coc / kMaxCoC * blur_radius;
    float  center_weight  = coc        / kMaxCoC * blur_radius;
    if (sample_depth > depth)
    {
      sample_weight       = clamp(sample_weight, 0.0f, center_weight);
    }

    float m = smoothstep(radius - radius_step, radius + radius_step, sample_weight);
    accum += lerp(accum / total, sample_color, m);
    total += 1.0f;

    radius += radius_step / radius;
  }

  blur_buffer[thread_id] = float4(accum / total, 1.0f);
}


ConstantBuffer<DoFCompositeSrt> g_CompositeSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFComposite(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   hdr_buffer    = DEREF(g_CompositeSrt.hdr_buffer);
            
  Texture2D<float>    coc_buffer    = DEREF(g_CompositeSrt.coc_buffer);
  Texture2D<float4>   blur_buffer   = DEREF(g_CompositeSrt.blur_buffer);

  RWTexture2D<float4> render_target = DEREF(g_CompositeSrt.render_target);

  float2 full_res;
  render_target.GetDimensions(full_res.x, full_res.y);

  float2 uv = float2(thread_id) / full_res;

  // NOTE(Brandon): The color buffer is expected to be the same dimensions as the render target
  float3 unblurred = hdr_buffer[thread_id].rgb;

  float  coc       = coc_buffer[thread_id / kDoFResolutionScale];

#if 0
  float2 half_res;
  blur_buffer.GetDimensions(half_res.x, half_res.y);

  float2 uv_step_half = 1.0f / half_res;
  float2 near_uv0   = uv - uv_step_half / 2.0f;
  float2 near_uv1   = float2(near_uv0.x + uv_step_half.x, near_uv0.y                 );
  float2 near_uv2   = float2(near_uv0.x,                  near_uv0.y + uv_step_half.y);
  float2 near_uv3   = float2(near_uv0.x + uv_step_half.x, near_uv0.y + uv_step_half.y);

  float3 blurred = 0.0f;
  blurred       += blur_buffer.Sample(g_BilinearSampler, near_uv0).rgb;
  blurred       += blur_buffer.Sample(g_BilinearSampler, near_uv1).rgb;
  blurred       += blur_buffer.Sample(g_BilinearSampler, near_uv2).rgb;
  blurred       += blur_buffer.Sample(g_BilinearSampler, near_uv3).rgb;

  blurred /= 4.0f;
#endif

  float3 blurred = blur_buffer.Sample(g_BilinearSampler, uv).rgb;

  render_target[thread_id] = float4(blurred, 1.0f); // float4(lerp(far_blend, blurred, clamp(coc.x, 0.0f, 1.0f)), 1.0f);
}
