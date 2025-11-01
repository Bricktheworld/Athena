#include "Core/Engine/memory.h"

#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/renderer.h"

struct RtDiffuseGiPageInitParams
{
  ComputePSO probe_init_pso;
  ComputePSO probe_reproj_pso;

  RgRWTexture2DArray<u32>               page_table;
  RgRWStructuredBuffer<DiffuseGiProbe>  probe_buffer;
  RgTexture2DArray<u32>                 page_table_prev;
};

static void
render_handler_probe_init(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  RtDiffuseGiPageInitParams* params = (RtDiffuseGiPageInitParams*)data;

  static bool s_NeedsInit = true;
  if (s_NeedsInit)
  {
    s_NeedsInit = false;

    RtDiffuseGiProbeInitSrt srt;
    srt.page_table      = params->page_table;
    ctx->compute_bind_srt(srt);
    ctx->set_compute_pso(&params->probe_init_pso);
    ctx->dispatch(ALIGN_POW2(kProbeCountPerClipmap.x, 8) / 8, kProbeCountPerClipmap.y * kProbeClipmapCount, ALIGN_POW2(kProbeCountPerClipmap.z, 8) / 8);
    return;
  }

  if (settings.disable_diffuse_gi)
  {
    return;
  }

  RtDiffuseGiProbeReprojectSrt srt;
  srt.page_table      = params->page_table;
  srt.probe_buffer    = params->probe_buffer;
  srt.page_table_prev = params->page_table_prev;
  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->probe_reproj_pso);
  ctx->dispatch(ALIGN_POW2(kProbeCountPerClipmap.x, 8) / 8, kProbeCountPerClipmap.y * kProbeClipmapCount, ALIGN_POW2(kProbeCountPerClipmap.z, 8) / 8);
}

static void
render_handler_probe_ray_alloc(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  UNREFERENCED_PARAMETER(ctx);
  UNREFERENCED_PARAMETER(settings);
  UNREFERENCED_PARAMETER(data);
}

struct RtDiffuseGiTraceRayParams
{
  ComputePSO probe_trace_ray_pso;

  RgTexture2DArray<u32>                 page_table;
  RgRWStructuredBuffer<GiRayLuminance>  ray_output_buffer;
  RgStructuredBuffer<DiffuseGiProbe>    probe_buffer;
};

static void
render_handler_probe_trace_rays(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  if (settings.disable_diffuse_gi)
  {
    return;
  }

  RtDiffuseGiTraceRayParams* params = (RtDiffuseGiTraceRayParams*)data;

  RtDiffuseGiTraceRaySrt srt;
  srt.rotation          = generate_random_rotation();
  srt.page_table        = params->page_table;
  srt.ray_output_buffer = params->ray_output_buffer;
  srt.probe_buffer      = params->probe_buffer;

  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->probe_trace_ray_pso);
  ctx->dispatch(kProbeCountPerClipmap.x, kProbeCountPerClipmap.y * kProbeClipmapCount, kProbeCountPerClipmap.z);
}

struct RtDiffuseGiProbeBlendParams
{
  ComputePSO probe_blend_pso;

  RgStructuredBuffer<GiRayLuminance>    ray_buffer;
  RgRWStructuredBuffer<DiffuseGiProbe>  probe_buffer;
};

static void
render_handler_probe_blend(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  if (settings.disable_diffuse_gi)
  {
    return;
  }

  RtDiffuseGiProbeBlendParams* params = (RtDiffuseGiProbeBlendParams*)data;

  RtDiffuseGiProbeBlendSrt srt;
  srt.ray_buffer   = params->ray_buffer;
  srt.probe_buffer = params->probe_buffer;

  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->probe_blend_pso);
  ctx->dispatch(ALIGN_POW2(kProbeMaxActiveCount, 64) / 64, 1, 1);
}

DiffuseGiResources
init_rt_diffuse_gi(AllocHeap heap, RgBuilder* builder)
{
  RgHandle<GpuTexture> probe_page_table = rg_create_texture_array_ex(
    builder,
    "RT Diffuse GI - Probe Page Table",
    kProbeCountPerClipmap.x,
    kProbeCountPerClipmap.z,
    (u16)(kProbeCountPerClipmap.y * kProbeClipmapCount),
    kGpuFormatR32Uint,
    1 // Need previous frame to copy to current frame
  );

  RgHandle<GpuBuffer> probe_buffer           = rg_create_buffer(builder, "RT Diffuse GI - Luminance Probe Buffer", sizeof(DiffuseGiProbe) * kProbeMaxActiveCount);
  RgHandle<GpuBuffer> ray_luminance          = rg_create_buffer(builder, "RT Diffuse GI - Ray Luminance Data",     sizeof(GiRayLuminance) * kProbeMaxRayCount);

  {
    RtDiffuseGiPageInitParams* params = HEAP_ALLOC(RtDiffuseGiPageInitParams, g_InitHeap, 1);

    params->probe_init_pso   = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiPageTableInit),      "RT Diffuse GI Page Table Init");
    params->probe_reproj_pso = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiPageTableReproject), "RT Diffuse GI Page Table Reproject");

    RgPassBuilder* pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "RT Diffuse GI - Probe Init",  params, &render_handler_probe_init);
    params->page_table       = RgRWTexture2DArray<u32>(pass, &probe_page_table);
    params->probe_buffer     = RgRWStructuredBuffer<DiffuseGiProbe>(pass, &probe_buffer);
    params->page_table_prev  = RgTexture2DArray<u32>(pass, probe_page_table, -1);
  }
  {
    RtDiffuseGiTraceRayParams* params = HEAP_ALLOC(RtDiffuseGiTraceRayParams, g_InitHeap, 1);

    params->probe_trace_ray_pso = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiTraceRays), "RT Diffuse GI Trace Ray");

    RgPassBuilder* pass       = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "RT Diffuse GI - Probe Trace Ray", params, &render_handler_probe_trace_rays);
    params->page_table        = RgTexture2DArray<u32>(pass, probe_page_table);
    params->probe_buffer      = RgStructuredBuffer<DiffuseGiProbe>(pass, probe_buffer);
    params->ray_output_buffer = RgRWStructuredBuffer<GiRayLuminance>(pass, &ray_luminance);
  }
  {
    RtDiffuseGiProbeBlendParams* params = HEAP_ALLOC(RtDiffuseGiProbeBlendParams, g_InitHeap, 1);

    params->probe_blend_pso   = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiProbeBlend),  "RT Diffuse GI Probe Blend");

    RgPassBuilder* pass       = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "RT Diffuse GI - Probe Blend", params, &render_handler_probe_blend);
    params->ray_buffer        = RgStructuredBuffer<GiRayLuminance>(pass, ray_luminance);
    params->probe_buffer      = RgRWStructuredBuffer<DiffuseGiProbe>(pass, &probe_buffer);
  }

  DiffuseGiResources ret;
  ret.probe_buffer     = probe_buffer;
  ret.probe_page_table = probe_page_table;

  return ret;
}

ReadDiffuseGi
read_diffuse_gi(RgPassBuilder* pass, const DiffuseGiResources& resources)
{
  ReadDiffuseGi ret;
  ret.diffuse_probes = RgStructuredBuffer<DiffuseGiProbe>(pass, resources.probe_buffer);
  ret.page_table     = RgTexture2DArray<u32>(pass, resources.probe_page_table);
  return ret;
}