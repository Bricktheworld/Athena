#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/lighting.h"

#include "Core/Engine/Shaders/Include/standard_brdf_common.hlsli"

RgHandle<GpuTexture>
init_hdr_buffer(RgBuilder* builder)
{
  RgHandle<GpuTexture> ret = rg_create_texture(builder, "HDR Buffer", FULL_RES(builder), kGpuFormatRGBA16Float);
  return ret;
}

struct LightingParams
{
  ComputePSO            pso;
  RgRWTexture2D<float4> hdr_buffer;
  ReadGBuffer           gbuffer;
  ReadDiffuseGi         diffuse_gi;
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
  srt.diffuse_gi_probes              = params->diffuse_gi.diffuse_probes;
  srt.diffuse_gi_page_table          = params->diffuse_gi.page_table;
  srt.render_target                  = params->hdr_buffer;

  ctx->set_compute_pso(&params->pso);
  ctx->compute_bind_srt(srt);
  ctx->dispatch(ALIGN_POW2(ctx->m_Width, 8) / 8, ALIGN_POW2(ctx->m_Height, 8) / 8, 1);
}

void
init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GBuffer& gbuffer,
  const DiffuseGiResources diffuse_gi,
  RgHandle<GpuTexture>* hdr_buffer
) {
  LightingParams* params = HEAP_ALLOC(LightingParams, g_InitHeap, 1);
  RgPassBuilder* pass    = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Lighting", params, &render_handler_lighting);

  params->hdr_buffer     = RgRWTexture2D<float4>(pass, hdr_buffer);
  params->gbuffer        = read_gbuffer(pass, gbuffer);
  params->diffuse_gi     = read_diffuse_gi(pass, diffuse_gi);
  params->pso            = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_StandardBrdf), "Standard BRDF");
}

