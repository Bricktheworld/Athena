#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/blit.h"
#include "Core/Engine/Render/depth_of_field.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/lighting.h"
#include "Core/Engine/Render/misc.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Render/taa.h"
#include "Core/Engine/Render/visibility_buffer.h"

#include "Core/Engine/Shaders/root_signature.hlsli"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

Renderer g_Renderer;

UnifiedGeometryBuffer g_UnifiedGeometryBuffer;
ShaderManager*  g_ShaderManager           = nullptr;
DescriptorPool* g_DescriptorCbvSrvUavPool = nullptr;

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

void
reload_engine_shader(const char* entry_point_name, const u8* bin, u64 bin_size)
{
  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    if (_stricmp(entry_point_name, kEngineShaderNames[i]) == 0)
    {
      wait_for_gpu_device_idle(g_GpuDevice);
      reload_shader_from_memory(g_ShaderManager->shaders + i, bin, bin_size);
      dbgln("Hot reloaded shader %s", entry_point_name);
      return;
    }
  }
  ASSERT_MSG(false, "Failed to reload engine shader %s, not found!", entry_point_name);
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
  GBuffer gbuffer = init_gbuffer(&builder);
  FrameResources frame_resources = init_frame_init_pass(scratch_arena, &builder);

  init_scene_gpu_upload_pass(scratch_arena, &builder);

  init_gbuffer_static(scratch_arena, &builder, &gbuffer);

  DiffuseGiResources diffuse_gi = init_rt_diffuse_gi(scratch_arena, &builder);

  RgHandle<GpuTexture> hdr_buffer  = init_hdr_buffer(&builder);
  init_lighting(scratch_arena, &builder, gbuffer, diffuse_gi, &hdr_buffer);

  RgHandle<GpuTexture> taa_buffer  = init_taa_buffer(&builder);
  init_taa(scratch_arena, &builder, hdr_buffer, gbuffer, &taa_buffer);

  RgHandle<GpuTexture> dof_buffer = init_depth_of_field(scratch_arena, &builder, gbuffer.depth, taa_buffer);

  RgHandle<GpuTexture> tonemapped_buffer = init_tonemapping(scratch_arena, &builder, dof_buffer);

  init_debug_draw_pass(scratch_arena, &builder, frame_resources, &gbuffer, &tonemapped_buffer);

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
}

static void
destroy_renderer_psos()
{
  destroy_compute_pipeline(&g_Renderer.texture_copy_pso);
  destroy_graphics_pipeline(&g_Renderer.back_buffer_blit_pso);
}

void
init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  HWND window
) {
  zero_memory(&g_Renderer, sizeof(g_Renderer));

  g_DescriptorCbvSrvUavPool   = HEAP_ALLOC(DescriptorPool, g_InitHeap, 1);
  *g_DescriptorCbvSrvUavPool  = init_descriptor_pool(g_InitHeap, 2048, kDescriptorHeapTypeCbvSrvUav, kGrvTemporalCount * kBackBufferCount + kGrvCount);

  const uint32_t kGraphMemory = MiB(32);
  g_Renderer.graph_allocator  = init_linear_allocator(HEAP_ALLOC_ALIGNED(g_InitHeap, kGraphMemory, alignof(u64)), kGraphMemory);

  init_scene();

  init_renderer_dependency_graph(swap_chain, kRgDestroyAll);
  init_renderer_psos(device, swap_chain);


  g_Renderer.imgui_descriptor_heap = init_descriptor_linear_allocator(device, 1, kDescriptorHeapTypeCbvSrvUav);
  init_imgui_ctx(device, kGpuFormatRGBA16Float, window, &g_Renderer.imgui_descriptor_heap);

  new (&g_Renderer.settings) RenderSettings();
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
init_unified_geometry_buffer(const GpuDevice* device)
{
  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));

  GpuBufferDesc vertex_uber_desc = {0};
  vertex_uber_desc.size = kVertexBufferSize;

  g_UnifiedGeometryBuffer.lock              = init_spin_lock();
  g_UnifiedGeometryBuffer.vertex_buffer     = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapGpuOnly, "Vertex Buffer");
  g_UnifiedGeometryBuffer.vertex_buffer_pos = 0;

  GpuBufferDesc index_uber_desc = {0};
  index_uber_desc.size = kIndexBufferSize;

  g_UnifiedGeometryBuffer.index_buffer     = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapGpuOnly, "Index Buffer");
  g_UnifiedGeometryBuffer.index_buffer_pos = 0;

  g_UnifiedGeometryBuffer.blas_allocator   = init_gpu_linear_allocator(MiB(256), kGpuHeapGpuOnly);
}

void
destroy_unified_geometry_buffer()
{
  free_gpu_buffer(&g_UnifiedGeometryBuffer.vertex_buffer);
  free_gpu_buffer(&g_UnifiedGeometryBuffer.index_buffer);

  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));
}

u64
alloc_uber_vertex(u64 size)
{
  spin_acquire(&g_UnifiedGeometryBuffer.lock);
  defer { spin_release(&g_UnifiedGeometryBuffer.lock); };

  u64 ret      = g_UnifiedGeometryBuffer.vertex_buffer_pos;
  u64 capacity = g_UnifiedGeometryBuffer.vertex_buffer.desc.size;
  ASSERT_MSG_FATAL(ret + size <= capacity, "Failed to allocate %llu bytes from uber vertex buffer which already has %llu/%llu bytes allocated (%f %%). Consider bumping kVertexBufferSize.", size, ret, capacity, ((f64)ret / (f64)capacity * 100.0));
  g_UnifiedGeometryBuffer.vertex_buffer_pos += size;
  return ret;
}

u64
alloc_uber_index(u64 size)
{
  spin_acquire(&g_UnifiedGeometryBuffer.lock);
  defer { spin_release(&g_UnifiedGeometryBuffer.lock); };

  u64 ret      = g_UnifiedGeometryBuffer.index_buffer_pos;
  u64 capacity = g_UnifiedGeometryBuffer.index_buffer.desc.size;
  ASSERT_MSG_FATAL(ret + size <= capacity, "Failed to allocate %llu bytes from uber index buffer which already has %llu/%llu bytes allocated (%f %%). Consider bumping kIndexBufferSize.", size, ret, capacity, ((f64)ret / (f64)capacity * 100.0));
  g_UnifiedGeometryBuffer.index_buffer_pos += size;
  return ret;
}

GpuRtBlas
alloc_uber_blas(u32 vertex_start, u32 vertex_count, u32 index_start, u32 index_count, const char* name)
{
  spin_acquire(&g_UnifiedGeometryBuffer.lock);
  defer { spin_release(&g_UnifiedGeometryBuffer.lock); };

  GpuRtBlasDesc desc;
  desc.vertex_start  = vertex_start;
  desc.vertex_count  = vertex_count;
  // TODO(bshihabi): When we add vertex buffer compression we'll need to figure out how to construct the BLAS with the compressed data
  desc.vertex_format = kGpuFormatRGBA16Snorm;
  desc.vertex_stride = sizeof(Vertex);
  desc.index_start   = index_start;
  desc.index_count   = index_count;

  return alloc_gpu_rt_blas(g_UnifiedGeometryBuffer.blas_allocator, g_UnifiedGeometryBuffer.vertex_buffer, g_UnifiedGeometryBuffer.index_buffer, desc, name);
}
