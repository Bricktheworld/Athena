#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Shaders/interlop.hlsli"

struct ToneMappingParams
{
  RgTexture2D<float4> hdr_buffer;
  RgRtv               dst;
};

static void 
render_handler_tonemapping(RenderContext* ctx, const void* data)
{
  const ToneMappingParams* params = (const ToneMappingParams*)data;
  ctx->clear_render_target_view(params->dst, Vec4(0.0f, 0.0f, 0.0f, 0.0f));

  ctx->om_set_render_targets({params->dst}, None);

  ToneMappingSrt srt;
  srt.texture     = params->hdr_buffer;
  srt.disable_hdr = g_Renderer.disable_hdr;

  ctx->graphics_bind_srt(srt);
  ctx->set_graphics_pso(&g_Renderer.tonemapping_pso);

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

  return ret;
}

struct CoCGenerateParams
{
  RgTexture2D<float4>  hdr_buffer;
  RgRWTexture2D<float> coc_buffer;
  ComputePSO           pso;
};

static void
render_handler_coc_generate(RenderContext* ctx, const void* data)
{
  const CoCGenerateParams* params = (const CoCGenerateParams*)data;
  UNREFERENCED_PARAMETER(params);
  UNREFERENCED_PARAMETER(ctx);

  // ctx->set_compute_pso(&params->pso);
  // DofCocSrt srt;

  // ctx->compute_bind_srt();
}

void init_depth_of_field(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> hdr_buffer
) {
  RgHandle<GpuTexture> coc_buffer = rg_create_texture(builder, "CoC Buffer", FULL_RES(builder), kGpuFormatR16Float);

  {
    CoCGenerateParams* params = HEAP_ALLOC(CoCGenerateParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field CoC Generate", params, &render_handler_coc_generate);
    params->hdr_buffer        = RgTexture2D<float4>(pass,  hdr_buffer);
    params->coc_buffer        = RgRWTexture2D<float>(pass, &coc_buffer);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFCoC), "Depth of Field CoC Generate");
  }
}

