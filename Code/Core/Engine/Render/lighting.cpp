#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/lighting.h"

#include "Core/Engine/Shaders/Include/standard_brdf_common.hlsli"

void
render_handler_lighting(const RenderEntry*, u32)
{
  const ViewCtx* view_ctx = &g_RenderHandlerState.main_view;
  RenderBuffers* buffers  = &g_RenderHandlerState.buffers;
  CmdList*       cmd      = &g_RenderHandlerState.cmd_list;

  gpu_texture_layout_transition(cmd, &buffers->gbuffer.material_id.texture,               kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->gbuffer.diffuse_metallic.texture,          kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->gbuffer.normal_roughness.texture,          kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->gbuffer.depth.texture,                     kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->probe_page_table.get_temporal(0)->texture, kGpuTextureLayoutShaderResource);

  gpu_texture_layout_transition(cmd, &buffers->hdr.texture,                               kGpuTextureLayoutUnorderedAccess);

  StandardBrdfSrt srt;
  srt.gbuffer_material_ids           = {buffers->gbuffer.material_id.srv.index};
  srt.gbuffer_diffuse_rgb_metallic_a = {buffers->gbuffer.diffuse_metallic.srv.index};
  srt.gbuffer_normal_rgb_roughness_a = {buffers->gbuffer.normal_roughness.srv.index};
  srt.gbuffer_depth                  = {buffers->gbuffer.depth.srv.index};
  srt.diffuse_gi_probes              = {buffers->probe_buffer.srv.index};
  srt.diffuse_gi_page_table          = {buffers->probe_page_table->srv.index};
  srt.render_target                  = {buffers->hdr.uav.index};
  gpu_bind_compute_pso(cmd, kCS_StandardBrdf);
  gpu_bind_srt(cmd, srt);
  gpu_dispatch(cmd, ALIGN_POW2(view_ctx->width, 8) / 8, ALIGN_POW2(view_ctx->height, 8) / 8, 1);
}
