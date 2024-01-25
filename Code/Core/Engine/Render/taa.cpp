#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/taa.h"

#include "Core/Engine/Shaders/interlop.hlsli"

struct TAAParams
{
  RgReadHandle<GpuTexture>  prev_hdr;
  RgReadHandle<GpuTexture>  curr_hdr;
  RgReadHandle<GpuTexture>  prev_velocity;
  RgReadHandle<GpuTexture>  curr_velocity;

  RgReadHandle<GpuTexture>  gbuffer_depth;

  RgWriteHandle<GpuTexture> taa;
};

static void
render_handler_taa(RenderContext* ctx, const void* data)
{
  TAAParams* params = (TAAParams*)data;

  ctx->compute_bind_shader_resources<interlop::TAAResources>(
    {
      .prev_hdr      = params->prev_hdr,
      .curr_hdr      = params->curr_hdr,
      .prev_velocity = params->prev_velocity,
      .curr_velocity = params->curr_velocity,

      .gbuffer_depth = params->gbuffer_depth,

      .taa           = params->taa,
    }
  );

  ctx->set_compute_pso(&g_Renderer.taa_pso);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

RgHandle<GpuTexture>
init_taa_buffer(RgBuilder* builder)
{
  RgHandle<GpuTexture> ret = rg_create_texture_ex(builder, "TAA Buffer", FULL_RES(builder), DXGI_FORMAT_R11G11B10_FLOAT, 1);
  return ret;
}

void
init_taa(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture> hdr_lit, const GBuffer& gbuffer, RgHandle<GpuTexture>* taa_buffer)
{
  TAAParams* params = HEAP_ALLOC(TAAParams, g_InitHeap, 1);
  zero_memory(params, sizeof(TAAParams));

  RgPassBuilder* pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "TAA", params, &render_handler_taa, 5, 1);
  params->prev_hdr      = rg_read_texture(pass, *taa_buffer,      kReadTextureSrvNonPixelShader, -1);
  params->curr_velocity = rg_read_texture(pass, gbuffer.velocity, kReadTextureSrvNonPixelShader);
  params->prev_velocity = rg_read_texture(pass, gbuffer.velocity, kReadTextureSrvNonPixelShader, -1);
  params->curr_hdr      = rg_read_texture(pass, hdr_lit,          kReadTextureSrvNonPixelShader);
  params->gbuffer_depth = rg_read_texture(pass, gbuffer.depth,    kReadTextureSrvNonPixelShader);

  params->taa           = rg_write_texture(pass, taa_buffer, kWriteTextureUav);
}
