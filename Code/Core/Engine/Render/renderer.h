#pragma once
#include "Core/Foundation/assets.h"

#include "Core/Engine/job_system.h"

#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Generated/shader_table.h"

constant D3D12_COMPARISON_FUNC kDepthComparison = D3D12_COMPARISON_FUNC_GREATER;

constant f32 kZNear = 0.1f;

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

ShaderManager init_shader_manager(const GpuDevice* device);
void destroy_shader_manager(ShaderManager* shader_manager);

struct RenderMeshInst
{
  GraphicsPSO gbuffer_pso;
  u32 index_buffer_offset = 0;
  u32 index_count;
  EngineShaderIndex vertex_shader   = kVS_Basic;
  EngineShaderIndex material_shader = kPS_BasicNormalGloss;
};

enum ResolutionScale
{
  kFullRes,
  kHalfRes,
  kQuarterRes,
  kEigthRes,
};

static const DXGI_FORMAT kGBufferRenderTargetFormats[] = 
{
  DXGI_FORMAT_R32_UINT,            // Material ID
  DXGI_FORMAT_R8G8B8A8_UNORM,      // RGB -> Diffuse, A -> Metallic
  DXGI_FORMAT_R16G16B16A16_FLOAT,  // RGB -> Normal,  A -> Roughness
  DXGI_FORMAT_R32G32_FLOAT,        // RG -> Velocity
};

struct RenderOptions
{
  f32 aperture = 5.6f;
  f32 focal_dist = 3.0f;
  f32 focal_range = 20.0f;
};

struct Camera
{
  Vec3 world_pos = Vec3(0, 0, -1);
  f32  pitch     = 0;
  f32  yaw       = 0;
};

struct Renderer
{
  RenderGraph graph;
  LinearAllocator graph_allocator;

  DescriptorLinearAllocator imgui_descriptor_heap;

  Array<RenderMeshInst> meshes;
  Camera prev_camera;
  Camera camera;
  Vec2   taa_jitter;
  bool   disable_taa = false;

  DirectionalLight directional_light;

  GraphicsPSO   vbuffer_pso;
  ComputePSO    debug_vbuffer_pso;

  ComputePSO    taa_pso;

  GraphicsPSO   post_processing_pipeline;
  RayTracingPSO standard_brdf_pso;
  ShaderTable   standard_brdf_st;

  GraphicsPSO   back_buffer_blit_pso;

  RayTracingPSO ddgi_probe_trace_pso;
  ShaderTable   ddgi_probe_trace_st;

  ComputePSO    ddgi_probe_blend_pso;
};

extern Renderer g_Renderer;

void init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  const ShaderManager& shader_manager,
  HWND window
);
void renderer_on_resize(const GpuDevice* device, const SwapChain* swap_chain);
void destroy_renderer();


void begin_renderer_recording();
void submit_mesh(RenderMeshInst mesh);

void execute_render(
  const GpuDevice* device,
  SwapChain* swap_chain,
  Camera* camera,
  const GpuBuffer& vertex_buffer,
  const GpuBuffer& index_buffer,
  const GpuBvh& bvh,
  const RenderOptions& render_options,
  const DirectionalLight& directional_light
);

enum SceneObjectFlags : u8
{
  kSceneObjectPendingLoad = 0x1,
  kSceneObjectLoaded      = 0x2,
  kSceneObjectMesh        = 0x4,
};

struct RenderModel
{
  Array<RenderMeshInst> mesh_insts;
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
  
  Array<SceneObject>          scene_objects;
  Array<PointLight> point_lights;
  Camera                      camera;
  DirectionalLight  directional_light;
  LinearAllocator             scene_object_allocator;
};

Scene init_scene(AllocHeap heap);

SceneObject* add_scene_object(
  Scene* scene,
  const ShaderManager& shader_manager,
  const ModelData& model,
  EngineShaderIndex vertex_shader,
  EngineShaderIndex material_shader
);
PointLight* add_point_light(Scene* scene);

void build_acceleration_structures(GpuDevice* device);
void submit_scene(const Scene& scene);


