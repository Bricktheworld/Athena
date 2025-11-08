#pragma once
#include "Core/Foundation/assets.h"

#include "Core/Engine/scene.h"
#include "Core/Engine/job_system.h"

#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Generated/shader_table.h"

static constexpr DepthFunc kDepthComparison = kDepthFuncGreater;

static constexpr f32 kZNear = 0.1f;

struct ShaderManager;
struct Scene;
struct Window;

extern ShaderManager*  g_ShaderManager;
extern DescriptorPool* g_DescriptorCbvSrvUavPool;
extern Window*         g_MainWindow;

struct Window
{
  SwapChain swap_chain;
  bool      needs_resize = false;
};

struct ShaderManager
{
  GpuShader shaders[kEngineShaderCount];
};

void init_shader_manager(const GpuDevice* device);
void destroy_shader_manager();
const GpuShader* get_engine_shader(u32 index);
void reload_engine_shader(const char* entry_point_name, const u8* bin, u64 bin_size);


enum ResolutionScale
{
  kFullRes,
  kHalfRes,
  kQuarterRes,
  kEigthRes,
};

static const GpuFormat kGBufferRenderTargetFormats[] = 
{
  kGpuFormatR32Uint,     // Material ID
  kGpuFormatRGBA8Unorm,  // RGB -> Diffuse, A -> Metallic
  kGpuFormatRGBA16Float, // RGB -> Normal,  A -> Roughness
  kGpuFormatRG32Float,   // RG -> Velocity
};

// !!WARNING!! !!WARNING!! !!WARNING!!
// MUST BE KEPT IN SYNC WITH GpuRenderSettings and to_gpu_render_settings
struct RenderSettings
{
  // !!WARNING!! !!WARNING!! !!WARNING!!
  f32   focal_dist               = 8.0f;
  f32   focal_range              = 3.0f;
  f32   dof_blur_radius          = 15.0f;
  u32   dof_sample_count         = 32;

  f32   aperture                 = 16.0f;
  f32   shutter_time             = 1.0 / 60.0f;
  f32   iso                      = 500.0f;
  u32   __pad0__;

  Vec2  mouse_pos                = Vec2(0.0f, 0.0f);
  u32   __pad1__;
  u32   __pad2__;

  Vec3  diffuse_gi_probe_spacing = Vec3(1.0f, 1.8f, 1.0f);

  bool  disable_taa              = false;
  bool  disable_diffuse_gi       = false;
  bool  disable_hdr              = false;
  bool  disable_dof              = false;

  bool  debug_gi_probes          = false;
  bool  debug_gi_sample_probes   = false;
  bool  enabled_debug_draw       = false;
  bool  freeze_probe_rotation    = false;
};

inline RenderSettingsGpu to_gpu_render_settings(const RenderSettings& settings)
{
  RenderSettingsGpu ret;
  ret.focal_dist               = settings.focal_dist;
  ret.focal_range              = settings.focal_range;
  ret.dof_blur_radius          = settings.dof_blur_radius;
  ret.dof_sample_count         = settings.dof_sample_count;
  ret.aperture                 = settings.aperture;
  ret.shutter_time             = settings.shutter_time;
  ret.iso                      = settings.iso;
  ret.diffuse_gi_probe_spacing = settings.diffuse_gi_probe_spacing;

  ret.disable_taa              = settings.disable_taa;
  ret.disable_diffuse_gi       = settings.disable_diffuse_gi;
  ret.disable_hdr              = settings.disable_hdr;
  ret.disable_dof              = settings.disable_dof;

  ret.debug_gi_probes          = settings.debug_gi_probes;
  ret.debug_gi_sample_probes   = settings.debug_gi_sample_probes;
  ret.enabled_debug_draw       = settings.enabled_debug_draw;
  ret.freeze_gi_probe_rotation = settings.freeze_probe_rotation;
  ret.mouse_pos                = settings.mouse_pos;
  return ret;
}

struct Renderer
{
  LinearAllocator graph_allocator;

  DescriptorLinearAllocator imgui_descriptor_heap;

  Camera prev_camera;
  Camera camera;
  Vec2   taa_jitter;

  RenderSettings settings;

  DirectionalLight directional_light;

  GraphicsPSO   back_buffer_blit_pso;
  ComputePSO    texture_copy_pso;
};

extern Renderer g_Renderer;

void init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  HWND window
);
void renderer_on_resize(const SwapChain* swap_chain);
void renderer_hot_reload(const GpuDevice* device, const SwapChain* swap_chain);
void destroy_renderer();


struct UnifiedGeometryBuffer
{
  // TODO(Brandon): In the future, we don't really want to linear allocate these buffers.
  // We want uber buffers, but we want to be able to allocate and free vertices as we need.
  GpuBuffer vertex_buffer;
  GpuBuffer index_buffer;
  u64       vertex_buffer_pos = 0;
  u64       index_buffer_pos  = 0;
};

u64 alloc_uber_vertex(u64 size);
u64 alloc_uber_index(u64 size);

extern UnifiedGeometryBuffer g_UnifiedGeometryBuffer;

void init_unified_geometry_buffer(const GpuDevice* device);
void destroy_unified_geometry_buffer();

void build_acceleration_structures();


