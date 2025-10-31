#ifndef __DDGI_COMMON__
#define __DDGI_COMMON__

#include "lighting_common.hlsli"
#include "spherical_harmonics.hlsli"

#define kProbeCountPerClipmapX (8)
#define kProbeCountPerClipmapY (4)
#define kProbeCountPerClipmapZ (8)
#define kProbeCountPerClipmap (UVec3(kProbeCountPerClipmapX, kProbeCountPerClipmapY, kProbeCountPerClipmapZ))
#define kProbeClipmapCount (3)
#define kProbeMaxActiveCount (kProbeCountPerClipmapX * kProbeCountPerClipmapY * kProbeCountPerClipmapZ * kProbeClipmapCount)
#define kProbeMaxRayCount (64 * kProbeMaxActiveCount) // 32768; // This is 8 rays per probe in a 16 x 16 x 16 grid, choose wisely!

struct DiffuseGiProbe
{
  SH::L2_F16_RGB luminance;

  Vec3f16 mean;
  Vec3f16 short_mean;
  f16     vbbr;
  Vec3f16 variance;
  f16     inconsistency;
};

struct GiRayLuminance
{
  Nits3f16 luminance;
  Vec3f16  direction;
};

struct RtDiffuseGiProbeInitSrt
{
  RWTexture2DArrayPtr<u16> page_table;
};

struct RtDiffuseGiProbeReprojectSrt
{
  RWTexture2DArrayPtr<u16>               page_table;
  RWStructuredBufferPtr<DiffuseGiProbe>  probe_buffer;
  Texture2DArrayPtr<u16>                 page_table_prev;
};

struct RtDiffuseGiTraceRaySrt
{
  Mat4                                   rotation;
  Texture2DArrayPtr<u16>                 page_table;
  StructuredBufferPtr<DiffuseGiProbe>    probe_buffer;
  RWStructuredBufferPtr<GiRayLuminance>  ray_output_buffer;
};

struct RtDiffuseGiProbeBlendSrt
{
  StructuredBufferPtr<GiRayLuminance>    ray_buffer;
  RWStructuredBufferPtr<DiffuseGiProbe>  probe_buffer;
};

#if !defined(__cplusplus)
float3 get_probe_spacing(uint clipmap_idx)
{
  return g_RenderSettings.diffuse_gi_probe_spacing * pow(2.0f, clipmap_idx);
}

float3 get_probe_ws_pos(int3 probe_coord, uint clipmap_idx)
{
  float3 probe_spacing  = get_probe_spacing(clipmap_idx);
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

// Get floored coordinate of neighboring probes
int3 get_probe_floor(float3 ws_pos, out uint clipmap_idx)
{
  float3 ws_camera_pos     = g_ViewportBuffer.camera_world_pos.xyz;


  // TODO(bshihabi): Work with more than clipmap 0
  for (uint iclipmap = 0; iclipmap < kProbeClipmapCount; iclipmap++)
  {
    float3 probe_spacing     = get_probe_spacing(iclipmap);
    int3   clipmap_origin    = int3(ws_camera_pos / probe_spacing);
    float3 clipmap_origin_ws = (clipmap_origin - int3(kProbeCountPerClipmap / 2)) * probe_spacing + probe_spacing / 2;


    float3 ls_pos            = ws_pos - clipmap_origin_ws;
    int3   floor_coords      = int3(ls_pos / probe_spacing);
    // Guard band of 1
    if (all(floor_coords >= 0) && all(floor_coords < kProbeCountPerClipmap))
    {
      clipmap_idx = iclipmap;
      return floor_coords;
    }
  }
  clipmap_idx = kProbeClipmapCount;
  return -1;
}

Nits3 sample_indirect_luminance(float3 ws_pos, float3 normal, float3 diffuse, Texture2DArray<u16> diffuse_gi_page_table, StructuredBuffer<DiffuseGiProbe> diffuse_gi_probes)
{
  uint   clipmap_idx          = 0;
  int3   probe_base           = get_probe_floor(ws_pos, clipmap_idx);

  Lux3   indirect_illuminance = Lux3::zero();
  // Account for energy loss on bounce
  float3 kMaxDiffuse          = 0.9f;
  // The indirect illumination from our probes is coming from the normal direction
  float3 light_dir            = -normal;
  BSDF   bsdf                 = lambertian_diffuse_bsdf(normal, light_dir, min(diffuse, kMaxDiffuse));

  if (clipmap_idx < kProbeClipmapCount)
  {
    float3 biased_ws_pos      = ws_pos + normal * 0.1f;

    float3 probe_base_dist    = biased_ws_pos - get_probe_ws_pos(probe_base, clipmap_idx);
    float3 alpha              = saturate(probe_base_dist / get_probe_spacing(clipmap_idx));
    float  total_weights      = 0.0f;

    for (uint iprobe = 0; iprobe < 8; iprobe++)
    {
      // iprobe = 0 -> (0, 0, 0)
      // iprobe = 1 -> (1, 0, 0)
      // iprobe = 2 -> (0, 1, 0)
      // iprobe = 3 -> (1, 1, 0)
      // iprobe = 4 -> (0, 0, 1)
      // iprobe = 5 -> (1, 0, 1)
      // iprobe = 6 -> (0, 1, 1)
      // iprobe = 7 -> (1, 1, 1)
      int3   adj_coord_offset       = int3(iprobe, iprobe >> 1, iprobe >> 2) & int3(1, 1, 1);

      int3   adj_probe_coords       = probe_base + adj_coord_offset;
      if (any(adj_probe_coords < 0) || any(adj_probe_coords >= kProbeCountPerClipmap))
      {
        continue;
      }

      float3 adj_probe_ws_pos       = get_probe_ws_pos(adj_probe_coords, clipmap_idx);

      float3 ws_to_adj_dir          = normalize(adj_probe_ws_pos - ws_pos);
      float3 biased_to_adj_dir      = normalize(adj_probe_ws_pos - biased_ws_pos);
      float  biased_to_adj_dist     = length(adj_probe_ws_pos - biased_ws_pos);

      float3 trilinear              = max(0.001f, lerp(1.0f - alpha, alpha, adj_coord_offset));
      float  trilinear_weight       = trilinear.x * trilinear.y * trilinear.z;

      // Valve style "backface" weighting
      float  backface_weight        = max(dot(ws_to_adj_dir, normal), 0.0f);
             // backface_weight        = (backface_weight * backface_weight) + 0.2f;

      float  weight                 = trilinear_weight * backface_weight;

      int3   tex_coord              = get_probe_tex_coord(adj_probe_coords, clipmap_idx);
      u16    probe_idx              = diffuse_gi_page_table[tex_coord];
      DiffuseGiProbe probe          = diffuse_gi_probes[probe_idx];

      indirect_illuminance.m_Value += SH::CalculateIrradiance(probe.luminance, (half3)normal) * weight;
      total_weights                += weight;
    }

    if (total_weights > 0.0f)
    {
      indirect_illuminance.m_Value /= total_weights;
    }

  }

  return bsdf * indirect_illuminance;
}

#endif

#endif