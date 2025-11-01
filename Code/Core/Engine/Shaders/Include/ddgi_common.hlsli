#ifndef __DDGI_COMMON__
#define __DDGI_COMMON__

#include "debug_draw.hlsli"
#include "lighting_common.hlsli"
#include "spherical_harmonics.hlsli"

#define kProbeCountPerClipmapX (32)
#define kProbeCountPerClipmapY (4)
#define kProbeCountPerClipmapZ (32)
#define kProbeCountPerClipmap (UVec3(kProbeCountPerClipmapX, kProbeCountPerClipmapY, kProbeCountPerClipmapZ))
#define kProbeClipmapCount (2)
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

  Vec3f16 pad;
  f16     backface_percentage;
};

struct GiRayLuminance
{
  Nits3f16 luminance;
  Vec3f16  direction;
};

struct RtDiffuseGiProbeInitSrt
{
  RWTexture2DArrayPtr<u32> page_table;
};

struct RtDiffuseGiProbeReprojectSrt
{
  RWTexture2DArrayPtr<u32>               page_table;
  RWStructuredBufferPtr<DiffuseGiProbe>  probe_buffer;
  Texture2DArrayPtr<u32>                 page_table_prev;
};

struct RtDiffuseGiTraceRaySrt
{
  Mat4                                   rotation;
  Texture2DArrayPtr<u32>                 page_table;
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

int3 get_clipmap_origin(uint clipmap_idx)
{
  float3 ws_pos         = g_ViewportBuffer.camera_world_pos.xyz;
  float3 probe_spacing  = get_probe_spacing(clipmap_idx);

  // Prevent clipmap from going below the floor where it isn't useful
  ws_pos.y = max(ws_pos.y, probe_spacing.y * kProbeCountPerClipmap.y / 2);

  int3   clipmap_origin = int3(ws_pos / probe_spacing);
  return clipmap_origin;
}

int3 get_prev_clipmap_origin(uint clipmap_idx)
{
  float3 ws_pos_prev    = g_ViewportBuffer.prev_camera_world_pos.xyz;
  float3 probe_spacing  = get_probe_spacing(clipmap_idx);

  ws_pos_prev.y = max(ws_pos_prev.y, probe_spacing.y * kProbeCountPerClipmap.y / 2);

  int3   clipmap_origin = int3(ws_pos_prev / probe_spacing);
  return clipmap_origin;
}

float3 get_probe_ws_pos(int3 probe_coord, uint clipmap_idx)
{
  float3 probe_spacing  = get_probe_spacing(clipmap_idx);
  int3 clipmap_origin = get_clipmap_origin(clipmap_idx);
  return (clipmap_origin + probe_coord - int3(kProbeCountPerClipmap / 2)) * probe_spacing + probe_spacing / 2;
}

int3 get_probe_tex_coord(int3 probe_coord, uint clipmap_idx)
{
  int3 tex_coord = probe_coord.xzy;
  tex_coord.z   += clipmap_idx * kProbeCountPerClipmap.y;
  return tex_coord;
}

// Get floored coordinate of neighboring probes
int3 get_probe_floor(float3 ws_pos, inout uint clipmap_idx)
{
  // TODO(bshihabi): Work with more than clipmap 0
  for (uint iclipmap = clipmap_idx; iclipmap < kProbeClipmapCount; iclipmap++)
  {
    float3 probe_spacing     = get_probe_spacing(iclipmap);
    int3   clipmap_origin    = get_clipmap_origin(iclipmap);
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

Nits3 sample_indirect_luminance(float3 ws_pos, float3 normal, float3 diffuse, Texture2DArray<u32> diffuse_gi_page_table, StructuredBuffer<DiffuseGiProbe> diffuse_gi_probes, bool debug_draw_sampled_probes)
{
  // Helps deal with cases where the nearest probe is "inside" the geometry, so the backface weights basically just kill it, we'd ideally want to choose the neighboring cube of probes
  static const float kSampleBias = 0.1f;

  float3 biased_ws_pos        = ws_pos + normal * kSampleBias;

  uint   clipmap_idx          = 0;
  int3   probe_base           = get_probe_floor(biased_ws_pos, clipmap_idx);

  Lux3   indirect_illuminance = Lux3::zero();
  // Account for energy loss on bounce
  float3 kMaxDiffuse          = 0.9f;
  // The indirect illumination from our probes is coming from the normal direction
  float3 light_dir            = -normal;
  BSDF   bsdf                 = lambertian_diffuse_bsdf(normal, light_dir, min(diffuse, kMaxDiffuse));

  while (clipmap_idx < kProbeClipmapCount)
  {
    float3 probe_base_dist    = biased_ws_pos - get_probe_ws_pos(probe_base, clipmap_idx);
    float3 alpha              = saturate(probe_base_dist / get_probe_spacing(clipmap_idx));
    float  total_weights      = 0.0f;

    bool   below_volume       = get_probe_ws_pos(probe_base, clipmap_idx).y > biased_ws_pos.y;

    for (uint iprobe = 0; iprobe < (below_volume ? 4 : 8); iprobe++)
    {
      // iprobe = 0 -> (0, 0, 0)
      // iprobe = 1 -> (1, 0, 0)
      // iprobe = 4 -> (0, 0, 1)
      // iprobe = 5 -> (1, 0, 1)
      // iprobe = 2 -> (0, 1, 0)
      // iprobe = 3 -> (1, 1, 0)
      // iprobe = 6 -> (0, 1, 1)
      // iprobe = 7 -> (1, 1, 1)
      int3   adj_coord_offset       = int3(iprobe, iprobe >> 2, iprobe >> 1) & int3(1, 1, 1);

      int3   adj_probe_coords       = probe_base + adj_coord_offset;
      if (any(adj_probe_coords < 0) || any(adj_probe_coords >= kProbeCountPerClipmap))
      {
        continue;
      }


      float3 adj_probe_ws_pos       = get_probe_ws_pos(adj_probe_coords, clipmap_idx);

      float3 biased_to_adj_dir      = normalize(adj_probe_ws_pos - biased_ws_pos);
      float  biased_to_adj_dist     = length(adj_probe_ws_pos - biased_ws_pos);

      float3 trilinear              = max(0.001f, lerp(1.0f - alpha, alpha, adj_coord_offset));
      float  trilinear_weight       = trilinear.x * trilinear.y * trilinear.z;

      float  backface_weight        = max(dot(biased_to_adj_dir, normal), 0.0f);
      float  weight                 = trilinear_weight * backface_weight;

      int3   tex_coord              = get_probe_tex_coord(adj_probe_coords, clipmap_idx);
      u32    probe_idx              = diffuse_gi_page_table[tex_coord];
      DiffuseGiProbe probe          = diffuse_gi_probes[probe_idx];

      if (probe.backface_percentage > 0.4)
      {
        weight = 0.0f;
      }

      if (debug_draw_sampled_probes && weight > 0)
      {
        debug_draw_line(ws_pos,        biased_ws_pos,    float3(0.0f, 0.0f, 1.0f));
        debug_draw_line(biased_ws_pos, adj_probe_ws_pos, float3(1.0f, 0.0f, 0.0f));
      }

      indirect_illuminance.m_Value += SH::CalculateIrradiance(probe.luminance, (half3)normal) * weight;
      total_weights                += weight;
    }

    if (total_weights > 0.0f)
    {
      indirect_illuminance.m_Value /= total_weights;
      break;
    }

    clipmap_idx++;
    probe_base           = get_probe_floor(biased_ws_pos, clipmap_idx);

    indirect_illuminance = Lux3::zero();
  }

  return bsdf * indirect_illuminance;
}

Nits3 sample_indirect_luminance(float3 ws_pos, float3 normal, float3 diffuse, Texture2DArray<u32> diffuse_gi_page_table, StructuredBuffer<DiffuseGiProbe> diffuse_gi_probes)
{
  return sample_indirect_luminance(ws_pos, normal, diffuse, diffuse_gi_page_table, diffuse_gi_probes, false);
}

#endif

#endif