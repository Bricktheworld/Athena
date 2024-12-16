#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/taa.h"

#include "Core/Engine/Shaders/interlop.hlsli"

struct TemporalAAParams
{
  RgTexture2D<float4>  prev_hdr;
  RgTexture2D<float4>  curr_hdr;
  RgTexture2D<float2>  prev_velocity;
  RgTexture2D<float2>  curr_velocity;

  RgTexture2D<float>  gbuffer_depth;

  RgRWTexture2D<float4> taa;
};

static void
render_handler_taa(RenderContext* ctx, const void* data)
{
  TemporalAAParams* params = (TemporalAAParams*)data;

  if (!g_Renderer.disable_taa)
  {
    TemporalAASrt srt;
    srt.prev_hdr      = params->prev_hdr;
    srt.curr_hdr      = params->curr_hdr;
    srt.prev_velocity = params->prev_velocity;
    srt.curr_velocity = params->curr_velocity;

    srt.gbuffer_depth = params->gbuffer_depth;
    srt.taa           = params->taa;
    ctx->compute_bind_srt(srt);

    ctx->set_compute_pso(&g_Renderer.taa_pso);
    ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
  }
  else
  {
    TextureCopySrt srt;
    srt.src = params->curr_hdr;
    srt.dst = params->taa;
    ctx->compute_bind_srt(srt);
    ctx->set_compute_pso(&g_Renderer.texture_copy_pso);
    ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
  }
}

RgHandle<GpuTexture>
init_taa_buffer(RgBuilder* builder)
{
  RgHandle<GpuTexture> ret = rg_create_texture_ex(builder, "TAA Buffer", FULL_RES(builder), kGpuFormatRGBA16Float, 1);
  return ret;
}

void
init_taa(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture> hdr_lit, const GBuffer& gbuffer, RgHandle<GpuTexture>* taa_buffer)
{
  TemporalAAParams* params = HEAP_ALLOC(TemporalAAParams, g_InitHeap, 1);
  zero_memory(params, sizeof(TemporalAAParams));

  RgPassBuilder* pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "TAA", params, &render_handler_taa);
  params->prev_hdr      = RgTexture2D<float4>(pass, *taa_buffer, -1);
  params->curr_hdr      = RgTexture2D<float4>(pass, hdr_lit);
  params->prev_velocity = RgTexture2D<float2>(pass, gbuffer.velocity, -1);
  params->curr_velocity = RgTexture2D<float2>(pass, gbuffer.velocity);
  params->gbuffer_depth = RgTexture2D<float>(pass, gbuffer.depth);

  params->taa           = RgRWTexture2D<float4>(pass, taa_buffer);
}
