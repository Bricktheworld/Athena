#include "Core/Engine/memory.h"

#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/interlop.hlsli"

struct ProbeTraceParams
{
  DDGIVolDesc                   vol_desc = {};
  RgConstantBuffer<DDGIVolDesc> vol_desc_buffer;
  RgRWTexture2DArray<float4>    ray_data;
  RgTexture2DArray<float4>      irradiance;
};

static void
render_handler_probe_trace(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  ProbeTraceParams* params = (ProbeTraceParams*)data;
  if (!settings.freeze_probe_rotation)
  {
    params->vol_desc.probe_ray_rotation = generate_random_rotation();
  }

  params->vol_desc.debug_ray_probe = settings.debug_probe_ray_idx;
  params->vol_desc.probe_spacing   = settings.probe_spacing;
  ctx->write_cpu_upload_buffer(params->vol_desc_buffer, &params->vol_desc, sizeof(params->vol_desc));

  ProbeTraceSrt srt;
  srt.vol_desc         = params->vol_desc_buffer;
  srt.probe_irradiance = params->irradiance;
  srt.ray_data         = params->ray_data;
  ctx->ray_tracing_bind_srt(srt);

  ctx->set_ray_tracing_pso(&g_Renderer.ddgi_probe_trace_pso);
  ctx->dispatch_rays(
    &g_Renderer.ddgi_probe_trace_st,
    params->vol_desc.probe_num_rays,
    params->vol_desc.probe_count.x * params->vol_desc.probe_count.z,
    params->vol_desc.probe_count.y
  );
}

static void
init_probe_trace(
  AllocHeap heap,
  RgBuilder* builder,
  const DDGIVolDesc& desc,
  RgHandle<GpuBuffer> vol_desc_buffer,
  RgHandle<GpuTexture>  irradiance,
  RgHandle<GpuTexture>* ray_data
) {
  ProbeTraceParams* params = HEAP_ALLOC(ProbeTraceParams, g_InitHeap, 1);
  zero_memory(params, sizeof(ProbeTraceParams));
  params->vol_desc         = desc;

  RgPassBuilder*    pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "DDGI Probe Trace", params, &render_handler_probe_trace);
  params->vol_desc_buffer  = RgConstantBuffer<DDGIVolDesc>(pass, vol_desc_buffer);
  params->ray_data         = RgRWTexture2DArray<float4>   (pass, ray_data);
  params->irradiance       = RgTexture2DArray<float4>     (pass, irradiance);
}

struct ProbeBlendParams
{
  DDGIVolDesc                   vol_desc;

  RgConstantBuffer<DDGIVolDesc> vol_desc_buffer;
  RgTexture2DArray<float4>      ray_data;
  RgRWTexture2DArray<float4>    irradiance;
};

static void
render_handler_probe_blend(RenderContext* ctx, const RenderSettings&, const void* data)
{
  ProbeBlendParams* params = (ProbeBlendParams*)data;

  ProbeBlendingSrt srt;
  srt.vol_desc   = params->vol_desc_buffer;
  srt.ray_data   = params->ray_data;
  srt.irradiance = params->irradiance;
  ctx->compute_bind_srt(srt);

  ctx->set_compute_pso(&g_Renderer.ddgi_probe_blend_pso);
  ctx->dispatch(
    params->vol_desc.probe_count.x,
    params->vol_desc.probe_count.z,
    params->vol_desc.probe_count.y
  );
}

static void
init_probe_blend(
  AllocHeap heap,
  RgBuilder* builder,
  const DDGIVolDesc& desc,
  RgHandle<GpuBuffer> vol_desc_buffer,
  RgHandle<GpuTexture>  ray_data,
  RgHandle<GpuTexture>* irradiance
) {
  ProbeBlendParams* params = HEAP_ALLOC(ProbeBlendParams, g_InitHeap, 1);
  zero_memory(params, sizeof(ProbeBlendParams));
  params->vol_desc         = desc;


  RgPassBuilder*    pass  = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "DDGI Probe Blend", params, &render_handler_probe_blend);
  params->vol_desc_buffer = RgConstantBuffer<DDGIVolDesc>(pass, vol_desc_buffer);
  params->ray_data        = RgTexture2DArray<float4>(pass, ray_data);
  params->irradiance      = RgRWTexture2DArray<float4>(pass, irradiance);
}

Ddgi
init_ddgi(AllocHeap heap, RgBuilder* builder)
{
  DDGIVolDesc desc = {};
  desc.origin                 = Vec4(0.0f, 5.0f, 0.0f, 0.0);
  desc.probe_spacing          = Vec4(0.5f, 0.5f, 0.5f, 0.0);
  desc.probe_count            = UVec3(22, 5, 10);

  desc.probe_num_rays         = 128;
  desc.probe_hysteresis       = 0.97f;
  desc.probe_max_ray_distance = 20.0f;
  desc.probe_max_ray_distance = 20.0f;

  RgHandle<GpuBuffer>  vol_desc_buffer = rg_create_upload_buffer(builder, "DDGI Vol Desc", kGpuHeapSysRAMCpuToGpu, sizeof(DDGIVolDesc));
  RgHandle<GpuTexture> probe_ray_data  = rg_create_texture_array(
    builder,
    "Probe Ray Data",
    desc.probe_num_rays,
    desc.probe_count.x * desc.probe_count.z,
    (u16)desc.probe_count.y,
    kGpuFormatRGBA16Float
  );

  RgHandle<GpuTexture> probe_irradiance = rg_create_texture_array_ex(
    builder,
    "Probe Irradiance",
    desc.probe_count.x * kProbeNumIrradianceTexels,
    desc.probe_count.z * kProbeNumIrradianceTexels,
    (u16)desc.probe_count.y,
    kGpuFormatRGBA16Float,
    kInfiniteLifetime // We want the irradiance data from the previous frame to blend with on the current frame
  );

  init_probe_trace(
    heap,
    builder,
    desc,
    vol_desc_buffer,
    probe_irradiance,
    &probe_ray_data
  );

  init_probe_blend(
    heap,
    builder,
    desc,
    vol_desc_buffer,
    probe_ray_data,
    &probe_irradiance
  );

  Ddgi ret       = {0};
  ret.desc       = vol_desc_buffer;
  ret.irradiance = probe_irradiance;

  return ret;
}

ReadDdgi
read_ddgi(RgPassBuilder* pass_builder, const Ddgi& ddgi)
{
  ReadDdgi ret;

  ret.desc       = RgConstantBuffer<DDGIVolDesc>(pass_builder, ddgi.desc);
  ret.irradiance = RgTexture2DArray<float4>(pass_builder, ddgi.irradiance);

  return ret;
}
