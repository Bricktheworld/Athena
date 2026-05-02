#include "Core/Foundation/math.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/ddgi.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/Include/ddgi_common.hlsli"

void
render_handler_rt_diffuse_gi_init(const RenderEntry*, u32)
{
  CmdList*       cmd     = &g_RenderHandlerState.cmd_list;
  RenderBuffers* buffers = &g_RenderHandlerState.buffers;

  static bool s_NeedsInit = true;
  if (s_NeedsInit)
  {
    s_NeedsInit = false;

    gpu_texture_layout_transition(cmd, &buffers->probe_page_table.get_temporal(0)->texture, kGpuTextureLayoutUnorderedAccess);

    RtDiffuseGiProbeInitSrt srt;
    srt.page_table   = *buffers->probe_page_table.get_temporal(0);
    srt.probe_buffer = buffers->probe_buffer;
    gpu_bind_compute_pso(cmd, kCS_RtDiffuseGiPageTableInit);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, ALIGN_POW2(kProbeCountPerClipmap.x, 8) / 8, kProbeCountPerClipmap.y * kProbeClipmapCount, ALIGN_POW2(kProbeCountPerClipmap.z, 8) / 8);

    gpu_memory_barrier(cmd);
    gpu_texture_layout_transition(cmd, &buffers->probe_page_table.get_temporal(0)->texture, kGpuTextureLayoutShaderResource);
    return;
  }

  gpu_texture_layout_transition(cmd, &buffers->probe_page_table.get_temporal( 0)->texture, kGpuTextureLayoutUnorderedAccess);
  gpu_texture_layout_transition(cmd, &buffers->probe_page_table.get_temporal(-1)->texture, kGpuTextureLayoutShaderResource);

  gpu_clear_buffer_u32(cmd, buffers->probe_atomic_counters, 2, 0, 0);
  gpu_memory_barrier(cmd);

  {
    RtDiffuseGiProbeReprojectSrt srt;
    srt.page_table      = *buffers->probe_page_table.get_temporal(0);
    srt.probe_buffer    = buffers->probe_buffer;
    srt.page_table_prev = *buffers->probe_page_table.get_temporal(-1);
    srt.atomic_counters = buffers->probe_atomic_counters;
    gpu_bind_compute_pso(cmd, kCS_RtDiffuseGiPageTableReproject);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, ALIGN_POW2(kProbeCountPerClipmap.x, 8) / 8, kProbeCountPerClipmap.y * kProbeClipmapCount, ALIGN_POW2(kProbeCountPerClipmap.z, 8) / 8);
  }

  gpu_texture_layout_transition(cmd, &buffers->probe_page_table.get_temporal(0)->texture, kGpuTextureLayoutShaderResource);
  gpu_memory_barrier(cmd);

  {
    RtDiffuseGiProbeAllocRaysSrt srt;
    srt.page_table        = *buffers->probe_page_table.get_temporal(0);
    srt.probe_buffer      = buffers->probe_buffer;
    srt.ray_output_buffer = buffers->ray_luminance;
    srt.ray_allocs        = buffers->ray_allocs;
    srt.atomic_counters   = buffers->probe_atomic_counters;
    gpu_bind_compute_pso(cmd, kCS_RtDiffuseGiAllocRays);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, ALIGN_POW2(kProbeCountPerClipmap.x, 8) / 8, kProbeCountPerClipmap.y * kProbeClipmapCount, ALIGN_POW2(kProbeCountPerClipmap.z, 8) / 8);
  }

  gpu_memory_barrier(cmd);
}

void
render_handler_rt_diffuse_gi_trace_rays(const RenderEntry*, u32)
{
  RenderBuffers* buffers = &g_RenderHandlerState.buffers;

  RtDiffuseGiTraceRaySrt srt;
  srt.rotation          = generate_random_rotation();
  srt.page_table        = *buffers->probe_page_table.get_temporal(0);
  srt.probe_buffer      = buffers->probe_buffer;
  srt.ray_output_buffer = buffers->ray_luminance;
  srt.ray_alloc_args    = buffers->ray_allocs;
  gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_RtDiffuseGiTraceRays);
  gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);
  gpu_dispatch(&g_RenderHandlerState.cmd_list, kProbeCountPerClipmap.x, kProbeCountPerClipmap.y * kProbeClipmapCount, kProbeCountPerClipmap.z);

  gpu_memory_barrier(&g_RenderHandlerState.cmd_list);
}

void
render_handler_rt_diffuse_gi_probe_blend(const RenderEntry*, u32)
{
  RenderBuffers* buffers = &g_RenderHandlerState.buffers;

  RtDiffuseGiProbeBlendSrt srt;
  srt.ray_buffer     = buffers->ray_luminance;
  srt.probe_buffer   = buffers->probe_buffer;
  srt.ray_alloc_args = buffers->ray_allocs;
  gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_RtDiffuseGiProbeBlend);
  gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);
  gpu_dispatch(&g_RenderHandlerState.cmd_list, ALIGN_POW2(kProbeMaxActiveCount, 64) / 64, 1, 1);

  gpu_memory_barrier(&g_RenderHandlerState.cmd_list);
}
