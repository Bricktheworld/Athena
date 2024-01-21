#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/lighting.h"

RgHandle<GpuTexture>
init_hdr_buffer(RgBuilder* builder)
{
  RgHandle<GpuTexture> ret = rg_create_texture(builder, "HDR Buffer", FULL_RES(builder), DXGI_FORMAT_R11G11B10_FLOAT);
  return ret;
}

struct LightingParams
{
  RgWriteHandle<GpuTexture> hdr_buffer;
  ReadGBuffer             gbuffer;
  ReadDdgi                ddgi;
};

static void
render_handler_lighting(RenderContext* ctx, const void* data)
{
  const LightingParams* params = (const LightingParams*)data;

  ctx->ray_tracing_bind_shader_resources<interlop::StandardBrdfRTResources>({
    .gbuffer_material_ids           = params->gbuffer.material_id,
    .gbuffer_world_pos              = params->gbuffer.world_pos,
    .gbuffer_diffuse_rgb_metallic_a = params->gbuffer.diffuse_metallic,
    .gbuffer_normal_rgb_roughness_a = params->gbuffer.normal_roughness,

    .ddgi_vol_desc                  = params->ddgi.desc,
    .ddgi_probe_irradiance          = params->ddgi.irradiance,
    .ddgi_probe_distance            = params->ddgi.distance,

    .render_target                  = params->hdr_buffer,
  });

  ctx->set_ray_tracing_pso(&g_Renderer.standard_brdf_pso);
  ctx->dispatch_rays(&g_Renderer.standard_brdf_st, ctx->m_Width, ctx->m_Height, 1);
}

void
init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  const GBuffer& gbuffer,
  const Ddgi& ddgi,
  RgHandle<GpuTexture>* hdr_buffer
) {
  LightingParams* params = HEAP_ALLOC(LightingParams, g_InitHeap, 1);
  RgPassBuilder* pass    = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Lighting", params, &render_handler_lighting, kGBufferReadCount + kDdgiReadCount, 1);

  params->hdr_buffer     = rg_write_texture(pass, hdr_buffer, kWriteTextureUav);
  params->gbuffer        = read_gbuffer(pass, gbuffer, kReadTextureSrv);
  params->ddgi           = read_ddgi(pass, ddgi, kReadTextureSrv);
}

