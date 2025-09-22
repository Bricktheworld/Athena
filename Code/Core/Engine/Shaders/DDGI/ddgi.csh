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
  RWTexture2DArray<u16> page_table = DEREF(g_ProbeInitSrt.page_table);

  uint clipmap_idx = thread_id.y / kProbeCountPerClipmap.y;

  uint probe_idx   = clipmap_idx * (kProbeCountPerClipmap.x * kProbeCountPerClipmap.y * kProbeCountPerClipmap.z);

  int3 dst_coord   = int3(thread_id.x, thread_id.y % kProbeCountPerClipmap.y, thread_id.z);
  probe_idx       += dst_coord.y * kProbeCountPerClipmap.x * kProbeCountPerClipmap.z;
  probe_idx       += dst_coord.z * kProbeCountPerClipmap.x;
  probe_idx       += dst_coord.x;

  int3 tex_coord = dst_coord.xzy;
  tex_coord.z += clipmap_idx * kProbeCountPerClipmap.y;

  page_table[tex_coord] = (u16)probe_idx;
}

float3 get_probe_ws_pos(int3 probe_coord, uint clipmap_idx, RtDiffuseGiSettings settings)
{
  float3 probe_spacing  = settings.probe_spacing[clipmap_idx];
  float3 ws_pos         = g_ViewportBuffer.camera_world_pos.xyz;
  int3   clipmap_origin = int3(ws_pos / probe_spacing);
  return (clipmap_origin + probe_coord - int3(kProbeCountPerClipmap / 2)) * probe_spacing + probe_spacing / 2;
}

int3 get_probe_tex_coord(int3 probe_coord, uint clipmap_idx)
{
  int3 tex_coord = probe_coord.xzy;
  tex_coord.z   += clipmap_idx * kProbeCountPerClipmap.y;
  return tex_coord;
}

ConstantBuffer<RtDiffuseGiProbeReprojectSrt> g_ProbeReprojectSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 1, 8)]
void CS_RtDiffuseGiPageTableReproject(uint3 thread_id : SV_DispatchThreadID)
{
  ConstantBuffer<RtDiffuseGiSettings> settings        = DEREF(g_ProbeReprojectSrt.settings);
  RWStructuredBuffer<DiffuseGiProbe>  probe_buffer    = DEREF(g_ProbeReprojectSrt.probe_buffer);
  RWTexture2DArray<u16>               page_table      = DEREF(g_ProbeReprojectSrt.page_table);
  Texture2DArray<u16>                 page_table_prev = DEREF(g_ProbeReprojectSrt.page_table_prev);

  float3 prev_ws_pos         = g_ViewportBuffer.prev_camera_world_pos.xyz;
  float3 ws_pos              = g_ViewportBuffer.camera_world_pos.xyz;

  uint   clipmap_idx         = thread_id.y / kProbeCountPerClipmap.y;

  float3 probe_spacing       = settings.probe_spacing[clipmap_idx];
  int3   clipmap_origin      = int3(ws_pos      / probe_spacing);
  int3   prev_clipmap_origin = int3(prev_ws_pos / probe_spacing);

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

  int3 src_tex_coord = get_probe_tex_coord(src_coord, clipmap_idx);

  if (clipmap_diff.x == 0 && clipmap_diff.y == 0 && clipmap_diff.z == 0)
  {
    u16 probe_idx = page_table_prev[src_tex_coord];
    page_table[src_tex_coord] = probe_idx;

    float3 kClipmapToColor[] = { float3(0.0, 0.0, 1.0), float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0) };
    float3 probe_ws_pos      = get_probe_ws_pos(src_coord, clipmap_idx, settings);
    debug_draw_spherical_harmonic(probe_ws_pos, 0.1f, probe_buffer[probe_idx].luminance);
    return;
  }

  int3 reproj_coord = src_coord + clipmap_diff;

  // Logic to figure out if we need to reset the probe because it went wrapped around the clipmap
  if (
    reproj_coord.x < 0 || reproj_coord.x >= kProbeCountPerClipmap.x ||
    reproj_coord.y < 0 || reproj_coord.y >= kProbeCountPerClipmap.y ||
    reproj_coord.z < 0 || reproj_coord.z >= kProbeCountPerClipmap.z
  ) {
    u16 reset_probe = page_table_prev[src_tex_coord];

    // Reset the probe
    probe_buffer[reset_probe].luminance = SH::L2_F16_RGB::Zero();
  }

  // Reproject with overwrite logic for "new" probes (probes that were reset)
  reproj_coord = (reproj_coord % kProbeCountPerClipmap + kProbeCountPerClipmap) % kProbeCountPerClipmap;

  int3 dst_tex_coord = get_probe_tex_coord(reproj_coord, clipmap_idx);

  page_table[dst_tex_coord] = page_table_prev[src_tex_coord];
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

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_RtDiffuseGiTraceRays(
  uint3 group_id : SV_GroupID,
  uint group_idx : SV_GroupIndex,
  uint group_thread_id : SV_GroupThreadID
) {
  float4x4                            base_rotation     = g_ProbeTraceRaySrt.rotation;
  Texture2DArray<u16>                 page_table        = DEREF(g_ProbeTraceRaySrt.page_table);
  ConstantBuffer<RtDiffuseGiSettings> settings          = DEREF(g_ProbeTraceRaySrt.settings);
  RWStructuredBuffer<GiRayLuminance>  ray_output_buffer = DEREF(g_ProbeTraceRaySrt.ray_output_buffer);

  // TODO(bshihabi): Importance sample ray direction
  // half3 luminance = SH::Evaluate();

  // We are assuming uniform 64 samples for now, we will want this to be much more flexible in the future
  float3 sample_rotation = spherical_fibonacci(group_thread_id, 64);

  // Get probe coordinates for which probe we're sending rays for
  uint   clipmap_idx     = group_id.y / kProbeCountPerClipmap.y;
  int3   probe_coord     = int3(group_id.x, group_id.y % kProbeCountPerClipmap.y, group_id.z);
  int3   probe_tex_coord = get_probe_tex_coord(probe_coord, clipmap_idx);
  u16    probe_idx       = page_table[probe_tex_coord];


  float3 probe_ws_pos    = get_probe_ws_pos(probe_coord, clipmap_idx, settings);

  if (clipmap_idx == 0)
  {
    debug_draw_line(probe_ws_pos, probe_ws_pos + sample_rotation * 0.5f, float3(1.0, 0.0, 0.0));
  }

  RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
  RayDesc ray;
  ray.Origin    = probe_ws_pos;
  ray.Direction = sample_rotation;
  ray.TMin      = 0.01f;
  // Short rays for probes
  ray.TMax      = 20.0f;

  DirectionalLight directional_light = g_ViewportBuffer.directional_light;

  query.TraceRayInline(g_AccelerationStructure, 0, 0xFF, ray);
  query.Proceed();

  // TODO(bshihabi): This needs to be dynamic since each probe might have different amounts allocated to it.
  uint dst_idx = probe_idx * 64 + group_thread_id;
  if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
  {
    Vertex vert   = get_traced_vertex(query.CommittedInstanceIndex(), query.CommittedTriangleBarycentrics());
    // TODO(bshihabi): Get actual diffuse from material
    float3 diffuse = 1.0f;
    float3 ws_pos = ray.Origin + ray.Direction * query.CommittedRayT();
    float3 normal = vert.normal;

    float shadow_atten = light_visibility(
      directional_light.direction.xyz,
      ws_pos,
      normal,
      1e27f,
      0.001f
    );

    Lux3  directional_illuminance;
    directional_illuminance.m_Value = directional_light.illuminance * directional_light.diffuse.rgb;

    BSDF  directional_bsdf = lambertian_diffuse_bsdf(normal, directional_light.direction.xyz, diffuse);
    Nits3 luminance        = directional_bsdf * directional_illuminance.attenuated(shadow_atten);

    ray_output_buffer[dst_idx].luminance.m_Value = (half3)luminance.m_Value;
    ray_output_buffer[dst_idx].direction         = (half3)sample_rotation;
  }
  else
  {
    // Add sky luminance
    ray_output_buffer[dst_idx].luminance.m_Value = -1.0;
    ray_output_buffer[dst_idx].direction         = (half3)sample_rotation;
  }
}

ConstantBuffer<RtDiffuseGiProbeBlendSrt> g_ProbeBlendSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_RtDiffuseGiProbeBlend(uint thread_id : SV_DispatchThreadID, uint group_id : SV_GroupID)
{
  ConstantBuffer<RtDiffuseGiSettings> settings     = DEREF(g_ProbeBlendSrt.settings);
  StructuredBuffer<GiRayLuminance>    ray_buffer   = DEREF(g_ProbeBlendSrt.ray_buffer);
  RWStructuredBuffer<DiffuseGiProbe>  probe_buffer = DEREF(g_ProbeBlendSrt.probe_buffer);

  uint           probe_idx = thread_id;

  // TODO(bshihabi): Add dynamic ray allocation
  uint           src_idx   = probe_idx * 64;
  SH::L2_F16_RGB luminance = SH::L2_F16_RGB::Zero();
  for (u32 isample = 0; isample < 64; isample++)
  {
    GiRayLuminance sample = ray_buffer[src_idx + isample];
    luminance = luminance + SH::ProjectOntoL2(sample.direction, sample.luminance.m_Value);
  }

  // TODO(bshihabi): We should blend based on variance
  probe_buffer[probe_idx].luminance = SH::Lerp(probe_buffer[probe_idx].luminance, luminance, 0.9h);
}
