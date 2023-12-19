#ifndef __DDGI_COMMON__
#define __DDGI_COMMON__
#include "../interlop.hlsli"
#include "../root_signature.hlsli"
#include "math.hlsli"
#include "rt_common.hlsli"

float3 spherical_fibonacci(float sample_idx, float num_samples)
{
  // Credit to NVIDIA DDGI implementation of spherical fibonacci.
  const float b = (sqrt(5.f) * 0.5f + 0.5f) - 1.f;
  float phi = k2PI * frac(sample_idx * b);
  float cos_theta = 1.f - (2.f * sample_idx + 1.f) * (1.f / num_samples);
  float sin_theta = sqrt(saturate(1.f - (cos_theta * cos_theta)));

  return float3((cos(phi) * sin_theta), (sin(phi) * sin_theta), cos_theta);
}

float3 get_probe_ray_dir(int ray_index, interlop::DDGIVolDesc vol_desc)
{
  // Each ray index will have a predictable spherical fibonacci unit vector direction
  float3 direction = spherical_fibonacci(ray_index, vol_desc.probe_num_rays);

  // Then ALL the rays in the frame will have the same randomized rotation applied to their respective spherical
  // fibonacci pattern
  return normalize(mul(vol_desc.probe_ray_rotation, float4(direction, 1.0f)).xyz);
}

float3 get_probe_ws_pos(int3 probe_coords, interlop::DDGIVolDesc vol_desc)
{
	float3 probe_grid_ws_pos = probe_coords * vol_desc.probe_spacing.xyz;

	int3   probe_counts      = int3(vol_desc.probe_count_x, vol_desc.probe_count_y, vol_desc.probe_count_z);

	float3 probe_grid_shift = (vol_desc.probe_spacing.xyz * (probe_counts - 1)) * 0.5f;

	float3 probe_ws_pos = (probe_grid_ws_pos - probe_grid_shift) + vol_desc.origin.xyz;

	return probe_ws_pos;
}

int get_probes_per_plane(interlop::DDGIVolDesc vol_desc)
{
  return vol_desc.probe_count_x * vol_desc.probe_count_z;
}

uint3 get_ray_data_texel_coords(int ray_index, int probe_index, interlop::DDGIVolDesc vol_desc)
{
  int probes_per_plane = get_probes_per_plane(vol_desc);

  uint3 coords;
  coords.x = ray_index;
  coords.z = probe_index / probes_per_plane;
  coords.y = probe_index - (coords.z * probes_per_plane);

  return coords;
}

float3 get_surface_bias(float3 normal, float3 camera_dir, interlop::DDGIVolDesc vol_desc)
{
  return normal * 0.0001f + (-camera_dir * 0.1f);
}

int get_plane_index(int3 probe_coords)
{
  return probe_coords.y;
}

int get_probe_index_in_plane(int3 probe_coords, int3 probe_counts)
{
  return probe_coords.x + (probe_counts.x * probe_coords.z);
}

int get_probe_index_in_plane(uint3 tex_coords, int3 probe_counts, int probe_num_texels)
{
	return int(tex_coords.x / probe_num_texels) + (probe_counts.x * int(tex_coords.y / probe_num_texels));
}

int get_probe_index(int3 probe_coords, interlop::DDGIVolDesc vol_desc)
{
  int3 probe_counts = int3(vol_desc.probe_count_x, vol_desc.probe_count_y, vol_desc.probe_count_z);
  int probes_per_plane = get_probes_per_plane(vol_desc);
  int plane_index      = get_plane_index(probe_coords);
  int probe_index_in_plane = get_probe_index_in_plane(probe_coords, probe_counts);
  return (plane_index * probes_per_plane) + probe_index_in_plane;
}

int get_probe_index(uint3 tex_coords, int probe_num_texels, interlop::DDGIVolDesc vol_desc)
{
  int3 probe_counts = int3(vol_desc.probe_count_x, vol_desc.probe_count_y, vol_desc.probe_count_z);
  int probes_per_plane = get_probes_per_plane(vol_desc);
  int probe_index_in_plane = get_probe_index_in_plane(tex_coords, probe_counts, probe_num_texels);

  return (tex_coords.z * probes_per_plane) + probe_index_in_plane;
}

uint3 get_probe_texel_coords(int probe_index, interlop::DDGIVolDesc vol_desc)
{
  int probes_per_plane = get_probes_per_plane(vol_desc);
  int plane_index      = int(probe_index / probes_per_plane);

  int x = (probe_index % vol_desc.probe_count_x);
  int y = (probe_index / vol_desc.probe_count_x) % vol_desc.probe_count_z;

  return uint3(x, y, plane_index);
}

float3 get_probe_uv(int probe_index, float2 octant_coords, int num_interior_texels, interlop::DDGIVolDesc vol_desc)
{
  uint3 coords = get_probe_texel_coords(probe_index, vol_desc);

  // Add the 2 border texels
  float num_texels = num_interior_texels + 2.0f;

  float tex_width  = num_texels * vol_desc.probe_count_x;
  float tex_height = num_texels * vol_desc.probe_count_z;

  float2 uv        = float2(coords.x * num_texels, coords.y * num_texels) + (num_texels * 0.5f);
  uv              += octant_coords.xy * ((float)num_interior_texels * 0.5f);
  uv              /= float2(tex_width, tex_height);

  return float3(uv, coords.z);
}

// Source for octahedral encoding is where everyone else gets it from: https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
// I really wonder how many graphics things are just from random websites...

// Decodes UV coordinates on [0, 1] square to a normalized direction
float3 octahedral_decode_dir(float2 coords)
{
  coords = coords * 2.0f - 1.0f;

  // Source: https://twitter.com/Stubbesaurus/status/937994790553227264
  float3 n = float3(coords.x, coords.y, 1.0f - abs(coords.x) - abs(coords.y));
  float  t = saturate(-n.z);
  n.xy    += n.xy >= 0.0f ? -t : t;
  return normalize(n);
}

float2 octahedral_wrap(float2 v)
{
  return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
}

// Converts a normalized unit vector to an octahedral coordinate in [0, 1] UV space
float2 octahedral_encode_dir(float3 n)
{
  n       /= abs(n.x) + abs(n.y) + abs(n.z);
  n.xy     = n.z >= 0.0f ? n.xy : octahedral_wrap(n.xy);
  n.xy     = n.xy * 0.5f + 0.5f;
  return n.xy;
}

// When sampling from the 8 probes surrounding a position, we want to get the "base" probe defining the corner of the
// cuboid that is formed by those 8 probes
int3 get_base_probe_grid_coords(float3 ws_pos, interlop::DDGIVolDesc vol_desc)
{
  int3 probe_counts = int3(vol_desc.probe_count_x, vol_desc.probe_count_y, vol_desc.probe_count_z);
  float3 rel_pos    = ws_pos - vol_desc.origin.xyz;

  rel_pos          += (vol_desc.probe_spacing.xyz * (probe_counts - 1)) * 0.5f;

  int3 probe_coords = int3(rel_pos / vol_desc.probe_spacing.xyz);

  return clamp(probe_coords, 0, probe_counts - 1);
}

float3 get_vol_irradiance(float3 ws_pos,
                          float3 surface_bias,
                          float3 normal,
                          interlop::DDGIVolDesc vol_desc,
                          Texture2DArray<float4> probe_irradiance_tex,
                          Texture2DArray<float2> probe_distance_tex)
{
  int3 probe_counts        = int3(vol_desc.probe_count_x, vol_desc.probe_count_y, vol_desc.probe_count_z);

	float3 biased_ws_pos     = ws_pos + surface_bias;

	int3   base_probe_coords = get_base_probe_grid_coords(biased_ws_pos, vol_desc);

	float3 base_probe_dist   = biased_ws_pos - get_probe_ws_pos(base_probe_coords, vol_desc);
	float3 alpha             = clamp(base_probe_dist / vol_desc.probe_spacing.xyz, 0.0f, 1.0f);

	float3 irradiance        = 0.0f;
	float  total_weights     = 0.0f;
	for (int iprobe = 0; iprobe < 8; iprobe++)
	{
    // iprobe = 0 -> (0, 0, 0)
    // iprobe = 1 -> (1, 0, 0)
    // iprobe = 2 -> (0, 1, 0)
    // iprobe = 3 -> (1, 1, 0)
    // iprobe = 4 -> (0, 0, 1)
    // iprobe = 5 -> (1, 0, 1)
    // iprobe = 6 -> (0, 1, 1)
    // iprobe = 7 -> (1, 1, 1)
		int3   adj_coord_offset   = int3(iprobe, iprobe >> 1, iprobe >> 2) & int3(1, 1, 1);

		int3   adj_probe_coords   = clamp(base_probe_coords + adj_coord_offset, 0, probe_counts - 1);

		int    adj_probe_index    = get_probe_index(adj_probe_coords, vol_desc);
		float3 adj_probe_ws_pos   = get_probe_ws_pos(adj_probe_coords, vol_desc);

		float3 ws_to_adj_dir      = normalize(adj_probe_ws_pos - ws_pos);
		float3 biased_to_adj_dir  = normalize(adj_probe_ws_pos - biased_ws_pos);
		float  biased_to_adj_dist = length(adj_probe_ws_pos - biased_ws_pos);

		float3 trilinear          = max(0.001f, lerp(1.0f - alpha, alpha, adj_coord_offset));
		float  trilinear_weight   = trilinear.x * trilinear.y * trilinear.z;
		float  weight             = 1.0f;

		float3 adj_dist_uv = get_probe_uv(adj_probe_index,
                                      octahedral_encode_dir(-biased_to_adj_dir),
                                      kProbeNumDistanceInteriorTexels,
                                      vol_desc);

		weight *= trilinear_weight;

		float3 irradiance_uv = get_probe_uv(adj_probe_index,
                                        octahedral_encode_dir(normal),
                                        kProbeNumIrradianceInteriorTexels,
                                        vol_desc);

		float3 adj_irradiance = probe_irradiance_tex.SampleLevel(g_ClampSampler, irradiance_uv, 0).rgb;

		irradiance    += weight * adj_irradiance;
		total_weights += weight;
	}

	if (total_weights == 0.0f)
    return float3(0.0f, 0.0f, 0.0f);

  // Figure out the weighted average
	irradiance /= total_weights;

  // This gamma-esque correction makes it so that we only really care about the big amounts of indirect lighting.
  // We really don't want the sort of feeling that the entire scene is being illuminated by nothing
  // NOTE that this power is not based on any physical data, it's just what looks good to my eye
  irradiance  = pow(irradiance, 2.1f);

	return irradiance;
}

#endif