#include "Core/Engine/memory.h"

#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/Include/ddgi_common.hlsli"

struct RtDiffuseGiPageInitParams
{
  ComputePSO probe_init_pso;
  ComputePSO probe_reproj_pso;

  RgRWTexture2DArray<u16>               page_table;
  RgRWStructuredBuffer<DiffuseGiProbe>  probe_buffer;
  RgTexture2DArray<u16>                 page_table_prev;
  RgConstantBuffer<RtDiffuseGiSettings> settings;
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
    ctx->dispatch(1, kProbeCountPerClipmap.y * kProbeClipmapCount, 1);
    return;
  }

  RtDiffuseGiSettings gi_settings;
  gi_settings.probe_spacing[0] = settings.probe_spacing;
  gi_settings.probe_spacing[1] = 1.5f * gi_settings.probe_spacing[0];
  gi_settings.probe_spacing[2] = 1.5f * gi_settings.probe_spacing[1];

  ctx->write_cpu_upload_buffer(params->settings, &gi_settings, sizeof(gi_settings));

  RtDiffuseGiProbeReprojectSrt srt;
  srt.page_table      = params->page_table;
  srt.probe_buffer    = params->probe_buffer;
  srt.page_table_prev = params->page_table_prev;
  srt.settings        = params->settings;
  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->probe_reproj_pso);
  ctx->dispatch(kProbeCountPerClipmap.x / 8, kProbeCountPerClipmap.y * kProbeClipmapCount, kProbeCountPerClipmap.z / 8);
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

  RgTexture2DArray<u16>                 page_table;
  RgRWStructuredBuffer<GiRayLuminance>  ray_output_buffer;
  RgConstantBuffer<RtDiffuseGiSettings> settings;
};

static void
render_handler_probe_trace_rays(RenderContext* ctx, const RenderSettings&, const void* data)
{
  RtDiffuseGiTraceRayParams* params = (RtDiffuseGiTraceRayParams*)data;

  RtDiffuseGiTraceRaySrt srt;
  srt.rotation          = generate_random_rotation();
  srt.page_table        = params->page_table;
  srt.ray_output_buffer = params->ray_output_buffer;
  srt.settings          = params->settings;

  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->probe_trace_ray_pso);
  ctx->dispatch(kProbeCountPerClipmap.x, kProbeCountPerClipmap.y * kProbeClipmapCount, kProbeCountPerClipmap.z);
}

static void
render_handler_probe_blend(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  UNREFERENCED_PARAMETER(ctx);
  UNREFERENCED_PARAMETER(settings);
  UNREFERENCED_PARAMETER(data);
}

void
init_rt_diffuse_gi(AllocHeap heap, RgBuilder* builder)
{
  RgHandle<GpuTexture> probe_page_table = rg_create_texture_array_ex(
    builder,
    "RT Diffuse GI - Probe Page Table",
    kProbeCountPerClipmap.x,
    kProbeCountPerClipmap.z,
    (u16)(kProbeCountPerClipmap.y * kProbeClipmapCount),
    kGpuFormatR16Uint,
    1 // Need previous frame to copy to current frame
  );

  RgHandle<GpuBuffer> rt_diffuse_gi_settings = rg_create_upload_buffer(builder, "RT Diffuse GI - Settings", kGpuHeapSysRAMCpuToGpu, sizeof(RtDiffuseGiSettings));
  RgHandle<GpuBuffer> probe_buffer           = rg_create_buffer(builder, "RT Diffuse GI - Luminance Probe Buffer", sizeof(DiffuseGiProbe) * kProbeMaxActiveCount);
  RgHandle<GpuBuffer> ray_luminance          = rg_create_buffer(builder, "RT Diffuse GI - Ray Luminance Data",     sizeof(GiRayLuminance) * kProbeMaxRayCount);

  {
    RtDiffuseGiPageInitParams* params = HEAP_ALLOC(RtDiffuseGiPageInitParams, g_InitHeap, 1);

    params->probe_init_pso   = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiPageTableInit),      "RT Diffuse GI Page Table Init");
    params->probe_reproj_pso = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiPageTableReproject), "RT Diffuse GI Page Table Reproject");

    RgPassBuilder* pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "RT Diffuse GI - Probe Init",  params, &render_handler_probe_init);
    params->page_table       = RgRWTexture2DArray<u16>(pass, &probe_page_table);
    params->probe_buffer     = RgRWStructuredBuffer<DiffuseGiProbe>(pass, &probe_buffer);
    params->page_table_prev  = RgTexture2DArray<u16>(pass, probe_page_table, -1);
    params->settings         = RgConstantBuffer<RtDiffuseGiSettings>(pass, rt_diffuse_gi_settings);
  }
  {
    RtDiffuseGiTraceRayParams* params = HEAP_ALLOC(RtDiffuseGiTraceRayParams, g_InitHeap, 1);

    params->probe_trace_ray_pso = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtDiffuseGiTraceRays), "RT Diffuse GI Trace Ray");

    RgPassBuilder* pass       = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "RT Diffuse GI - Probe Trace Ray", params, &render_handler_probe_trace_rays);
    params->page_table        = RgTexture2DArray<u16>(pass, probe_page_table);
    params->ray_output_buffer = RgRWStructuredBuffer<GiRayLuminance>(pass, &ray_luminance);
    params->settings          = RgConstantBuffer<RtDiffuseGiSettings>(pass, rt_diffuse_gi_settings);
  }
}