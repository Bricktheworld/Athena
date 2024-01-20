#include "Core/Foundation/math.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Shaders/interlop.hlsli"

GBuffer
init_gbuffer(RgBuilder* builder)
{
  GBuffer ret = {0};
  ret.material_id      = rg_create_texture(builder, "GBuffer Material ID",      FULL_RES(builder), DXGI_FORMAT_R32_UINT          );
  ret.world_pos        = rg_create_texture(builder, "GBuffer World Position",   FULL_RES(builder), DXGI_FORMAT_R32G32B32A32_FLOAT);
  ret.diffuse_metallic = rg_create_texture(builder, "GBuffer Diffuse Metallic", FULL_RES(builder), DXGI_FORMAT_R8G8B8A8_UNORM    );
  ret.normal_roughness = rg_create_texture(builder, "GBuffer Normal Roughness", FULL_RES(builder), DXGI_FORMAT_R16G16B16A16_FLOAT);
  ret.depth            = rg_create_texture(builder, "GBuffer Depth",            FULL_RES(builder), DXGI_FORMAT_D32_FLOAT         );
  return ret;
}

struct GBufferStaticParams
{
  RgReadHandle<GpuBuffer> transform_buffer;

  RgWriteHandle<GpuImage> material_id;
  RgWriteHandle<GpuImage> world_pos;
  RgWriteHandle<GpuImage> diffuse_metallic;
  RgWriteHandle<GpuImage> normal_roughness;
  RgWriteHandle<GpuImage> depth;
};

static void
render_handler_gbuffer_static(RenderContext* ctx, const void* data)
{
  constant f32 kZNear = 0.1f;

  GBufferStaticParams* params = (GBufferStaticParams*)data;

  interlop::Transform model;
  model.model = Mat4::columns(
    Vec4(1, 0, 0, 0),
    Vec4(0, 1, 0, 0),
    Vec4(0, 0, 1, 0),
    Vec4(0, 0, 10, 1)
  );
  model.model_inverse = transform_inverse_no_scale(model.model);
  ctx->write_cpu_upload_buffer(params->transform_buffer, &model, sizeof(model));

  ctx->om_set_render_targets(
    {
      params->material_id,
      params->world_pos,
      params->diffuse_metallic,
      params->normal_roughness
    },
    params->depth
  );

  ctx->rs_set_viewport(0.0f, 0.0f, (f32)ctx->m_Width, (f32)ctx->m_Height);
  ctx->rs_set_scissor_rect(0, 0, S32_MAX, S32_MAX);

  ctx->clear_render_target_view(params->material_id,      Vec4(0.0f));
  ctx->clear_render_target_view(params->world_pos,        Vec4(0.0f));
  ctx->clear_render_target_view(params->diffuse_metallic, Vec4(0.0f));
  ctx->clear_render_target_view(params->normal_roughness, Vec4(0.0f));

  ctx->clear_depth_stencil_view(params->depth, kClearDepth, 0.0f, 0);

  ctx->ia_set_primitive_topology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  ctx->ia_set_index_buffer(&g_UnifiedGeometryBuffer.index_buffer, sizeof(u32));

  for (const RenderMeshInst& mesh_inst : g_Renderer.meshes)
  {
    ctx->set_graphics_pso(&mesh_inst.gbuffer_pso);
    ctx->graphics_bind_shader_resources<interlop::MaterialRenderResources>({.transform = params->transform_buffer});
    ctx->draw_indexed_instanced(mesh_inst.index_count, 1, mesh_inst.index_buffer_offset, 0, 0);
  }
}

void
init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer)
{
  GBufferStaticParams* params   = HEAP_ALLOC(GBufferStaticParams, g_InitHeap, 1);
  zero_memory(params, sizeof(GBufferStaticParams));

  RgHandle<GpuBuffer> transform = rg_create_upload_buffer(builder, "Transform Buffer", sizeof(interlop::Scene));

  RgPassBuilder*      pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static", params, &render_handler_gbuffer_static, 2, 5);

  params->material_id           = rg_write_texture(pass, &gbuffer->material_id,      kWriteTextureColorTarget );
  params->world_pos             = rg_write_texture(pass, &gbuffer->world_pos,        kWriteTextureColorTarget );
  params->diffuse_metallic      = rg_write_texture(pass, &gbuffer->diffuse_metallic, kWriteTextureColorTarget );
  params->normal_roughness      = rg_write_texture(pass, &gbuffer->normal_roughness, kWriteTextureColorTarget );
  params->depth                 = rg_write_texture(pass, &gbuffer->depth,            kWriteTextureDepthStencil);

  params->transform_buffer      = rg_read_buffer(pass, transform, kReadBufferCbv);
}

ReadGBuffer
read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer, ReadTextureAccessMask access)
{
  ReadGBuffer ret      = {0};
  ret.material_id      = rg_read_texture(pass_builder, gbuffer.material_id,      access);
  ret.world_pos        = rg_read_texture(pass_builder, gbuffer.world_pos,        access);
  ret.diffuse_metallic = rg_read_texture(pass_builder, gbuffer.diffuse_metallic, access);
  ret.normal_roughness = rg_read_texture(pass_builder, gbuffer.normal_roughness, access);
  ret.depth            = rg_read_texture(pass_builder, gbuffer.depth,            access);

  return ret;
}
