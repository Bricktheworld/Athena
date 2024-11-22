#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/blit.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/depth_of_field.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/lighting.h"
#include "Core/Engine/Render/misc.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Render/taa.h"
#include "Core/Engine/Render/visibility_buffer.h"

#include "Core/Engine/Shaders/interlop.hlsli"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

Renderer g_Renderer;

UnifiedGeometryBuffer g_UnifiedGeometryBuffer;

ShaderManager* g_ShaderManager = nullptr;

void
init_shader_manager(const GpuDevice* device)
{
  if (g_ShaderManager == nullptr)
  {
    g_ShaderManager = HEAP_ALLOC(ShaderManager, g_InitHeap, 1);
  }

  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    g_ShaderManager->shaders[i] = load_shader_from_memory(device, kEngineShaderBinSrcs[i], kEngineShaderBinSizes[i]);
  }
}

void
destroy_shader_manager()
{
  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    destroy_shader(&g_ShaderManager->shaders[i]);
  }

  zero_memory(g_ShaderManager, sizeof(ShaderManager));
}

const GpuShader*
get_engine_shader(u32 index)
{
  ASSERT(g_ShaderManager != nullptr);
  ASSERT(index < kEngineShaderCount);
  return &g_ShaderManager->shaders[index];
}

static void
init_renderer_dependency_graph(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  RenderGraphDestroyFlags flags
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  RgBuilder builder = init_rg_builder(scratch_arena, swap_chain->width, swap_chain->height);

  GBuffer   gbuffer = init_gbuffer(&builder);

  init_frame_init_pass(scratch_arena, &builder);

//  VBuffer vbuffer = init_vbuffer(scratch_arena, &builder);

  init_gbuffer_static(scratch_arena, &builder, &gbuffer);

  Ddgi ddgi = init_ddgi(scratch_arena, &builder);

  RgHandle<GpuTexture> hdr_buffer  = init_hdr_buffer(&builder);
  init_lighting(scratch_arena, &builder, gbuffer, ddgi, &hdr_buffer);

  RgHandle<GpuTexture> taa_buffer  = init_taa_buffer(&builder);
  init_taa(scratch_arena, &builder, hdr_buffer, gbuffer, &taa_buffer);

  RgHandle<GpuTexture> post_buffer = init_post_processing(scratch_arena, &builder, taa_buffer);

  init_imgui_pass(scratch_arena, &builder, &post_buffer);

//  RgHandle<GpuTexture> debug_vbuffer = rg_create_texture(&builder, "Debug VBuffer", FULL_RES(&builder), DXGI_FORMAT_R32G32B32A32_FLOAT);
//  init_debug_vbuffer(scratch_arena, &builder, vbuffer, &debug_vbuffer);

  init_back_buffer_blit(scratch_arena, &builder, post_buffer);

  compile_render_graph(g_InitHeap, builder, device, &g_Renderer.graph, flags);
}

static void
init_renderer_psos(
  const GpuDevice* device,
  const SwapChain* swap_chain
) {
  GraphicsPipelineDesc visibility_pipeline_desc =
  {
    .vertex_shader   = get_engine_shader(kVS_Basic),
    .pixel_shader    = get_engine_shader(kPS_VisibilityBuffer),
    .rtv_formats     = Span{DXGI_FORMAT_R32_UINT},
    .dsv_format      = DXGI_FORMAT_D32_FLOAT,
    .comparison_func = kDepthComparison,
    .stencil_enable  = false,
  };
  g_Renderer.vbuffer_pso       = init_graphics_pipeline(device, visibility_pipeline_desc, "Visibility Buffer");
  g_Renderer.debug_vbuffer_pso = init_compute_pipeline(device, get_engine_shader(kCS_VisibilityBufferVisualize), "Visibility Buffer Visualize");


  g_Renderer.taa_pso = init_compute_pipeline(device, get_engine_shader(kCS_TAA), "TAA");

  GraphicsPipelineDesc post_pipeline_desc = 
  {
    .vertex_shader = get_engine_shader(kVS_Fullscreen),
    .pixel_shader  = get_engine_shader(kPS_ToneMapping),
    .rtv_formats   = Span{swap_chain->format},
  };
  g_Renderer.post_processing_pipeline = init_graphics_pipeline(device, post_pipeline_desc, "Post Processing");

  GraphicsPipelineDesc fullscreen_pipeline_desc =
  {
    .vertex_shader = get_engine_shader(kVS_Fullscreen),
    .pixel_shader  = get_engine_shader(kPS_Fullscreen),
    .rtv_formats   = Span{swap_chain->format},
  };

  g_Renderer.back_buffer_blit_pso = init_graphics_pipeline(device, fullscreen_pipeline_desc, "Blit");

  g_Renderer.standard_brdf_pso = init_ray_tracing_pipeline(device, get_engine_shader(kRT_StandardBrdf), "Standard BRDF RT");
  g_Renderer.standard_brdf_st  = init_shader_table(device, g_Renderer.standard_brdf_pso, "Standard BRDF Shader Table");

  g_Renderer.ddgi_probe_trace_pso = init_ray_tracing_pipeline(device, get_engine_shader(kRT_ProbeTrace), "Probe Trace RT");
  g_Renderer.ddgi_probe_trace_st  = init_shader_table(device, g_Renderer.ddgi_probe_trace_pso, "Probe Trace Shader Table");

  g_Renderer.ddgi_probe_blend_pso = init_compute_pipeline(device, get_engine_shader(kCS_ProbeBlending), "Probe Blending");

}

static void
destroy_renderer_psos()
{
  destroy_graphics_pipeline(&g_Renderer.vbuffer_pso);
  destroy_compute_pipeline(&g_Renderer.debug_vbuffer_pso);
  destroy_compute_pipeline(&g_Renderer.taa_pso);
  destroy_graphics_pipeline(&g_Renderer.post_processing_pipeline);
  destroy_ray_tracing_pipeline(&g_Renderer.standard_brdf_pso);
  destroy_shader_table(&g_Renderer.standard_brdf_st);
  destroy_graphics_pipeline(&g_Renderer.back_buffer_blit_pso);
  destroy_ray_tracing_pipeline(&g_Renderer.ddgi_probe_trace_pso);
  destroy_shader_table(&g_Renderer.ddgi_probe_trace_st);
  destroy_compute_pipeline(&g_Renderer.ddgi_probe_blend_pso);
}

void
init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  HWND window
) {
  zero_memory(&g_Renderer, sizeof(g_Renderer));

  const uint32_t kGraphMemory = MiB(32);
  g_Renderer.graph_allocator = init_linear_allocator(HEAP_ALLOC_ALIGNED(g_InitHeap, kGraphMemory, alignof(uint64_t)), kGraphMemory);

  init_renderer_dependency_graph(device, swap_chain, kRgDestroyAll);
  init_renderer_psos(device, swap_chain);

  g_Renderer.imgui_descriptor_heap = init_descriptor_linear_allocator(device, 1, kDescriptorHeapTypeCbvSrvUav);
  init_imgui_ctx(device, swap_chain, window, &g_Renderer.imgui_descriptor_heap);
}

void
renderer_hot_reload(const GpuDevice* device, const SwapChain* swap_chain)
{
  destroy_renderer_psos();
  init_renderer_psos(device, swap_chain);
}

void
renderer_on_resize(
  const GpuDevice* device,
  const SwapChain* swap_chain
) {
  destroy_render_graph(&g_Renderer.graph, kRgDestroyAll);
  init_renderer_dependency_graph(device, swap_chain, kRgDestroyAll);
}

void
destroy_renderer()
{
  destroy_renderer_psos();
  destroy_render_graph(&g_Renderer.graph, kRgDestroyAll);
  destroy_imgui_ctx();
  zero_memory(&g_Renderer, sizeof(g_Renderer));
}

void
build_acceleration_structures(GpuDevice* device)
{
  g_UnifiedGeometryBuffer.bvh = init_acceleration_structure(
    device,
    g_UnifiedGeometryBuffer.vertex_buffer,
    g_UnifiedGeometryBuffer.vertex_buffer_offset,
    sizeof(Vertex),
    g_UnifiedGeometryBuffer.index_buffer,
    g_UnifiedGeometryBuffer.index_buffer_offset,
    "Scene Acceleration Structure"
  );
}


void
begin_renderer_recording()
{
  g_Renderer.meshes = init_array<RenderMeshInst>(g_FrameHeap, 128);
}

void
submit_mesh(RenderMeshInst mesh)
{
  *array_add(&g_Renderer.meshes) = mesh;
}

void
init_unified_geometry_buffer(const GpuDevice* device)
{
  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));

  GpuBufferDesc vertex_uber_desc = {0};
  vertex_uber_desc.size = MiB(512);

  g_UnifiedGeometryBuffer.vertex_buffer        = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapTypeLocal, "Vertex Buffer");
  g_UnifiedGeometryBuffer.vertex_buffer_offset = 0;

  GpuBufferDesc index_uber_desc = {0};
  index_uber_desc.size = MiB(512);

  g_UnifiedGeometryBuffer.index_buffer        = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapTypeLocal, "Index Buffer");
  g_UnifiedGeometryBuffer.index_buffer_offset = 0;
}

void
destroy_unified_geometry_buffer()
{
  free_gpu_buffer(&g_UnifiedGeometryBuffer.vertex_buffer);
  free_gpu_buffer(&g_UnifiedGeometryBuffer.index_buffer);

  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));
}

Scene
init_scene(AllocHeap heap)
{
  Scene ret = {0};
  ret.scene_objects = init_array<SceneObject>(heap, 128);
  ret.point_lights  = init_array<PointLight>(heap, 128);
  static constexpr size_t kSceneObjectHeapSize = MiB(8);
  ret.scene_object_allocator = init_linear_allocator(
    HEAP_ALLOC_ALIGNED(heap, kSceneObjectHeapSize, 1),
    kSceneObjectHeapSize
  );

  ret.directional_light.direction = Vec4(-1.0f, -1.0f, 0.0f, 0.0f);
  ret.directional_light.diffuse   = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
  ret.directional_light.intensity = 5.0f;
  return ret;
}

static UploadContext g_UploadContext;

void
init_global_upload_context(const GpuDevice* device)
{
  GpuBufferDesc staging_desc = {0};
  staging_desc.size = MiB(32);

  g_UploadContext.staging_buffer = alloc_gpu_buffer_no_heap(device, staging_desc, kGpuHeapTypeUpload, "Staging Buffer");
  g_UploadContext.staging_offset = 0;
  g_UploadContext.cmd_list_allocator = init_cmd_list_allocator(g_InitHeap, device, &device->copy_queue, 16);
  g_UploadContext.cmd_list = alloc_cmd_list(&g_UploadContext.cmd_list_allocator);
  g_UploadContext.device = device;
  static constexpr u64 kCpuUploadArenaSize = MiB(4);
  g_UploadContext.cpu_upload_arena = init_linear_allocator(
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
  if (g_UploadContext.staging_offset == 0)
    return;

  FenceValue value = submit_cmd_lists(&g_UploadContext.cmd_list_allocator, {g_UploadContext.cmd_list});
  g_UploadContext.cmd_list = alloc_cmd_list(&g_UploadContext.cmd_list_allocator);

  block_gpu_fence(&g_UploadContext.cmd_list_allocator.fence, value);

  g_UploadContext.staging_offset = 0;
}

static void
upload_gpu_data(GpuBuffer* dst_gpu, u64 dst_offset, const void* src, u64 size)
{
  if (g_UploadContext.staging_buffer.desc.size - g_UploadContext.staging_offset < size)
  {
    flush_upload_staging();
  }
  void* dst = (void*)(u64(unwrap(g_UploadContext.staging_buffer.mapped)) + g_UploadContext.staging_offset);
  memcpy(dst, src, size);

  g_UploadContext.cmd_list.d3d12_list->CopyBufferRegion(dst_gpu->d3d12_buffer,
                                                         dst_offset,
                                                         g_UploadContext.staging_buffer.d3d12_buffer,
                                                         g_UploadContext.staging_offset,
                                                         size);
  g_UploadContext.staging_offset += size;
}

static u32
alloc_into_vertex_uber(u32 vertex_count)
{
  u32 ret = g_UnifiedGeometryBuffer.vertex_buffer_offset;
  ASSERT((ret + vertex_count) * sizeof(Vertex) <= g_UnifiedGeometryBuffer.vertex_buffer.desc.size);

  g_UnifiedGeometryBuffer.vertex_buffer_offset += vertex_count;

  return ret;
}

static u32
alloc_into_index_uber(u32 index_count)
{
  u32 ret = g_UnifiedGeometryBuffer.index_buffer_offset;
  ASSERT((ret + index_count) * sizeof(u32) <= g_UnifiedGeometryBuffer.index_buffer.desc.size);

  g_UnifiedGeometryBuffer.index_buffer_offset += index_count;

  return ret;
}

static RenderModel
init_render_model(AllocHeap heap, const ModelData& model)
{
  RenderModel ret = {0};
  ret.mesh_insts = init_array<RenderMeshInst>(heap, model.mesh_insts.size);
  for (u32 imesh_inst = 0; imesh_inst < model.mesh_insts.size; imesh_inst++)
  {
    const MeshInstData* src = &model.mesh_insts[imesh_inst];
    RenderMeshInst* dst = array_add(&ret.mesh_insts);

    reset_linear_allocator(&g_UploadContext.cpu_upload_arena);

    u32 vertex_buffer_offset = alloc_into_vertex_uber((u32)src->vertices.size);

    dst->index_count         = (u32)src->indices.size;
    dst->index_buffer_offset = alloc_into_index_uber(dst->index_count);
    dst->vertex_shader       = kVS_Basic;
    dst->material_shader     = kPS_BasicNormalGloss;

    GraphicsPipelineDesc graphics_pipeline_desc =
    {
      .vertex_shader   = get_engine_shader(dst->vertex_shader),
      .pixel_shader    = get_engine_shader(dst->material_shader),
      .rtv_formats     = kGBufferRenderTargetFormats,
      .dsv_format      = DXGI_FORMAT_D32_FLOAT,
      .comparison_func = kDepthComparison,
      .stencil_enable  = false,
    };

    dst->gbuffer_pso = init_graphics_pipeline(g_UploadContext.device, graphics_pipeline_desc, "Mesh PSO");

    u32* indices = HEAP_ALLOC(u32, (AllocHeap)g_UploadContext.cpu_upload_arena, dst->index_count);

    for (u32 iindex = 0; iindex < dst->index_count; iindex++)
    {
      indices[iindex] = src->indices[iindex] + vertex_buffer_offset;
    }

    upload_gpu_data(
      &g_UnifiedGeometryBuffer.vertex_buffer,
      vertex_buffer_offset * sizeof(Vertex),
      src->vertices.memory,
      src->vertices.size * sizeof(Vertex)
    );

    upload_gpu_data(
      &g_UnifiedGeometryBuffer.index_buffer,
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
  const ModelData& model,
  EngineShaderIndex vertex_shader,
  EngineShaderIndex material_shader
) {
  UNREFERENCED_PARAMETER(vertex_shader);
  UNREFERENCED_PARAMETER(material_shader);
  SceneObject* ret = array_add(&scene->scene_objects);
  ret->flags = kSceneObjectMesh;
  ret->model = init_render_model(scene->scene_object_allocator, model);

  return ret;
}

PointLight* add_point_light(Scene* scene)
{
  PointLight* ret = array_add(&scene->point_lights);
  ret->position = Vec4(0, 0, 0, 1);
  ret->color = Vec4(1, 1, 1, 1);
  ret->radius = 10;
  ret->intensity = 10;

  return ret;
}

static Vec2
get_taa_jitter()
{
  static const Vec2 kHaltonSequence[] =
  {
    Vec2(0.500000f, 0.333333f),
    Vec2(0.250000f, 0.666667f),
    Vec2(0.750000f, 0.111111f),
    Vec2(0.125000f, 0.444444f),
    Vec2(0.625000f, 0.777778f),
    Vec2(0.375000f, 0.222222f),
    Vec2(0.875000f, 0.555556f),
    Vec2(0.062500f, 0.888889f),
    Vec2(0.562500f, 0.037037f),
    Vec2(0.312500f, 0.370370f),
    Vec2(0.812500f, 0.703704f),
    Vec2(0.187500f, 0.148148f),
    Vec2(0.687500f, 0.481481f),
    Vec2(0.437500f, 0.814815f),
    Vec2(0.937500f, 0.259259f),
    Vec2(0.031250f, 0.592593f),
  };

  u32  idx = g_Renderer.graph.frame_id % ARRAY_LENGTH(kHaltonSequence);
  Vec2 ret = kHaltonSequence[idx] - Vec2(0.5f, 0.5f);
  ret.x   /= (f32)g_Renderer.graph.width;
  ret.y   /= (f32)g_Renderer.graph.height;

  ret     *= 2.0f;
  return ret;
}

void
submit_scene(const Scene& scene)
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
      submit_mesh(mesh_inst);
    }
  }

  g_Renderer.taa_jitter        = get_taa_jitter();

  g_Renderer.prev_camera       = g_Renderer.camera;
  g_Renderer.camera            = scene.camera;
  g_Renderer.directional_light = scene.directional_light;
}
