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

struct FrameInitParams
{
  RgConstantBuffer  <Viewport>    viewport_buffer;
  RgStructuredBuffer<SceneObjGpu> scene_obj_buffer;
  RgStructuredBuffer<MaterialGpu> material_buffer;
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
  main_viewport.proj              = perspective_infinite_reverse_lh(kPI / 4.0f, (f32)ctx->m_Width / (f32)ctx->m_Height, kZNear);
  main_viewport.view_proj         = main_viewport.proj * view;
  main_viewport.prev_view_proj    = main_viewport.proj * prev_view;
  main_viewport.inverse_view_proj = inverse_mat4(main_viewport.view_proj);
  main_viewport.camera_world_pos  = g_Renderer.camera.world_pos;
  main_viewport.directional_light = g_Renderer.directional_light;
  main_viewport.taa_jitter        = !settings.disable_taa ? g_Renderer.taa_jitter : Vec2(0.0f, 0.0f);

  ctx->write_cpu_upload_buffer(params->viewport_buffer, &main_viewport, sizeof(main_viewport));

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

  ctx->set_graphics_root_shader_resource_view(kIndexBufferSlot ,          &g_UnifiedGeometryBuffer.index_buffer);
  ctx->set_graphics_root_shader_resource_view(kVertexBufferSlot,          &g_UnifiedGeometryBuffer.vertex_buffer);
  ctx->set_graphics_root_constant_buffer_view(kViewportBufferSlot,        params->viewport_buffer);
  ctx->set_graphics_root_shader_resource_view(kSceneObjBufferSlot,        params->scene_obj_buffer);
  ctx->set_graphics_root_shader_resource_view(kMaterialBufferSlot,        params->material_buffer);
  ctx->set_graphics_root_shader_resource_view(kAccelerationStructureSlot, &g_UnifiedGeometryBuffer.bvh.top_bvh);

  ctx->set_compute_root_shader_resource_view(kIndexBufferSlot ,           &g_UnifiedGeometryBuffer.index_buffer);
  ctx->set_compute_root_shader_resource_view(kVertexBufferSlot,           &g_UnifiedGeometryBuffer.vertex_buffer);
  ctx->set_compute_root_constant_buffer_view(kViewportBufferSlot,         params->viewport_buffer);
  ctx->set_compute_root_shader_resource_view(kSceneObjBufferSlot,         params->scene_obj_buffer);
  ctx->set_compute_root_shader_resource_view(kMaterialBufferSlot,         params->material_buffer);
  ctx->set_compute_root_shader_resource_view(kAccelerationStructureSlot,  &g_UnifiedGeometryBuffer.bvh.top_bvh);
}

FrameResources
init_frame_init_pass(AllocHeap heap, RgBuilder* builder)
{
  FrameInitParams* params = HEAP_ALLOC(FrameInitParams, g_InitHeap, 1);
  zero_memory(params, sizeof(FrameInitParams));

  FrameResources ret;

  ret.viewport_buffer  = rg_create_upload_buffer(builder, "Viewport Buffer",     kGpuHeapSysRAMCpuToGpu, sizeof(Viewport));
  ret.material_buffer  = rg_create_upload_buffer(builder, "Material Buffer",     kGpuHeapSysRAMCpuToGpu, sizeof(MaterialGpu) * kMaxSceneObjs, sizeof(MaterialGpu));
  ret.scene_obj_buffer = rg_create_upload_buffer(builder, "Scene Object Buffer", kGpuHeapSysRAMCpuToGpu, sizeof(SceneObjGpu) * kMaxSceneObjs, sizeof(SceneObjGpu));

  RgPassBuilder*      pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Frame Init", params, &render_handler_frame_init, true);
  params->viewport_buffer  = RgConstantBuffer<Viewport>     (pass, ret.viewport_buffer);
  params->material_buffer  = RgStructuredBuffer<MaterialGpu>(pass, ret.material_buffer);
  params->scene_obj_buffer = RgStructuredBuffer<SceneObjGpu>(pass, ret.scene_obj_buffer);

  return ret;
}

struct ImGuiParams
{
  RgRtv dst;
};

void
render_handler_imgui(RenderContext* ctx, const RenderSettings&, const void* data)
{
  ImGuiParams* params = (ImGuiParams*)data;

  // Start the Dear ImGui frame
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  ImGui::Begin("Rendering");

#if 0
  if (ImGui::BeginCombo("View", GET_RENDER_BUFFER_NAME(out_render_options->debug_view)))
  {
    for (u32 i = 0; i < RenderBuffers::kCount; i++)
    {
      bool is_selected = out_render_options->debug_view == i;
      if (ImGui::Selectable(GET_RENDER_BUFFER_NAME(i), is_selected))
      {
        out_render_options->debug_view = (RenderBuffers::Entry)i;
      }
  
      if (is_selected)
      {
        ImGui::SetItemDefaultFocus();
      }
    }
  
    ImGui::EndCombo();
  }


#endif

  ImGui::DragFloat3("Direction", (f32*)&g_Scene->directional_light.direction, 0.02f, -1.0f, 1.0f);
  ImGui::DragFloat3("Diffuse", (f32*)&g_Scene->directional_light.diffuse, 0.1f, 0.0f, 1.0f);
  ImGui::DragFloat ("Intensity", &g_Scene->directional_light.intensity, 0.1f, 0.0f, 100.0f);

  ImGui::InputFloat3("Camera Position", (f32*)&g_Scene->camera.world_pos);

  ImGui::Checkbox("Disable TAA", &g_Renderer.settings.disable_taa);
  ImGui::Checkbox("Disable HDR", &g_Renderer.settings.disable_hdr);
  ImGui::Checkbox("Disable DoF", &g_Renderer.settings.disable_dof);

  ImGui::DragFloat("Aperture", &g_Renderer.settings.aperture, 0.01f, 0.0f, 50.0f);

  static f32 focal_distance = g_Renderer.settings.focal_dist;
  ImGui::DragFloat("Focal Distance", &focal_distance, 0.01f, 0.0f, 1000.0f);
  g_Renderer.settings.focal_dist = 0.9f * g_Renderer.settings.focal_dist + 0.1f * focal_distance;

  ImGui::DragFloat("Focal Range", &g_Renderer.settings.focal_range, 0.01f, 0.0f, 100.0f);

  ImGui::DragInt("DoF Sample Count", (s32*)&g_Renderer.settings.dof_sample_count, 1.0f, 0, 256);
  ImGui::DragFloat("DoF Blur Radius", &g_Renderer.settings.dof_blur_radius, 0.1f, 0.0f, 40.0f);

  ImGui::End();

  ImGui::Begin("GPU Profiling");
  f64 frame_time = query_gpu_profiler_timestamp(kTotalFrameGpuMarker);
  ImGui::Text("Frame Time: %f ms", frame_time);
  for (const RenderPass& pass : g_RenderGraph->render_passes)
  {
    f64 dt = query_gpu_profiler_timestamp(pass.name);
    // dbgln("%s: %f", pass.name, dt);
    ImGui::Text("%s: %f ms", pass.name, dt);
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
}
