#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Shaders/interlop.hlsli"

struct PostProcessingParams
{
  RgReadHandle<GpuTexture>  hdr_buffer;
  RgWriteHandle<GpuTexture> dst;
};

static void 
render_handler_post_processing(RenderContext* ctx, const void* data)
{
  const PostProcessingParams* params = (const PostProcessingParams*)data;
  ctx->clear_render_target_view(params->dst, Vec4(0.0f, 0.0f, 0.0f, 0.0f));

  ctx->om_set_render_targets({params->dst}, None);

  ctx->graphics_bind_shader_resources<PostProcessingRenderResources>({.texture = params->hdr_buffer});
  ctx->set_graphics_pso(&g_Renderer.post_processing_pipeline);

  ctx->draw_instanced(3, 1, 0, 0);
}

RgHandle<GpuTexture>
init_post_processing(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  RgHandle<GpuTexture> hdr_buffer
) {
  PostProcessingParams* params = HEAP_ALLOC(PostProcessingParams, g_InitHeap, 1);
  zero_memory(params, sizeof(PostProcessingParams));

  RgPassBuilder* pass    = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Post Processing", params, &render_handler_post_processing, 1, 1);

  RgHandle<GpuTexture> ret = rg_create_texture(builder, "Post Processing Buffer", FULL_RES(builder), DXGI_FORMAT_R8G8B8A8_UNORM);

  params->hdr_buffer     = rg_read_texture(pass, hdr_buffer, kReadTextureSrv);
  params->dst            = rg_write_texture(pass, &ret, kWriteTextureColorTarget);

  return ret;
}

