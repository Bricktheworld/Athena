#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/material_manager.h"

#include "Core/Engine/Render/misc.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/root_signature.hlsli"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

#include "Core/Engine/Vendor/imgui/implot.h"

struct FrameInitParams
{
  RgConstantBuffer<Viewport>                  viewport_buffer;
  RgStructuredBuffer<SceneObjGpu>             scene_obj_buffer;
  RgStructuredBuffer<MaterialGpu>             material_buffer;
  RgRWStructuredBuffer<MultiDrawIndirectArgs> debug_draw_args_buffer;
  RgRWStructuredBuffer<DebugLinePoint>        debug_line_vert_buffer;
  RgRWStructuredBuffer<DebugSdf>              debug_sdf_buffer;

  ComputePSO                                  init_debug_draw_buffers_pso;
};

static
Mat4 view_from_camera(Camera* camera)
{
  constexpr float kPitchLimit = kPI / 2.0f - 0.05f;
  camera->pitch = MIN(kPitchLimit, MAX(-kPitchLimit, camera->pitch));
  if (camera->yaw > kPI)
  {
    camera->yaw -= kPI * 2.0f;
  }
  else if (camera->yaw < -kPI)
  {
    camera->yaw += kPI * 2.0f;
  }

  f32 y = sinf(camera->pitch);
  f32 r = cosf(camera->pitch);
  f32 z = r * cosf(camera->yaw);
  f32 x = r * sinf(camera->yaw);

  Vec3 lookat = Vec3(x, y, z);
  return look_at_lh(camera->world_pos, lookat, Vec3(0.0f, 1.0f, 0.0f));
}

static void
render_handler_frame_init(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  FrameInitParams* params = (FrameInitParams*)data;

  Mat4 prev_view = view_from_camera(&g_Renderer.prev_camera);
  Mat4 view      = view_from_camera(&g_Renderer.camera);

  Viewport main_viewport;
  main_viewport.proj                  = perspective_infinite_reverse_lh(kPI / 4.0f, (f32)ctx->m_Width / (f32)ctx->m_Height, kZNear);
  main_viewport.view                  = view;
  main_viewport.view_proj             = main_viewport.proj * view;
  main_viewport.prev_view_proj        = main_viewport.proj * prev_view;
  main_viewport.inverse_view_proj     = inverse_mat4(main_viewport.view_proj);
  main_viewport.camera_world_pos      = g_Renderer.camera.world_pos;
  main_viewport.prev_camera_world_pos = g_Renderer.prev_camera.world_pos;
  main_viewport.directional_light     = g_Renderer.directional_light;
  main_viewport.taa_jitter            = !settings.disable_taa ? g_Renderer.taa_jitter : Vec2(0.0f, 0.0f);

  ctx->write_cpu_upload_buffer(params->viewport_buffer, &main_viewport, sizeof(main_viewport));

#if 0
  {
    spin_acquire(&g_MaterialManager->spin_lock);
    defer { spin_release(&g_MaterialManager->spin_lock); };

    MaterialGpu* dst = (MaterialGpu*)unwrap(rg_deref_buffer(params->material_buffer)->mapped);

    for (u32 icmd = 0; icmd < g_MaterialManager->material_upload_count; icmd++)
    {
      const MaterialUploadCmd* cmd = g_MaterialManager->material_uploads + icmd;
      dst[cmd->mat_gpu_id] = cmd->material;
    }


    g_MaterialManager->material_upload_count = 0;
  }
#endif

  // For global resources you need to put manual resource barriers since they aren't tracked at compile time (assumed that everyone is going to use them)
  ctx->uav_barrier(params->debug_draw_args_buffer);
  ctx->uav_barrier(params->debug_line_vert_buffer);
  ctx->uav_barrier(params->debug_sdf_buffer);

  ctx->set_graphics_root_shader_resource_view(kIndexBufferSlot ,           &g_UnifiedGeometryBuffer.index_buffer);
  ctx->set_graphics_root_shader_resource_view(kVertexBufferSlot,           &g_UnifiedGeometryBuffer.vertex_buffer);
  ctx->set_graphics_root_shader_resource_view(kAccelerationStructureSlot,  &g_UnifiedGeometryBuffer.bvh.top_bvh);

  ctx->set_compute_root_shader_resource_view(kIndexBufferSlot ,           &g_UnifiedGeometryBuffer.index_buffer);
  ctx->set_compute_root_shader_resource_view(kVertexBufferSlot,           &g_UnifiedGeometryBuffer.vertex_buffer);
  ctx->set_compute_root_shader_resource_view(kAccelerationStructureSlot,  &g_UnifiedGeometryBuffer.bvh.top_bvh);

  // Clear/initialize the multi draw indirect args
  ctx->set_compute_pso(&params->init_debug_draw_buffers_pso);
  ctx->dispatch(UCEIL_DIV(MAX(kDebugMaxVertices, kDebugMaxSdfs), 64), 1, 1);

  ctx->uav_barrier(params->debug_draw_args_buffer);
  ctx->uav_barrier(params->debug_line_vert_buffer);
  ctx->uav_barrier(params->debug_sdf_buffer);
}

FrameResources
init_frame_init_pass(AllocHeap heap, RgBuilder* builder)
{
  FrameInitParams* params = HEAP_ALLOC(FrameInitParams, g_InitHeap, 1);
  zero_memory(params, sizeof(FrameInitParams));

  FrameResources ret;

  ret.viewport_buffer        = rg_create_upload_buffer(builder, "Viewport Buffer",     kGpuHeapSysRAMCpuToGpu, sizeof(Viewport));
#if 0
  ret.material_buffer        = rg_create_upload_buffer(builder, "Material Buffer",     kGpuHeapSysRAMCpuToGpu, sizeof(MaterialGpu)    * kMaxSceneObjs);
  ret.scene_obj_buffer       = rg_create_upload_buffer(builder, "Scene Object Buffer", kGpuHeapSysRAMCpuToGpu, sizeof(SceneObjGpu)    * kMaxSceneObjs);
#endif

  ret.debug_draw_args_buffer = rg_create_buffer(builder, "Debug Draw Args Buffer",      sizeof(MultiDrawIndirectArgs) * 2);
  ret.debug_line_vert_buffer = rg_create_buffer(builder, "Debug Lines Vertices Buffer", sizeof(DebugLinePoint) * kDebugMaxVertices);
  ret.debug_sdf_buffer       = rg_create_buffer(builder, "Debug SDF Buffer",            sizeof(DebugSdf)       * kDebugMaxSdfs);

  RgPassBuilder*      pass       = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Frame Init", params, &render_handler_frame_init, true);
  params->viewport_buffer        = RgConstantBuffer<Viewport>(pass, ret.viewport_buffer, 0, kViewportBufferSlot);
#if 0
  params->material_buffer        = RgStructuredBuffer<MaterialGpu>(pass, ret.material_buffer, 0, kMaterialBufferSlot);
  params->scene_obj_buffer       = RgStructuredBuffer<SceneObjGpu>(pass, ret.scene_obj_buffer, 0, kSceneObjBufferSlot);
#endif
  params->debug_draw_args_buffer = RgRWStructuredBuffer<MultiDrawIndirectArgs>(pass, &ret.debug_draw_args_buffer, kDebugArgsBufferSlot);
  params->debug_line_vert_buffer = RgRWStructuredBuffer<DebugLinePoint>(pass, &ret.debug_line_vert_buffer, kDebugVertexBufferSlot);
  params->debug_sdf_buffer       = RgRWStructuredBuffer<DebugSdf>(pass, &ret.debug_sdf_buffer, kDebugSdfBufferSlot);

  params->init_debug_draw_buffers_pso = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DebugDrawInitMultiDrawIndirectArgs), "Debug Draw Init MultiDrawIndirect Args");

  return ret;
}

struct ImGuiParams
{
  RgRtv dst;
};

f64 g_CpuEffectiveTime = 0.0;

void
render_handler_imgui(RenderContext* ctx, const RenderSettings&, const void* data)
{
  ImGuiParams* params = (ImGuiParams*)data;

  ImGui::Begin("Rendering");
  static bool s_ShowDetailedPerformance = false;

  ImGui::DragFloat3("Direction", (f32*)&g_Scene->directional_light.direction, 0.02f, -1.0f, 1.0f);
  ImGui::DragFloat3("Diffuse", (f32*)&g_Scene->directional_light.diffuse, 0.1f, 0.0f, 1.0f);
  ImGui::DragFloat ("Intensity", &g_Scene->directional_light.intensity, 0.1f, 0.0f, 100.0f);

  ImGui::InputFloat3("Camera Position", (f32*)&g_Scene->camera.world_pos);

  ImGui::Checkbox("Disable TAA", &g_Renderer.settings.disable_taa);
  ImGui::Checkbox("Disable HDR", &g_Renderer.settings.disable_hdr);
  ImGui::Checkbox("Disable DoF", &g_Renderer.settings.disable_dof);
  ImGui::Checkbox("Enable Debug Draw", &g_Renderer.settings.enabled_debug_draw);
  ImGui::Checkbox("Show Detailed Performance", &s_ShowDetailedPerformance);

  ImGui::DragFloat("Aperture", &g_Renderer.settings.aperture, 0.01f, 0.0f, 50.0f);

  static f32 s_FocalDistance = g_Renderer.settings.focal_dist;
  ImGui::DragFloat("Focal Distance", &s_FocalDistance, 0.01f, 0.0f, 1000.0f);
  g_Renderer.settings.focal_dist = 0.9f * g_Renderer.settings.focal_dist + 0.1f * s_FocalDistance;

  ImGui::DragFloat("Focal Range", &g_Renderer.settings.focal_range, 0.01f, 0.0f, 100.0f);

  ImGui::DragInt("DoF Sample Count", (s32*)&g_Renderer.settings.dof_sample_count, 1.0f, 0, 256);
  ImGui::DragFloat("DoF Blur Radius", &g_Renderer.settings.dof_blur_radius, 0.1f, 0.0f, 40.0f);

  ImGui::DragFloat3("Probe Spacing", (f32*)&g_Renderer.settings.probe_spacing, 0.01f, 0.0, 25.0);
  ImGui::DragInt("Probe Debug Rays", (s32*)&g_Renderer.settings.debug_probe_ray_idx, 1.0f, -1, 0x1000);
  ImGui::Checkbox("Probe Freeze Rotation", &g_Renderer.settings.freeze_probe_rotation);

  ImGui::End();

  ImGui::Begin("GPU Profiling", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
  ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetWindowSize(ImVec2(0.0f, 0.0f));

  f64 gpu_effective_time = query_gpu_profiler_timestamp(kTotalFrameGpuMarker);

  static constexpr f64 kHistoryMs           = 2.0 * 1000.0f;
  static constexpr u32 kFrameTimeBufferSize = (u32)(kHistoryMs / 4.0f);

  struct FrameTimeSample
  {
    f64 time           = 0.0f;
    f64 target_ms      = 0.0f;
    f64 cpu_frame_time = 0.0f;
    f64 gpu_frame_time = 0.0f;

    f64 err_time       = 0.0f;
  };

  f64 time      = ImGui::GetTime() * 1000.0f;

  // TODO(bshihabi): This is definitely not correct, but I'm not sure where to get this value from...
  f64 target_ms = 16.67;
  f64 frame_ms  = ImGui::GetIO().DeltaTime * 1000.0f;

  static FrameTimeSample* s_Buffer = HEAP_ALLOC(FrameTimeSample, g_DebugHeap, kFrameTimeBufferSize);
  static u32              s_Offset = 0;
  s_Buffer[s_Offset].time           = time;
  s_Buffer[s_Offset].target_ms      = target_ms;
  s_Buffer[s_Offset].cpu_frame_time = g_CpuEffectiveTime;
  s_Buffer[s_Offset].gpu_frame_time = gpu_effective_time;
  s_Buffer[s_Offset].err_time       = g_MainWindow->swap_chain.missed_vsync ? frame_ms : 0.0;

  s_Offset                         = (s_Offset + 1) % kFrameTimeBufferSize;

  ImGui::Text("%ls", g_GpuDevice->gpu_name);
  if (ImPlot::BeginPlot("##GPU Frame Time", ImVec2(300, 150), 0))
  {
    ImPlot::SetupAxes(nullptr, "ms", ImPlotAxisFlags_NoTickLabels, 0);
    ImPlot::SetupAxisLimits(ImAxis_X1, MAX(time - kHistoryMs, 0.0), time, ImGuiCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 33.334f);
    ImPlot::SetNextLineStyle(ImVec4(0.0f, 0.51f, 0.79f, 1.0f), 0.5f);
    ImPlot::PlotLine("CPU",      &s_Buffer[0].time, &s_Buffer[0].cpu_frame_time, kFrameTimeBufferSize, ImPlotLineFlags_SkipNaN, s_Offset, sizeof(FrameTimeSample));
    ImPlot::SetNextLineStyle(ImVec4(0.0f, 0.69f, 0.15f, 1.0f), 0.5f);
    ImPlot::PlotLine("GPU",      &s_Buffer[0].time, &s_Buffer[0].gpu_frame_time, kFrameTimeBufferSize, ImPlotLineFlags_SkipNaN, s_Offset, sizeof(FrameTimeSample));
    ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 0.5f);
    ImPlot::PlotBars("##ERR",    &s_Buffer[0].time, &s_Buffer[0].err_time,       kFrameTimeBufferSize, 0.1f, 0,                 s_Offset, sizeof(FrameTimeSample));
    ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.1f);
    ImPlot::PlotLine("##TARGET", &s_Buffer[0].time, &s_Buffer[0].target_ms,      kFrameTimeBufferSize, ImPlotLineFlags_SkipNaN, s_Offset, sizeof(FrameTimeSample));
    ImPlot::EndPlot();
  }
  ImGui::Text("Frame (ms): %f ms", frame_ms);
  ImGui::Text("CPU   (ms): %f ms", g_CpuEffectiveTime);
  ImGui::Text("GPU   (ms): %f ms", gpu_effective_time);


  if (s_ShowDetailedPerformance)
  {
    ImGui::Indent();
    for (const RenderPass& pass : g_RenderGraph->render_passes)
    {
      f64 dt = query_gpu_profiler_timestamp(pass.name);
      ImGui::Text("%s: %f ms", pass.name, dt);
    }
  }

  ImGui::End();

  ImGui::Render();

  ctx->rs_set_viewport(0.0f, 0.0f, (f32)ctx->m_Width, (f32)ctx->m_Height);
  ctx->rs_set_scissor_rect(0, 0, S32_MAX, S32_MAX);

  ctx->om_set_render_targets({params->dst}, None);

  ctx->set_descriptor_heaps({&g_Renderer.imgui_descriptor_heap});

  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), ctx->m_CmdBuffer.d3d12_list);

  ctx->rs_set_viewport(0.0f, 0.0f, (f32)ctx->m_Width, (f32)ctx->m_Height);
  ctx->rs_set_scissor_rect(0, 0, S32_MAX, S32_MAX);
}

void
init_imgui_pass(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* dst)
{
  ImGuiParams* params = HEAP_ALLOC(ImGuiParams, g_InitHeap, 1);

  RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "ImGui Pass" , params, &render_handler_imgui);
  params->dst         = RgRtv(pass, dst);

  ImPlot::CreateContext();
}

struct DebugDrawParams
{
  RgRtv dst;
  RgDsv depth;
  RgIndirectArgsBuffer               debug_draw_args_buffer;
  RgStructuredBuffer<DebugLinePoint> debug_line_vert_buffer;
  RgStructuredBuffer<DebugSdf>       debug_sdf_buffer;
  
  GraphicsPSO debug_draw_line_pso;
  GraphicsPSO debug_draw_sdf_pso;
};

static void
render_handler_debug_draw(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  DebugDrawParams* params = (DebugDrawParams*)data;
  if (!settings.enabled_debug_draw)
  {
    return;
  }

  ctx->om_set_render_targets({params->dst}, params->depth);

  ctx->set_graphics_pso(&params->debug_draw_line_pso);
  ctx->graphics_bind_srt(DebugLineDrawSrt{ params->debug_line_vert_buffer });
  ctx->ia_set_primitive_topology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  ctx->multi_draw_indirect(params->debug_draw_args_buffer, 0, 1);

  ctx->set_graphics_pso(&params->debug_draw_sdf_pso);
  ctx->graphics_bind_srt(DebugSdfDrawSrt{ params->debug_sdf_buffer });
  ctx->ia_set_primitive_topology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->multi_draw_indirect(params->debug_draw_args_buffer, sizeof(MultiDrawIndirectArgs), 1);
}

void
init_debug_draw_pass(AllocHeap heap, RgBuilder* builder, const FrameResources& frame_resources, GBuffer* gbuffer, RgHandle<GpuTexture>* dst)
{
  DebugDrawParams* params        = HEAP_ALLOC(DebugDrawParams, g_InitHeap, 1);

  RgPassBuilder*   pass          = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Debug Line Draw Pass", params, &render_handler_debug_draw);
  params->dst                    = RgRtv(pass, dst);
  params->depth                  = RgDsv(pass, &gbuffer->depth);

  params->debug_draw_args_buffer = RgIndirectArgsBuffer(pass, frame_resources.debug_draw_args_buffer);

  params->debug_line_vert_buffer = RgStructuredBuffer<DebugLinePoint>(pass, frame_resources.debug_line_vert_buffer);
  params->debug_sdf_buffer       = RgStructuredBuffer<DebugSdf>(pass, frame_resources.debug_sdf_buffer);


  {
    GraphicsPipelineDesc desc = 
    {
      .vertex_shader = get_engine_shader(kVS_DebugDrawLine),
      .pixel_shader  = get_engine_shader(kPS_DebugDrawLine),
      .rtv_formats   = Span{kGpuFormatRGBA16Float},
      .dsv_format    = kGpuFormatD32Float,
      .depth_func    = kDepthComparison,
      .topology      = kPrimitiveTopologyLine,
      .blend_enable  = true,
    };
    params->debug_draw_line_pso = init_graphics_pipeline(g_GpuDevice, desc, "Debug Line Draw");
  }
  {
    GraphicsPipelineDesc desc = 
    {
      .vertex_shader = get_engine_shader(kVS_DebugDrawSdf),
      .pixel_shader  = get_engine_shader(kPS_DebugDrawSdf),
      .rtv_formats   = Span{kGpuFormatRGBA16Float},
      .dsv_format    = kGpuFormatD32Float,
      .depth_func    = kDepthComparison,
      .blend_enable  = true,
    };
    params->debug_draw_sdf_pso  = init_graphics_pipeline(g_GpuDevice, desc, "Debug SDF Draw");
  }
}
