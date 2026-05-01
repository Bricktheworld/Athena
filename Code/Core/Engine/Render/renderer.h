#pragma once
#include "Core/Foundation/assets.h"
#include "Core/Foundation/threading.h"

#include "Core/Engine/scene.h"
#include "Core/Engine/job_system.h"

#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/frame_time.h"

#include "Core/Engine/Shaders/root_signature.hlsli"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/Include/ddgi_common.hlsli"
#include "Core/Engine/Shaders/Include/gbuffer_common.hlsli"
#include "Core/Engine/Generated/shader_table.h"

static constexpr DepthFunc kDepthComparison = kDepthFuncGreater;

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

enum RenderDebugLayer : u32
{
  kRenderDebugDefault,
  kRenderDebugDiffuse,
  kRenderDebugNormal,

  kRenderDebugDepth,
  kRenderDebugHZB0,
  kRenderDebugHZB1,
  kRenderDebugHZB2,
  kRenderDebugHZB3,

  kRenderDebugGiVariance,

  kRenderDebugLayerCount
};

static constexpr const char* kRenderDebugLayerNames[kRenderDebugLayerCount] =
{
  "Default",
  "GBuffer Albedo",
  "GBuffer Normals",

  "GBuffer Depth",
  "GBuffer HZB0",
  "GBuffer HZB1",
  "GBuffer HZB2",
  "GBuffer HZB3",

  "Gi Variance",
};
static_assert(ARRAY_LENGTH(kRenderDebugLayerNames) == kRenderDebugLayerCount, "Mismatched render debug layer names! Double check you added it correctly here (in the right order)");

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
  u32   debug_layer              = kRenderDebugDefault;

  Vec2  mouse_pos                = Vec2(0.0f, 0.0f);
  s32   forced_model_lod         = -1;
  u32   __pad2__;

  Vec3  diffuse_gi_probe_spacing = Vec3(1.0f, 1.8f, 1.0f);

  bool  disable_taa               = false;
  bool  disable_diffuse_gi        = false;
  bool  disable_hdr               = false;
  bool  disable_dof               = false;

  bool  debug_gi_probes           = false;
  bool  debug_gi_sample_probes    = false;
  bool  enabled_debug_draw        = false;
  bool  freeze_probe_rotation     = false;
  bool  disable_frustum_culling   = false;
  bool  freeze_occlusion_culling  = false;
  bool  disable_occlusion_culling = false;
  bool  disable_ray_tracing       = false;
};

inline RenderSettingsGpu to_gpu_render_settings(const RenderSettings& settings)
{
  RenderSettingsGpu ret;
  ret.focal_dist                = settings.focal_dist;
  ret.focal_range               = settings.focal_range;
  ret.dof_blur_radius           = settings.dof_blur_radius;
  ret.dof_sample_count          = settings.dof_sample_count;
  ret.aperture                  = settings.aperture;
  ret.shutter_time              = settings.shutter_time;
  ret.iso                       = settings.iso;
  ret.diffuse_gi_probe_spacing  = settings.diffuse_gi_probe_spacing;

  ret.disable_taa               = settings.disable_taa;
  ret.disable_diffuse_gi        = settings.disable_diffuse_gi;
  ret.disable_hdr               = settings.disable_hdr;
  ret.disable_dof               = settings.disable_dof;

  ret.debug_gi_probes           = settings.debug_gi_probes;
  ret.debug_gi_sample_probes    = settings.debug_gi_sample_probes;
  ret.enabled_debug_draw        = settings.enabled_debug_draw;
  ret.freeze_gi_probe_rotation  = settings.freeze_probe_rotation;
  ret.mouse_pos                 = settings.mouse_pos;
  ret.disable_frustum_culling   = settings.disable_frustum_culling;
  ret.disable_occlusion_culling = settings.disable_occlusion_culling;
  ret.freeze_occlusion_culling  = settings.freeze_occlusion_culling;
  return ret;
}

struct EnginePSOLibrary
{
  ComputePSO  compute_psos[kEngineShaderCount];
  GraphicsPSO back_buffer_blit;
  GraphicsPSO debug_draw_line_pso;
  GraphicsPSO debug_draw_sdf_pso;
  GraphicsPSO gbuffer_static;
  GraphicsPSO tonemapping;
};

struct Renderer
{
  LinearAllocator graph_allocator;

  DescriptorLinearAllocator imgui_descriptor_heap;

  Camera camera;

  RenderSettings settings;

  DirectionalLight directional_light;

  EnginePSOLibrary pso_library;
};

enum RenderLayer : u32
{
  kRenderLayerInit,

  kRenderLayerRtDiffuseGi,
  kRenderLayerGBuffer,

  kRenderLayerLighting,

  kRenderLayerPost,

  kRenderLayerDebug,
  kRenderLayerDebugDraw,

  kRenderLayerUI,

  kRenderLayerSubmit,

  kRenderLayerCount,
};

static constexpr const char* kRenderLayerNames[kRenderLayerCount] =
{
  "Init",
  "RtDiffuseGi",
  "GBuffer",
  "Lighting",
  "Post",
  "Debug",
  "DebugDraw",
  "UI",
  "Submit",
};
static_assert(ARRAY_LENGTH(kRenderLayerNames) == kRenderLayerCount, "Mismatched render layer names! Double check you added it correctly here (in the right order)");

enum RenderHandlerId : u32
{
  kRenderHandlerFrameInit,
  kRenderHandlerSceneUpload,
  kRenderHandlerBuildTlas,
  kRenderHandlerRtDiffuseGiInit,

  kRenderHandlerGBufferGenerateMultiDrawArgs,
  kRenderHandlerGBufferOpaque,
  kRenderHandlerGenerateHZB,
  kRenderHandlerRtDiffuseGiTraceRays,
  kRenderHandlerRtDiffuseGiProbeBlend,

  kRenderHandlerLighting,

  kRenderHandlerDoFGenerateCoC,
  kRenderHandlerDoFBokehBlur,
  kRenderHandlerDoFComposite,

  kRenderHandlerTemporalAA,

  kRenderHandlerTonemapping,

  kRenderHandlerDebugUi,

  kRenderHandlerDebugBuffers,
  kRenderHandlerDebugBufferBlit,
  kRenderHandlerIndirectDebugDraw,

  kRenderHandlerBackBufferBlit,

  kRenderHandlerCount
};


struct RenderEntry
{
  u32             sort_key = 0;
  RenderHandlerId handler  = kRenderHandlerFrameInit;
  void*           data     = nullptr;
};

struct RenderBatch
{
  RenderLayer  layer       = kRenderLayerInit;
  u32          entry_count = 0;
  RenderEntry* entries     = nullptr;
};

struct ViewCtx
{
  Mat4    proj;
  Mat4    view;
  Mat4    view_proj;
  Mat4    inverse_view_proj;

  u32     width;
  u32     height;

  Vec2    taa_jitter;

  Camera  camera;
  DirectionalLight directional_light;
  Frustum frustum;

  RenderBatch* render_batches     = nullptr;
  u32          render_batch_count = 0;
};

void submit_render_entry  (ViewCtx* view_ctx, RenderLayer layer, RenderHandlerId handler, u32 sort_key, void* data);
void submit_render_entries(ViewCtx* view_ctx, RenderLayer layer, RenderEntry* entries, u32 count);


void init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  HWND window
);
void renderer_on_resize(const SwapChain* swap_chain);
void destroy_renderer();



typedef void (RenderHandler)(const RenderEntry* entries, u32 entry_count);

struct RenderTarget
{
  GpuTexture    texture;
  GpuDescriptor rtv;

  // These have the same format as the texture. 
  // If you want a different format you'll need to alloc your own descriptor
  GpuDescriptor srv;
  GpuDescriptor uav;
};

struct DepthTarget
{
  GpuTexture    texture;
  GpuDescriptor dsv;

  // These have the same format as the texture
  // If you want a different format you'll need to alloc your own descriptor
  GpuDescriptor srv;

  // You can't access depth stencil buffers in unordered access because of
  // hardware limitations (compression of the depth buffer and stuff)
};

template <typename T, size_t BufferedFrameCount = kFramesInFlight>
struct TemporalResource
{
  T m_Resource[BufferedFrameCount];

  // idx: a number from (-BufferedFrameCount, 0] that represents how many frames
  // ago you want to access, for example:
  //   idx =  0 -> Current frame
  //   idx = -1 -> Previous frame
  //   idx = -2 -> Two frames ago
  // etc...
  T* get_temporal(s32 idx)
  {
    ASSERT_MSG_FATAL(idx <= 0, "get_temporal() specified idx %d but expected a number from (%d, 0]. \nThis idx represents how many frames ago you wanted to access. For example: \n  idx = 0 -> Current frame\n  idx = -1 -> Previous frame", idx, -(s32)BufferedFrameCount);
    ASSERT_MSG_FATAL(-idx < BufferedFrameCount, "get_temporal() specified idx %d but expected a number from (%d, 0]. \nThis idx represents how many frames ago you wanted to access. For example: \n  idx = 0 -> Current frame\n  idx = -1 -> Previous frame", idx, -(s32)BufferedFrameCount);

    s32 signed_frame_id = (u32)g_FrameId;
    signed_frame_id    += idx;
    s64 temporal_idx    = modulo(signed_frame_id, kFramesInFlight);
    ASSERT_MSG_FATAL(temporal_idx < BufferedFrameCount, "%lld >= %llu. This is a bug with TemporalResource::get_temporal", temporal_idx, BufferedFrameCount);
    return &m_Resource[temporal_idx];
  }

  // By default, pointer access goes to the current frame which is sensible
  T* operator->()
  {
    return get_temporal(0);
  }
};

template <typename T>
struct Texture2D
{
  GpuTexture    texture;
  GpuDescriptor srv;
  GpuDescriptor uav;

  operator Texture2DPtr<T>() const
  {
    return Texture2DPtr<T>{srv.index};
  }

  operator RWTexture2DPtr<T>() const
  {
    return RWTexture2DPtr<T>{uav.index};
  }
};

template <typename T>
struct Texture2DArray
{
  GpuTexture    texture;
  GpuDescriptor srv;
  GpuDescriptor uav;

  operator Texture2DArrayPtr<T>() const
  {
    return Texture2DArrayPtr<T>{srv.index};
  }

  operator RWTexture2DArrayPtr<T>() const
  {
    return RWTexture2DArrayPtr<T>{uav.index};
  }
};

template <typename T>
struct StructuredBuffer
{
  GpuBuffer     buffer;
  GpuDescriptor srv;
  GpuDescriptor uav;

  operator StructuredBufferPtr<T>() const
  {
    return StructuredBufferPtr<T>{srv.index};
  }

  operator RWStructuredBufferPtr<T>() const
  {
    return RWStructuredBufferPtr<T>{uav.index};
  }
};

template <typename T>
struct AppendStructuredBuffer
{
  GpuBuffer     buffer;
  GpuBuffer     counter;
  GpuDescriptor srv;
  GpuDescriptor uav;
  GpuDescriptor counter_uav;

  operator StructuredBufferPtr<T>() const
  {
    return StructuredBufferPtr<T>{srv.index};
  }

  operator RWStructuredBufferPtr<T>() const
  {
    return RWStructuredBufferPtr<T>{uav.index};
  }
};

struct RenderBuffers
{
  struct GBuffer
  {
    RenderTarget                   material_id;
    RenderTarget                   diffuse_metallic;
    RenderTarget                   normal_roughness;
    TemporalResource<RenderTarget> velocity;
    DepthTarget                    depth;
    Texture2D<f32>                 hzb;
    GpuDescriptor                  hzb_mip_uavs[kHZBMipCount];
  } gbuffer;

  // Frame / misc
  TemporalResource<GpuBuffer> viewport_buffer;
  TemporalResource<GpuBuffer> render_settings;
  GpuBuffer debug_draw_args_buffer;
  StructuredBuffer<DebugLinePoint> debug_line_vert_buffer;
  StructuredBuffer<DebugSdf> debug_sdf_buffer;

  // Scene
  StructuredBuffer<SceneObjGpu> scene_obj_buffer;
  StructuredBuffer<RtObjGpu>    rt_obj_buffer;
  AppendStructuredBuffer<D3D12RaytracingInstanceDesc> rt_tlas_instances;
  GpuRtTlas rt_tlas;
  GpuBuffer tlas_scratch;

  // GBuffer indirect
  AppendStructuredBuffer<MultiDrawIndirectIndexedArgs> indirect_args;
  StructuredBuffer<u32> scene_obj_gpu_ids;
  StructuredBuffer<u64> occlusion_results;

  // Lighting / post-process
  Texture2D<Vec4f16> hdr;
  TemporalResource<RenderTarget> taa;
  Texture2D<Vec4f16> coc_buffer;
  Texture2D<Vec4f16> blur_buffer;
  Texture2D<Vec4f16> depth_of_field;
  RenderTarget tonemapped_buffer;

  // DDGI
  TemporalResource<Texture2DArray<u32>> probe_page_table;
  StructuredBuffer<DiffuseGiProbe> probe_buffer;
  StructuredBuffer<GiRayLuminance> ray_luminance;

  // Due to issues with render target aliasing, blits must be done at different times
  // but they all end up in this buffer
  RenderTarget  debug_buffer;

  GpuRingBuffer upload_buffer;

  GpuDescriptor back_buffer_rtv;
};

struct RenderHandlerState
{
  ViewCtx        main_view;
  ViewCtx        prev_main_view;
  RenderSettings settings;
  CmdList        cmd_list;

  u32            frame_id;

  RenderBuffers  buffers = {0};
};

extern RenderHandlerState g_RenderHandlerState;

extern Renderer g_Renderer;

ViewCtx* submit_scene(const SwapChain* swap_chain, GpuTexture* back_buffer);
void     render_view_ctx(ViewCtx* view_ctx);

struct UnifiedGeometryBuffer
{
  SpinLock  lock;
  // TODO(Brandon): In the future, we don't really want to linear allocate these buffers.
  // We want uber buffers, but we want to be able to allocate and free vertices as we need.
  GpuBuffer vertex_buffer;
  GpuBuffer index_buffer;
  u64       vertex_buffer_pos = 0;
  u64       index_buffer_pos  = 0;

  GpuLinearAllocator blas_allocator;
};


extern UnifiedGeometryBuffer g_UnifiedGeometryBuffer;

void init_unified_geometry_buffer(const GpuDevice* device);
void destroy_unified_geometry_buffer();

THREAD_SAFE u64       alloc_uber_vertex(u64 size);
THREAD_SAFE u64       alloc_uber_index(u64 size);
THREAD_SAFE GpuRtBlas alloc_uber_blas(u32 vertex_start, u32 vertex_count, u32 index_start, u32 index_count, const char* name);



/////////// HELPER GPU FUNCTIONS /////////////
inline void
gpu_bind_compute_pso(CmdList* cmd, EngineShaderIndex index)
{
  gpu_bind_compute_pso(cmd, g_Renderer.pso_library.compute_psos[index]);
}

template <typename T>
inline void
gpu_bind_srt(CmdList* cmd, const T& srt)
{
  gpu_bind_root_constants(cmd, 0, (const u32*)&srt, UCEIL_DIV(sizeof(srt), sizeof(u32)));
}

void gpu_clear_render_target(CmdList* cmd, RenderTarget* rtv, const Vec4& clear_color);
void gpu_clear_depth_target (CmdList* cmd, DepthTarget* target, DepthStencilClearFlags flags, f32 depth, u8 stencil);
void gpu_bind_render_targets(CmdList* cmd, RenderTarget** rtvs, u32 rtv_count, DepthTarget* dsv = nullptr);
inline void gpu_bind_render_target(CmdList* cmd, RenderTarget* rtv, DepthTarget* dsv = nullptr)
{
  gpu_bind_render_targets(cmd, &rtv, 1, dsv);
}

void gpu_clear_buffer_u32(CmdList* cmd, RWStructuredBufferPtr<u32> dst, u32 count, u32 offset, u32 value);

FenceValue gpu_flush_cmds();
struct GpuStagingAllocation
{
  u8* cpu_dst    = nullptr;
  u64 gpu_offset = 0;
};
GpuStagingAllocation gpu_alloc_staging_bytes(CmdList* cmd, u32 size, u32 alignment = 1);

template <typename T>
struct GpuInitGrvHelper;

template <typename T>
struct GpuInitGrvHelper<ConstantBufferPtr<T>>
{
  static void init(u32 idx, const TemporalResource<GpuBuffer>& buffer)
  {
    for (u32 iframe = 0; iframe < kFramesInFlight; iframe++)
    {
      GpuDescriptor grv = alloc_table_descriptor(g_DescriptorCbvSrvUavPool, kGrvTemporalCount * iframe + idx);

      GpuBufferCbvDesc desc = {0};
      desc.buffer_offset    = 0;
      desc.size             = sizeof(T);
      init_buffer_cbv(&grv, &buffer.m_Resource[iframe], desc);
    }
  }
};

template <typename T>
struct GpuInitGrvHelper<StructuredBufferPtr<T>>
{
  static void init(u32 idx, const GpuBuffer& buffer)
  {
    GpuDescriptor grv = alloc_table_descriptor(g_DescriptorCbvSrvUavPool, kGrvTemporalCount * kBackBufferCount + idx);

    GpuBufferSrvDesc desc = {0};
    desc.first_element    = 0;
    desc.num_elements     = buffer.desc.size / sizeof(T);
    desc.stride           = sizeof(T);
    desc.format           = kGpuFormatUnknown;
    desc.is_raw           = false;
    init_buffer_srv(&grv, &buffer, desc);
  }
};

template <typename T>
struct GpuInitGrvHelper<RWStructuredBufferPtr<T>>
{
  static void init(u32 idx, const GpuBuffer& buffer)
  {
    GpuDescriptor grv = alloc_table_descriptor(g_DescriptorCbvSrvUavPool, kGrvTemporalCount * kBackBufferCount + idx);

    GpuBufferUavDesc desc = {0};
    desc.first_element    = 0;
    desc.num_elements     = buffer.desc.size / sizeof(T);
    desc.stride           = sizeof(T);
    desc.format           = kGpuFormatUnknown;
    desc.is_raw           = false;
    desc.counter_offset   = 0;
    init_buffer_uav(&grv, &buffer, desc);
  }
};

template <>
struct GpuInitGrvHelper<RaytracingAccelerationStructurePtr>
{
  static void init(u32 idx, const GpuRtTlas& tlas)
  {
    GpuDescriptor grv = alloc_table_descriptor(g_DescriptorCbvSrvUavPool, kGrvTemporalCount * kBackBufferCount + idx);
    init_bvh_srv(&grv, &tlas);
  }
};

template <typename T>
inline void
gpu_init_grv(u32 idx, const GpuBuffer& buffer)
{
  GpuInitGrvHelper<T>::init(idx, buffer);
}

template <typename T>
inline void
gpu_init_grv(u32 idx, const TemporalResource<GpuBuffer>& buffer)
{
  GpuInitGrvHelper<T>::init(idx, buffer);
}

template <typename T>
inline void
gpu_init_grv(u32 idx, const GpuRtTlas& tlas)
{
  GpuInitGrvHelper<T>::init(idx, tlas);
}



