#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/post_processing.h"

#include "Core/Engine/Shaders/interlop.hlsli"

void
render_handler_tonemapping(const RenderEntry*, u32)
{
  CmdList*              cmd      = &g_RenderHandlerState.cmd_list;
  RenderBuffers*        buffers  = &g_RenderHandlerState.buffers;
  const ViewCtx*        view     = &g_RenderHandlerState.main_view;
  const RenderSettings* settings = &g_RenderHandlerState.settings;

  gpu_texture_layout_transition(cmd, &buffers->depth_of_field.texture, kGpuTextureLayoutShaderResource);

  gpu_bind_render_target(cmd, &buffers->tonemapped_buffer);
  gpu_clear_render_target(cmd, &buffers->tonemapped_buffer, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
  gpu_set_viewports(cmd, 0.0f, 0.0f, (f32)view->width, (f32)view->height);

  ToneMappingSrt srt;
  srt.texture     = Texture2DPtr<Vec4>{buffers->depth_of_field.srv.index};
  srt.disable_hdr = (u32)settings->disable_hdr;
  gpu_bind_graphics_pso(cmd, g_Renderer.pso_library.tonemapping);
  gpu_bind_srt(cmd, srt);
  gpu_draw_instanced(cmd, 3, 1, 0, 0);
}
