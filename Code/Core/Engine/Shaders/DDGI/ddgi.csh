#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/ddgi_common.hlsli"
#include "../Include/rt_common.hlsli"
#include "../Include/debug_draw.hlsli"


ConstantBuffer<RtDiffuseGiProbeInitSrt> g_ProbeInitSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 1, 8)]
void CS_RtDiffuseGiPageTableInit(uint3 thread_id : SV_DispatchThreadID)
{
  RWTexture2DArray<u32>               page_table   = DEREF(g_ProbeInitSrt.page_table);
  RWStructuredBuffer<DiffuseGiProbe>  probe_buffer = DEREF(g_ProbeInitSrt.probe_buffer);

  uint clipmap_idx = thread_id.y / kProbeCountPerClipmap.y;

  uint probe_idx   = clipmap_idx * (kProbeCountPerClipmap.x * kProbeCountPerClipmap.y * kProbeCountPerClipmap.z);

  if (any(thread_id.xz >= kProbeCountPerClipmap.xz))
  {
    return;
  }

  int3 dst_coord   = int3(thread_id.x, thread_id.y % kProbeCountPerClipmap.y, thread_id.z);
  probe_idx       += dst_coord.y * kProbeCountPerClipmap.x * kProbeCountPerClipmap.z;
  probe_idx       += dst_coord.z * kProbeCountPerClipmap.x;
  probe_idx       += dst_coord.x;

  int3 tex_coord = dst_coord.xzy;
  tex_coord.z += clipmap_idx * kProbeCountPerClipmap.y;

  page_table[tex_coord] = (u32)probe_idx;

  DiffuseGiProbe new_probe           = probe_buffer[probe_idx];

  new_probe.luminance                = SH::L1_F16_RGB::Zero();

  // The probe is going to have high variance initially so we can resolve it quickly
  new_probe.short_mean               = SH::L1_F16_RGB::Zero();
  new_probe.variance                 = SH::L1_F16_RGB::Zero();
  new_probe.vbbr                     = 1.0h;
  new_probe.inconsistency            = 10.0h;
  new_probe.sample_count             = 0;
  new_probe.frames_since_last_traced = 0xFFFE;

  probe_buffer[probe_idx] = new_probe;
}

float get_probe_weight(f32 inconsistency, uint clipmap_idx, u16 sample_count, u16 frames_since_last_traced)
{
  if (sample_count < 1024)
  {
    return 10.0f;
  }
  else if (frames_since_last_traced >= 30)
  {
    return 5.0f;
  }
  else
  {
    return inconsistency * pow(1.1f, kProbeClipmapCount - clipmap_idx);
  }
}

ConstantBuffer<RtDiffuseGiProbeReprojectSrt> g_ProbeReprojectSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 1, 8)]
void CS_RtDiffuseGiPageTableReproject(uint3 thread_id : SV_DispatchThreadID)
{
  if (any(thread_id.xz >= kProbeCountPerClipmap.xz))
  {
    return;
  }

  RWStructuredBuffer<DiffuseGiProbe>  probe_buffer    = DEREF(g_ProbeReprojectSrt.probe_buffer);
  RWStructuredBuffer<uint>            atomic_counters = DEREF(g_ProbeReprojectSrt.atomic_counters);
  RWTexture2DArray<u32>               page_table      = DEREF(g_ProbeReprojectSrt.page_table);
  Texture2DArray<u32>                 page_table_prev = DEREF(g_ProbeReprojectSrt.page_table_prev);

  float3 prev_ws_pos         = g_ViewportBuffer.prev_camera_world_pos.xyz;
  float3 ws_pos              = g_ViewportBuffer.camera_world_pos.xyz;

  uint   clipmap_idx         = thread_id.y / kProbeCountPerClipmap.y;

  float3 probe_spacing       = get_probe_spacing(clipmap_idx);
  int3   clipmap_origin      = get_clipmap_origin(clipmap_idx);
  int3   prev_clipmap_origin = get_prev_clipmap_origin(clipmap_idx);

  int3 clipmap_diff = clipmap_origin - prev_clipmap_origin;

  int3 src_coord = int3(thread_id.x, thread_id.y % kProbeCountPerClipmap.y, thread_id.z);
  if (
    clipmap_idx >= kProbeClipmapCount      ||
    src_coord.x >= kProbeCountPerClipmap.x ||
    src_coord.y >= kProbeCountPerClipmap.y ||
    src_coord.z >= kProbeCountPerClipmap.z
  ) {
    return;
  }

  int3 src_tex_coord           = get_probe_tex_coord(src_coord, clipmap_idx);
  u32  probe_idx               = page_table_prev[src_tex_coord];
  f32  inconsistency           = (f32)probe_buffer[probe_idx].inconsistency;
  u16  sample_count            = probe_buffer[probe_idx].sample_count;
  u16  frames_since_last_trace = probe_buffer[probe_idx].frames_since_last_traced;
  if (clipmap_diff.x == 0 && clipmap_diff.y == 0 && clipmap_diff.z == 0)
  {
    page_table[src_tex_coord] = probe_idx;

    float3 kClipmapToColor[] = { float3(0.0, 0.0, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0) };
    float3 probe_ws_pos      = get_probe_ws_pos(src_coord, clipmap_idx);

    debug_draw_spherical_harmonic(probe_ws_pos, 0.1f, probe_buffer[probe_idx].luminance);
  }
  else
  {
    int3 reproj_coord = src_coord - clipmap_diff;

    // Logic to figure out if we need to reset the probe because it went wrapped around the clipmap
    bool needs_reset  = any(reproj_coord < 0) || any(reproj_coord >= kProbeCountPerClipmap);
    reproj_coord      = (reproj_coord % kProbeCountPerClipmap + kProbeCountPerClipmap) % kProbeCountPerClipmap;
    if (needs_reset)
    {
      // Reset the probe
      DiffuseGiProbe new_probe     = probe_buffer[probe_idx];
      // Get a neighboring probe close to the center of the clipmap
      int3           adj_coord     = reproj_coord;
      int3           adj_tex_coord = get_probe_tex_coord(adj_coord, clipmap_idx);
      u32            adj_idx       = page_table_prev[adj_tex_coord];
      DiffuseGiProbe adj_probe     = probe_buffer[adj_idx];

      // Get the adjacent probe's luminance
      new_probe.luminance                = adj_probe.luminance;

      // The probe is going to have high variance initially so we can resolve it quickly
      new_probe.short_mean               = adj_probe.luminance;
      new_probe.vbbr                     = 0.3h;
      new_probe.inconsistency            = 0.5h;
      new_probe.sample_count             = 0;
      new_probe.frames_since_last_traced = 0xFFFE;

      probe_buffer[probe_idx] = new_probe;

      inconsistency           = (f32)new_probe.inconsistency;
      sample_count            = 0;
      frames_since_last_trace = 0xFFFE;
    }

    if (needs_reset)
    {
      float3 probe_ws_pos      = get_probe_ws_pos(reproj_coord, clipmap_idx);
      debug_draw_sphere(probe_ws_pos, 0.1f, float3(1.0f, 0.0f, 0.0f));
    }

    int3 dst_tex_coord = get_probe_tex_coord(reproj_coord, clipmap_idx);

    page_table[dst_tex_coord] = page_table_prev[src_tex_coord];
  }

  float weight = get_probe_weight(inconsistency, clipmap_idx, sample_count, frames_since_last_trace);
  InterlockedAdd(atomic_counters[0], weight * 1000.0f);
}

ConstantBuffer<RtDiffuseGiProbeAllocRaysSrt> g_ProbeRayAllocSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 1, 8)]
void CS_RtDiffuseGiAllocRays(uint3 thread_id : SV_DispatchThreadID)
{
  StructuredBuffer<DiffuseGiProbe>   probe_buffer      = DEREF(g_ProbeRayAllocSrt.probe_buffer);
  Texture2DArray<u32>                page_table        = DEREF(g_ProbeRayAllocSrt.page_table);
  RWStructuredBuffer<GiRayAlloc>     dst_allocs        = DEREF(g_ProbeRayAllocSrt.ray_allocs);
  RWStructuredBuffer<GiRayLuminance> ray_output_buffer = DEREF(g_ProbeRayAllocSrt.ray_output_buffer);
  RWStructuredBuffer<u32>            atomic_counters   = DEREF(g_ProbeRayAllocSrt.atomic_counters);

  // Get probe coordinates for which probe we're allocating rays for
  uint   clipmap_idx       = thread_id.y / kProbeCountPerClipmap.y;
  int3   probe_coord       = int3(thread_id.x, thread_id.y % kProbeCountPerClipmap.y, thread_id.z);

  if (
    clipmap_idx   >= kProbeClipmapCount      ||
    probe_coord.x >= kProbeCountPerClipmap.x ||
    probe_coord.y >= kProbeCountPerClipmap.y ||
    probe_coord.z >= kProbeCountPerClipmap.z
  ) {
    return;
  }

  int3   probe_tex_coord   = get_probe_tex_coord(probe_coord, clipmap_idx);
  uint   probe_idx         = page_table[probe_tex_coord];
  
  DiffuseGiProbe probe     = probe_buffer[probe_idx];
  f32    total_weight      = atomic_counters[0] / 1000.0f;
  f32    inconsistency     = probe.inconsistency;
  f32    weight            = get_probe_weight(inconsistency, clipmap_idx, probe.sample_count, probe.frames_since_last_traced);
  f32    relative_priority = weight / total_weight;
  u32    ray_count         = clamp(relative_priority * g_RenderSettings.diffuse_gi_ray_budget, 0, kProbeMaxRays);

  u32    ray_idx           = 0;
  if (ray_count > 0)
  {
    InterlockedAdd(atomic_counters[1], ray_count, ray_idx);
  }

  dst_allocs[probe_idx].ray_idx   = ray_idx;
  dst_allocs[probe_idx].ray_count = ray_count;

  // float3 probe_ws_pos      = get_probe_ws_pos(probe_coord, clipmap_idx);
  // debug_draw_sphere(probe_ws_pos, 0.1f, lerp(float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), (f32)ray_count / (f32)kProbeMaxRays));
}

float3 spherical_fibonacci(float sample_idx, float num_samples)
{
  // Credit to NVIDIA DDGI implementation of spherical fibonacci.
  const float b = (sqrt(5.f) * 0.5f + 0.5f) - 1.f;
  float phi = k2PI * frac(sample_idx * b);
  float cos_theta = 1.f - (2.f * sample_idx + 1.f) * (1.f / num_samples);
  float sin_theta = sqrt(saturate(1.f - (cos_theta * cos_theta)));

  return float3((cos(phi) * sin_theta), (sin(phi) * sin_theta), cos_theta);
}

ConstantBuffer<RtDiffuseGiTraceRaySrt> g_ProbeTraceRaySrt : register(b0);

// TODO(bshihabi): Replace this with something faster and better (blue noise texture)
float rand(float co) { return frac(sin(co*(91.3458)) * 47453.5453); }

#define kDirectionSampleCount (32)
groupshared float  g_PrefixSumLuminance[kDirectionSampleCount];
groupshared float3 g_RandDirections[kDirectionSampleCount];

[WaveSize(kProbeMaxRays)]
[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(kProbeMaxRays, 1, 1)]
void CS_RtDiffuseGiTraceRays(
  uint3 group_id : SV_GroupID,
  uint group_thread_id : SV_GroupThreadID
) {
  float4x4                            base_rotation     = g_ProbeTraceRaySrt.rotation;
  Texture2DArray<u32>                 page_table        = DEREF(g_ProbeTraceRaySrt.page_table);
  RWStructuredBuffer<GiRayLuminance>  ray_output_buffer = DEREF(g_ProbeTraceRaySrt.ray_output_buffer);
  StructuredBuffer<DiffuseGiProbe>    probe_buffer      = DEREF(g_ProbeTraceRaySrt.probe_buffer);
  StructuredBuffer<GiRayAlloc>        ray_alloc_args    = DEREF(g_ProbeTraceRaySrt.ray_alloc_args);


  // Get probe coordinates for which probe we're sending rays for
  uint   clipmap_idx     = group_id.y / kProbeCountPerClipmap.y;
  int3   probe_coord     = int3(group_id.x, group_id.y % kProbeCountPerClipmap.y, group_id.z);
  int3   probe_tex_coord = get_probe_tex_coord(probe_coord, clipmap_idx);
  u32    probe_idx       = page_table[probe_tex_coord];

  GiRayAlloc args        = ray_alloc_args[probe_idx];
  u32        dst_idx     = args.ray_idx + group_thread_id;

  // Importance sampling
  uint   lane_count = WaveGetLaneCount();
  uint   sample_idx = lane_count - 1;
  uint   lane_idx   = WaveGetLaneIndex();

  // Normalized prefix sum
  float  total_luminance      = 0;
  for (uint i = 0; i < kDirectionSampleCount; i += lane_count)
  {
    float3 rand_direction       = g_BlueNoiseUnitVec3.Sample(g_PointSamplerWrap, float2((float)group_thread_id / (float)kDirectionSampleCount, probe_idx / 128.0f) + g_ViewportBuffer.frame_id / 128.0f).xyz * 2.0f - 1.0f;
    // float3 rand_direction       = normalize(mul((float3x3)base_rotation, spherical_fibonacci(i + lane_idx, kDirectionSampleCount)));
    float  luminance            = luma_rec709((float3)SH::Evaluate(probe_buffer[probe_idx].luminance, (half3)rand_direction));
    float  luminance_prefix_sum = WavePrefixSum(luminance) + luminance + total_luminance;
    total_luminance            += WaveActiveSum(luminance);
    g_PrefixSumLuminance[i + lane_idx] = luminance_prefix_sum;
    g_RandDirections[i + lane_idx] = rand_direction;
  }

  GroupMemoryBarrierWithGroupSync();

  // x = CDF(X)
  // Default to uniform sphere — handles uninitialised probes (total_luminance == 0)
  // where the CDF is flat and iluminance/sphere_integral would be 0/0.
  float  pdf = 1.0f / (4.0f * kPI);
  float  X   = total_luminance * rand((float)(WaveGetLaneIndex() + 1) / (float)(lane_count + 1));
  for (u32 i = 0; i < kDirectionSampleCount; i++)
  {
    float iluminance_prefix_sum = g_PrefixSumLuminance[i];
    if (iluminance_prefix_sum >= X)
    {
      sample_idx            = i;
      float iluminance      = g_PrefixSumLuminance[i] - (i > 0 ? g_PrefixSumLuminance[i - 1] : 0.0f);
      float sphere_integral = total_luminance * (4.0f * kPI / (float)kDirectionSampleCount);
      if (sphere_integral > 0.0f)
      {
        pdf = iluminance / sphere_integral;
      }
      break;
    }
  }

  // Nop any threads beyond the ray count
  if (group_thread_id >= args.ray_count)
  {
    return;
  }

  // Importance sample direction
  float3 sample_direction = g_RandDirections[sample_idx];

  float3 probe_ws_pos     = get_probe_ws_pos(probe_coord, clipmap_idx);

  RayQuery<
    RAY_FLAG_CULL_NON_OPAQUE |
    RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
  RayDesc ray;
  ray.Origin    = probe_ws_pos;
  ray.Direction = sample_direction;
  ray.TMin      = 0.01f;
  // Short rays for probes
  ray.TMax      = 50.0f;
  // debug_draw_line(ray.Origin, ray.Origin + ray.Direction * 0.5f, float3(1.0, 0.0, 0.0));

  DirectionalLight directional_light = g_ViewportBuffer.directional_light;

  query.TraceRayInline(g_AccelerationStructure, RAY_FLAG_NONE, 0xFF, ray);
  query.Proceed();

  if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
  {
    VertexUncompressed vert      = get_traced_vertex(query);
    uint               gpu_id    = query.CommittedInstanceID();
    SceneObjGpu        scene_obj = g_SceneObjs[gpu_id];
    MaterialGpu        material  = g_Materials[scene_obj.mat_id];
    float3             diffuse   = 0.9;
    if (material.diffuse != 0)
    {
      Texture2D<float4> diffuse_tex = DEREF(material.diffuse);

      diffuse = diffuse_tex.Sample(g_BilinearSamplerWrap, vert.uv).rgb;
    }
    float3             ws_pos    = ray.Origin + ray.Direction * query.CommittedRayT();
    float3             normal    = vert.normal;

    // If we hit a backface
    if (dot(normal, ray.Direction) > 0)
    {
      ray_output_buffer[dst_idx].luminance.m_Value = (half3)-1.0;
      ray_output_buffer[dst_idx].direction         = (half3)sample_direction;
    }
    else
    {

      float shadow_atten = light_visibility(
        directional_light.direction.xyz,
        ws_pos,
        normal,
        1e27f,
        0.001f
      );

  #if 0
      bool is_debug = clipmap_idx == 0 && (all(probe_coord == kProbeCountPerClipmap / 2) || all(probe_coord == kProbeCountPerClipmap / 2 - 1));
      if (is_debug)
      {
        // debug_draw_line(ws_pos, ws_pos + normal, float3(1.0, 0.0, 0.0));
        uint  recursive_clipmap_idx = 0;
        int3  recursive_probe_base  = get_probe_floor(ws_pos, recursive_clipmap_idx);
        if (recursive_clipmap_idx == clipmap_idx && all(recursive_probe_base == probe_coord))
        {
          debug_draw_line(ray.Origin, ray.Origin + ray.Direction * query.CommittedRayT(), float3(1.0, 0.0, 0.0));
          debug_draw_line(ws_pos, ws_pos + normal * 0.1f, float3(0.0, 0.0, 1.0));
        }
      }
  #endif

      Lux3  directional_illuminance;
      directional_illuminance.m_Value = directional_light.illuminance * directional_light.diffuse.rgb;

      BSDF  directional_bsdf   = lambertian_diffuse_bsdf(normal, directional_light.direction.xyz, diffuse);
      Nits3 direct_luminance   = directional_bsdf * directional_illuminance.attenuated(shadow_atten);

      Nits3 indirect_luminance = sample_indirect_luminance(ws_pos, normal, diffuse, page_table, probe_buffer);

      Nits3 luminance;
      luminance.m_Value  = direct_luminance.m_Value + indirect_luminance.m_Value;
      luminance.m_Value /= pdf;

      ray_output_buffer[dst_idx].luminance.m_Value = (half3)luminance.m_Value;
      ray_output_buffer[dst_idx].direction         = (half3)sample_direction;

      if (mouse_intersect_sphere(probe_ws_pos, 0.1f))
      {
        debug_draw_line(ray.Origin, ray.Origin + ray.Direction * query.CommittedRayT(), luminance.m_Value);
      }
    }
  }
  else
  {
    // Add sky illuminance
    Lux3 sky_illuminance;
    sky_illuminance.m_Value = directional_light.sky_illuminance * directional_light.sky_diffuse;

    // Already know that the illuminance is in terms of a white diffuse surface pointed directly at the sky, so just need to divide by PI basically
    BSDF lambertian_bsdf = lambertian_diffuse_bsdf(sample_direction, -sample_direction, 1.0f);

    Nits3 luminance         = lambertian_bsdf * sky_illuminance;

    ray_output_buffer[dst_idx].luminance.m_Value = (half3)0.0f;
    ray_output_buffer[dst_idx].direction         = (half3)sample_direction;

    if (mouse_intersect_sphere(probe_ws_pos, 0.1f))
    {
      debug_draw_line(ray.Origin, ray.Origin + ray.Direction * 5.0f, 0.0f);
    }
  }
}

ConstantBuffer<RtDiffuseGiProbeBlendSrt> g_ProbeBlendSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_RtDiffuseGiProbeBlend(uint thread_id : SV_DispatchThreadID, uint group_id : SV_GroupID)
{
  StructuredBuffer<GiRayLuminance>    ray_buffer     = DEREF(g_ProbeBlendSrt.ray_buffer);
  RWStructuredBuffer<DiffuseGiProbe>  probe_buffer   = DEREF(g_ProbeBlendSrt.probe_buffer);
  StructuredBuffer<GiRayAlloc>        ray_alloc_args = DEREF(g_ProbeBlendSrt.ray_alloc_args);

  uint           probe_idx = thread_id;
  if (probe_idx >= kProbeMaxActiveCount)
  {
    return;
  }

  GiRayAlloc args    = ray_alloc_args[probe_idx];
  u32        src_idx = args.ray_idx;

  if (args.ray_count == 0)
  {
    if (probe_buffer[probe_idx].frames_since_last_traced < 0xFFFE)
    {
      probe_buffer[probe_idx].frames_since_last_traced++;
    }
    return;
  }

  uint           backface_count = 0;
  SH::L1_F16_RGB luminance      = SH::L1_F16_RGB::Zero();
  for (u32 isample = 0; isample < args.ray_count; isample++)
  {
    GiRayLuminance sample = ray_buffer[src_idx + isample];
    if (all(sample.luminance.m_Value < 0))
    {
      backface_count++;
    }
    else
    {
      SH::L1_F16_RGB proj = SH::ProjectOntoL1(sample.direction, sample.luminance.m_Value);
      luminance  = luminance + proj;
    }
  }

  luminance = luminance * (1.0h / (f16)args.ray_count);

  DiffuseGiProbe probe = probe_buffer[probe_idx];

  // TODO(bshihabi): I'm not too sure where the NaN is coming from, but this seems to happen sometimes and the probes go crazy because of it.
  // this is my temporary solution
  if (SH::IsNan(probe.variance))
  {
    probe.variance = SH::L1_F16_RGB::Zero();
  }
  if (SH::IsNan(probe.short_mean))
  {
    probe.short_mean = SH::L1_F16_RGB::Zero();
  }
  if (isnan(probe.inconsistency))
  {
    probe.inconsistency = 0.3h;
  }
  if (isnan(probe.vbbr))
  {
    probe.vbbr = 0.3h;
  }

  float3 total_catch_up_blend = 0.0f;
  float  total_vbbr           = 0.0f;
  float  total_inconsistency  = 0.0f;
  {
    static const float kShortWindowBlend = 0.08f;
    static const float kVarianceBlend    = kShortWindowBlend * 0.5f;

    for (int i = 0; i < SH::L1_F16_RGB::NumCoefficients; i++)
    {
      float3 y             = (float3)luminance.C[i];
      float3 mean          = (float3)probe.luminance.C[i];
      float3 short_mean    = (float3)probe.short_mean.C[i];
      float3 variance      = (float3)probe.variance.C[i];
      float  inconsistency = (float)probe.inconsistency;
      float  vbbr          = (float)probe.vbbr;

      // Welford's variance algorithm (with EMA/lerp)
      // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm
      float3 delta      = y - short_mean;
      short_mean        = lerp(short_mean, y, kShortWindowBlend);
      float3 delta2     = y - short_mean;

      variance          = lerp(variance, delta * delta2, kVarianceBlend);
      float3 std_dev    = sqrt(max(1e-5, variance));
      float3 short_diff = mean - short_mean;

      float  relative_diff = luma_rec709(abs(short_diff) / max(1e-5, std_dev));
      inconsistency        = lerp(inconsistency, relative_diff, kShortWindowBlend);

      float  variance_based_blend_reduction = clamp(luma_rec709(0.5 * short_mean / max(1e-5, std_dev)), 1.0 / 32, 1.0);
      float3 catch_up_blend = clamp(smoothstep(0.0f, 1.0f, relative_diff * max(0.02, inconsistency - 0.2)), 1.0 / 256, 1.0);
      catch_up_blend       *= vbbr;
      catch_up_blend        = saturate(catch_up_blend);

      vbbr                  = lerp(vbbr, variance_based_blend_reduction, 0.1);

      total_catch_up_blend += catch_up_blend;
      total_vbbr           += vbbr;
      total_inconsistency  += inconsistency;
      probe.short_mean.C[i] = (half3)short_mean;
      probe.variance.C[i]   = (half3)variance;
    }
  }

  float3 mean_catch_up_blend = total_catch_up_blend / SH::L1_F16_RGB::NumCoefficients;
  float  mean_inconsistency  = total_inconsistency  / SH::L1_F16_RGB::NumCoefficients;
  float  mean_vbbr           = total_vbbr           / SH::L1_F16_RGB::NumCoefficients;
  probe.inconsistency        = (half)mean_inconsistency;
  probe.vbbr                 = (half)mean_vbbr;

  probe.luminance  = SH::Lerp(probe.luminance, luminance, (half3)mean_catch_up_blend);

  if (SH::IsNan(probe.luminance))
  {
    SH::L1_F16_RGB err = SH::ProjectOntoL1(half3(0.0f, 1.0f, 0.0f), half3(1.0f, 0.0f, 0.0f));
    probe.luminance    = err;
  }

  if (probe.sample_count < 0xFFFE)
  {
    probe.sample_count   += (u16)args.ray_count;
  }
  probe.frames_since_last_traced = 0;
  probe_buffer[probe_idx]        = probe;
}
