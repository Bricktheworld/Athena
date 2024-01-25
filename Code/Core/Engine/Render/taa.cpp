#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/taa.h"

#include "Core/Engine/Shaders/interlop.hlsli"

struct TAAParams
{
  RgReadHandle<GpuTexture>  prev;
  RgReadHandle<GpuTexture>  velocity;
  RgWriteHandle<GpuTexture> curr;
};

static void
render_handler_taa(RenderContext* ctx, const void* data)
{
  TAAParams* params = (TAAParams*)data;

  if (g_Renderer.disable_taa)
    return;

  ctx->compute_bind_shader_resources<interlop::TAAResources>(
    {
      .prev     = params->prev,
      .velocity = params->velocity,
      .curr     = params->curr,
    }
  );

  ctx->set_compute_pso(&g_Renderer.taa_pso);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

void
init_taa(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* hdr_lit, const GBuffer& gbuffer)
{
  TAAParams* params = HEAP_ALLOC(TAAParams, g_InitHeap, 1);
  zero_memory(params, sizeof(TAAParams));

  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "TAA", params, &render_handler_taa, 2, 1);
  params->prev        = rg_read_texture(pass, *hdr_lit, kReadTextureSrv, -1);
  params->velocity    = rg_read_texture(pass, gbuffer.velocity, kReadTextureSrv);
  params->curr        = rg_write_texture(pass, hdr_lit, kWriteTextureUav);
}
