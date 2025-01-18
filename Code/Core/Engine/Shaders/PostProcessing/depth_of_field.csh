#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/math.hlsli"

static const int kKernelRadius = 8;

static const int kDilateSize = 3;

static const float kMaxCoC = 10.0f;

ConstantBuffer<DoFCocSrt> g_CoCSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFCoC( uint2 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   hdr_buffer   = DEREF(g_CoCSrt.hdr_buffer);
  Texture2D<float>    depth_buffer = DEREF(g_CoCSrt.depth_buffer);

  RWTexture2D<float4> coc_buffer   = DEREF(g_CoCSrt.coc_buffer);

  float2 resolution;
  depth_buffer.GetDimensions(resolution.x, resolution.y);

  float2 full_res_uv = float2(thread_id * kDoFResolutionScale + 1) / resolution;

  float z_near       = g_CoCSrt.z_near;
  float aperture     = g_CoCSrt.aperture;
  float focal_dist   = g_CoCSrt.focal_dist;
  float focal_range  = g_CoCSrt.focal_range;
  float z            = z_near / depth_buffer.Sample(g_BilinearSampler, full_res_uv);

  float3 color       = hdr_buffer.Sample(g_BilinearSampler, full_res_uv).rgb;

  if (z > focal_dist)
  {
    z = focal_dist + max(0, z - focal_dist - focal_range);
  }

  float coc = (1.0f - focal_dist / z) * 0.7f * aperture;

  coc_buffer[thread_id] = float4(color, min(abs(coc), kMaxCoC));
}

ConstantBuffer<DoFBokehBlurSrt> g_BokehBlurSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFBokehBlur(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float>    depth_buffer = DEREF(g_BokehBlurSrt.depth_buffer);
  Texture2D<float4>   coc_buffer   = DEREF(g_BokehBlurSrt.coc_buffer);
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

  float coc         = max(coc_buffer[thread_id].a, 0.0f);

  float depth       = z_near / depth_buffer.Sample(g_BilinearSampler, full_res_uv);

  float radius_step = (blur_radius * blur_radius) / (2.0f * sample_count);

  float radius      = radius_step;

  float  accum_sample_weight = 0.0f;
  float3 accum_color         = coc_buffer[thread_id].rgb;
  float  total = 1.0f;

  float2 sample_dir       = float2(1.0f, 0.0f);
  float2 full_res_uv_step = 1.0f / full_res;

  for (uint i = 0; i < sample_count; i++)
  {
    // Sample in a sunflower pattern using the golden ratio (creates a disk sampling kernel)
    sample_dir            = mul(kGoldenRotation, sample_dir);
    float2 sample_uv      = clamp(uv + sample_dir * full_res_uv_step * radius, 0.0f, 1.0f);

    // Sample the color/depth/CoC at this location
    float3 sample_color   = hdr_buffer.Sample(g_BilinearSampler, sample_uv).rgb;
    float  sample_depth   = z_near / depth_buffer.Sample(g_BilinearSampler, sample_uv);
    float  sample_coc     = coc_buffer.Sample(g_BilinearSampler, sample_uv).a;

    // We're going to weight the sample we just got based on its CoC
    float  sample_weight  = sample_coc / kMaxCoC * blur_radius;
    float  center_weight  = coc        / kMaxCoC * blur_radius;
    
    // If the sample is behind us, that means that it shouldn't bleed very much into us
    // That is because background objects don't blur over the foreground very much
    // What DOES happen is that blurred foreground objects appear semi transparent,
    // and that's why this isn't just a weight of 0.0f.
    if (sample_depth > depth)
    {
      // We want foreground objects to be semi transparent like in real photos by mixing in
      // some more of the background into the foreground
      float kHackTranslucentForeground = 2.0f;
      sample_weight = clamp(sample_weight, 0.0f, kHackTranslucentForeground * center_weight);
    }

    float m = smoothstep(radius - radius_step, radius + radius_step, sample_weight);
    accum_color         += lerp(accum_color / total, sample_color, m);
    accum_sample_weight += sample_weight;
    total += 1.0f;

    radius += radius_step / radius;
  }

  blur_buffer[thread_id] = float4(accum_color / total, accum_sample_weight / total);
}


ConstantBuffer<DoFCompositeSrt> g_CompositeSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFComposite(uint2 thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   hdr_buffer    = DEREF(g_CompositeSrt.hdr_buffer);
            
  Texture2D<float4>   coc_buffer    = DEREF(g_CompositeSrt.coc_buffer);
  Texture2D<float4>   blur_buffer   = DEREF(g_CompositeSrt.blur_buffer);

  RWTexture2D<float4> render_target = DEREF(g_CompositeSrt.render_target);

  float2 full_res;
  render_target.GetDimensions(full_res.x, full_res.y);

  float2 half_res;
  blur_buffer.GetDimensions(half_res.x, half_res.y);

  float2 uv = float2(thread_id) / full_res;

  // NOTE(Brandon): The color buffer is expected to be the same dimensions as the render target
  float3 unblurred = hdr_buffer[thread_id].rgb;

#if 1

  float blur_amount = blur_buffer[thread_id / kDoFResolutionScale].a;
  // 16 tap tent filter
  static const float2 kOffsets[16] =
  {
    float2(-4.0f / 3.0f, -4.0f / 3.0f),
    float2(-1.0f / 3.0f, -4.0f / 3.0f),
    float2( 1.0f / 3.0f, -4.0f / 3.0f),
    float2( 4.0f / 3.0f, -4.0f / 3.0f),

    float2(-4.0f / 3.0f, -1.0f / 3.0f),
    float2(-1.0f / 3.0f, -1.0f / 3.0f),
    float2( 1.0f / 3.0f, -1.0f / 3.0f),
    float2( 4.0f / 3.0f, -1.0f / 3.0f),

    float2(-4.0f / 3.0f,  1.0f / 3.0f),
    float2(-1.0f / 3.0f,  1.0f / 3.0f),
    float2( 1.0f / 3.0f,  1.0f / 3.0f),
    float2( 4.0f / 3.0f,  1.0f / 3.0f),

    float2(-4.0f / 3.0f,  4.0f / 3.0f),
    float2(-1.0f / 3.0f,  4.0f / 3.0f),
    float2( 1.0f / 3.0f,  4.0f / 3.0f),
    float2( 4.0f / 3.0f,  4.0f / 3.0f)
  };

  float4 blurred = 0.0f;

  for (uint i = 0; i < 16; i++)
  {
    float2 offset    = kOffsets[i] * lerp(0.0f, 0.5f, saturate(blur_amount));
    float2 sample_uv = uv + offset / half_res;

    blurred += blur_buffer.Sample(g_BilinearSampler, sample_uv);
  }

  blurred /= 16.0f;
#endif

#if 0
  // 4 tap tent filter
  static const float2 kOffsets[4] =
  {
    float2(-1.0f / 2.0f, -1.0f / 2.0f),
    float2( 1.0f / 2.0f, -1.0f / 2.0f),
    float2(-1.0f / 2.0f,  1.0f / 2.0f),
    float2( 1.0f / 2.0f,  1.0f / 2.0f)
  };

  float4 blurred = 0.0f;

  for (uint i = 0; i < 4; i++)
  {
    float2 offset    = kOffsets[i];
    float2 sample_uv = uv + offset / half_res;

    blurred += blur_buffer.Sample(g_BilinearSampler, sample_uv);
  }

  blurred *= 0.25f;
#endif
#if 0
  float4 blurred = blur_buffer.Sample(g_BilinearSampler, uv);
#endif

  render_target[thread_id] = float4(lerp(unblurred, blurred.rgb, saturate(blurred.a)), 1.0f); // float4(lerp(far_blend, blurred, clamp(coc.x, 0.0f, 1.0f)), 1.0f);
}
