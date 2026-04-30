#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/depth_of_field.h"

#include "Core/Engine/Shaders/interlop.hlsli"

void
render_handler_dof_generate_coc(const RenderEntry*, u32)
{
  if (g_RenderHandlerState.settings.disable_dof)
  {
    return;
  }

  CmdList*              cmd      = &g_RenderHandlerState.cmd_list;
  RenderBuffers*        buffers  = &g_RenderHandlerState.buffers;
  const ViewCtx*        view     = &g_RenderHandlerState.main_view;
  const RenderSettings* settings = &g_RenderHandlerState.settings;

  gpu_texture_layout_transition(cmd, &buffers->coc_buffer.texture, kGpuTextureLayoutUnorderedAccess);

  DoFCocSrt srt;
  srt.hdr_buffer   = {buffers->taa.get_temporal(0)->srv.index};
  srt.depth_buffer = {buffers->gbuffer.depth.srv.index};
  srt.coc_buffer   = {buffers->coc_buffer.uav.index};
  srt.z_near       = kZNear;
  srt.aperture     = settings->aperture;
  srt.focal_dist   = settings->focal_dist;
  srt.focal_range  = settings->focal_range;
  gpu_bind_compute_pso(cmd, kCS_DoFCoC);
  gpu_bind_srt(cmd, srt);
  gpu_dispatch(cmd, UCEIL_DIV(view->width, 8), UCEIL_DIV(view->height, 8), 1);

  gpu_memory_barrier(cmd);
  gpu_texture_layout_transition(cmd, &buffers->coc_buffer.texture, kGpuTextureLayoutShaderResource);
}

void
render_handler_dof_bokeh_blur(const RenderEntry*, u32)
{
  if (g_RenderHandlerState.settings.disable_dof)
  {
    return;
  }

  CmdList*              cmd      = &g_RenderHandlerState.cmd_list;
  RenderBuffers*        buffers  = &g_RenderHandlerState.buffers;
  const ViewCtx*        view     = &g_RenderHandlerState.main_view;
  const RenderSettings* settings = &g_RenderHandlerState.settings;

  gpu_texture_layout_transition(cmd, &buffers->blur_buffer.texture, kGpuTextureLayoutUnorderedAccess);

  DoFBokehBlurSrt srt;
  srt.depth_buffer = {buffers->gbuffer.depth.srv.index};
  srt.coc_buffer   = {buffers->coc_buffer.srv.index};
  srt.hdr_buffer   = {buffers->taa.get_temporal(0)->srv.index};
  srt.blur_buffer  = {buffers->blur_buffer.uav.index};
  srt.z_near       = kZNear;
  srt.blur_radius  = settings->dof_blur_radius;
  srt.sample_count = settings->dof_sample_count;
  gpu_bind_compute_pso(cmd, kCS_DoFBokehBlur);
  gpu_bind_srt(cmd, srt);
  gpu_dispatch(cmd, UCEIL_DIV(view->width / kDoFResolutionScale, 8), UCEIL_DIV(view->height / kDoFResolutionScale, 8), 1);

  gpu_memory_barrier(cmd);
  gpu_texture_layout_transition(cmd, &buffers->blur_buffer.texture, kGpuTextureLayoutShaderResource);
}

void
render_handler_dof_composite(const RenderEntry*, u32)
{
  CmdList*              cmd      = &g_RenderHandlerState.cmd_list;
  RenderBuffers*        buffers  = &g_RenderHandlerState.buffers;
  const ViewCtx*        view     = &g_RenderHandlerState.main_view;
  const RenderSettings* settings = &g_RenderHandlerState.settings;

  gpu_texture_layout_transition(cmd, &buffers->depth_of_field.texture, kGpuTextureLayoutUnorderedAccess);

  if (settings->disable_dof)
  {
    TextureCopySrt srt;
    srt.src = {buffers->taa.get_temporal(0)->srv.index};
    srt.dst = {buffers->depth_of_field.uav.index};
    gpu_bind_compute_pso(cmd, kCS_TextureCopy);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, UCEIL_DIV(view->width, 8), UCEIL_DIV(view->height, 8), 1);
  }
  else
  {
    DoFCompositeSrt srt;
    srt.coc_buffer    = {buffers->coc_buffer.srv.index};
    srt.hdr_buffer    = {buffers->taa.get_temporal(0)->srv.index};
    srt.blur_buffer   = {buffers->blur_buffer.srv.index};
    srt.render_target = {buffers->depth_of_field.uav.index};
    gpu_bind_compute_pso(cmd, kCS_DoFComposite);
    gpu_bind_srt(cmd, srt);
    gpu_dispatch(cmd, UCEIL_DIV(view->width, 8), UCEIL_DIV(view->height, 8), 1);
  }

  gpu_memory_barrier(cmd);
  gpu_texture_layout_transition(cmd, &buffers->depth_of_field.texture, kGpuTextureLayoutShaderResource);
}
