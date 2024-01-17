#include "Core/Engine/memory.h"

#include "Core/Engine/Render/post_processing.h"

void
init_post_processing(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  RgHandle<GpuImage> hdr_buffer
) {
  PostProcessingParams* params = HEAP_ALLOC(PostProcessingParams, g_InitHeap, 1);
  zero_memory(params, sizeof(PostProcessingParams));

  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Post Processing", params, &render_handler_post_processing, 1, 1);

  params->hdr_buffer  = rg_read_texture(pass, hdr_buffer, kReadTextureSrv);
  params->back_buffer = rg_write_texture(pass, &builder->back_buffer, kWriteTextureColorTarget);
}

void 
render_handler_post_processing(RenderContext* context, const void* data)
{
  const PostProcessingParams* params = (const PostProcessingParams*)data;
  context->clear_render_target_view(params->back_buffer, Vec4(0.0f, 0.0f, 1.0f, 1.0f));
}
