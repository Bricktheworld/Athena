#include "../root_signature.hlsli"
#include "../interlop.hlsli"

static const int kKernelRadius = 8;
//static const int kKernelCount = 17;

static const int kDilateSize = 8;

static const half kMaxCoC = 2.0h;

static const float4 kKernel0BracketsRealXY_ImZW = float4(-0.045884,1.097245,-0.033796,0.838935);
static const float2 kKernel0Weights_RealX_ImY = float2(0.411259,-0.548794);
static const float4 kKernel0_RealX_ImY_RealZ_ImW[] =
{
  float4(/*XY: Non Bracketed*/0.005645,0.020657,/*Bracketed WZ:*/0.046962,0.064907),
  float4(/*XY: Non Bracketed*/0.025697,-0.013190,/*Bracketed WZ:*/0.065237,0.024562),
  float4(/*XY: Non Bracketed*/-0.016100,-0.033796,/*Bracketed WZ:*/0.027145,0.000000),
  float4(/*XY: Non Bracketed*/-0.045884,0.008247,/*Bracketed WZ:*/0.000000,0.050114),
  float4(/*XY: Non Bracketed*/-0.017867,0.052848,/*Bracketed WZ:*/0.025534,0.103278),
  float4(/*XY: Non Bracketed*/0.030969,0.056175,/*Bracketed WZ:*/0.070042,0.107244),
  float4(/*XY: Non Bracketed*/0.063053,0.032363,/*Bracketed WZ:*/0.099282,0.078860),
  float4(/*XY: Non Bracketed*/0.074716,0.008899,/*Bracketed WZ:*/0.109911,0.050892),
  float4(/*XY: Non Bracketed*/0.076760,0.000000,/*Bracketed WZ:*/0.111774,0.040284),
  float4(/*XY: Non Bracketed*/0.074716,0.008899,/*Bracketed WZ:*/0.109911,0.050892),
  float4(/*XY: Non Bracketed*/0.063053,0.032363,/*Bracketed WZ:*/0.099282,0.078860),
  float4(/*XY: Non Bracketed*/0.030969,0.056175,/*Bracketed WZ:*/0.070042,0.107244),
  float4(/*XY: Non Bracketed*/-0.017867,0.052848,/*Bracketed WZ:*/0.025534,0.103278),
  float4(/*XY: Non Bracketed*/-0.045884,0.008247,/*Bracketed WZ:*/0.000000,0.050114),
  float4(/*XY: Non Bracketed*/-0.016100,-0.033796,/*Bracketed WZ:*/0.027145,0.000000),
  float4(/*XY: Non Bracketed*/0.025697,-0.013190,/*Bracketed WZ:*/0.065237,0.024562),
  float4(/*XY: Non Bracketed*/0.005645,0.020657,/*Bracketed WZ:*/0.046962,0.064907)
};
static const float4 kKernel1BracketsRealXY_ImZW = float4(-0.002843,0.595479,0.000000,0.189160);
static const float2 kKernel1Weights_RealX_ImY   = float2(0.513282,4.561110);
static const float4 kKernel1_RealX_ImY_RealZ_ImW[] =
{
  float4(/*XY: Non Bracketed*/-0.002843,0.003566,/*Bracketed WZ:*/0.000000,0.018854),
  float4(/*XY: Non Bracketed*/-0.001296,0.008744,/*Bracketed WZ:*/0.002598,0.046224),
  float4(/*XY: Non Bracketed*/0.004764,0.014943,/*Bracketed WZ:*/0.012775,0.078998),
  float4(/*XY: Non Bracketed*/0.016303,0.019581,/*Bracketed WZ:*/0.032153,0.103517),
  float4(/*XY: Non Bracketed*/0.032090,0.020162,/*Bracketed WZ:*/0.058664,0.106584),
  float4(/*XY: Non Bracketed*/0.049060,0.016015,/*Bracketed WZ:*/0.087162,0.084666),
  float4(/*XY: Non Bracketed*/0.063712,0.008994,/*Bracketed WZ:*/0.111767,0.047547),
  float4(/*XY: Non Bracketed*/0.073402,0.002575,/*Bracketed WZ:*/0.128041,0.013610),
  float4(/*XY: Non Bracketed*/0.076760,0.000000,/*Bracketed WZ:*/0.133679,0.000000),
  float4(/*XY: Non Bracketed*/0.073402,0.002575,/*Bracketed WZ:*/0.128041,0.013610),
  float4(/*XY: Non Bracketed*/0.063712,0.008994,/*Bracketed WZ:*/0.111767,0.047547),
  float4(/*XY: Non Bracketed*/0.049060,0.016015,/*Bracketed WZ:*/0.087162,0.084666),
  float4(/*XY: Non Bracketed*/0.032090,0.020162,/*Bracketed WZ:*/0.058664,0.106584),
  float4(/*XY: Non Bracketed*/0.016303,0.019581,/*Bracketed WZ:*/0.032153,0.103517),
  float4(/*XY: Non Bracketed*/0.004764,0.014943,/*Bracketed WZ:*/0.012775,0.078998),
  float4(/*XY: Non Bracketed*/-0.001296,0.008744,/*Bracketed WZ:*/0.002598,0.046224),
  float4(/*XY: Non Bracketed*/-0.002843,0.003566,/*Bracketed WZ:*/0.000000,0.018854)
};

float4 compute_c0_xy_c1_zw(float input, int kernel_index)
{
  float2 c0 = kKernel0_RealX_ImY_RealZ_ImW[kernel_index + kKernelRadius].xy;
  float2 c1 = kKernel1_RealX_ImY_RealZ_ImW[kernel_index + kKernelRadius].xy;

  float2 input2 = float2(input, input);

  float4 ret;
  ret.xy = input2 * c0;
  ret.zw = input2 * c1;

  return ret;
}

float2 mult_complex(float2 p, float2 q)
{
  // (Pr+Pi)*(Qr+Qi) = (Pr*Qr+Pr*Qi+Pi*Qr-Pi*Qi)
  return float2(p.x * q.x - p.y * q.y, p.x * q.y + p.y * q.x);
}

ConstantBuffer<DofBlurHorizSrt> g_BlurHorizSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFBlurHorizontal( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>  color_buffer       = DEREF(g_BlurHorizSrt.color_buffer);
  Texture2D<float2>  coc_buffer         = DEREF(g_BlurHorizSrt.coc_buffer);

  RWTexture2D<float4> red_near_target   = DEREF(g_BlurHorizSrt.red_near_target);
  RWTexture2D<float4> blue_near_target  = DEREF(g_BlurHorizSrt.blue_near_target);
  RWTexture2D<float4> green_near_target = DEREF(g_BlurHorizSrt.green_near_target);

  RWTexture2D<float4> red_far_target    = DEREF(g_BlurHorizSrt.red_far_target);
  RWTexture2D<float4> blue_far_target   = DEREF(g_BlurHorizSrt.blue_far_target);
  RWTexture2D<float4> green_far_target  = DEREF(g_BlurHorizSrt.green_far_target);

  float2 in_res;
  color_buffer.GetDimensions(in_res.x, in_res.y);

  float2 out_res;
  red_near_target.GetDimensions(out_res.x, out_res.y);

  float2 uv_step = float2(1.0f, 1.0f)   / in_res;
  float2 uv      = float2(thread_id.xy) / out_res;

  bool is_near = thread_id.z == 0;
  half filter_radius = is_near ? coc_buffer.Sample(g_BilinearSampler, uv).x : coc_buffer.Sample(g_BilinearSampler, uv).y;

  half4 red_component   = half4(0.0h, 0.0h, 0.0h, 0.0h);
  half4 green_component = half4(0.0h, 0.0h, 0.0h, 0.0h);
  half4 blue_component  = half4(0.0h, 0.0h, 0.0h, 0.0h);
  for (int i = -kKernelRadius; i <= kKernelRadius; i++)
  {
    float2 sample_uv    = uv + uv_step / 4.0f * float2((float)i, 0.0f) * filter_radius;
    float3 sample_color = color_buffer.Sample(g_BilinearSampler, sample_uv).rgb;

    half2  sample_coc_pair = coc_buffer.Sample(g_BilinearSampler, sample_uv);
    half   sample_coc   = is_near ? sample_coc_pair.x : sample_coc_pair.y;

    // TODO(Brandon): There's an annoying pop-in halo effect that occurs very abruptly when objects move from
    // the focus range into the far plane. This is caused by _very_ far pixels immediately going from not
    // sampling objects that are in focus to doing a full blur (since they are very far they have a large CoC).
    // The fix is to do some sort of multiplier that is dependent on the difference between CoCs. The idea being,
    // if a very far and large CoC is sampling one with a very small CoC, it shouldn't have as much of an impact.
    // Another way of doing this as described in the original paper is to simply multiply by the CoC,
    // but this results in a much more ugly darkening effect at the boundaries between near, focus, and far.
    // This is kinda a complex problem so I'm not entirely sure how to solve it. It's just not worth my time
    // right now to solve :P
    half   multiplier		= 1.0h; //clamp(sample_coc, 0.0h, 1.0h);
    if (!is_near && sample_coc <= 0.0h)
    {
      multiplier = 0.0h;
    }


    red_component      += compute_c0_xy_c1_zw(sample_color.r * multiplier, i);
    green_component    += compute_c0_xy_c1_zw(sample_color.g * multiplier, i);
    blue_component     += compute_c0_xy_c1_zw(sample_color.b * multiplier, i);
  }

  if (is_near)
  {
    red_near_target  [thread_id.xy] = red_component;
    green_near_target[thread_id.xy] = green_component;
    blue_near_target [thread_id.xy] = blue_component;
  }
  else
  {
    red_far_target  [thread_id.xy] = red_component;
    green_far_target[thread_id.xy] = green_component;
    blue_far_target [thread_id.xy] = blue_component;
  }
}

ConstantBuffer<DofBlurVertSrt> g_BlurVertSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFBlurVertical( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float2>   coc_buffer          = DEREF(g_BlurVertSrt.coc_buffer);
                                        
  Texture2D<float4>   red_near_buffer     = DEREF(g_BlurVertSrt.red_near_buffer);
  Texture2D<float4>   green_near_buffer   = DEREF(g_BlurVertSrt.green_near_buffer);
  Texture2D<float4>   blue_near_buffer    = DEREF(g_BlurVertSrt.blue_near_buffer);
                                        
  Texture2D<float4>   red_far_buffer      = DEREF(g_BlurVertSrt.red_far_buffer);
  Texture2D<float4>   green_far_buffer    = DEREF(g_BlurVertSrt.green_far_buffer);
  Texture2D<float4>   blue_far_buffer     = DEREF(g_BlurVertSrt.blue_far_buffer);
                                            
  RWTexture2D<float3> blurred_near_target = DEREF(g_BlurVertSrt.blurred_near_target);
  RWTexture2D<float3> blurred_far_target  = DEREF(g_BlurVertSrt.blurred_far_target);

  float2 coc_res;
  coc_buffer.GetDimensions(coc_res.x, coc_res.y);

  float2 out_res;
  blurred_near_target.GetDimensions(out_res.x, out_res.y);

  float2 uv_step = float2(1.0f, 1.0f)   / out_res;
  float2 uv      = float2(thread_id.xy) / out_res;
 
  bool is_near = thread_id.z == 0;
  half filter_radius = is_near ? coc_buffer.Sample(g_BilinearSampler, uv).x : coc_buffer.Sample(g_BilinearSampler, uv).y;

  half4 red_component   = half4(0.0h, 0.0h, 0.0h, 0.0h);
  half4 green_component = half4(0.0h, 0.0h, 0.0h, 0.0h);
  half4 blue_component  = half4(0.0h, 0.0h, 0.0h, 0.0h);
  for (int i = -kKernelRadius; i <= kKernelRadius; i++)
  {
    float2 sample_uv  = uv + uv_step / 4.0f * float2(0.0f, (float)i) * filter_radius;

    half4 sample_red, sample_green, sample_blue;
    if (is_near)
    {
      sample_red   = red_near_buffer.Sample(g_BilinearSampler, sample_uv);
      sample_green = green_near_buffer.Sample(g_BilinearSampler, sample_uv);
      sample_blue  = blue_near_buffer.Sample(g_BilinearSampler, sample_uv);
    }
    else
    {
      sample_red   = red_far_buffer.Sample(g_BilinearSampler, sample_uv);
      sample_green = green_far_buffer.Sample(g_BilinearSampler, sample_uv);
      sample_blue  = blue_far_buffer.Sample(g_BilinearSampler, sample_uv);
    }

    float2 c0 = kKernel0_RealX_ImY_RealZ_ImW[i + kKernelRadius].xy;
    float2 c1 = kKernel1_RealX_ImY_RealZ_ImW[i + kKernelRadius].xy;

    red_component.xy += mult_complex(sample_red.xy, c0);
    red_component.zw += mult_complex(sample_red.zw, c1);

    green_component.xy += mult_complex(sample_green.xy, c0);
    green_component.zw += mult_complex(sample_green.zw, c1);

    blue_component.xy += mult_complex(sample_blue.xy, c0);
    blue_component.zw += mult_complex(sample_blue.zw, c1);
  }

  float3 output_color;
  output_color.r = dot(red_component.xy, kKernel0Weights_RealX_ImY) + dot(red_component.zw, kKernel1Weights_RealX_ImY);
  output_color.g = dot(green_component.xy, kKernel0Weights_RealX_ImY) + dot(green_component.zw, kKernel1Weights_RealX_ImY);
  output_color.b = dot(blue_component.xy, kKernel0Weights_RealX_ImY) + dot(blue_component.zw, kKernel1Weights_RealX_ImY);
  if (is_near)
  {
    blurred_near_target[thread_id.xy] = output_color;
  }
  else
  {
    blurred_far_target[thread_id.xy] = output_color;
  }
}

ConstantBuffer<DofCocSrt> g_CoCSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFCoC( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   color_buffer  = DEREF(g_CoCSrt.color_buffer);
  Texture2D<float>    depth_buffer  = DEREF(g_CoCSrt.depth_buffer);

  RWTexture2D<float2> render_target = DEREF(g_CoCSrt.render_target);

  float z_near       = g_CoCSrt.z_near;
  float aperture     = g_CoCSrt.aperture;
  float focal_dist   = g_CoCSrt.focal_dist;
  float focal_range  = g_CoCSrt.focal_range;
  float z            = z_near / depth_buffer[thread_id.xy];

  if (z > focal_dist)
  {
    z = focal_dist + max(0, z - focal_dist - focal_range);
  }

  float coc = (1.0f - focal_dist / z) * 0.7f * aperture;

  render_target[thread_id.xy] = half2(clamp(-coc, 0.0h, kMaxCoC), clamp(coc, 0.0h, kMaxCoC));
}

ConstantBuffer<DofCoCDilateSrt> g_CoCDilateSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFCoCDilate( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float2>   coc_buffer    = DEREF(g_CoCDilateSrt.coc_buffer);
  RWTexture2D<float2> render_target = DEREF(g_CoCDilateSrt.render_target);

  float2 resolution;
  coc_buffer.GetDimensions(resolution.x, resolution.y);

  float2 uv_step = float2(1.0f, 1.0f) / resolution;
  float2 uv      = thread_id.xy       / resolution;

  half max_near_coc = 0.0h;
  for (int x = -kDilateSize; x <= kDilateSize; x++)
  {
    for (int y = -kDilateSize; y <= kDilateSize; y++)
    {
      float2 sample_uv  = uv + uv_step * float2((float)x, (float)y);
      half sample_coc = coc_buffer.Sample(g_BilinearSampler, sample_uv).x;
      max_near_coc = max(max_near_coc, sample_coc);
    }
  }

  render_target[thread_id.xy] = half2(max_near_coc, coc_buffer.Sample(g_BilinearSampler, uv).y);
}

ConstantBuffer<DofCompositeSrt> g_CompositeSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_DoFComposite( uint3 thread_id : SV_DispatchThreadID )
{
  Texture2D<float4>   color_buffer  = DEREF(g_CompositeSrt.color_buffer);
            
  Texture2D<float2>   coc_buffer    = DEREF(g_CompositeSrt.coc_buffer);
  Texture2D<float3>   near_buffer   = DEREF(g_CompositeSrt.near_buffer);
  Texture2D<float3>   far_buffer    = DEREF(g_CompositeSrt.far_buffer);

  RWTexture2D<float3> render_target = DEREF(g_CompositeSrt.render_target);

  float width, height;
  render_target.GetDimensions(width, height);
  float2 uv = float2(thread_id.xy) / float2(width, height);

  // NOTE(Brandon): The color buffer is expected to be the same dimensions as the render target
  half3 color = color_buffer[thread_id.xy].rgb;

  half2 coc   = coc_buffer[thread_id.xy];
  half3 near  = near_buffer.Sample(g_BilinearSampler, uv).rgb;
  half3 far   = far_buffer.Sample(g_BilinearSampler, uv).rgb;

  half3 far_blend = lerp(color, far, clamp(coc.y, 0.0h, 1.0h));
  render_target[thread_id.xy] = half3(lerp(far_blend, near, clamp(coc.x, 0.0h, 1.0h)));
}