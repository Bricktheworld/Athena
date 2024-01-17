#include "Core/Engine/Render/gbuffer.h"

GBuffer
init_gbuffer(RgBuilder* builder)
{
  GBuffer ret = {0};
  ret.material_id      = rg_create_texture(builder, "GBuffer Material ID"     , FULL_RES(builder), DXGI_FORMAT_R32_UINT          );
  ret.world_pos        = rg_create_texture(builder, "GBuffer World Position"  , FULL_RES(builder), DXGI_FORMAT_R32G32B32A32_FLOAT);
  ret.diffuse_metallic = rg_create_texture(builder, "GBuffer Diffuse Metallic", FULL_RES(builder), DXGI_FORMAT_R8G8B8A8_UNORM    );
  ret.normal_roughness = rg_create_texture(builder, "GBuffer Normal Roughness", FULL_RES(builder), DXGI_FORMAT_R16G16B16A16_FLOAT);
  ret.depth            = rg_create_texture(builder, "GBuffer Depth",            FULL_RES(builder), DXGI_FORMAT_D32_FLOAT         );
  return ret;
}

void
init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer)
{
  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static", nullptr, &render_handler_gbuffer_static, 0, 5);

  rg_write_texture(pass, &gbuffer->material_id,      kWriteTextureColorTarget );
  rg_write_texture(pass, &gbuffer->world_pos,        kWriteTextureColorTarget );
  rg_write_texture(pass, &gbuffer->diffuse_metallic, kWriteTextureColorTarget );
  rg_write_texture(pass, &gbuffer->normal_roughness, kWriteTextureColorTarget );
  rg_write_texture(pass, &gbuffer->depth,            kWriteTextureDepthStencil);
}

void
render_handler_gbuffer_static(RenderContext* render_context, const void* data)
{
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
