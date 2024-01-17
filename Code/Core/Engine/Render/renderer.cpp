#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/depth_of_field.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/lighting.h"
#include "Core/Engine/Render/post_processing.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Vendor/ufbx/ufbx.h"
#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

ShaderManager
init_shader_manager(const GraphicsDevice* device)
{
  ShaderManager ret = {0};
  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    // const wchar_t* path = kShaderPaths[i];
    ret.shaders[i] = load_shader_from_memory(device, kEngineShaderBinSrcs[i], kEngineShaderBinSizes[i]);
  }

  return ret;
}

void
destroy_shader_manager(ShaderManager* shader_manager)
{
  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    destroy_shader(&shader_manager->shaders[i]);
  }

  zero_memory(shader_manager, sizeof(ShaderManager));
}

Renderer
init_renderer(
  const GraphicsDevice* device,
  const SwapChain* swap_chain,
  const ShaderManager& shader_manager,
  HWND window
) {
  Renderer ret = {0};

  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  RgBuilder builder = init_rg_builder(scratch_arena, swap_chain->width, swap_chain->height);
  GBuffer   gbuffer = init_gbuffer(&builder);

  init_gbuffer_static(scratch_arena, &builder, &gbuffer);

  RgHandle<GpuImage> hdr_buffer = init_hdr_buffer(&builder);
  init_lighting(scratch_arena, &builder, device, gbuffer, &hdr_buffer);

  init_post_processing(scratch_arena, &builder, device, hdr_buffer);

  ret.graph = compile_render_graph(g_InitHeap, builder, device);

  return ret;
}

void
destroy_renderer(Renderer* renderer)
{
  destroy_imgui_ctx();
  zero_memory(renderer, sizeof(Renderer));
}

void
build_acceleration_structures(GraphicsDevice* device, Scene* scene)
{
  scene->bvh = init_acceleration_structure(device,
                                           scene->vertex_uber_buffer,
                                           scene->vertex_uber_buffer_offset,
                                           sizeof(interlop::Vertex),
                                           scene->index_uber_buffer,
                                           scene->index_uber_buffer_offset,
                                           "Scene Acceleration Structure");
}


void
begin_renderer_recording(Renderer* renderer)
{
  renderer->meshes = init_array<RenderMeshInst>(g_FrameHeap, 128);
}

void
submit_mesh(Renderer* renderer, RenderMeshInst mesh)
{
  *array_add(&renderer->meshes) = mesh;
}

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

Scene
init_scene(AllocHeap heap, const GraphicsDevice* device)
{
  Scene ret = {0};
  ret.scene_objects = init_array<SceneObject>(heap, 128);
  ret.point_lights  = init_array<interlop::PointLight>(heap, 128);
  static constexpr size_t kSceneObjectHeapSize = MiB(8);
  ret.scene_object_allocator = init_linear_allocator(
    HEAP_ALLOC_ALIGNED(heap, kSceneObjectHeapSize, 1),
    kSceneObjectHeapSize
  );

  GpuBufferDesc vertex_uber_desc = {0};
  vertex_uber_desc.size = MiB(512);

  ret.vertex_uber_buffer = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapTypeLocal, "Vertex Buffer");

  GpuBufferDesc index_uber_desc = {0};
  index_uber_desc.size = MiB(512);

  ret.index_uber_buffer = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapTypeLocal, "Index Buffer");

  ret.directional_light.direction = Vec4(-1.0f, -1.0f, 0.0f, 0.0f);
  ret.directional_light.diffuse   = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
  ret.directional_light.intensity = 5.0f;
  return ret;
}

static UploadContext g_upload_context;

void
init_global_upload_context(const GraphicsDevice* device)
{
  GpuBufferDesc staging_desc = {0};
  staging_desc.size = MiB(32);

  g_upload_context.staging_buffer = alloc_gpu_buffer_no_heap(device, staging_desc, kGpuHeapTypeUpload, "Staging Buffer");
  g_upload_context.staging_offset = 0;
  g_upload_context.cmd_list_allocator = init_cmd_list_allocator(g_InitHeap, device, &device->copy_queue, 16);
  g_upload_context.cmd_list = alloc_cmd_list(&g_upload_context.cmd_list_allocator);
  g_upload_context.device = device;
  static constexpr u64 kCpuUploadArenaSize = MiB(4);
  g_upload_context.cpu_upload_arena = init_linear_allocator(
    HEAP_ALLOC_ALIGNED(g_InitHeap, kCpuUploadArenaSize, 1), 
    kCpuUploadArenaSize
  );
}

void
destroy_global_upload_context()
{
//  ACQUIRE(&g_upload_context, UploadContext* upload_ctx)
//  {
//    free_gpu_ring_buffer(&upload_ctx->ring_buffer);
//  };
}

static void
flush_upload_staging()
{
  if (g_upload_context.staging_offset == 0)
    return;

  FenceValue value = submit_cmd_lists(&g_upload_context.cmd_list_allocator, {g_upload_context.cmd_list});
  g_upload_context.cmd_list = alloc_cmd_list(&g_upload_context.cmd_list_allocator);

  block_for_fence_value(&g_upload_context.cmd_list_allocator.fence, value);

  g_upload_context.staging_offset = 0;
}

static void
upload_gpu_data(GpuBuffer* dst_gpu, u64 dst_offset, const void* src, u64 size)
{
  if (g_upload_context.staging_buffer.desc.size - g_upload_context.staging_offset < size)
  {
    flush_upload_staging();
  }
  void* dst = (void*)(u64(unwrap(g_upload_context.staging_buffer.mapped)) + g_upload_context.staging_offset);
  memcpy(dst, src, size);

  g_upload_context.cmd_list.d3d12_list->CopyBufferRegion(dst_gpu->d3d12_buffer,
                                                         dst_offset,
                                                         g_upload_context.staging_buffer.d3d12_buffer,
                                                         g_upload_context.staging_offset,
                                                         size);
  g_upload_context.staging_offset += size;
}

static u32
alloc_into_vertex_uber(Scene* scene, u32 vertex_count)
{
  u32 ret = scene->vertex_uber_buffer_offset;
  ASSERT((ret + vertex_count) * sizeof(interlop::Vertex) <= scene->vertex_uber_buffer.desc.size);

  scene->vertex_uber_buffer_offset += vertex_count;

  return ret;
}

static u32
alloc_into_index_uber(Scene* scene, u32 index_count)
{
  u32 ret = scene->index_uber_buffer_offset;
  ASSERT((ret + index_count) * sizeof(u32) <= scene->index_uber_buffer.desc.size);

  scene->index_uber_buffer_offset += index_count;

  return ret;
}

static RenderModel
init_render_model(AllocHeap heap, const ModelData& model, Scene* scene, const ShaderManager& shader_manager)
{
  RenderModel ret = {0};
  ret.mesh_insts = init_array<RenderMeshInst>(heap, model.mesh_insts.size);
  for (u32 imesh_inst = 0; imesh_inst < model.mesh_insts.size; imesh_inst++)
  {
    const MeshInstData* src = &model.mesh_insts[imesh_inst];
    RenderMeshInst* dst = array_add(&ret.mesh_insts);

    reset_linear_allocator(&g_upload_context.cpu_upload_arena);

    u32 vertex_buffer_offset = alloc_into_vertex_uber(scene, src->vertices.size);

    dst->index_count         = src->indices.size;
    dst->index_buffer_offset = alloc_into_index_uber(scene, dst->index_count);
    dst->vertex_shader       = kVS_Basic;
    dst->material_shader     = kPS_BasicNormalGloss;

    GraphicsPipelineDesc graphics_pipeline_desc =
    {
      .vertex_shader   = shader_manager.shaders[dst->vertex_shader],
      .pixel_shader    = shader_manager.shaders[dst->material_shader],
      .rtv_formats     = kGBufferRenderTargetFormats,
      .dsv_format      = kRenderBufferDescs[RenderBuffers::kGBufferDepth].format,
      .comparison_func = kDepthComparison,
      .stencil_enable  = false,
    };

    dst->gbuffer_pso = init_graphics_pipeline(g_upload_context.device, graphics_pipeline_desc, "Mesh PSO");

    u32* indices = HEAP_ALLOC(u32, (AllocHeap)g_upload_context.cpu_upload_arena, dst->index_count);

    for (u32 iindex = 0; iindex < dst->index_count; iindex++)
    {
      indices[iindex] = src->indices[iindex] + vertex_buffer_offset;
    }

    upload_gpu_data(
      &scene->vertex_uber_buffer,
      vertex_buffer_offset * sizeof(Vertex),
      src->vertices.memory,
      src->vertices.size * sizeof(Vertex)
    );

    upload_gpu_data(
      &scene->index_uber_buffer,
      dst->index_buffer_offset * sizeof(u32),
      indices,
      dst->index_count * sizeof(u32)
    );
  }
  flush_upload_staging();
  return ret;
}

SceneObject*
add_scene_object(
  Scene* scene,
  const ShaderManager& shader_manager,
  const ModelData& model,
  EngineShaderIndex vertex_shader,
  EngineShaderIndex material_shader
) {
  SceneObject* ret = array_add(&scene->scene_objects);
  ret->flags = kSceneObjectMesh;
  ret->model = init_render_model(scene->scene_object_allocator, model, scene, shader_manager);

  return ret;
}

interlop::PointLight* add_point_light(Scene* scene)
{
  interlop::PointLight* ret = array_add(&scene->point_lights);
  ret->position = Vec4(0, 0, 0, 1);
  ret->color = Vec4(1, 1, 1, 1);
  ret->radius = 10;
  ret->intensity = 10;

  return ret;
}

void
submit_scene(const Scene& scene, Renderer* renderer)
{
  for (const SceneObject& obj : scene.scene_objects)
  {
//    u8 flags = obj.flags;
//    if (flags & kSceneObjectPendingLoad)
//    {
//      if (!job_has_completed(obj.loading_signal))
//        continue;
//
//      flags &= ~kSceneObjectPendingLoad;
//    }

    for (const RenderMeshInst& mesh_inst : obj.model.mesh_insts)
    {
      submit_mesh(renderer, mesh_inst);
    }
  }
}
