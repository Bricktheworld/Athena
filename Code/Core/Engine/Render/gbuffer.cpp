#include "Core/Foundation/math.h"

#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/memory.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Shaders/interlop.hlsli"


GBuffer
init_gbuffer(RgBuilder* builder)
{
  GBuffer ret = {0};
  ret.material_id      = rg_create_texture   (builder, "GBuffer Material ID",      FULL_RES(builder), kGpuFormatR32Uint      );
  ret.diffuse_metallic = rg_create_texture   (builder, "GBuffer Diffuse Metallic", FULL_RES(builder), kGpuFormatRGBA8Unorm   );
  ret.normal_roughness = rg_create_texture   (builder, "GBuffer Normal Roughness", FULL_RES(builder), kGpuFormatRGBA16Float  );
  ret.velocity         = rg_create_texture_ex(builder, "GBuffer Velocity",         FULL_RES(builder), kGpuFormatRG32Float, 1 );
  ret.depth            = rg_create_texture   (builder, "GBuffer Depth",            FULL_RES(builder), kGpuFormatD32Float     );
  return ret;
}

struct GBufferStaticParams
{
  RgConstantBuffer<Transform> transform_buffer;

  RgRtv material_id;
  RgRtv diffuse_metallic;
  RgRtv normal_roughness;
  RgRtv velocity;
  RgDsv depth;
};

static void
render_handler_gbuffer_static(RenderContext* ctx, const RenderSettings&, const void* data)
{
  GBufferStaticParams* params = (GBufferStaticParams*)data;

  Transform model;
  model.model = Mat4::columns(
    Vec4(1, 0, 0, 0),
    Vec4(0, 1, 0, 0),
    Vec4(0, 0, 1, 0),
    Vec4(0, 0, 0, 1)
  );
  model.model_inverse = transform_inverse_no_scale(model.model);
  ctx->write_cpu_upload_buffer(params->transform_buffer, &model, sizeof(model));

  ctx->om_set_render_targets(
    {
      params->material_id,
      params->diffuse_metallic,
      params->normal_roughness,
      params->velocity
    },
    params->depth
  );

  ctx->rs_set_viewport(0.0f, 0.0f, (f32)ctx->m_Width, (f32)ctx->m_Height);
  ctx->rs_set_scissor_rect(0, 0, S32_MAX, S32_MAX);

  ctx->clear_render_target_view(params->material_id,      Vec4(0.0f));
  ctx->clear_render_target_view(params->diffuse_metallic, Vec4(0.0f));
  ctx->clear_render_target_view(params->normal_roughness, Vec4(0.0f));
  ctx->clear_render_target_view(params->velocity,         Vec4(0.0f));

  ctx->clear_depth_stencil_view(params->depth, kClearDepth, 0.0f, 0);

  ctx->ia_set_primitive_topology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  ctx->ia_set_index_buffer(&g_UnifiedGeometryBuffer.index_buffer, sizeof(u32));

  for (const RenderModelSubset& subset : g_Renderer.meshes)
  {
    ASSERT_MSG_FATAL(subset.material != kNullAssetId, "Model subset has null material!");

    auto material_res = get_material_asset(subset.material);
    if (!material_res)
    {
      continue;
    }

    Texture2DPtr<float4> diffuse = {0};
    {
      AssetId asset = material_res.value()->textures[0];
      auto res = get_srv_texture_asset(asset);
      if (res)
      {
        diffuse = res.value();
      }
    }

    Texture2DPtr<float4> normal = {0};
    {
      AssetId asset = material_res.value()->textures[1];
      auto res = get_srv_texture_asset(asset);
      if (res)
      {
        normal = res.value();
      }
    }

    ctx->set_graphics_pso(&subset.gbuffer_pso);

    MaterialSrt srt;
    srt.transform = params->transform_buffer;
    srt.diffuse   = diffuse;
    srt.normal    = normal;
    srt.gpu_id    = 0;
    ctx->graphics_bind_srt(srt);
    ctx->draw_indexed_instanced(subset.index_count, 1, subset.index_buffer_offset, 0, 0);
  }
}

void
init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer)
{
  GBufferStaticParams* params   = HEAP_ALLOC(GBufferStaticParams, g_InitHeap, 1);
  zero_memory(params, sizeof(GBufferStaticParams));

  RgHandle<GpuBuffer> transform = rg_create_upload_buffer(builder, "Transform Buffer", kGpuHeapSysRAMCpuToGpu, sizeof(Transform));

  RgPassBuilder*      pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static", params, &render_handler_gbuffer_static);

  params->material_id           = RgRtv(pass, &gbuffer->material_id);
  params->diffuse_metallic      = RgRtv(pass, &gbuffer->diffuse_metallic);
  params->normal_roughness      = RgRtv(pass, &gbuffer->normal_roughness);
  params->velocity              = RgRtv(pass, &gbuffer->velocity);
  params->depth                 = RgDsv(pass, &gbuffer->depth);

  params->transform_buffer      = RgConstantBuffer<Transform>(pass, transform);
}

ReadGBuffer
read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer)
{
  ReadGBuffer ret;
  ret.material_id      = RgTexture2D<uint>  (pass_builder, gbuffer.material_id);
  ret.diffuse_metallic = RgTexture2D<float4>(pass_builder, gbuffer.diffuse_metallic);
  ret.normal_roughness = RgTexture2D<float4>(pass_builder, gbuffer.normal_roughness);
  ret.velocity         = RgTexture2D<float2>(pass_builder, gbuffer.velocity);
  ret.depth            = RgTexture2D<float> (pass_builder, gbuffer.depth);

  return ret;
}
