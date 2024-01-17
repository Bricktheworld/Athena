#pragma once
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"

#include "Core/Foundation/Containers/hash_table.h"
#include "Core/Foundation/Containers/array.h"

#include "Core/Engine/Render/graphics.h"


enum ResourceType : u8
{
  kResourceTypeImage,
  kResourceTypeBuffer,

  kResourceTypeCount,
};

struct TransientResourceDesc
{
  union
  {
    struct TextureDesc
    {
      u32         width;
      u32         height;
      DXGI_FORMAT format;

      union
      {
        Vec4      color_clear_value;
        struct
        {
          f32     depth_clear_value;
          s8      stencil_clear_value;
        };
      };

    } texture_desc;

    struct BufferDesc
    {
      u64         size;
      u32         stride;
      GpuHeapType heap_type;
    } buffer_desc;
  };
  const char*  name              = nullptr;
  ResourceType type              = kResourceTypeImage;
  u8           temporal_lifetime = 0;
};

struct ResourceHandle
{
  u32          id                = 0;
  u32          version           = 0;
  ResourceType type              = kResourceTypeImage;
  u8           temporal_lifetime = 0;
  u16          __padding1__      = 0;


  auto operator<=>(const ResourceHandle& rhs) const = default;
};
static_assert(offsetof(ResourceHandle, id)                == 0);
static_assert(offsetof(ResourceHandle, version)           == 4);
static_assert(offsetof(ResourceHandle, type)              == 8);
static_assert(offsetof(ResourceHandle, temporal_lifetime) == 9);
static_assert(offsetof(ResourceHandle, __padding1__)      == 10);
static_assert(sizeof(ResourceHandle)                      == 12);

struct Sampler;

template <typename T>
inline constexpr ResourceType kResourceType;

#define RESOURCE_TEMPLATE_TYPE(T, enum_type) \
template <> \
inline constexpr ResourceType kResourceType<T> = enum_type

RESOURCE_TEMPLATE_TYPE(GpuImage, kResourceTypeImage);
RESOURCE_TEMPLATE_TYPE(GpuBuffer, kResourceTypeBuffer);

template <typename T>
struct RgHandle
{
  u32 id                = 0;
  u32 version           = 0;
  u64 temporal_lifetime = 0;

  operator ResourceHandle() const { return { id, version, kResourceType<T>, (u8)temporal_lifetime, 0 }; }
};

template <typename T>
struct RgReadHandle
{
  u32 id                = 0;
  u32 temporal_lifetime = 0;
};

template <typename T>
struct RgWriteHandle
{
  u32 id                = 0;
  u32 temporal_lifetime = 0;

  operator RgReadHandle<T>() const { return { id, temporal_lifetime }; };
};

typedef u32 RenderPassId;

struct ShaderResource
{
  u32 id                         = 0;
  ResourceType type              = kResourceTypeImage;
  DescriptorType descriptor_type = kDescriptorTypeUav;
  u32 stride                     = 0;
};

namespace priv
{
  template <typename T>
  struct View
  {
    typedef GpuBuffer kType;
  };
  
  template <>
  struct View<GpuImage>
  {
    typedef GpuImage kType;
  };
}

template <typename T>
struct Uav
{
  using U = priv::View<T>::kType;

  u32                  id              = 0;
  const ResourceType   type            = kResourceType<U>;
  const DescriptorType descriptor_type = kDescriptorTypeUav;
  const u32            stride          = sizeof(T);

  Uav(RgWriteHandle<U> h) : id(h.id) {}

  Uav& operator=(RgHandle<U> h)
  {
    id = h.id;
    return *this;
  }
};
static_assert(sizeof(Uav<u32>) == sizeof(ShaderResource));

template <typename T>
struct Srv
{
  using U = priv::View<T>::kType;

  u32                  id              = 0;
  const ResourceType   type            = kResourceType<U>;
  const DescriptorType descriptor_type = kDescriptorTypeSrv;
  const u32            stride          = sizeof(T);

  Srv(RgReadHandle<U> h) : id(h.id) {}

  Srv& operator=(RgHandle<U> h)
  {
    id = h.id;
    return *this;
  }
};
static_assert(sizeof(Srv<u32>) == sizeof(ShaderResource));

template <typename T>
struct Cbv
{
  u32 id = 0;
  const ResourceType type = kResourceTypeBuffer;
  const DescriptorType descriptor_type = kDescriptorTypeCbv;
  const u32 __padding__ = 0;

  Cbv(RgReadHandle<GpuBuffer> h) : id(h.id) {}

  Cbv& operator=(RgHandle<GpuBuffer> h) 
  {
    id = h.id;
    return *this;
  }
};
static_assert(sizeof(Cbv<u32>) == sizeof(ShaderResource));

enum DepthStencilClearFlags
{
  kClearDepth        = 0x1 << 0,
  kClearStencil      = 0x1 << 1,
  kClearDepthStencil = kClearDepth | kClearStencil,
};

struct RenderGraph;
struct RenderContext
{
  const RenderGraph*    m_Graph      = nullptr;
  const GraphicsDevice* m_Device     = nullptr;
  u32                   m_FrameIndex = 0;
  CmdList               m_CmdBuffer;

  void clear_depth_stencil_view(
    RgWriteHandle<GpuImage> depth_stencil,
    DepthStencilClearFlags flags,
    f32 depth,
    u8 stencil
  );

  void clear_render_target_view(RgWriteHandle<GpuImage> render_target_view, const Vec4& rgba);

  void draw_indexed_isntanced(
    u32 index_count_per_instance,
    u32 instance_count,
    u32 start_index_location,
    s32 base_vertex_location,
    u32 start_instance_location
  );

  void draw_instanced(
    u32 vertex_count_per_instance,
    u32 instance_count,
    u32 start_vertex_location,
    u32 start_instance_location
  );

  void ia_set_index_buffer(const GpuBuffer*        buffer, u32 size = 0);
  void ia_set_index_buffer(RgReadHandle<GpuBuffer> buffer, u32 size = 0);

  void ia_set_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology);
  
  void ia_set_vertex_buffer(
    u32 start_slot,
    const GpuBuffer* buffer,
    u32 size,
    u32 stride
  );

  void ia_set_vertex_buffer(
    u32 start_slot,
    RgReadHandle<GpuBuffer> buffer,
    u32 size,
    u32 stride
  );

  void om_set_render_targets(Span<RgWriteHandle<GpuImage>> rtvs, Option<RgWriteHandle<GpuImage>> dsv);

  void set_compute_root_shader_resource_view(u32 root_parameter_index, const GpuBuffer*        buffer);
  void set_compute_root_shader_resource_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer);

  void set_graphics_root_shader_resource_view(u32 root_parameter_index, const GpuBuffer*        buffer);
  void set_graphics_root_shader_resource_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer);

  void set_graphics_root_32bit_constants(u32 root_parameter_index, Span<u32> src, u32 dst_offset);

  void graphics_bind_shader_resources(Span<ShaderResource> resources);

  template <typename T>
  void graphics_bind_shader_resources(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const ShaderResource* shader_resources = (const ShaderResource*)&resource;
    u32                   num_resources    = sizeof(T) / sizeof(ShaderResource);
    graphics_bind_shader_resources(Span(shader_resources, num_resources));
  }

  void write_upload_buffer(RgHandle<GpuBuffer> dst, const void* src, u64 size);
};

typedef void (RenderHandler)(RenderContext* render_context, const void* data);

struct RgPassBuilder
{
  RenderPassId     pass_id  = 0;
  CmdQueueType     queue    = kCmdQueueTypeGraphics;
  const char*      name     = "Unnamed";
  RenderHandler*   handler  = 0;
  void*            data     = nullptr;

  struct ResourceAccessData
  {
    ResourceHandle handle   = {0};
    u32            access   = 0;
    bool           is_write = false;
  };

  Array<ResourceAccessData> read_resources;
  Array<ResourceAccessData> write_resources;
};

struct RgBuilder
{
  Array<RgPassBuilder>                  render_passes;
  Array<ResourceHandle>                 resource_list;
  HashTable<u32, TransientResourceDesc> resource_descs;

  RgHandle<GpuImage>                    back_buffer;

  u32                                   handle_index = 0;
  u32                                   width        = 0;
  u32                                   height       = 0;
};
#define FULL_RES_WIDTH(builder) (builder->width)
#define FULL_RES_HEIGHT(builder) (builder->height)
#define HALF_RES_WIDTH(builder) (builder->width / 2)
#define HALF_RES_HEIGHT(builder) (builder->height / 2)
#define QTR_RES_WIDTH(builder) (builder->width / 4)
#define QTR_RES_HEIGHT(builder) (builder->height / 4)

#define FULL_RES(builder) FULL_RES_WIDTH(builder), FULL_RES_HEIGHT(builder)
#define HALF_RES(builder) HALF_RES_WIDTH(buidler), HALF_RES_HEIGHT(builder)
#define QTR_RES(builder) QTR_RES_WIDTH(builder), QTR_RES_HEIGHT(builder)

struct RenderPass
{
  RenderHandler* handler = nullptr;
  void*          data    = nullptr;
  const char*    name    = "Unknown";
};

enum ResourceBarrierType
{
  kResourceBarrierTransition,
  kResourceBarrierAliasing,
  kResourceBarrierUav,
};

struct RgResourceBarrier
{
  union
  {
    struct
    {
      u32                   resource_id;
      ResourceType          resource_type;
      D3D12_RESOURCE_STATES before;
      D3D12_RESOURCE_STATES after;
    } transition;

    struct
    {
      u32                   before_resource_id;
      u32                   after_resource_id;
      ResourceType          resource_type;
    } aliasing;

    struct
    {
      u32                   resource_id;
      ResourceType          resource_type;
    } uav;
  };
  ResourceBarrierType       type;
};

struct RgDependencyLevel
{
  Array<RenderPassId>      render_passes;
  Array<RgResourceBarrier> barriers;
};

struct RgDescriptorHeap
{
  DescriptorLinearAllocator cbv_srv_uav;
  DescriptorLinearAllocator rtv;
  DescriptorLinearAllocator dsv;
};

struct RgDescriptorKey
{
  u32            id             = 0;
  DescriptorType type           = kDescriptorTypeCbv;
  u8             temporal_frame = 0;
  u16            __padding1__   = 0;

  auto operator<=>(const RgDescriptorKey& rhs) const = default;
};
static_assert(offsetof(RgDescriptorKey, id)             == 0);
static_assert(offsetof(RgDescriptorKey, type)           == 4);
static_assert(offsetof(RgDescriptorKey, temporal_frame) == 5);
static_assert(offsetof(RgDescriptorKey, __padding1__)   == 6);
static_assert(sizeof  (RgDescriptorKey)                 == 8);

struct RgResourceKey
{
  u32 id             = 0;
  u32 temporal_frame = 0;

  auto operator<=>(const RgResourceKey& rhs) const = default;
};
static_assert(offsetof(RgResourceKey, id)             == 0);
static_assert(offsetof(RgResourceKey, temporal_frame) == 4);
static_assert(sizeof  (RgResourceKey)                 == 8);

struct RenderGraph
{
  Array<RenderPass>                      render_passes;
  Array<RgDependencyLevel>               dependency_levels;

  GpuLinearAllocator                     local_heap;
  GpuLinearAllocator                     upload_heaps[kFramesInFlight];
  Array<GpuLinearAllocator>              temporal_heaps;

  RgDescriptorHeap                       descriptor_heap;
  Array<RgDescriptorHeap>                temporal_descriptor_heap;

  HashTable<RgDescriptorKey, Descriptor> descriptor_map;
  HashTable<RgResourceKey,   GpuBuffer > buffer_map;
  HashTable<RgResourceKey,   GpuImage  > texture_map;

  RgHandle<GpuImage>                     back_buffer;

  Array<RgResourceBarrier>               exit_barriers;

  CmdListAllocator                       cmd_allocator;
};

RgBuilder   init_rg_builder(AllocHeap heap, u32 width, u32 height);
RenderGraph compile_render_graph(AllocHeap heap, const RgBuilder& builder, const GraphicsDevice* device);
void        execute_render_graph(RenderGraph* graph, const GraphicsDevice* device, const GpuImage* back_buffer, u32 frame_index);

RgPassBuilder* add_render_pass(
  AllocHeap heap,
  RgBuilder* builder,
  CmdQueueType queue,
  const char* name,
  void* data,
  RenderHandler* handler,
  u32 num_read_resources,
  u32 num_write_resources
);

enum ReadTextureAccessMask : u32
{
  // Specific to textures
  kReadTextureDepthStencil      = 0x1u << 0,

  // SRV
  kReadTextureSrvPixelShader    = 0x1u << 1,
  kReadTextureSrvNonPixelShader = 0x1u << 2,
  kReadTextureSrv               = (kReadTextureSrvPixelShader | kReadTextureSrvNonPixelShader),

  // Other
  kReadTextureCopySrc           = 0x1u << 3,


  // Read everything
  kReadTextureAll               = (kReadTextureDepthStencil | kReadTextureSrv),
};

// I have very intentionall chosen to make the write access not a mask like the read mask
// You can only write one portion of the resource at a time, not all at the same time.
// If you need to write it more than one way, use multiple render passes so the 
// render graph will manage the transitions for you.
enum WriteTextureAccess : u32
{
  // Specific to textures
  kWriteTextureDepthStencil,
  kWriteTextureColorTarget,

  // UAV
  kWriteTextureUav,

  // Other
  kWriteTextureCopyDst,
};

RgReadHandle<GpuImage>  rg_read_texture (RgPassBuilder* builder, RgHandle<GpuImage>  texture, ReadTextureAccessMask access);
RgWriteHandle<GpuImage> rg_write_texture(RgPassBuilder* builder, RgHandle<GpuImage>* texture, WriteTextureAccess    access);

enum ReadBufferAccessMask : u32
{
  // Specific to buffers
  kReadBufferVertex            = 0x1u << 0,
  kReadBufferIndex             = 0x1u << 1,
  kReadBufferCbv               = 0x1u << 2,
  kReadBufferIndirectArgs      = 0x1u << 3,

  // SRV
  kReadBufferSrvPixelShader    = 0x1u << 4,
  kReadBufferSrvNonPixelShader = 0x1u << 5,
  kReadBufferSrv               = (kReadBufferSrvPixelShader | kReadBufferSrvNonPixelShader),

  // Other
  kReadBufferCopySrc           = 0x1u << 6,

  kReadAll                     = (kReadBufferVertex       | 
                                  kReadBufferIndex        | 
                                  kReadBufferCbv          |
                                  kReadBufferIndirectArgs | 
                                  kReadBufferSrv          | 
                                  kReadBufferCopySrc),
};

enum WriteBufferAccess : u32
{
  // UAV
  kWriteBufferUav,
};

RgReadHandle<GpuBuffer>  rg_read_buffer (RgPassBuilder* builder, RgHandle<GpuBuffer>  buffer, ReadBufferAccessMask access);
RgWriteHandle<GpuBuffer> rg_write_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, WriteBufferAccess    access);

static constexpr u32 kRgWindowWidth  = -1;
static constexpr u32 kRgWindowHeight = -2;

RgHandle<GpuImage> rg_create_texture(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format
);

RgHandle<GpuImage> rg_create_texture_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format,
  u8 temporal_lifetime
);

RgHandle<GpuBuffer> rg_create_buffer(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride
);

RgHandle<GpuBuffer> rg_create_upload_buffer(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride
);

RgHandle<GpuBuffer> rg_create_buffer_ex(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride,
  u8 temporal_lifetime
);

#if 0
// When creating transient buffers, if a src is provided, then it will implicitly
// be allocated on the upload heap. Thus, we restrict the size of `src` to < 256 bytes.
RgHandle<GpuBuffer> create_buffer(
  RgBuilder* graph,
  const char* name,
  GpuBufferDesc desc,
  Option<const void*> src
);

template <typename T>
RgHandle<GpuBuffer> create_buffer(
  RgBuilder* graph,
  const char* name,
  const T& src
) {
  static_assert(sizeof(T) <= 256, "Upload buffers in Render Graph cannot be >256 bytes");

  GpuBufferDesc desc = {0};
  desc.size = sizeof(T);
  desc.flags = D3D12_RESOURCE_FLAG_NONE;

  return create_buffer(graph, name, desc, &src);
}
#endif

#if 0
void cmd_graphics_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources);

template <typename T>
void cmd_graphics_bind_shader_resources(RgPassBuilder* render_pass, const T& resources)
{
  static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

  const auto* shader_resources = reinterpret_cast<const ShaderResource*>(&resources);
  u32 num_resources = sizeof(T) / sizeof(ShaderResource);
  cmd_graphics_bind_shader_resources(render_pass, Span(shader_resources, num_resources));
}

void cmd_compute_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources);

template <typename T>
void cmd_compute_bind_shader_resources(RgPassBuilder* render_pass, const T& resources)
{
  static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

  const auto* shader_resources = reinterpret_cast<const ShaderResource*>(&resources);
  u32 num_resources = sizeof(T) / sizeof(ShaderResource);
  cmd_compute_bind_shader_resources(render_pass, Span(shader_resources, num_resources));
}

// NOTE(Brandon): These function exactly the same as the compute versions, they are just for name clarity.
void cmd_ray_tracing_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources);

template <typename T>
void cmd_ray_tracing_bind_shader_resources(RgPassBuilder* render_pass, const T& resources)
{
  static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

  const auto* shader_resources = reinterpret_cast<const ShaderResource*>(&resources);
  u32 num_resources = sizeof(T) / sizeof(ShaderResource);
  cmd_ray_tracing_bind_shader_resources(render_pass, Span(shader_resources, num_resources));
}

void cmd_draw_instanced(
  RgPassBuilder* render_pass,
  u32 vertex_count_per_instance,
  u32 instance_count,
  u32 start_vertex_location,
  u32 start_instance_location
);

void cmd_draw_indexed_instanced(
  RgPassBuilder* render_pass,
  u32 index_count_per_instance,
  u32 instance_count,
  u32 start_index_location,
  s32 base_vertex_location,
  u32 start_instance_location
);

void cmd_dispatch(
  RgPassBuilder* render_pass,
  u32 thread_group_count_x,
  u32 thread_group_count_y,
  u32 thread_group_count_z
);

void cmd_ia_set_primitive_topology(RgPassBuilder* render_pass, D3D12_PRIMITIVE_TOPOLOGY primitive_topology);

void cmd_rs_set_viewport(RgPassBuilder* render_pass, D3D12_VIEWPORT viewport);

void cmd_rs_set_scissor_rect(RgPassBuilder* render_pass, D3D12_RECT rect);

void cmd_om_set_blend_factor(RgPassBuilder* render_pass,  Vec4 blend_factor);

void cmd_om_set_stencil_ref(RgPassBuilder* render_pass, u32 stencil_ref);

void cmd_set_graphics_pso(RgPassBuilder* render_pass, const GraphicsPSO* pipeline);

void cmd_set_compute_pso(RgPassBuilder* render_pass, const ComputePSO* compute_pso);

void cmd_set_ray_tracing_pso(RgPassBuilder* render_pass, const RayTracingPSO* ray_tracing_pso);

void cmd_ia_set_index_buffer(
  RgPassBuilder* render_pass,
  const GpuBuffer* index_buffer,
  DXGI_FORMAT format = DXGI_FORMAT_R16_UINT
);

void cmd_om_set_render_targets(
  RgPassBuilder* render_pass, 
  Span<RgHandle<GpuImage>> render_targets,
  Option<RgHandle<GpuImage>> depth_stencil_target
);

void cmd_clear_render_target_view(
  RgPassBuilder* render_pass, 
  Handle<GpuImage>* render_target,
  Vec4 clear_color
);

void cmd_clear_depth_stencil_view(
  RgPassBuilder* render_pass, 
  Handle<GpuImage>* depth_stencil,
  D3D12_CLEAR_FLAGS clear_flags,
  f32 depth,
  u8 stencil
);

//  void cmd_clear_unordered_access_view_uint(RenderPass* render_pass,
//                                            Handle<GpuImage>* uav,
//                                            Span<u32> values);
//
//  void cmd_clear_unordered_access_view_uint(RenderPass* render_pass,
//                                            Handle<GpuBuffer>* uav,
//                                            const u32* values);

//  void cmd_clear_unordered_access_view_float(RenderPass* render_pass,
//                                             Handle<GpuImage>* uav,
//                                             Span<f32> values);
//
//  void cmd_clear_unordered_access_view_float(RenderPass* render_pass,
//                                             Handle<GpuBuffer>* uav,
//                                             const f32* values);

void cmd_dispatch_rays(
  RgPassBuilder* render_pass,
  const GpuBvh* bvh,
  const GpuBuffer* index_buffer,
  const GpuBuffer* vertex_buffer,
  ShaderTable shader_table,
  u32 x,
  u32 y,
  u32 z
);

void cmd_draw_imgui_on_top(RgPassBuilder* render_pass, const DescriptorLinearAllocator* descriptor_linear_allocator);
#endif

