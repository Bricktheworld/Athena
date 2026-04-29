#include "Core/Foundation/math.h"

#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/memory.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/Include/gbuffer_common.hlsli"

void
render_handler_gbuffer_generate_multidraw_args(const RenderEntry* entry, u32)
{
  auto* params = (GBufferGenerateMultiDrawArgsEntry*)entry->data;
  RenderBuffers* buffers = &g_RenderHandlerState.buffers;

  u32 scene_obj_count = get_gpu_scene_obj_count();
  if (scene_obj_count == 0)
  {
    return;
  }

  // Clear the indirect args counter so IncrementCounter() starts from 0
  gpu_clear_buffer_u32(&g_RenderHandlerState.cmd_list, RWStructuredBufferPtr<u32>{buffers->indirect_args.counter_uav.index}, 1, 0, 0);
  gpu_memory_barrier(&g_RenderHandlerState.cmd_list);

  // Fill indirect args from prev-frame occlusion results
  if (params->phase == 0)
  {
    GBufferFillIndirectArgsPhaseOneSrt srt;
    srt.scene_obj_count     = scene_obj_count;
    srt.scene_obj_occlusion = buffers->occlusion_results;
    srt.scene_obj_gpu_ids   = buffers->scene_obj_gpu_ids;
    srt.multi_draw_args     = buffers->indirect_args;
    gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_GBufferFillMultiDrawIndirectArgsPhaseOne);
    gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);
    gpu_dispatch(&g_RenderHandlerState.cmd_list, UCEIL_DIV(scene_obj_count, 64), 1, 1);
    gpu_memory_barrier(&g_RenderHandlerState.cmd_list);
  }
  else
  {
    GBufferFillIndirectArgsPhaseTwoSrt srt;
    srt.scene_obj_count     = scene_obj_count;
    srt.scene_obj_occlusion = buffers->occlusion_results;
    srt.scene_obj_gpu_ids   = buffers->scene_obj_gpu_ids;
    srt.multi_draw_args     = buffers->indirect_args;
    srt.hzb                 = buffers->gbuffer.hzb;
    gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_GBufferFillMultiDrawIndirectArgsPhaseTwo);
    gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);
    gpu_dispatch(&g_RenderHandlerState.cmd_list, UCEIL_DIV(scene_obj_count, 64), 1, 1);
    gpu_memory_barrier(&g_RenderHandlerState.cmd_list);
  }
}

void
render_handler_gbuffer_opaque(const RenderEntry* entry, u32)
{
  auto* params = (GBufferOpaqueEntry*)entry->data;
  RenderBuffers* buffers = &g_RenderHandlerState.buffers;

  u32 scene_obj_count = get_gpu_scene_obj_count();
  if (scene_obj_count == 0)
  {
    return;
  }

  RenderTarget* render_targets[] =
  {
    &buffers->gbuffer.material_id,
    &buffers->gbuffer.diffuse_metallic,
    &buffers->gbuffer.normal_roughness,
    buffers->gbuffer.velocity.get_temporal(0)
  };
  DepthTarget* depth_target = &buffers->gbuffer.depth;

  gpu_bind_render_targets(&g_RenderHandlerState.cmd_list, render_targets, ARRAY_LENGTH(render_targets), depth_target);

  if (params->should_clear_targets)
  {
    for (RenderTarget* target : render_targets)
    {
      gpu_clear_render_target(&g_RenderHandlerState.cmd_list, target, Vec4(0.0f));
    }
    gpu_clear_depth_target(&g_RenderHandlerState.cmd_list, depth_target, kClearDepth, 0.0f, 0);
  }

  gpu_ia_set_primitive_topology(&g_RenderHandlerState.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  gpu_ia_set_index_buffer(&g_RenderHandlerState.cmd_list, &g_UnifiedGeometryBuffer.index_buffer, sizeof(u16));

  gpu_bind_graphics_pso(&g_RenderHandlerState.cmd_list, g_Renderer.pso_library.gbuffer_static);

  GBufferIndirectSrt srt;
  srt.scene_obj_gpu_ids = buffers->scene_obj_gpu_ids;
  gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);

  gpu_multi_draw_indirect_indexed(&g_RenderHandlerState.cmd_list, &buffers->indirect_args.buffer, &buffers->indirect_args.counter, 0, kMaxSceneObjs);
}

void
render_handler_generate_hzb(const RenderEntry*, u32)
{
  CmdList*       cmd     = &g_RenderHandlerState.cmd_list;
  RenderBuffers* buffers = &g_RenderHandlerState.buffers;
  const ViewCtx* view    = &g_RenderHandlerState.main_view;

  gpu_texture_layout_transition(cmd, &buffers->gbuffer.depth.texture, kGpuTextureLayoutShaderResource);
  gpu_texture_layout_transition(cmd, &buffers->gbuffer.hzb.texture,   kGpuTextureLayoutUnorderedAccess);

  GenerateHZBSrt srt;
  srt.gbuffer_depth = Texture2DPtr<f32>  {buffers->gbuffer.depth.srv.index};
  srt.depth_mip0    = RWTexture2DPtr<f32>{buffers->gbuffer.hzb_mip_uavs[0].index};
  srt.depth_mip1    = RWTexture2DPtr<f32>{buffers->gbuffer.hzb_mip_uavs[1].index};
  srt.depth_mip2    = RWTexture2DPtr<f32>{buffers->gbuffer.hzb_mip_uavs[2].index};
  srt.depth_mip3    = RWTexture2DPtr<f32>{buffers->gbuffer.hzb_mip_uavs[3].index};
  gpu_bind_compute_pso(cmd, kCS_GenerateHZB);
  gpu_bind_srt(cmd, srt);
  gpu_dispatch(cmd, UCEIL_DIV(view->width, 2 * kHZBDownsampleDimension), UCEIL_DIV(view->height, 2 * kHZBDownsampleDimension), 1);

  gpu_texture_layout_transition(cmd, &buffers->gbuffer.hzb.texture, kGpuTextureLayoutShaderResource);
}
