#pragma once
#include "Core/Foundation/assets.h"

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
extern Scene*          g_Scene;
extern Window*         g_MainWindow;

struct Window
{
  SwapChain swap_chain;
  bool      needs_resize = false;
};

// TODO(Brandon): This entire system will be reworked once I figure out,
// generally how I want to handle render entries in this engine. For now,
// everything will just get created at the start and there will be no streaming.

struct UploadContext
{
  GpuBuffer staging_buffer;
  u64 staging_offset = 0;
  CmdListAllocator cmd_list_allocator;
  CmdList cmd_list;
  const GpuDevice* device = nullptr;
  LinearAllocator cpu_upload_arena;
};

void init_global_upload_context(const GpuDevice* device);
void destroy_global_upload_context();

struct ShaderManager
{
  GpuShader shaders[kEngineShaderCount];
};

void init_shader_manager(const GpuDevice* device);
void destroy_shader_manager();
const GpuShader* get_engine_shader(u32 index);


struct RenderModelSubset
{
  GraphicsPSO       gbuffer_pso;
  u32               index_buffer_offset = 0;
  u32               index_count         = 0;
  EngineShaderIndex vertex_shader       = kVS_Basic;
  EngineShaderIndex material_shader     = kPS_BasicNormalGloss;

  AssetId           material            = kNullAssetId;
};

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

struct Camera
{
  Vec3 world_pos = Vec3(0, 0, -1);
  f32  pitch     = 0;
  f32  yaw       = 0;
};

struct RenderSettings
{
  f32  aperture;
  f32  focal_dist;
  f32  focal_range;

  f32  dof_blur_radius  = 0.2f;
  u32  dof_sample_count = 128;

  u32  debug_probe_ray_idx = U32_MAX;

  bool disable_taa           = false;
  bool debug_gi_probes       = false;
  bool disable_hdr           = false;
  bool disable_dof           = false;
  bool disable_debug_lines   = true;
  bool freeze_probe_rotation = false;
};

struct Renderer
{
  LinearAllocator graph_allocator;

  DescriptorLinearAllocator imgui_descriptor_heap;

  Array<RenderModelSubset> meshes;

  Camera prev_camera;
  Camera camera;
  Vec2   taa_jitter;

  RenderSettings settings;

  DirectionalLight directional_light;

  RayTracingPSO standard_brdf_pso;
  ShaderTable   standard_brdf_st;

  GraphicsPSO   back_buffer_blit_pso;
  ComputePSO    texture_copy_pso;

  RayTracingPSO ddgi_probe_trace_pso;
  ShaderTable   ddgi_probe_trace_st;

  ComputePSO    ddgi_probe_blend_pso;
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


void begin_renderer_recording();
void submit_mesh(RenderModelSubset mesh);

enum SceneObjectFlags : u8
{
  kSceneObjectPendingLoad = 0x1,
  kSceneObjectLoaded      = 0x2,
  kSceneObjectMesh        = 0x4,
};

struct RenderModel
{
  Array<RenderModelSubset> model_subsets;
};

struct SceneObject
{
  RenderModel model;
  u8 flags = 0;
};

struct UnifiedGeometryBuffer
{
  // TODO(Brandon): In the future, we don't really want to linear allocate these buffers.
  // We want uber buffers, but we want to be able to allocate and free vertices as we need.
  GpuBuffer vertex_buffer;
  GpuBuffer index_buffer;
  u32       vertex_buffer_offset = 0;
  u32       index_buffer_offset = 0;

  GpuBvh    bvh;
};

extern UnifiedGeometryBuffer g_UnifiedGeometryBuffer;

void init_unified_geometry_buffer(const GpuDevice* device);
void destroy_unified_geometry_buffer();

struct Scene
{
  GpuBuffer top_bvh;
  GpuBuffer bottom_bvh;
  
  Array<SceneObject> scene_objects;
  Array<PointLight>  point_lights;
  Camera             camera;
  DirectionalLight   directional_light;
  LinearAllocator    scene_object_allocator;
};

SceneObject* add_scene_object(
  const ModelData& model,
  EngineShaderIndex vertex_shader,
  EngineShaderIndex material_shader
);
PointLight* add_point_light(Scene* scene);

void build_acceleration_structures(GpuDevice* device);
void submit_scene();


