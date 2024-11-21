#pragma once
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"

#include "Core/Foundation/Containers/hash_table.h"
#include "Core/Foundation/Containers/array.h"

#include "Core/Engine/Render/graphics.h"


enum ResourceType : u8
{
  kResourceTypeTexture,
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
      u32         array_size;
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
  ResourceType type              = kResourceTypeTexture;
  u8           temporal_lifetime = 0;
};

struct ResourceHandle
{
  u32          id                = 0;
  u32          version           = 0;
  ResourceType type              = kResourceTypeTexture;
  u8           temporal_lifetime = 0;
  u16          __padding1__      = 0;

  auto operator<=>(const ResourceHandle& rhs) const = default;
};

struct Sampler;

template <typename T>
inline constexpr ResourceType kResourceType;

#define RESOURCE_TEMPLATE_TYPE(T, enum_type) \
template <> \
inline constexpr ResourceType kResourceType<T> = enum_type

RESOURCE_TEMPLATE_TYPE(GpuTexture, kResourceTypeTexture);
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
  u16 temporal_lifetime = 0;
  s16 temporal_frame    = 0;
};

template <typename T>
struct RgWriteHandle
{
  u32 id                = 0;
  u16 temporal_lifetime = 0;
  s16 temporal_frame    = 0;

  operator RgReadHandle<T>() const { return { id, temporal_lifetime, temporal_frame }; };
};

typedef u32 RenderPassId;

struct ShaderResource
{
  u32            id                = 0;
  ResourceType   type              = kResourceTypeTexture;
  DescriptorType descriptor_type   = kDescriptorTypeUav;
  u8             temporal_lifetime = 0;
  s8             temporal_frame    = 0;
};

namespace priv
{
  template <typename T>
  struct View
  {
    typedef GpuBuffer kType;
  };
  
  template <>
  struct View<GpuTexture>
  {
    typedef GpuTexture kType;
  };
}

template <typename T>
struct Uav
{
  using U = priv::View<T>::kType;

  u32                  id                = 0;
  const ResourceType   type              = kResourceType<U>;
  const DescriptorType descriptor_type   = kDescriptorTypeUav;
  u8                   temporal_lifetime = 0;
  s8                   temporal_frame    = 0;

  Uav(RgWriteHandle<U> h) : id(h.id), temporal_lifetime(h.temporal_lifetime), temporal_frame(h.temporal_frame) {}

  Uav& operator=(RgWriteHandle<U> h)
  {
    id                = h.id;
    temporal_lifetime = h.temporal_lifetime;
    temporal_frame    = h.temporal_frame;
    return *this;
  }
};
static_assert(sizeof(Uav<u32>) == sizeof(ShaderResource));

template <typename T>
struct Srv
{
  using U = priv::View<T>::kType;

  u32                  id                = 0;
  const ResourceType   type              = kResourceType<U>;
  const DescriptorType descriptor_type   = kDescriptorTypeSrv;
  u8                   temporal_lifetime = 0;
  s8                   temporal_frame    = 0;

  Srv(RgReadHandle<U> h) : id(h.id), temporal_lifetime(h.temporal_lifetime), temporal_frame(h.temporal_frame) {}

  Srv& operator=(RgReadHandle<U> h)
  {
    id                = h.id;
    temporal_lifetime = h.temporal_lifetime;
    temporal_frame    = h.temporal_frame;
    return *this;
  }
};
static_assert(sizeof(Srv<u32>) == sizeof(ShaderResource));

template <typename T>
struct Cbv
{
  u32                  id                = 0;
  const ResourceType   type              = kResourceTypeBuffer;
  const DescriptorType descriptor_type   = kDescriptorTypeCbv;
  u8                   temporal_lifetime = 0;
  s8                   temporal_frame    = 0;

  Cbv(RgReadHandle<GpuBuffer> h) : id(h.id), temporal_lifetime(h.temporal_lifetime), temporal_frame(h.temporal_frame) {}

  Cbv& operator=(RgReadHandle<GpuBuffer> h) 
  {
    id                = h.id;
    temporal_lifetime = h.temporal_lifetime;
    temporal_frame    = h.temporal_frame;
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
  const GpuDevice* m_Device     = nullptr;
  CmdList               m_CmdBuffer;

  u32                   m_Width      = 0;
  u32                   m_Height     = 0;

  void clear_depth_stencil_view(
    RgWriteHandle<GpuTexture> depth_stencil,
    DepthStencilClearFlags flags,
    f32 depth,
    u8 stencil
  );

  void clear_render_target_view(RgWriteHandle<GpuTexture> render_target_view, const Vec4& rgba);

  void set_graphics_pso(const GraphicsPSO* pso);
  void set_compute_pso(const ComputePSO* pso);
  void set_ray_tracing_pso(const RayTracingPSO* pso);

  void draw_indexed_instanced(
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

  void dispatch(u32 x, u32 y, u32 z);
  void dispatch_rays(const ShaderTable* shader_table, u32 x, u32 y, u32 z);

  void ia_set_index_buffer(const GpuBuffer*        buffer, u32 stride, u32 size = 0);
  void ia_set_index_buffer(RgReadHandle<GpuBuffer> buffer, u32 stride, u32 size = 0);

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

  void clear_state();

  void om_set_render_targets(Span<RgWriteHandle<GpuTexture>> rtvs, Option<RgWriteHandle<GpuTexture>> dsv);
  void rs_set_scissor_rect(s32 left, s32 top, s32 right, s32 bottom);
  void rs_set_viewport(f32 left, f32 top, f32 width, f32 height);

  void set_compute_root_shader_resource_view(u32 root_parameter_index, const GpuBuffer*        buffer);
  void set_compute_root_shader_resource_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer);

  void set_graphics_root_shader_resource_view(u32 root_parameter_index, const GpuBuffer*        buffer);
  void set_graphics_root_shader_resource_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer);

  void set_compute_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer*        buffer);
  void set_compute_root_constant_buffer_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer);

  void set_graphics_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer*        buffer);
  void set_graphics_root_constant_buffer_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer);

  void set_graphics_root_32bit_constants(u32 root_parameter_index, Span<u32> src, u32 dst_offset);
  void set_compute_root_32bit_constants(u32 root_parameter_index, Span<u32> src, u32 dst_offset);

  void set_descriptor_heaps(Span<const DescriptorLinearAllocator*> heaps);

  void graphics_bind_shader_resources   (Span<ShaderResource> resources);
  void compute_bind_shader_resources    (Span<ShaderResource> resources);
  void ray_tracing_bind_shader_resources(Span<ShaderResource> resources);

  template <typename T>
  void graphics_bind_shader_resources(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const ShaderResource* shader_resources = (const ShaderResource*)&resource;
    u32                   num_resources    = sizeof(T) / sizeof(ShaderResource);
    graphics_bind_shader_resources(Span(shader_resources, num_resources));
  }

  template <typename T>
  void compute_bind_shader_resources(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const ShaderResource* shader_resources = (const ShaderResource*)&resource;
    u32                   num_resources    = sizeof(T) / sizeof(ShaderResource);
    compute_bind_shader_resources(Span(shader_resources, num_resources));
  }

  template <typename T>
  void ray_tracing_bind_shader_resources(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const ShaderResource* shader_resources = (const ShaderResource*)&resource;
    u32                   num_resources    = sizeof(T) / sizeof(ShaderResource);
    ray_tracing_bind_shader_resources(Span(shader_resources, num_resources));
  }

  // NOTE(Brandon): It is NOT a mistake that I am not using RgWriteHandle. As long as the
  // buffer was created in an upload heap then you can write to it, since this is CPU writing
  // and the graph doesn't care when you write to the buffer (since it doesn't affect how 
  // the GPU runs its passes).
  void write_cpu_upload_buffer(RgReadHandle<GpuBuffer> dst, const void* src, u64 size);
  void write_cpu_upload_buffer(const GpuBuffer*        dst, const void* src, u64 size);
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
    ResourceHandle handle         = {0};
    u32            access         = 0;
    s8             temporal_frame = 0;
    bool           is_write       = false;
  };

  Array<ResourceAccessData> read_resources;
  Array<ResourceAccessData> write_resources;
  Array<RenderPassId>       manual_dependencies;
};

struct RgBuilder
{
  Array<RgPassBuilder>                  render_passes;
  Array<ResourceHandle>                 resource_list;
  HashTable<u32, TransientResourceDesc> resource_descs;

  RgHandle<GpuTexture>                  back_buffer;

  u32                                   handle_index = 0;
  u32                                   width        = 0;
  u32                                   height       = 0;

  Option<RenderPassId>                  frame_init   = None;
};
#define FULL_RES_WIDTH(builder) ((builder)->width)
#define FULL_RES_HEIGHT(builder) ((builder)->height)
#define HALF_RES_WIDTH(builder) ((builder)->width / 2)
#define HALF_RES_HEIGHT(builder) ((builder)->height / 2)
#define QTR_RES_WIDTH(builder) ((builder)->width / 4)
#define QTR_RES_HEIGHT(builder) ((builder)->height / 4)

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
      s8                    resource_temporal_frame;
      u8                    resource_temporal_lifetime;
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

struct RgResourceKey
{
  u32 id             = 0;
  u32 temporal_frame = 0;

  auto operator<=>(const RgResourceKey& rhs) const = default;
};

struct RenderGraph
{
  Array<RenderPass>                      render_passes;
  Array<RgDependencyLevel>               dependency_levels;

  GpuLinearAllocator                     local_heap;
  GpuLinearAllocator                     upload_heaps[kFramesInFlight];
  Array<GpuLinearAllocator>              temporal_heaps;

  RgDescriptorHeap                       descriptor_heap;

  HashTable<RgDescriptorKey, Descriptor> descriptor_map;
  HashTable<RgResourceKey,   GpuBuffer > buffer_map;
  HashTable<RgResourceKey,   GpuTexture> texture_map;

  RgHandle<GpuTexture>                   back_buffer;

  Array<RgResourceBarrier>               exit_barriers;

  CmdListAllocator                       cmd_allocator;

  u32                                    frame_id = 0;
  u32                                    width    = 0;
  u32                                    height   = 0;
};


enum RenderGraphDestroyFlags : u32
{
  kRgFreePhysicalResources    = 1U << 0,
  kRgDestroyResourceHeaps     = 1U << 1,
  kRgDestroyCmdListAllocators = 1U << 2,

  // If you're using approximately the same render graph, but just the resources changed (i.e., just reinit the physical resources)
  kRgDestroyMinimal           = kRgFreePhysicalResources,

  // If you want to free the entire graph
  kRgDestroyAll               = kRgFreePhysicalResources | kRgDestroyResourceHeaps | kRgDestroyCmdListAllocators,
};

RgBuilder init_rg_builder(AllocHeap heap, u32 width, u32 height);
void      compile_render_graph(AllocHeap heap, const RgBuilder& builder, const GpuDevice* device, RenderGraph* out, RenderGraphDestroyFlags flags = kRgDestroyAll);
void      destroy_render_graph(RenderGraph* graph, RenderGraphDestroyFlags flags = kRgDestroyAll);
void      execute_render_graph(RenderGraph* graph, const GpuDevice* device, const GpuTexture* back_buffer, u32 frame_index);

RgPassBuilder* add_render_pass(
  AllocHeap heap,
  RgBuilder* builder,
  CmdQueueType queue,
  const char* name,
  void* data,
  RenderHandler* handler,
  u32 num_read_resources,
  u32 num_write_resources,
  bool is_frame_init = false
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

RgReadHandle<GpuTexture>  rg_read_texture (RgPassBuilder* builder, RgHandle<GpuTexture>  texture, ReadTextureAccessMask access, s8 temporal_frame = 0);
RgWriteHandle<GpuTexture> rg_write_texture(RgPassBuilder* builder, RgHandle<GpuTexture>* texture, WriteTextureAccess    access);

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

constant u32 kRgWindowWidth    = -1;
constant u32 kRgWindowHeight   = -2;

constant u8  kInfiniteLifetime = 0xFF;

RgHandle<GpuTexture> rg_create_texture(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format
);

RgHandle<GpuTexture> rg_create_texture_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format,
  u8 temporal_lifetime
);

RgHandle<GpuTexture> rg_create_texture_array(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u32 array_size,
  DXGI_FORMAT format
);

RgHandle<GpuTexture> rg_create_texture_array_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u32 array_size,
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
  u32 stride = 0
);

RgHandle<GpuBuffer> rg_create_buffer_ex(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride,
  u8 temporal_lifetime
);
