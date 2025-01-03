#include "Core/Engine/memory.h"

#include "Core/Engine/Render/misc.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/root_signature.hlsli"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

struct FrameInitParams
{
  RgConstantBuffer<Viewport> scene_buffer;
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
render_handler_frame_init(RenderContext* ctx, const void* data)
{
  FrameInitParams* params = (FrameInitParams*)data;

  Mat4 prev_view = view_from_camera(&g_Renderer.prev_camera);
  Mat4 view      = view_from_camera(&g_Renderer.camera);

  Viewport scene;
  scene.proj              = perspective_infinite_reverse_lh(kPI / 4.0f, (f32)ctx->m_Width / (f32)ctx->m_Height, kZNear);
  scene.view_proj         = scene.proj * view;
  scene.prev_view_proj    = scene.proj * prev_view;
  scene.inverse_view_proj = inverse_mat4(scene.view_proj);
  scene.camera_world_pos  = g_Renderer.camera.world_pos;
  scene.directional_light = g_Renderer.directional_light;
  scene.taa_jitter        = !g_Renderer.disable_taa ? g_Renderer.taa_jitter : Vec2(0.0f, 0.0f);

  ctx->write_cpu_upload_buffer(params->scene_buffer, &scene, sizeof(scene));

  ctx->set_graphics_root_shader_resource_view(kIndexBufferSlot ,          &g_UnifiedGeometryBuffer.index_buffer);
  ctx->set_graphics_root_shader_resource_view(kVertexBufferSlot,          &g_UnifiedGeometryBuffer.vertex_buffer);
  ctx->set_graphics_root_constant_buffer_view(kSceneBufferSlot,           params->scene_buffer);
  ctx->set_graphics_root_shader_resource_view(kAccelerationStructureSlot, &g_UnifiedGeometryBuffer.bvh.top_bvh);

  ctx->set_compute_root_shader_resource_view(kIndexBufferSlot ,           &g_UnifiedGeometryBuffer.index_buffer);
  ctx->set_compute_root_shader_resource_view(kVertexBufferSlot,           &g_UnifiedGeometryBuffer.vertex_buffer);
  ctx->set_compute_root_constant_buffer_view(kSceneBufferSlot,            params->scene_buffer);
  ctx->set_compute_root_shader_resource_view(kAccelerationStructureSlot,  &g_UnifiedGeometryBuffer.bvh.top_bvh);
}

void
init_frame_init_pass(AllocHeap heap, RgBuilder* builder)
{
  FrameInitParams* params = HEAP_ALLOC(FrameInitParams, g_InitHeap, 1);
  zero_memory(params, sizeof(FrameInitParams));

  RgHandle<GpuBuffer> scene_buffer = rg_create_upload_buffer(builder, "Viewport Buffer", sizeof(Viewport));

  RgPassBuilder*      pass         = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Frame Init", params, &render_handler_frame_init, true);
  params->scene_buffer             = RgConstantBuffer<Viewport>(pass, scene_buffer);
}

struct ImGuiParams
{
  RgRtv dst;
};

void
render_handler_imgui(RenderContext* ctx, const void* data)
{
  ImGuiParams* params = (ImGuiParams*)data;

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
