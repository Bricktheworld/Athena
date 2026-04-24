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

  GraphicsPSO gbuffer_pso;

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

  ctx->ia_set_index_buffer(&g_UnifiedGeometryBuffer.index_buffer, sizeof(u16));

  ctx->set_graphics_pso(&params->gbuffer_pso);

  const SceneObj* obj = get_all_scene_objs();
  for (u32 i = 0; i < kMaxSceneObjs; i++, obj++)
  {
    if (!(obj->flags & kSceneObjRender))
    {
      continue;
    }

    // ASSERT_MSG_FATAL(subset.material != kNullAssetId, "Model subset has null material!");

#if 0
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
#endif

    MaterialSrt srt;
    srt.diffuse_base = Vec4(1.0f);
    srt.gpu_id       = obj->gpu_id;
    ctx->graphics_bind_srt(srt);
    ctx->draw_indexed_instanced(obj->index_count, 1, obj->start_index, 0, 0);
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

  // TODO(bshihabi): This should all be handled by asset streaming
  GraphicsPipelineDesc graphics_pipeline_desc =
  {
    .vertex_shader   = get_engine_shader(kVS_Basic),
    .pixel_shader    = get_engine_shader(kPS_BasicNormalGloss),
    .rtv_formats     = kGBufferRenderTargetFormats,
    .dsv_format      = kGpuFormatD32Float,
    .depth_func      = kDepthComparison,
    .stencil_enable  = false,
  };

  params->gbuffer_pso           = init_graphics_pipeline(g_GpuDevice, graphics_pipeline_desc, "Mesh PSO");

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
