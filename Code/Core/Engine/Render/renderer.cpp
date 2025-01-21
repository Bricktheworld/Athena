#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"

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
ShaderManager*  g_ShaderManager           = nullptr;
DescriptorPool* g_DescriptorCbvSrvUavPool = nullptr;
Scene*          g_Scene                   = nullptr;

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
  const SwapChain* swap_chain,
  RenderGraphDestroyFlags flags
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  RgBuilder builder = init_rg_builder(scratch_arena, swap_chain->width, swap_chain->height);

  GBuffer   gbuffer = init_gbuffer(&builder);

  init_frame_init_pass(scratch_arena, &builder);

  init_gbuffer_static(scratch_arena, &builder, &gbuffer);

  Ddgi ddgi = init_ddgi(scratch_arena, &builder);

  RgHandle<GpuTexture> hdr_buffer  = init_hdr_buffer(&builder);
  init_lighting(scratch_arena, &builder, gbuffer, ddgi, &hdr_buffer);

  RgHandle<GpuTexture> taa_buffer  = init_taa_buffer(&builder);
  init_taa(scratch_arena, &builder, hdr_buffer, gbuffer, &taa_buffer);

  RgHandle<GpuTexture> dof_buffer = init_depth_of_field(scratch_arena, &builder, gbuffer.depth, taa_buffer);

  RgHandle<GpuTexture> tonemapped_buffer = init_tonemapping(scratch_arena, &builder, dof_buffer);

  init_imgui_pass(scratch_arena, &builder, &tonemapped_buffer);

  init_back_buffer_blit(scratch_arena, &builder, tonemapped_buffer);

  compile_render_graph(g_InitHeap, builder, flags);
}

static void
init_renderer_psos(
  const GpuDevice* device,
  const SwapChain* swap_chain
) {
  GraphicsPipelineDesc fullscreen_pipeline_desc =
  {
    .vertex_shader = get_engine_shader(kVS_Fullscreen),
    .pixel_shader  = get_engine_shader(kPS_Fullscreen),
    .rtv_formats   = Span{swap_chain->format},
  };

  g_Renderer.back_buffer_blit_pso = init_graphics_pipeline(device, fullscreen_pipeline_desc, "Blit");
  g_Renderer.texture_copy_pso = init_compute_pipeline(device, get_engine_shader(kCS_TextureCopy), "Texture Copy");

  g_Renderer.standard_brdf_pso = init_ray_tracing_pipeline(device, get_engine_shader(kRT_StandardBrdf), "Standard BRDF RT");
  g_Renderer.standard_brdf_st  = init_shader_table(device, g_Renderer.standard_brdf_pso, "Standard BRDF Shader Table");

  g_Renderer.ddgi_probe_trace_pso = init_ray_tracing_pipeline(device, get_engine_shader(kRT_ProbeTrace), "Probe Trace RT");
  g_Renderer.ddgi_probe_trace_st  = init_shader_table(device, g_Renderer.ddgi_probe_trace_pso, "Probe Trace Shader Table");

  g_Renderer.ddgi_probe_blend_pso = init_compute_pipeline(device, get_engine_shader(kCS_ProbeBlending), "Probe Blending");

}

static void
destroy_renderer_psos()
{
  destroy_ray_tracing_pipeline(&g_Renderer.standard_brdf_pso);
  destroy_shader_table(&g_Renderer.standard_brdf_st);
  destroy_compute_pipeline(&g_Renderer.texture_copy_pso);
  destroy_graphics_pipeline(&g_Renderer.back_buffer_blit_pso);
  destroy_ray_tracing_pipeline(&g_Renderer.ddgi_probe_trace_pso);
  destroy_shader_table(&g_Renderer.ddgi_probe_trace_st);
  destroy_compute_pipeline(&g_Renderer.ddgi_probe_blend_pso);
}

static void
init_scene()
{
  g_Scene = HEAP_ALLOC(Scene, g_InitHeap, 1);
  zero_memory(g_Scene, sizeof(Scene));

  g_Scene->scene_objects = init_array<SceneObject>(g_InitHeap, 128);
  g_Scene->point_lights  = init_array<PointLight>(g_InitHeap, 128);
  static constexpr size_t kSceneObjectHeapSize = MiB(8);
  g_Scene->scene_object_allocator = init_linear_allocator(
    HEAP_ALLOC_ALIGNED(g_InitHeap, kSceneObjectHeapSize, 1),
    kSceneObjectHeapSize
  );

  g_Scene->directional_light.direction = Vec4(-1.0f, -1.0f, 0.0f, 0.0f);
  g_Scene->directional_light.diffuse   = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
  g_Scene->directional_light.intensity = 5.0f;
}

void
init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  HWND window
) {
  zero_memory(&g_Renderer, sizeof(g_Renderer));

  init_scene();
  g_DescriptorCbvSrvUavPool   = HEAP_ALLOC(DescriptorPool, g_InitHeap, 1);
  *g_DescriptorCbvSrvUavPool  = init_descriptor_pool(g_InitHeap, device, 2048, kDescriptorHeapTypeCbvSrvUav);

  const uint32_t kGraphMemory = MiB(32);
  g_Renderer.graph_allocator  = init_linear_allocator(HEAP_ALLOC_ALIGNED(g_InitHeap, kGraphMemory, alignof(u64)), kGraphMemory);

  init_renderer_dependency_graph(swap_chain, kRgDestroyAll);
  init_renderer_psos(device, swap_chain);


  g_Renderer.imgui_descriptor_heap = init_descriptor_linear_allocator(device, 1, kDescriptorHeapTypeCbvSrvUav);
  init_imgui_ctx(device, kGpuFormatRGBA16Float, window, &g_Renderer.imgui_descriptor_heap);

  g_Renderer.settings.aperture    = 5.6f;
  g_Renderer.settings.focal_dist  = 8.0f;
  g_Renderer.settings.focal_range = 3.0f;
  g_Renderer.settings.dof_blur_radius  = 15.0f;
  g_Renderer.settings.dof_sample_count = 32;
}

void
renderer_hot_reload(const GpuDevice* device, const SwapChain* swap_chain)
{
  destroy_renderer_psos();
  init_renderer_psos(device, swap_chain);
}

void
renderer_on_resize(const SwapChain* swap_chain)
{
  destroy_render_graph(kRgDestroyAll);
  init_renderer_dependency_graph(swap_chain, kRgDestroyAll);
}

void
destroy_renderer()
{
  destroy_renderer_psos();
  destroy_render_graph(kRgDestroyAll);
  destroy_imgui_ctx();
  zero_memory(&g_Renderer, sizeof(g_Renderer));
}

void
build_acceleration_structures(GpuDevice* device)
{
  g_UnifiedGeometryBuffer.bvh = init_gpu_bvh(
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
  g_Renderer.meshes = init_array<RenderModelSubset>(g_FrameHeap, 128);
}

void
submit_mesh(RenderModelSubset mesh)
{
  *array_add(&g_Renderer.meshes) = mesh;
}

void
init_unified_geometry_buffer(const GpuDevice* device)
{
  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));

  GpuBufferDesc vertex_uber_desc = {0};
  vertex_uber_desc.size = MiB(512);

  g_UnifiedGeometryBuffer.vertex_buffer        = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapGpuOnly, "Vertex Buffer");
  g_UnifiedGeometryBuffer.vertex_buffer_offset = 0;

  GpuBufferDesc index_uber_desc = {0};
  index_uber_desc.size = MiB(512);

  g_UnifiedGeometryBuffer.index_buffer        = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapGpuOnly, "Index Buffer");
  g_UnifiedGeometryBuffer.index_buffer_offset = 0;
}

void
destroy_unified_geometry_buffer()
{
  free_gpu_buffer(&g_UnifiedGeometryBuffer.vertex_buffer);
  free_gpu_buffer(&g_UnifiedGeometryBuffer.index_buffer);

  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));
}

static UploadContext g_UploadContext;

void
init_global_upload_context(const GpuDevice* device)
{
  GpuBufferDesc staging_desc = {0};
  staging_desc.size = MiB(64);

  g_UploadContext.staging_buffer = alloc_gpu_buffer_no_heap(device, staging_desc, kGpuHeapSysRAMCpuToGpu, "Staging Buffer");
  g_UploadContext.staging_offset = 0;
  g_UploadContext.cmd_list_allocator = init_cmd_list_allocator(g_InitHeap, device, &device->copy_queue, 16);
  g_UploadContext.cmd_list = alloc_cmd_list(&g_UploadContext.cmd_list_allocator);
  g_UploadContext.device = device;
  static constexpr u64 kCpuUploadArenaSize = MiB(64);
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
  ret.model_subsets = init_array<RenderModelSubset>(heap, model.model_subsets.size);
  for (u32 imodel_subset = 0; imodel_subset < model.model_subsets.size; imodel_subset++)
  {
    const ModelSubsetData* src = &model.model_subsets[imodel_subset];
    RenderModelSubset* dst = array_add(&ret.model_subsets);

    reset_linear_allocator(&g_UploadContext.cpu_upload_arena);

    u32 vertex_buffer_offset = alloc_into_vertex_uber((u32)src->vertices.size);

    dst->index_count         = (u32)src->indices.size;
    dst->index_buffer_offset = alloc_into_index_uber(dst->index_count);
    dst->vertex_shader       = kVS_Basic;
    dst->material_shader     = kPS_BasicNormalGloss;
    dst->material            = src->material;

    // Kick asset loading
    kick_asset_load(dst->material);

    GraphicsPipelineDesc graphics_pipeline_desc =
    {
      .vertex_shader   = get_engine_shader(dst->vertex_shader),
      .pixel_shader    = get_engine_shader(dst->material_shader),
      .rtv_formats     = kGBufferRenderTargetFormats,
      .dsv_format      = kGpuFormatD32Float,
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
  const ModelData& model,
  EngineShaderIndex vertex_shader,
  EngineShaderIndex material_shader
) {
  UNREFERENCED_PARAMETER(vertex_shader);
  UNREFERENCED_PARAMETER(material_shader);
  SceneObject* ret = array_add(&g_Scene->scene_objects);
  ret->flags = kSceneObjectMesh;
  ret->model = init_render_model(g_Scene->scene_object_allocator, model);

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

  u32  idx = g_FrameId % ARRAY_LENGTH(kHaltonSequence);
  Vec2 ret = kHaltonSequence[idx] - Vec2(0.5f, 0.5f);
  ret.x   /= (f32)g_RenderGraph->width;
  ret.y   /= (f32)g_RenderGraph->height;

  ret     *= 2.0f;
  return ret;
}

void
submit_scene()
{
  for (const SceneObject& obj : g_Scene->scene_objects)
  {
//    u8 flags = obj.flags;
//    if (flags & kSceneObjectPendingLoad)
//    {
//      if (!job_has_completed(obj.loading_signal))
//        continue;
//
//      flags &= ~kSceneObjectPendingLoad;
//    }

    for (const RenderModelSubset& mesh_inst : obj.model.model_subsets)
    {
      submit_mesh(mesh_inst);
    }
  }

  g_Renderer.taa_jitter        = get_taa_jitter();

  g_Renderer.prev_camera       = g_Renderer.camera;
  g_Renderer.camera            = g_Scene->camera;
  g_Renderer.directional_light = g_Scene->directional_light;
}
