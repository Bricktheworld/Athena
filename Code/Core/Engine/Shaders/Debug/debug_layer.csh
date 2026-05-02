#include "../interlop.hlsli"
#include "../root_signature.hlsli"

#include "../Include/debug_common.hlsli"

ConstantBuffer<DebugNormalSrt> g_DebugNormalSrt : register(b0);

[numthreads(8, 8, 1)]
void CS_DebugNormals(uint2 global_thread_id : SV_DispatchThreadID)
{
  Texture2D<float4>   gbuffer_normal_roughness = DEREF(g_DebugNormalSrt.gbuffer_normal_roughness);
  RWTexture2D<float4> dst                      = DEREF(g_DebugNormalSrt.dst);

  uint2 dimensions;
  dst.GetDimensions(dimensions.x, dimensions.y);


  if (all(global_thread_id >= dimensions))
  {
    return;
  }

  float3 normal = gbuffer_normal_roughness[global_thread_id].xyz;

  float3 debug_normal = normal * 0.5f + 0.5f;

  dst[global_thread_id] = float4(debug_normal, 1.0f);
}

ConstantBuffer<DebugDepthSrt> g_DebugDepthSrt : register(b0);
[numthreads(8, 8, 1)]
void CS_DebugDepth(uint2 global_thread_id : SV_DispatchThreadID)
{
  Texture2D<float>    gbuffer_depth = DEREF(g_DebugDepthSrt.gbuffer_depth);
  RWTexture2D<float4> dst           = DEREF(g_DebugDepthSrt.dst);

  uint2 dimensions;
  dst.GetDimensions(dimensions.x, dimensions.y);

  if (all(global_thread_id >= dimensions))
  {
    return;
  }

  float depth = gbuffer_depth[global_thread_id];

  float linear_depth = kZNear / depth;
  linear_depth /= 200.0f;

  float tonemapped_depth = pow(linear_depth, 1.0f / 2.2f);

  dst[global_thread_id] = float4(tonemapped_depth, 0.0f, 0.0f, 1.0f);
}

ConstantBuffer<DebugGiVarianceSrt> g_DebugGiVarianceSrt : register(b0);
[numthreads(8, 8, 1)]
void CS_DebugGiVariance(uint2 global_thread_id : SV_DispatchThreadID)
{
  Texture2D<float>                 gbuffer_depth            = DEREF(g_DebugGiVarianceSrt.gbuffer_depth);
  Texture2D<float4>                gbuffer_normal_roughness = DEREF(g_DebugGiVarianceSrt.gbuffer_normal_roughness);
  Texture2D<float4>                lighting                 = DEREF(g_DebugGiVarianceSrt.lighting);

  StructuredBuffer<DiffuseGiProbe> diffuse_gi_probes        = DEREF(g_DebugGiVarianceSrt.diffuse_gi_probes);
  Texture2DArray<u32>              diffuse_gi_page_table    = DEREF(g_DebugGiVarianceSrt.diffuse_gi_page_table);
  RWTexture2D<float4>              dst                      = DEREF(g_DebugGiVarianceSrt.dst);

  float2 screen_size;
  gbuffer_depth.GetDimensions(screen_size.x, screen_size.y);

  if (all(global_thread_id >= screen_size))
  {
    return;
  }

  float  depth   = gbuffer_depth[global_thread_id];
  float3 ws_pos  = screen_to_world(float3(float2(global_thread_id) + 0.5f, depth), screen_size).xyz;
  float3 normal  = gbuffer_normal_roughness[global_thread_id].xyz;

  float  vbbr = get_indirect_vbbr(ws_pos, normal, diffuse_gi_page_table, diffuse_gi_probes);

  dst[global_thread_id] = float4(lerp(float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), vbbr), 1.0f);

}