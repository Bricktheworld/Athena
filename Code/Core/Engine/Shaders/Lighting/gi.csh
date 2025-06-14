#include "../root_signature.hlsli"
#include "../interlop.hlsli"

struct GiProbePageMapSrt
{
  RWTexture2DPtr<uint> page_table;
  RWBufferPtr<int4>    probe_to_coord;
};

#ifndef __cplusplus
#include "../include/spherical_harmonics.hlsli"

ConstantBuffer<GiProbePageMapSrt> g_ProbePageMapSrt : register(b0);

// This is technically low occupancy but it should be fast
[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(16, 16, 16)]
void CS _GiProbePageMap(uint3 thread_id : SV_GroupThreadID, uint group_id : SV_GroupID)
{
  RWTexture2D<uint> page_table     = DEREF(g_ProbePageMapSrt.page_table);
  RWBuffer<uint4>   probe_to_coord = DEREF(g_ProbePageMapSrt.probe_to_coord);

  uint  clipmap_idx      = group_id;
  int3  clipmap_coord    = (int3)thread_id.xyz - kProbeCountPerClipmap / 2;
  uint3 page_table_coord = uint3(thread_id.xz, thread_id.y + group_id * kProbeCounterPerClipmap.y);

  // TODO(bshihabi): Make these configurable
  f32   kClipmapSpacing[3] =
  {
    0.5f,
    2.5f,
    5.0f,
  };

  int3  prev_clipmap_center = int3(g_ViewportBuffer.prev_camera_world_pos.xyz / kClipmapSpacing[clipmap_idx]);
  int3  curr_clipmap_center = int3(g_ViewportBuffer.camera_world_pos.xyz      / kClipmapSpacing[clipmap_idx]);
  uint  prev_probe_idx      = page_table[page_table_coord];
}

#if 0
[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_ GiProbeBlend(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
  ConstantBuffer<DDGIVolDesc> vol_desc   = DEREF(g_Srt.vol_desc);
  Texture2DArray<float4>      ray_data   = DEREF(g_Srt.ray_data);
  RWTexture2DArray<float3>    radiance   = DEREF(g_Srt.radiance);

  SH::L2_RGB radiance_sh = SH::L2_RGB::Zero();
  for (int iray = 0; iray < vol_desc.probe_num_rays; iray++)
  {
    float3 sampled_ray_dir     = get_probe_ray_dir(iray, vol_desc);
    uint3  sample_texel_coords = get_ray_data_texel_coords(iray, probe_index, vol_desc);
    float3 sample_radiance     = ray_data[sample_texel_coords].rgb;
    float  sample_distance     = ray_data[sample_texel_coords].a;

    // If the sample distance is negative, that means that it hit a backface and we definitely don't
    // want to blend it
    if (sample_distance < 0.0f)
    {
      continue;
    }

    radiance_sh += SH::ProjectOntoL2(sampled_ray_dir, sample_radiance);
  }
  SH::L2_RGB prev_radiance_sh;

  radiance[]

  prev_radiance_sh.C[0] = 
  prev_radiance_sh.C[1] = 
  prev_radiance_sh.C[2] =
}
#endif
#endif
