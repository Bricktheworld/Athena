#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"

#include "Core/Engine/Render/misc.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/root_signature.hlsli"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

#include "Core/Engine/Vendor/imgui/implot.h"

static f32
camera_params_to_ev100(f32 aperture, f32 shutter_time, f32 iso)
{
  return log2f(((aperture * aperture) / shutter_time) * (100.0f / iso));
}

static f32
ev100_to_max_luminance(f32 ev100)
{
  return 1.2f * powf(2.0f, ev100);
}

// https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
// http://www.vendian.org/mncharity/dir3/blackbody/UnstableURLs/bbr_color.html
// Fitted curve for kelvin to RGB that I did not come up with
static Vec3
diffuse_from_temperature(f32 temperature)
{
  Vec3 ret = 0.0f;

  temperature /= 100.0f;

  if (temperature <= 66)
  {
    ret.r = 255.0f;

    ret.g = temperature;
    ret.g = 99.4708025861f * logf(ret.g) - 161.1195681661f;
    ret.g = CLAMP(ret.g, 0.0f, 255.0f);
  }
  else
  {
    ret.r = (temperature - 60.0f) / 255.0f;
    ret.r = 329.698727446f * powf(ret.r, -0.1332047592f);
    ret.r = CLAMP(ret.r, 0.0f, 255.0f);

    ret.g = temperature - 60.0f;
    ret.g = 288.1221695283f * powf(ret.g, -0.0755148492f);
    ret.g = CLAMP(ret.g, 0.0f, 255.0f);
  }

  if (temperature >= 66)
  {
    ret.b = 255.0f;
  }
  else
  {
    if (temperature <= 19)
    {
      ret.b = 0.0f;
    }
    else
    {
      ret.b = temperature - 10.0f;
      ret.b = 138.5177312231f * logf(ret.b) - 305.0447927307f;
      ret.b = CLAMP(ret.b, 0.0f, 255.0f);
    }
  }

  return ret / 255.0f;
}

void
render_handler_frame_init(const RenderEntry*, u32)
{
  const ViewCtx*        view_ctx      = &g_RenderHandlerState.main_view;
  const ViewCtx*        prev_view_ctx = &g_RenderHandlerState.prev_main_view;
  const RenderSettings* settings      = &g_RenderHandlerState.settings;

  ViewportGpu viewport_gpu;
  viewport_gpu.proj                  = view_ctx->proj;
  viewport_gpu.view                  = view_ctx->view;
  viewport_gpu.view_proj             = view_ctx->view_proj;
  viewport_gpu.prev_view_proj        = prev_view_ctx->view_proj;
  viewport_gpu.inverse_view_proj     = view_ctx->inverse_view_proj;
  viewport_gpu.camera_world_pos      = view_ctx->camera.world_pos;
  viewport_gpu.prev_camera_world_pos = prev_view_ctx->camera.world_pos;
  viewport_gpu.frame_id              = g_RenderHandlerState.frame_id;


  // TODO(bshihabi): We should really do this somewhere else...
  // Pre-expose light values
  f32 ev100         = camera_params_to_ev100(settings->aperture, settings->shutter_time, settings->iso);
  f32 max_luminance = ev100_to_max_luminance(ev100);

  DirectionalLight directional_light = view_ctx->directional_light;
  // This just happens to work out even though the units are completely wrong, they fix themselves later
  // in the math. TODO(bshihabi): There should be a nicer way of doing this
  directional_light.illuminance     /= max_luminance;
  directional_light.sky_illuminance /= max_luminance;
  directional_light.diffuse          = diffuse_from_temperature((f32)directional_light.temperature);
  viewport_gpu.directional_light     = directional_light;

  viewport_gpu.taa_jitter            = !settings->disable_taa ? view_ctx->taa_jitter : Vec2(0.0f, 0.0f);


  {
    void* dst = unwrap(g_RenderHandlerState.buffers.viewport_buffer->mapped);
    memcpy(dst, &viewport_gpu, sizeof(viewport_gpu));
  }

  {
    RenderSettingsGpu render_settings_gpu = to_gpu_render_settings(*settings);
    void* dst = unwrap(g_RenderHandlerState.buffers.render_settings->mapped);
    memcpy(dst, &render_settings_gpu, sizeof(render_settings_gpu));
  }

  gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_DebugDrawInitMultiDrawIndirectArgs);
  gpu_dispatch(&g_RenderHandlerState.cmd_list, UCEIL_DIV(MAX(kDebugMaxVertices, kDebugMaxSdfs), 64), 1, 1);
  gpu_memory_barrier(&g_RenderHandlerState.cmd_list);
}

f64 g_CpuEffectiveTime = 0.0;

void
render_handler_debug_ui(const RenderEntry*, u32)
{
  ImGui::Begin("Rendering");
  static bool s_ShowDetailedPerformance = false;

  Camera*           camera            = get_scene_camera();
  DirectionalLight* directional_light = get_scene_directional_light();
  ImGui::DragFloat3("Sun Direction", (f32*)&directional_light->direction, 0.02f, -1.0f, 1.0f);
  // ImGui::DragFloat3("Sun Diffuse", (f32*)&g_Scene->directional_light.diffuse, 0.1f, 0.0f, 1.0f);
  ImGui::DragInt   ("Sun Temperature (Kelvin)", (s32*)&directional_light->temperature, 10.0f, 1000, 40000);
  ImGui::DragFloat ("Sun Intensity (Lux)", &directional_light->illuminance, 10.0f, 0.0f, 200000.0f);

  ImGui::DragFloat3("Sky Diffuse",         (f32*)&directional_light->sky_diffuse, 0.1f, 0.0f, 1.0f);
  ImGui::DragFloat ("Sky Intensity (Lux)", &directional_light->sky_illuminance, 10.0f, 0.0f, 100000.0f);

  ImGui::InputFloat3("Camera Position", (f32*)&camera->world_pos);

  ImGui::Combo("Debug Layer", (s32*)&g_Renderer.settings.debug_layer, kRenderDebugLayerNames, kRenderDebugLayerCount);

  ImGui::Checkbox("Disable TAA", &g_Renderer.settings.disable_taa);
  ImGui::Checkbox("Disable Diffuse GI", &g_Renderer.settings.disable_diffuse_gi);
  ImGui::Checkbox("Disable HDR", &g_Renderer.settings.disable_hdr);
  ImGui::Checkbox("Disable DoF", &g_Renderer.settings.disable_dof);
  ImGui::Checkbox("Disable Frustum Culling", &g_Renderer.settings.disable_frustum_culling);
  ImGui::Checkbox("Disable Occlusion Culling", &g_Renderer.settings.disable_occlusion_culling);
  ImGui::Checkbox("Disable Ray Tracing", &g_Renderer.settings.disable_ray_tracing);
  ImGui::Checkbox("Freeze Occlusion Culling", &g_Renderer.settings.freeze_occlusion_culling);
  ImGui::DragInt("Forced Model LoD", &g_Renderer.settings.forced_model_lod, 0.1, -1, 3);
  ImGui::Checkbox("Enable Debug Draw", &g_Renderer.settings.enabled_debug_draw);
  ImGui::Checkbox("Show Detailed Performance", &s_ShowDetailedPerformance);

  ImGui::DragFloat("Aperture", &g_Renderer.settings.aperture, 0.01f, 0.0f, 50.0f);
  ImGui::DragFloat("Shutter Time", &g_Renderer.settings.shutter_time, 0.001f, 0.0f, 1.0f);
  ImGui::DragFloat("ISO", &g_Renderer.settings.iso, 1.0f, 0.0f, 10000.0f);

  static f32 s_FocalDistance = g_Renderer.settings.focal_dist;
  ImGui::DragFloat("Focal Distance", &s_FocalDistance, 0.01f, 0.0f, 1000.0f);
  g_Renderer.settings.focal_dist = 0.9f * g_Renderer.settings.focal_dist + 0.1f * s_FocalDistance;

  ImGui::DragFloat("Focal Range", &g_Renderer.settings.focal_range, 0.01f, 0.0f, 100.0f);

  ImGui::DragInt("DoF Sample Count", (s32*)&g_Renderer.settings.dof_sample_count, 1.0f, 0, 256);
  ImGui::DragFloat("DoF Blur Radius", &g_Renderer.settings.dof_blur_radius, 0.1f, 0.0f, 40.0f);

  ImGui::DragFloat3("Probe Spacing", (f32*)&g_Renderer.settings.diffuse_gi_probe_spacing, 0.01f, 0.0, 25.0);
  // ImGui::DragInt("Probe Debug Rays", (s32*)&g_Renderer.settings.debug_probe_ray_idx, 1.0f, -1, 0x1000);
  ImGui::Checkbox("Probe Freeze Rotation", &g_Renderer.settings.freeze_probe_rotation);
  ImGui::Checkbox("Debug Sampled Probes (Hover with Mouse)", &g_Renderer.settings.debug_gi_sample_probes);

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

  char file_io_fmt_bps[32];
  char gpu_io_fmt_bps[32];

  bytes_to_readable_str(file_io_fmt_bps, sizeof(file_io_fmt_bps), g_AssetStreamingStats.file_io_bps);
  bytes_to_readable_str(gpu_io_fmt_bps,  sizeof(gpu_io_fmt_bps),  g_AssetStreamingStats.gpu_io_bps);

  ImGui::Text("File I/O: %s/Sec", file_io_fmt_bps);
  ImGui::Text("GPU  I/O: %s/Sec", gpu_io_fmt_bps);

  char gpu_memory_fmt[32];
  bytes_to_readable_str(gpu_memory_fmt, sizeof(gpu_memory_fmt), (f64)get_gpu_memory_usage());

  ImGui::Text("VRAM: %s", gpu_memory_fmt);

  if (g_GpuDevice->flags & kGpuFlagsEnableDevelopmentStablePower)
  {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Development GPU Stable Power");
  }


  if (s_ShowDetailedPerformance)
  {
    ImGui::Indent();
    const ViewCtx* view_ctx = &g_RenderHandlerState.main_view;

    u32 layer = kRenderLayerCount;
    for (u32 ibatch = 0; ibatch < view_ctx->render_batch_count; ibatch++)
    {
      const RenderBatch* batch = view_ctx->render_batches + ibatch;
      if (layer == batch->layer)
      {
        continue;
      }
      layer = batch->layer;

      const char* name = kRenderLayerNames[layer];
      if (!has_gpu_profiler_timestamp(name))
      {
        continue;
      }

      f64 dt = query_gpu_profiler_timestamp(name);
      ImGui::Text("%s: %f ms", name, dt);
    }
  }

  ImGui::End();

  ImGui::Render();

  RenderTarget* tonemapping = &g_RenderHandlerState.buffers.tonemapped_buffer;

  gpu_bind_render_target(&g_RenderHandlerState.cmd_list, tonemapping);
  gpu_bind_descriptor_heap(&g_RenderHandlerState.cmd_list, &g_Renderer.imgui_descriptor_heap);

  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_RenderHandlerState.cmd_list.d3d12_list);
}

void
render_handler_indirect_debug_draw(const RenderEntry*, u32)
{
  RenderBuffers* buffers       = &g_RenderHandlerState.buffers;
  RenderTarget*  render_target = &buffers->tonemapped_buffer;
  DepthTarget*   depth_target  = &buffers->gbuffer.depth;
  CmdList*       cmd           = &g_RenderHandlerState.cmd_list;

  gpu_memory_barrier(cmd);

  gpu_bind_render_target(cmd, render_target, depth_target);
  gpu_bind_graphics_pso(cmd, g_Renderer.pso_library.debug_draw_line_pso);
  DebugLineDrawSrt line_srt;
  line_srt.debug_line_vert_buffer = {buffers->debug_line_vert_buffer.srv.index};
  gpu_bind_srt(cmd, line_srt);
  gpu_ia_set_primitive_topology(cmd, D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  gpu_multi_draw_indirect(cmd, &buffers->debug_draw_args_buffer, nullptr, 0, 1);

  gpu_bind_graphics_pso(cmd, g_Renderer.pso_library.debug_draw_sdf_pso);
  DebugSdfDrawSrt sdf_srt;
  sdf_srt.debug_sdf_buffer = {buffers->debug_sdf_buffer.srv.index};
  gpu_bind_srt(cmd, sdf_srt);
  gpu_ia_set_primitive_topology(cmd, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  gpu_multi_draw_indirect(cmd, &buffers->debug_draw_args_buffer, nullptr, sizeof(MultiDrawIndirectArgs), 1);
}
