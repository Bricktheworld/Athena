#include "Core/Engine/memory.h"

#include "Core/Engine/Render/visibility_buffer.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/root_signature.hlsli"

struct VBufferParams
{
  RgReadHandle<GpuBuffer>   transform_buffer;
  RgWriteHandle<GpuTexture> render_target;
  RgWriteHandle<GpuTexture> depth_target;
};

static void
render_handler_visibility_buffer(RenderContext* ctx, const void* data)
{
  VBufferParams* params = (VBufferParams*)data;

  Transform model;
  model.model = Mat4::columns(
    Vec4(1, 0, 0, 0),
    Vec4(0, 1, 0, 0),
    Vec4(0, 0, 1, 0),
    Vec4(0, 0, 10, 1)
  );
  model.model_inverse = transform_inverse_no_scale(model.model);
  ctx->write_cpu_upload_buffer(params->transform_buffer, &model, sizeof(model));

  ctx->om_set_render_targets({params->render_target}, params->depth_target);

  ctx->rs_set_viewport(0.0f, 0.0f, (f32)ctx->m_Width, (f32)ctx->m_Height);
  ctx->rs_set_scissor_rect(0, 0, S32_MAX, S32_MAX);

  ctx->clear_render_target_view(params->render_target, Vec4(0.0f));
  ctx->clear_depth_stencil_view(params->depth_target, kClearDepth, 0.0f, 0);

  ctx->ia_set_primitive_topology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  ctx->ia_set_index_buffer(&g_UnifiedGeometryBuffer.index_buffer, sizeof(u32));

  ctx->set_graphics_pso(&g_Renderer.vbuffer_pso);
  for (const RenderMeshInst& mesh_inst : g_Renderer.meshes)
  {
    ctx->graphics_bind_shader_resources<MaterialRenderResources>({.transform = params->transform_buffer});
    ctx->draw_indexed_instanced(mesh_inst.index_count, 1, mesh_inst.index_buffer_offset, 0, 0);
  }
}

VBuffer
init_vbuffer(AllocHeap heap, RgBuilder* builder)
{
  VBufferParams* params = HEAP_ALLOC(VBufferParams, g_InitHeap, 1);
  zero_memory(params, sizeof(VBufferParams));

  RgHandle<GpuTexture> prim_id   = rg_create_texture(builder, "Visibility Buffer Primitive IDs", FULL_RES(builder), DXGI_FORMAT_R32_UINT);
  RgHandle<GpuTexture> depth     = rg_create_texture(builder, "Visibility Buffer Depth",         FULL_RES(builder), DXGI_FORMAT_D32_FLOAT);

  RgHandle<GpuBuffer>  transform = rg_create_upload_buffer(builder, "Transform Buffer", sizeof(Transform));

  RgPassBuilder*       pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Visibility Buffer", params, &render_handler_visibility_buffer, 1, 2);
  params->render_target          = rg_write_texture(pass, &prim_id,  kWriteTextureColorTarget);
  params->depth_target           = rg_write_texture(pass, &depth,    kWriteTextureDepthStencil);
  params->transform_buffer       = rg_read_buffer  (pass, transform, kReadBufferCbv);

  VBuffer ret = {0};
  ret.primitive_ids    = prim_id;
  ret.depth            = depth;

  return ret;
}

struct DebugVBufferParams
{
  RgReadHandle<GpuTexture>  prim_ids;
  RgWriteHandle<GpuTexture> dst;
};

static void
render_handler_debug_vbuffer(RenderContext* ctx, const void* data)
{
  DebugVBufferParams* params = (DebugVBufferParams*)data;
  ctx->compute_bind_shader_resources<DebugVisualizerResources>(
    {
      .input = params->prim_ids,
      .output = params->dst
    }
  );

  ctx->set_compute_pso(&g_Renderer.debug_vbuffer_pso);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

void
init_debug_vbuffer(AllocHeap heap, RgBuilder* builder, const VBuffer& vbuffer, RgHandle<GpuTexture>* dst)
{
  DebugVBufferParams* params = HEAP_ALLOC(DebugVBufferParams, g_InitHeap, 1);
  zero_memory(params, sizeof(DebugVBufferParams));

  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Debug Visibility Buffer", params, &render_handler_debug_vbuffer, 1, 1);
  params->dst         = rg_write_texture(pass, dst,                   kWriteTextureUav);
  params->prim_ids    = rg_read_texture (pass, vbuffer.primitive_ids, kReadTextureSrvNonPixelShader);
}
