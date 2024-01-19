#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/blit.h"

#include "Core/Engine/Shaders/interlop.hlsli"

void
init_back_buffer_blit(AllocHeap heap, RgBuilder* builder, RgHandle<GpuImage> src)
{
  BlitParams* params  = HEAP_ALLOC(BlitParams, g_InitHeap, 1);
  zero_memory(params, sizeof(BlitParams));

  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Back Buffer Blit", params, &render_handler_back_buffer_blit, 1, 1);

  params->src         = rg_read_texture (pass, src, kReadTextureSrvPixelShader);
  params->dst         = rg_write_texture(pass, &builder->back_buffer, kWriteTextureColorTarget);
}

void
render_handler_back_buffer_blit(RenderContext* ctx, const void* data)
{
  BlitParams* params = (BlitParams*)data;

  ctx->set_graphics_pso(&g_Renderer.back_buffer_blit_pso);

  ctx->clear_render_target_view(params->dst, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
  ctx->om_set_render_targets({params->dst}, None);

  ctx->graphics_bind_shader_resources<interlop::FullscreenRenderResources>({.texture = params->src});
  ctx->draw_instanced(3, 1, 0, 0);
}
