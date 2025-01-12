#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/blit.h"

#include "Core/Engine/Shaders/interlop.hlsli"

struct BlitParams
{
  RgTexture2D<float4> src;
  RgRtv               dst;
};

static void
render_handler_back_buffer_blit(RenderContext* ctx, const RenderSettings&, const void* data)
{
  BlitParams* params = (BlitParams*)data;

  ctx->set_graphics_pso(&g_Renderer.back_buffer_blit_pso);

  ctx->clear_render_target_view(params->dst, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
  ctx->om_set_render_targets({params->dst}, None);

  ctx->graphics_bind_srt<FullscreenSrt>({.texture = params->src});
  ctx->draw_instanced(3, 1, 0, 0);
}

void
init_back_buffer_blit(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture> src)
{
  BlitParams* params  = HEAP_ALLOC(BlitParams, g_InitHeap, 1);
  zero_memory(params, sizeof(BlitParams));

  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Back Buffer Blit", params, &render_handler_back_buffer_blit);

  params->src         = RgTexture2D<float4>(pass, src);
  params->dst         = RgRtv(pass, &builder->back_buffer);
}

