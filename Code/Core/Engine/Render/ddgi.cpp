#include "Core/Engine/memory.h"

#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/interlop.hlsli"

struct ProbeTraceParams
{
  DDGIVolDesc               vol_desc = {0};
  RgReadHandle<GpuBuffer>   vol_desc_buffer;
  RgWriteHandle<GpuTexture> ray_data;
  RgReadHandle<GpuTexture>  irradiance;
};

static void
render_handler_probe_trace(RenderContext* ctx, const void* data)
{
  ProbeTraceParams* params = (ProbeTraceParams*)data;
  params->vol_desc.probe_ray_rotation = generate_random_rotation();

  ctx->write_cpu_upload_buffer(params->vol_desc_buffer, &params->vol_desc, sizeof(params->vol_desc));

  ctx->ray_tracing_bind_shader_resources<ProbeTraceRTResources>(
    {
      .vol_desc = params->vol_desc_buffer,
      .probe_irradiance = params->irradiance,
      .probe_distance = params->irradiance,
      .ray_data = params->ray_data
    }
  );

  ctx->set_ray_tracing_pso(&g_Renderer.ddgi_probe_trace_pso);
  ctx->dispatch_rays(
    &g_Renderer.ddgi_probe_trace_st,
    params->vol_desc.probe_num_rays,
    params->vol_desc.probe_count_x * params->vol_desc.probe_count_z,
    params->vol_desc.probe_count_y
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

  RgPassBuilder*    pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "DDGI Probe Trace", params, &render_handler_probe_trace, 4, 1);
  params->vol_desc_buffer  = rg_read_buffer  (pass, vol_desc_buffer, kReadBufferCbv);
  params->ray_data         = rg_write_texture(pass, ray_data,        kWriteTextureUav);
  params->irradiance       = rg_read_texture (pass, irradiance,      kReadTextureSrvNonPixelShader);
}

struct ProbeBlendParams
{
  DDGIVolDesc               vol_desc;
  RgReadHandle<GpuBuffer>   vol_desc_buffer;
  RgReadHandle<GpuTexture>  ray_data;
  RgWriteHandle<GpuTexture> irradiance;
};

static void
render_handler_probe_blend(RenderContext* ctx, const void* data)
{
  ProbeBlendParams* params = (ProbeBlendParams*)data;

  ctx->compute_bind_shader_resources<ProbeBlendingCSResources>(
    {
      .vol_desc   = params->vol_desc_buffer,
      .ray_data   = params->ray_data,
      .irradiance = params->irradiance,
    }
  );

  ctx->set_compute_pso(&g_Renderer.ddgi_probe_blend_pso);
  ctx->dispatch(
    params->vol_desc.probe_count_x,
    params->vol_desc.probe_count_z,
    params->vol_desc.probe_count_y
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


  RgPassBuilder*    pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "DDGI Probe Blend", params, &render_handler_probe_blend, 2, 1);
  params->vol_desc_buffer  = rg_read_buffer  (pass, vol_desc_buffer, kReadBufferCbv);
  params->ray_data         = rg_read_texture (pass, ray_data,        kReadTextureSrv);
  params->irradiance       = rg_write_texture(pass, irradiance,      kWriteTextureUav);
}

struct ProbeDebugParams
{
  ReadDdgi ddgi;
  RgWriteHandle<GpuTexture> render_target;
};

static void
render_handler_probe_debug(RenderContext* ctx, const void* data)
{
  ProbeDebugParams* params = (ProbeDebugParams*)data;
  UNREFERENCED_PARAMETER(ctx);
  UNREFERENCED_PARAMETER(params);
}

static void
init_probe_debug(
  AllocHeap heap,
  RgBuilder* builder,
  const Ddgi& ddgi,
  RgHandle<GpuTexture>* dst
) {
  ProbeDebugParams* params = HEAP_ALLOC(ProbeDebugParams, g_InitHeap, 1);
  zero_memory(params, sizeof(ProbeDebugParams));

  RgPassBuilder*    pass   = add_render_pass (heap, builder, kCmdQueueTypeGraphics, "DDGI Probe Debug", params, &render_handler_probe_debug, 1, 1);
  params->ddgi             = read_ddgi       (pass, ddgi, kReadTextureSrv);
  params->render_target    = rg_write_texture(pass, dst,  kWriteTextureUav);
}

Ddgi
init_ddgi(AllocHeap heap, RgBuilder* builder)
{
  DDGIVolDesc desc = {0};
  desc.origin                 = Vec4(0.0f, 5.0f, 0.0f, 0.0f);
  desc.probe_spacing          = Vec4(0.5f, 0.5f, 0.5f, 0.0f);
  desc.probe_count_x          = 22;
  desc.probe_count_y          = 5;
  desc.probe_count_z          = 22;

  desc.probe_num_rays         = 128;
  desc.probe_hysteresis       = 0.97f;
  desc.probe_max_ray_distance = 20.0f;
  desc.probe_max_ray_distance = 20.0f;

  RgHandle<GpuBuffer> vol_desc_buffer = rg_create_upload_buffer(builder, "DDGI Vol Desc", sizeof(DDGIVolDesc));
  RgHandle<GpuTexture>  probe_ray_data  = rg_create_texture_array(
    builder,
    "Probe Ray Data",
    desc.probe_num_rays,
    desc.probe_count_x * desc.probe_count_z,
    desc.probe_count_y,
    DXGI_FORMAT_R16G16B16A16_FLOAT
  );

  RgHandle<GpuTexture> probe_irradiance = rg_create_texture_array_ex(
    builder,
    "Probe Irradiance",
    desc.probe_count_x * kProbeNumIrradianceTexels,
    desc.probe_count_z * kProbeNumIrradianceTexels,
    desc.probe_count_y,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
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
read_ddgi(RgPassBuilder* pass_builder, const Ddgi& ddgi, ReadTextureAccessMask access)
{
  ReadDdgi ret = {0};

  ret.desc       = rg_read_buffer(pass_builder,  ddgi.desc,       kReadBufferCbv);;
  ret.irradiance = rg_read_texture(pass_builder, ddgi.irradiance, access);

  return ret;
}
