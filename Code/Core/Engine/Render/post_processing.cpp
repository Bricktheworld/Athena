#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Shaders/interlop.hlsli"

struct ToneMappingParams
{
  RgTexture2D<float4> hdr_buffer;
  RgRtv               dst;

  GraphicsPSO         pso;
};

static void 
render_handler_tonemapping(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  const ToneMappingParams* params = (const ToneMappingParams*)data;
  ctx->clear_render_target_view(params->dst, Vec4(0.0f, 0.0f, 0.0f, 0.0f));

  ctx->om_set_render_targets({params->dst}, None);

  ToneMappingSrt srt;
  srt.texture     = params->hdr_buffer;
  srt.disable_hdr = settings.disable_hdr;

  ctx->graphics_bind_srt(srt);
  ctx->set_graphics_pso(&params->pso);

  ctx->draw_instanced(3, 1, 0, 0);
}

RgHandle<GpuTexture>
init_tonemapping(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> hdr_buffer
) {
  ToneMappingParams* params = HEAP_ALLOC(ToneMappingParams, g_InitHeap, 1);
  zero_memory(params, sizeof(ToneMappingParams));

  RgPassBuilder* pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Tone Mapping", params, &render_handler_tonemapping);

  RgHandle<GpuTexture> ret = rg_create_texture(builder, "Tone Mapping Buffer", FULL_RES(builder), kGpuFormatRGBA16Float);

  params->hdr_buffer       = RgTexture2D<float4>(pass, hdr_buffer);
  params->dst              = RgRtv(pass, &ret);

  GraphicsPipelineDesc desc = 
  {
    .vertex_shader = get_engine_shader(kVS_Fullscreen),
    .pixel_shader  = get_engine_shader(kPS_ToneMapping),
    .rtv_formats   = Span{kGpuFormatRGBA16Float},
  };
  params->pso              = init_graphics_pipeline(g_GpuDevice, desc, "Tone Mapping");

  return ret;
}
