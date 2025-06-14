#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/lighting.h"

RgHandle<GpuTexture>
init_hdr_buffer(RgBuilder* builder)
{
  RgHandle<GpuTexture> ret = rg_create_texture(builder, "HDR Buffer", FULL_RES(builder), kGpuFormatRGBA16Float);
  return ret;
}

struct LightingParams
{
  RgRWTexture2D<float4> hdr_buffer;
  ReadGBuffer           gbuffer;
};

static void
render_handler_lighting(RenderContext* ctx, const RenderSettings&, const void* data)
{
  const LightingParams* params = (const LightingParams*)data;

  StandardBrdfSrt srt;
  srt.gbuffer_material_ids           = params->gbuffer.material_id;
  srt.gbuffer_diffuse_rgb_metallic_a = params->gbuffer.diffuse_metallic;
  srt.gbuffer_normal_rgb_roughness_a = params->gbuffer.normal_roughness;
  srt.gbuffer_depth                  = params->gbuffer.depth;
  srt.render_target                  = params->hdr_buffer;

  ctx->set_ray_tracing_pso(&g_Renderer.standard_brdf_pso);
  ctx->ray_tracing_bind_srt(srt);
  ctx->dispatch_rays(&g_Renderer.standard_brdf_st, ctx->m_Width, ctx->m_Height, 1);
}

void
init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GBuffer& gbuffer,
  RgHandle<GpuTexture>* hdr_buffer
) {
  LightingParams* params = HEAP_ALLOC(LightingParams, g_InitHeap, 1);
  RgPassBuilder* pass    = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Lighting", params, &render_handler_lighting);

  params->hdr_buffer     = RgRWTexture2D<float4>(pass, hdr_buffer);
  params->gbuffer        = read_gbuffer(pass, gbuffer);
}

