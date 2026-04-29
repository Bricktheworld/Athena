#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/taa.h"

#include "Core/Engine/Shaders/interlop.hlsli"

void
render_handler_temporal_aa(const RenderEntry*, u32)
{
  CmdList*              cmd      = &g_RenderHandlerState.cmd_list;
  RenderBuffers*        buffers  = &g_RenderHandlerState.buffers;
  const ViewCtx*        view     = &g_RenderHandlerState.main_view;
  const RenderSettings* settings = &g_RenderHandlerState.settings;

  gpu_texture_layout_transition(cmd, &buffers->hdr.texture,                                 kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->taa.get_temporal(-1)->texture,               kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->gbuffer.velocity.get_temporal(-1)->texture,  kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->gbuffer.velocity.get_temporal( 0)->texture,  kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->taa.get_temporal(0)->texture,                kGpuTextureLayoutUnorderedAccess);

  if (!settings->disable_taa)
  {
    TemporalAASrt srt;
    srt.prev_hdr      = Texture2DPtr<Vec4> {buffers->taa.get_temporal(-1)->srv.index};
    srt.curr_hdr      = Texture2DPtr<Vec4> {buffers->hdr.srv.index};
    srt.prev_velocity = Texture2DPtr<Vec2> {buffers->gbuffer.velocity.get_temporal(-1)->srv.index};
    srt.curr_velocity = Texture2DPtr<Vec2> {buffers->gbuffer.velocity.get_temporal( 0)->srv.index};
    srt.gbuffer_depth = Texture2DPtr<f32>  {buffers->gbuffer.depth.srv.index};
    srt.taa           = RWTexture2DPtr<Vec4>{buffers->taa.get_temporal(0)->uav.index};
    gpu_bind_compute_pso(cmd, kCS_TAA);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, UCEIL_DIV(view->width, 8), UCEIL_DIV(view->height, 8), 1);
  }
  else
  {
    TextureCopySrt srt;
    srt.src = Texture2DPtr<Vec4> {buffers->hdr.srv.index};
    srt.dst = RWTexture2DPtr<Vec4>{buffers->taa.get_temporal(0)->uav.index};
    gpu_bind_compute_pso(cmd, kCS_TextureCopy);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, UCEIL_DIV(view->width, 8), UCEIL_DIV(view->height, 8), 1);
  }

  gpu_texture_layout_transition(cmd, &buffers->taa.get_temporal(0)->texture, kGpuTextureLayoutShaderResource);
}
