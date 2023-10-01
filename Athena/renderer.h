#pragma once
#include "job_system.h"
#include "render_graph.h"
#include "shaders/interlop.hlsli"

constant D3D12_COMPARISON_FUNC kDepthComparison = D3D12_COMPARISON_FUNC_GREATER;

constant f32 kZNear = 0.1f;

// TODO(Brandon): This entire system will be reworked once I figure out,
// generally how I want to handle render entries in this engine. For now,
// everything will just get created at the start and there will be no streaming.

struct UploadContext
{
  gfx::GpuBuffer staging_buffer;
  u64 staging_offset = 0;
  gfx::CmdListAllocator cmd_list_allocator;
  gfx::CmdList cmd_list;
  const gfx::GraphicsDevice* device = nullptr;
  MemoryArena cpu_upload_arena;
};

void init_global_upload_context(MEMORY_ARENA_PARAM, const gfx::GraphicsDevice* device);
void destroy_global_upload_context();

enum ShaderIndex : u8
{
  kVsBasic,
  kVsFullscreen,

  kPsBasicNormalGloss,
  kPsFullscreen,
  kPsPostProcessing,

  kCsStandardBrdf,

  kCsDofCoC,
  kCsDofCoCDilate,
  kCsDofBlurHoriz,
  kCsDofBlurVert,
  kCsDofComposite,


  kCsDebugGBuffer,
  kCsDebugCoC,

  kRtBasic,
  kRtStandardBrdf,
  kRtProbeTrace,

  kCsProbeBlending,
  kCsProbeDistanceBlending,
  kCsDebugProbe,

  kShaderCount,
};

static const wchar_t* kShaderPaths[] =
{
  L"vertex/basic_vs.hlsl.bin",
  L"vertex/fullscreen_vs.hlsl.bin",

  L"pixel/basic_normal_gloss_ps.hlsl.bin",
  L"pixel/fullscreen_ps.hlsl.bin",
  L"pixel/post_processing_ps.hlsl.bin",

  L"compute/standard_brdf_cs.hlsl.bin",

  L"compute/dof_coc_cs.hlsl.bin",
  L"compute/dof_coc_dilate_cs.hlsl.bin",
  L"compute/dof_blur_horiz_cs.hlsl.bin",
  L"compute/dof_blur_vert_cs.hlsl.bin",
  L"compute/dof_composite_cs.hlsl.bin",

  L"compute/debug_gbuffer_cs.hlsl.bin",
  L"compute/debug_coc_cs.hlsl.bin",

  L"ray_tracing/basic_rt.hlsl.bin",
  L"ray_tracing/standard_brdf_rt.hlsl.bin",
  L"ray_tracing/probe_trace_rt.hlsl.bin",
  L"compute/probe_blending_cs.hlsl.bin",
  L"compute/probe_distance_blending_cs.hlsl.bin",
  L"compute/debug_probe_cs.hlsl.bin",
};
static_assert(ARRAY_LENGTH(kShaderPaths) == kShaderCount);

struct ShaderManager
{
  gfx::GpuShader shaders[kShaderCount];
};

ShaderManager init_shader_manager(const gfx::GraphicsDevice* device);
void destroy_shader_manager(ShaderManager* shader_manager);

struct Mesh
{
  gfx::GraphicsPSO gbuffer_pso;
  u32 index_buffer_offset = 0;
  u32 index_count = 0;
  ShaderIndex vertex_shader = kVsBasic;
  ShaderIndex material_shader = kPsBasicNormalGloss;
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

struct Renderer
{
  gfx::render::TransientResourceCache transient_resource_cache;
  gfx::GraphicsPSO fullscreen_pipeline;
  gfx::GraphicsPSO post_processing_pipeline;

  gfx::ComputePSO standard_brdf_pipeline;

  gfx::ComputePSO dof_coc_pipeline;
  gfx::ComputePSO dof_coc_dilate_pipeline;
  gfx::ComputePSO dof_blur_horiz_pipeline;
  gfx::ComputePSO dof_blur_vert_pipeline;
  gfx::ComputePSO dof_composite_pipeline;

  gfx::ComputePSO debug_gbuffer_pipeline;
  gfx::RayTracingPSO basic_rt_pipeline;
  gfx::ShaderTable   basic_rt_shader_table;

  gfx::RayTracingPSO standard_brdf_rt_pipeline;
  gfx::ShaderTable   standard_brdf_rt_shader_table;

  gfx::RayTracingPSO probe_trace_rt_pipeline;
  gfx::ShaderTable   probe_trace_rt_shader_table;

  gfx::ComputePSO probe_blending_cs_pipeline;
  gfx::ComputePSO probe_distance_blending_cs_pipeline;
  gfx::ComputePSO debug_probe_cs_pipeline;

  gfx::DescriptorLinearAllocator imgui_descriptor_heap;

	interlop::DDGIVolDesc ddgi_vol_desc;
  gfx::GpuImage probe_ray_data;
  gfx::GpuImage probe_irradiance;
  gfx::GpuImage probe_distance;
  gfx::GpuImage probe_offset;

  Array<Mesh> meshes;
};

Renderer init_renderer(MEMORY_ARENA_PARAM,
                       const gfx::GraphicsDevice* device,
                       const gfx::SwapChain* swap_chain,
                       const ShaderManager& shader_manager,
                       HWND window);
void destroy_renderer(Renderer* renderer);


void begin_renderer_recording(MEMORY_ARENA_PARAM, Renderer* renderer);
void submit_mesh(Renderer* renderer, Mesh mesh);

struct Camera
{
  Vec3 world_pos = Vec3(0, 0, -1);
  f32 pitch = 0;
  f32 yaw   = 0;
};
void execute_render(MEMORY_ARENA_PARAM,
                    Renderer* renderer,
                    const gfx::GraphicsDevice* device,
                    gfx::SwapChain* swap_chain,
                    Camera* camera,
                    const gfx::GpuBuffer& vertex_buffer,
                    const gfx::GpuBuffer& index_buffer,
                    const gfx::GpuBvh& bvh,
                    const RenderOptions& render_options,
                    const interlop::DirectionalLight& directional_light);

enum SceneObjectFlags : u8
{
  kSceneObjectPendingLoad = 0x1,
  kSceneObjectLoaded      = 0x2,
  kSceneObjectMesh        = 0x4,
};


struct SceneObject
{
  Array<Mesh> meshes;
  u8 flags = 0;
};

struct Scene
{
  // TODO(Brandon): In the future, we don't really want to linear allocate these buffers.
  // We want uber buffers, but we want to be able to allocate and free vertices as we need.
  gfx::GpuBuffer vertex_uber_buffer;
  u32 vertex_uber_buffer_offset = 0;
  gfx::GpuBuffer index_uber_buffer;
  u32 index_uber_buffer_offset = 0;

  gfx::GpuBuffer top_bvh;
  gfx::GpuBuffer bottom_bvh;
  gfx::GpuBvh bvh;
  
  Array<SceneObject>          scene_objects;
  Array<interlop::PointLight> point_lights;
  Camera                      camera;
  interlop::DirectionalLight  directional_light;
  MemoryArena                 scene_object_heap;
};

Scene init_scene(MEMORY_ARENA_PARAM, const gfx::GraphicsDevice* device);

SceneObject* add_scene_object(Scene* scene,
                              const ShaderManager& shader_manager,
                              const char* mesh,
                              ShaderIndex vertex_shader,
                              ShaderIndex material_shader);
interlop::PointLight* add_point_light(Scene* scene);

void build_acceleration_structures(gfx::GraphicsDevice* device, Scene* scene);
void submit_scene(const Scene& scene, Renderer* renderer);


