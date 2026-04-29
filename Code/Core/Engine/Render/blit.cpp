#include "Core/Engine/memory.h"

#include "Core/Engine/Render/blit.h"

#include "Core/Engine/Shaders/interlop.hlsli"

void
render_handler_back_buffer_blit(const RenderEntry* entry, u32)
{
  BlitEntry* params = (BlitEntry*)entry->data;

  GpuDescriptor* rtv = &g_RenderHandlerState.buffers.back_buffer_rtv;
  const ViewCtx* view_ctx = &g_RenderHandlerState.main_view;

  // Transition the backbuffer back for present
  gpu_texture_layout_transition(&g_RenderHandlerState.cmd_list, &params->src->texture, kGpuTextureLayoutGeneral);
  gpu_texture_layout_transition(&g_RenderHandlerState.cmd_list, params->back_buffer,   kGpuTextureLayoutRenderTarget);

  gpu_bind_render_targets(&g_RenderHandlerState.cmd_list, rtv, 1, None);
  gpu_set_viewports(&g_RenderHandlerState.cmd_list, 0.0f, 0.0f, (f32)view_ctx->width, (f32)view_ctx->height);
  gpu_clear_render_target(&g_RenderHandlerState.cmd_list, rtv, Vec4(0.0f, 0.0f, 0.0f, 1.0f));

  gpu_bind_graphics_pso(&g_RenderHandlerState.cmd_list, g_Renderer.pso_library.back_buffer_blit);

  FullscreenSrt srt;
  srt.texture = { params->src->srv.index };
  gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);

  gpu_draw_instanced(&g_RenderHandlerState.cmd_list, 3, 1, 0, 0);

  // Transition the backbuffer back for present
  gpu_texture_layout_transition(&g_RenderHandlerState.cmd_list, params->back_buffer, kGpuTextureLayoutGeneral);
}

