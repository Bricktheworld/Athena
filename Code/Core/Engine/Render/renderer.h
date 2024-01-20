#pragma once
#include "Core/Foundation/assets.h"

#include "Core/Engine/job_system.h"

#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/shader_table.h"

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
  const GraphicsDevice* device = nullptr;
  LinearAllocator cpu_upload_arena;
};

void init_global_upload_context(const GraphicsDevice* device);
void destroy_global_upload_context();

struct ShaderManager
{
  GpuShader shaders[kEngineShaderCount];
};

ShaderManager init_shader_manager(const GraphicsDevice* device);
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

struct RenderBufferDesc
{
  const char* debug_name = "Unknown Render Buffer";
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  ResolutionScale res = kFullRes;

  union
  {
    Vec4 color_clear_value;
    struct
    {
      f32 depth_clear_value;
      s8 stencil_clear_value;
    };
  };
};


#define DECLARE_RENDER_BUFFER_EX(name, dxgi_format, clear_value, res_scale) {.debug_name = name, .format = dxgi_format, .res = res_scale, .color_clear_value = clear_value}
#define DECLARE_RENDER_BUFFER(debug_name, format) DECLARE_RENDER_BUFFER_EX(debug_name, format, Vec4(0, 0, 0, 1), kFullRes)
#define DECLARE_DEPTH_BUFFER_EX(name, dxgi_format, depth_clear, stencil_clear) {.debug_name = name, .format = dxgi_format, .res = kFullRes, .depth_clear_value = depth_clear, .stencil_clear_value = stencil_clear}
#define DECLARE_DEPTH_BUFFER(debug_name, format) DECLARE_DEPTH_BUFFER_EX(debug_name, format, 0.0f, 0)

namespace RenderBuffers
{
  enum Entry
  {
    kGBufferMaterialId,
    kGBufferWorldPos,
    kGBufferDiffuseRGBMetallicA,
    kGBufferNormalRGBRoughnessA,
    kGBufferDepth,

    kHDRLighting,

    kDoFCoC,
//    kDoFDilatedCoC,

    kDoFRedNear,
    kDoFGreenNear,
    kDoFBlueNear,

    kDoFRedFar,
    kDoFGreenFar,
    kDoFBlueFar,

    kDoFBlurredNear,
    kDoFBlurredFar,

    kDoFComposite,

    kNone,
    kCount = kNone,
  };
}

enum
{
  kGBufferRTCount = RenderBuffers::kGBufferNormalRGBRoughnessA - RenderBuffers::kGBufferMaterialId + 1,
};

static const DXGI_FORMAT kGBufferRenderTargetFormats[] = 
{
  DXGI_FORMAT_R32_UINT,            // Material ID
  DXGI_FORMAT_R32G32B32A32_FLOAT,  // Position   32 bits to spare
  DXGI_FORMAT_R8G8B8A8_UNORM,      // RGB -> Diffuse, A -> Metallic
  DXGI_FORMAT_R16G16B16A16_FLOAT,  // RGB -> Normal,  A -> Roughness
};

static const RenderBufferDesc kRenderBufferDescs[] =
{
  DECLARE_RENDER_BUFFER_EX("GBuffer Material ID", kGBufferRenderTargetFormats[RenderBuffers::kGBufferMaterialId], Vec4(), kFullRes),
  DECLARE_RENDER_BUFFER_EX("GBuffer World Pos", kGBufferRenderTargetFormats[RenderBuffers::kGBufferWorldPos], Vec4(), kFullRes),
  DECLARE_RENDER_BUFFER_EX("GBuffer Diffuse RGB Metallic A", kGBufferRenderTargetFormats[RenderBuffers::kGBufferDiffuseRGBMetallicA], Vec4(), kFullRes),
  DECLARE_RENDER_BUFFER_EX("GBuffer Normal RGB Roughness A", kGBufferRenderTargetFormats[RenderBuffers::kGBufferNormalRGBRoughnessA], Vec4(), kFullRes),
  DECLARE_DEPTH_BUFFER("GBuffer Depth", DXGI_FORMAT_D32_FLOAT),

  DECLARE_RENDER_BUFFER("HDR Lighting", DXGI_FORMAT_R11G11B10_FLOAT),

  // TODO(Brandon): Holy shit that's a lot of memory
  DECLARE_RENDER_BUFFER("DoF CoC Near R Far G", DXGI_FORMAT_R16G16_FLOAT),
//  DECLARE_RENDER_BUFFER_EX("DoF CoC Dilated Near R Far G", DXGI_FORMAT_R16G16_FLOAT, Vec4(), kQuarterRes),

  DECLARE_RENDER_BUFFER_EX("DoF Red Near", DXGI_FORMAT_R16G16B16A16_FLOAT, Vec4(), kQuarterRes),
  DECLARE_RENDER_BUFFER_EX("DoF Green Near", DXGI_FORMAT_R16G16B16A16_FLOAT, Vec4(), kQuarterRes),
  DECLARE_RENDER_BUFFER_EX("DoF Blue Near", DXGI_FORMAT_R16G16B16A16_FLOAT, Vec4(), kQuarterRes),

  DECLARE_RENDER_BUFFER_EX("DoF Red Far", DXGI_FORMAT_R16G16B16A16_FLOAT, Vec4(), kQuarterRes),
  DECLARE_RENDER_BUFFER_EX("DoF Green Far", DXGI_FORMAT_R16G16B16A16_FLOAT, Vec4(), kQuarterRes),
  DECLARE_RENDER_BUFFER_EX("DoF Blue Far", DXGI_FORMAT_R16G16B16A16_FLOAT, Vec4(), kQuarterRes),

  DECLARE_RENDER_BUFFER_EX("DoF Blurred Near", DXGI_FORMAT_R11G11B10_FLOAT, Vec4(), kQuarterRes),
  DECLARE_RENDER_BUFFER_EX("DoF Blurred Far", DXGI_FORMAT_R11G11B10_FLOAT, Vec4(), kQuarterRes),

  DECLARE_RENDER_BUFFER("DoF Composite", DXGI_FORMAT_R11G11B10_FLOAT),
};
static_assert(ARRAY_LENGTH(kRenderBufferDescs) == RenderBuffers::kCount);

#define GET_RENDER_BUFFER_NAME(entry) kRenderBufferDescs[entry].debug_name

struct RenderOptions
{
  f32 aperture = 5.6f;
  f32 focal_dist = 3.0f;
  f32 focal_range = 20.0f;
  RenderBuffers::Entry debug_view = RenderBuffers::kDoFBlurredNear;
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

  DescriptorLinearAllocator imgui_descriptor_heap;

  Array<RenderMeshInst> meshes;
  Camera camera;
  interlop::DirectionalLight directional_light;

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
  const GraphicsDevice* device,
  const SwapChain* swap_chain,
  const ShaderManager& shader_manager,
  HWND window
);
void destroy_renderer();


void begin_renderer_recording();
void submit_mesh(RenderMeshInst mesh);

void execute_render(
  const GraphicsDevice* device,
  SwapChain* swap_chain,
  Camera* camera,
  const GpuBuffer& vertex_buffer,
  const GpuBuffer& index_buffer,
  const GpuBvh& bvh,
  const RenderOptions& render_options,
  const interlop::DirectionalLight& directional_light
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

void init_unified_geometry_buffer(const GraphicsDevice* device);
void destroy_unified_geometry_buffer();

struct Scene
{
  GpuBuffer top_bvh;
  GpuBuffer bottom_bvh;
  
  Array<SceneObject>          scene_objects;
  Array<interlop::PointLight> point_lights;
  Camera                      camera;
  interlop::DirectionalLight  directional_light;
  LinearAllocator             scene_object_allocator;
};

Scene init_scene(AllocHeap heap, const GraphicsDevice* device);

SceneObject* add_scene_object(
  Scene* scene,
  const ShaderManager& shader_manager,
  const ModelData& model,
  EngineShaderIndex vertex_shader,
  EngineShaderIndex material_shader
);
interlop::PointLight* add_point_light(Scene* scene);

void build_acceleration_structures(GraphicsDevice* device);
void submit_scene(const Scene& scene);


