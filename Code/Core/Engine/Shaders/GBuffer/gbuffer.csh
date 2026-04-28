#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/gbuffer_common.hlsli"
#include "../Include/debug_draw.hlsli"

ConstantBuffer<GBufferFillIndirectArgsPhaseOneSrt> g_FillArgsPhaseOneSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_GBufferFillMultiDrawIndirectArgsPhaseOne(
  uint wave_id          : SV_GroupID,
  uint local_thread_id  : SV_GroupThreadID,
  uint global_thread_id : SV_DispatchThreadID
) {
  if (global_thread_id >= g_FillArgsPhaseOneSrt.scene_obj_count)
  {
    return;
  }

  RWStructuredBuffer<MultiDrawIndirectIndexedArgs> dst_args    = DEREF(g_FillArgsPhaseOneSrt.multi_draw_args);
  StructuredBuffer<u64>                            occlusion   = DEREF(g_FillArgsPhaseOneSrt.scene_obj_occlusion);
  RWStructuredBuffer<u32>                          dst_gpu_ids = DEREF(g_FillArgsPhaseOneSrt.scene_obj_gpu_ids);

  bool        visible = (~occlusion[wave_id]) & (1ULL << local_thread_id);

  u32         gpu_id  = global_thread_id;

  if (!visible && !g_RenderSettings.disable_occlusion_culling)
  {
    return;
  }

  SceneObjGpu obj = g_SceneObjs[gpu_id];

  MultiDrawIndirectIndexedArgs args;
  args.instance_count           = 1;
  args.index_count_per_instance = obj.index_count;
  args.start_index_location     = obj.start_index;
  args.base_vertex_location     = 0;
  args.start_instance_location  = 0;

  ;
  uint dst_idx = dst_args.IncrementCounter();
  dst_args   [dst_idx] = args;
  dst_gpu_ids[dst_idx] = gpu_id;
}

ConstantBuffer<GBufferFillIndirectArgsPhaseTwoSrt> g_FillArgsPhaseTwoSrt : register(b0);

float4 project_vs_sphere_to_aabb(float3 vs_center, float radius, float p00, float p11)
{
  // If it's inside the sphere then we just return 0 and the rest of the math works out
  if (vs_center.z < radius + kZNear) return float4(0.0f, 0.0f, 1.0f, 1.0f);

  float3 cr   = vs_center * radius;
  float  czr2 = vs_center.z * vs_center.z - radius * radius;

  float  vx   = sqrt(vs_center.x * vs_center.x + czr2);
  float  minx = (vx * vs_center.x - cr.z) / (vx * vs_center.z + cr.x);
  float  maxx = (vx * vs_center.x + cr.z) / (vx * vs_center.z - cr.x);

  float  vy   = sqrt(vs_center.y * vs_center.y + czr2);
  float  miny = (vy * vs_center.y - cr.z) / (vy * vs_center.z + cr.y);
  float  maxy = (vy * vs_center.y + cr.z) / (vy * vs_center.z - cr.y);

  float4 aabb = float4(minx * p00, miny * p11, maxx * p00, maxy * p11);
  // clip space -> uv space
         aabb = aabb.xwzy * float4(0.5f, -0.5f, 0.5f, -0.5f) + 0.5f;

  return aabb;
}

float4 project_ws_sphere_to_aabb(float3 ws_center, float radius, float p00, float p11)
{
  float3 vs_center = mul(g_ViewportBuffer.view, float4(ws_center, 1.0f)).xyz;
  return project_vs_sphere_to_aabb(vs_center, radius, p00, p11);
}


groupshared u64 g_WaveVisibility;
[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_GBufferFillMultiDrawIndirectArgsPhaseTwo(
  uint wave_id          : SV_GroupID,
  uint local_thread_id  : SV_GroupThreadID,
  uint global_thread_id : SV_DispatchThreadID
) {

  RWStructuredBuffer<MultiDrawIndirectIndexedArgs> dst_args    = DEREF(g_FillArgsPhaseTwoSrt.multi_draw_args);
  RWStructuredBuffer<u64>                          occlusion   = DEREF(g_FillArgsPhaseTwoSrt.scene_obj_occlusion);
  RWStructuredBuffer<u32>                          dst_gpu_ids = DEREF(g_FillArgsPhaseTwoSrt.scene_obj_gpu_ids);
  Texture2D<f32>                                   hzb         = DEREF(g_FillArgsPhaseTwoSrt.hzb);

  uint2 hzb_dimensions; 
  hzb.GetDimensions(hzb_dimensions.x, hzb_dimensions.y);


  bool visible = true;
  if (global_thread_id < g_FillArgsPhaseTwoSrt.scene_obj_count)
  {
    bool        previously_visible  = (~occlusion[wave_id]) & (1ULL << local_thread_id);

    u32         gpu_id              = global_thread_id;
    SceneObjGpu obj                 = g_SceneObjs[gpu_id];
 
    float3      ws_bsphere_center   = obj.obj_to_world._m03_m13_m23;
    // TODO(bshihabi): This isn't technically correct. Once objects start rotating this won't make any sense.
    float       ws_bsphere_radius   = obj.obj_to_world._m00;
    float3      vs_bsphere_center   = mul(g_ViewportBuffer.view, float4(ws_bsphere_center, 1.0f)).xyz;

    float4      uv_bsphere_aabb     = project_vs_sphere_to_aabb(vs_bsphere_center, ws_bsphere_radius, g_ViewportBuffer.proj._m00, g_ViewportBuffer.proj._m11);

    float4      ss_bsphere_aabb     = uv_bsphere_aabb * float4(hzb_dimensions.x, hzb_dimensions.y, hzb_dimensions.x, hzb_dimensions.y);
    float2      tl_aabb             = ss_bsphere_aabb.xy;
    float2      br_aabb             = ss_bsphere_aabb.zw;
    float2      center_aabb         = round((tl_aabb + br_aabb) * 0.5f);
    float2      aabb_dimensions     = ceil(br_aabb) - floor(tl_aabb);
    uint        minimum_texels      = max(aabb_dimensions.x, aabb_dimensions.y);

    uint        sample_lod_idx      = max(ceil(log2(minimum_texels)) - 1, 0);

    if (sample_lod_idx < kHZBMipCount && !g_RenderSettings.disable_occlusion_culling)
    {
      float furthest_linear_depth = kZNear / hzb.SampleLevel(g_MinSamplerClamp, center_aabb / hzb_dimensions, sample_lod_idx);
      if (vs_bsphere_center.z - ws_bsphere_radius >= furthest_linear_depth)
      {
        visible = false;
      }
    }

    if (!previously_visible && visible)
    {
      MultiDrawIndirectIndexedArgs args;
      args.instance_count           = 1;
      args.index_count_per_instance = obj.index_count;
      args.start_index_location     = obj.start_index;
      args.base_vertex_location     = 0;
      args.start_instance_location  = 0;

      uint dst_idx = dst_args.IncrementCounter();
      dst_args   [dst_idx] = args;
      dst_gpu_ids[dst_idx] = gpu_id;
    }
  }

  if (local_thread_id == 0)
  {
    g_WaveVisibility = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  u64 _;
  InterlockedOr(g_WaveVisibility, visible ? (1ULL << local_thread_id) : 0, _);
  GroupMemoryBarrierWithGroupSync();

  if (local_thread_id == 0 && !g_RenderSettings.freeze_occlusion_culling)
  {
    occlusion[wave_id] = ~g_WaveVisibility;
  }
}

ConstantBuffer<GenerateHZBSrt> g_GenerateHZBSrt : register(b0);

groupshared f32 g_MipLocal[kHZBDownsampleDimension][kHZBDownsampleDimension];

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(kHZBDownsampleDimension, kHZBDownsampleDimension, 1)]
void CS_GenerateHZB(
  uint2 wave_id          : SV_GroupID,
  uint2 local_thread_id  : SV_GroupThreadID,
  uint2 global_thread_id : SV_DispatchThreadID
) {

  Texture2D<float>   gbuffer_depth = DEREF(g_GenerateHZBSrt.gbuffer_depth);
  RWTexture2D<float> depth_mips[4] = 
  {
    DEREF(g_GenerateHZBSrt.depth_mip0),
    DEREF(g_GenerateHZBSrt.depth_mip1),
    DEREF(g_GenerateHZBSrt.depth_mip2),
    DEREF(g_GenerateHZBSrt.depth_mip3),
  };

  uint2 depth_size;
  gbuffer_depth.GetDimensions(depth_size.x, depth_size.y);

  if (any(global_thread_id > depth_size))
  {
    return;
  }

  // Load all of the data into LDS
  float2 initial_sample_uv = float2(global_thread_id * 2 + 1) / float2(depth_size);
  g_MipLocal[local_thread_id.y][local_thread_id.x] = gbuffer_depth.Sample(g_MinSamplerClamp, initial_sample_uv);


  [unroll]
  for (uint imip = 0; imip < 4; imip++)
  {
    // Early out threads if possible
    if (any(local_thread_id >= kHZBDownsampleDimension >> (imip + 1)))
    {
      return;
    }

    uint2 idx = local_thread_id * 2;
    GroupMemoryBarrierWithGroupSync();
    float s0 = g_MipLocal[idx.y + 0][idx.x + 0];
    float s1 = g_MipLocal[idx.y + 0][idx.x + 1];
    float s2 = g_MipLocal[idx.y + 1][idx.x + 0];
    float s3 = g_MipLocal[idx.y + 1][idx.x + 1];

    float m = max(max(s0, s1), max(s2, s3));
    depth_mips[imip][local_thread_id + wave_id * (kHZBDownsampleDimension >> (imip + 1))] = m;

    GroupMemoryBarrierWithGroupSync();
    g_MipLocal[idx.y][idx.x] = m;
  }
}

