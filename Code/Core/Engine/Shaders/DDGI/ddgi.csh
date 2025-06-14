#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/ddgi_common.hlsli"


ConstantBuffer<RtDiffuseGiProbeInitSrt> g_ProbeInitSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 1, 8)]
void CS_RtDiffuseGiPageTableInit( uint3 thread_id : SV_DispatchThreadID )
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

ConstantBuffer<RtDiffuseGiProbeReprojectSrt> g_ProbeReprojectSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 1, 8)]
void CS_RtDiffuseGiPageTableReproject( uint3 thread_id : SV_DispatchThreadID )
{
  ConstantBuffer<RtDiffuseGiSettings> settings        = DEREF(g_ProbeReprojectSrt.settings);
  RWStructuredBuffer<DiffuseGiProbe>  probe_buffer    = DEREF(g_ProbeReprojectSrt.probe_buffer);
  RWTexture2DArray<u16>               page_table      = DEREF(g_ProbeReprojectSrt.page_table);
  Texture2DArray<u16>                 page_table_prev = DEREF(g_ProbeReprojectSrt.page_table_prev);

  float3 prev_ws_pos         = g_ViewportBuffer.prev_camera_world_pos.xyz;
  float3 ws_pos              = g_ViewportBuffer.camera_world_pos.xyz;

  uint   clipmap_idx         = thread_id.y / kProbeCountPerClipmap.y;

  Vec3   probe_spacing       = settings.probe_spacing[clipmap_idx];
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

  int3 src_tex_coord = src_coord.xzy;
  src_tex_coord.z   += clipmap_idx * kProbeCountPerClipmap.y;

  if (clipmap_diff.x == 0 && clipmap_diff.y == 0 && clipmap_diff.z == 0)
  {
    page_table[src_tex_coord] = page_table_prev[src_tex_coord];
    return;
  }

  int3 reproj_coord = src_coord + clipmap_diff;

  // Logic to figure out if we need to reset the probe because it went offscreen
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

  int3 dst_tex_coord = reproj_coord.xzy;
  dst_tex_coord.z   += clipmap_idx * kProbeCountPerClipmap.y;

  page_table[dst_tex_coord] = page_table_prev[src_tex_coord];
}
