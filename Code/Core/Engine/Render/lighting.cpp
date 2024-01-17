#include "Core/Engine/memory.h"

#include "Core/Engine/Render/lighting.h"

RgHandle<GpuImage>
init_hdr_buffer(RgBuilder* builder)
{
  RgHandle<GpuImage> ret = rg_create_texture(builder, "HDR Buffer", FULL_RES(builder), DXGI_FORMAT_R11G11B10_FLOAT);
  return ret;
}

void
init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  const GBuffer& gbuffer,
  RgHandle<GpuImage>* hdr_buffer
) {
  LightingParams* params = HEAP_ALLOC(LightingParams, g_InitHeap, 1);
  RgPassBuilder* pass    = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Lighting", params, &render_handler_lighting, kGBufferReadCount, 1);

  params->hdr_buffer     = rg_write_texture(pass, hdr_buffer, kWriteTextureUav);
  params->gbuffer        = read_gbuffer(pass, gbuffer, kReadTextureSrv);
}

void
render_handler_lighting(RenderContext* context, const void* data)
{
  const LightingParams* params = (const LightingParams*)data;
}
