#pragma once
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"

#include "Core/Foundation/Containers/hash_table.h"
#include "Core/Foundation/Containers/array.h"

#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/frame_time.h"

struct RenderGraph;
struct RenderContext;
struct RgBuilder;
struct RenderSettings;

typedef u32 RenderPassId;

extern RenderGraph* g_RenderGraph;
extern RenderPassId g_HandlerId;

static constexpr u32 kRgBackBufferId = 0;

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
      u16         array_size;
      GpuFormat   format;
      u8          __pad__;

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
      u32         size;
      u32         stride;
      GpuHeapLocation heap_location;
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
  u8  temporal_lifetime = 0;
  u8  __pad0__          = 0;
  u16 __pad1__          = 0;

  operator ResourceHandle() const { return { id, version, kResourceType<T>, temporal_lifetime, 0 }; }
};

typedef void (RenderHandler)(RenderContext* render_context, const RenderSettings& settings, const void* data);

struct RgPassBuilder
{
  RenderPassId     pass_id         = 0;
  CmdQueueType     queue           = kCmdQueueTypeGraphics;
  const char*      name            = "Unnamed";
  RenderHandler*   handler         = 0;
  void*            data            = nullptr;
  u32              descriptor_idx  = 0;

  // NOTE(bshihabi): I hate when I have two structs that reference each other
  // but this one seems pretty reasonable so I'm keeping it
  RgBuilder*       graph           = nullptr;

  struct ResourceAccessData
  {
    ResourceHandle handle          = {0};
    u32            access          = 0;
    s8             temporal_frame  = 0;
    DescriptorType descriptor_type = kDescriptorTypeCbv;
    bool           is_write        = false;
    u8             __pad__         = 0;
    u32            descriptor_idx  = 0;

    union
    {
      GpuBufferCbvDesc  buffer_cbv;
      GpuBufferSrvDesc  buffer_srv;
      GpuBufferUavDesc  buffer_uav;

      GpuTextureSrvDesc texture_srv;
      GpuTextureUavDesc texture_uav;
    };
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
#define HALF_RES(builder) HALF_RES_WIDTH(builder), HALF_RES_HEIGHT(builder)
#define QTR_RES(builder) QTR_RES_WIDTH(builder), QTR_RES_HEIGHT(builder)
#define VAR_RES(builder, scale) ((builder)->width / (scale)), ((builder)->height / (scale))

struct RenderPass
{
  RenderHandler* handler = nullptr;
  void*          data    = nullptr;
  const char*    name    = "Unknown";
  Array<GpuDescriptor> descriptors;
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
  DescriptorLinearAllocator rtv;
  DescriptorLinearAllocator dsv;
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
  GpuLinearAllocator                     sysram_upload_heaps[kBackBufferCount];
#if 0 // Disabling for now, support isn't good
  GpuLinearAllocator                     vram_upload_heaps  [kBackBufferCount];
#endif
  Array<GpuLinearAllocator>              temporal_heaps;

  RgDescriptorHeap                       descriptor_heap;

  // HashTable<RgDescriptorKey, Descriptor> descriptor_map;
  HashTable<RgResourceKey, GpuBuffer > buffer_map;
  HashTable<RgResourceKey, GpuTexture> texture_map;

  RgHandle<GpuTexture>                   back_buffer;
  GpuDescriptor                          back_buffer_rtv;
  GpuDescriptor                          back_buffer_dsv;

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
void      compile_render_graph(AllocHeap heap, const RgBuilder& builder, RenderGraphDestroyFlags flags = kRgDestroyAll);
void      destroy_render_graph(RenderGraphDestroyFlags flags = kRgDestroyAll);
void      execute_render_graph(const GpuTexture* back_buffer, const RenderSettings& settings);

u8 rg_get_temporal_frame(u32 frame_id, u32 temporal_lifetime, s8 offset);
template <typename T>
const GpuTexture*
rg_deref_texture(T rg_descriptor)
{
  RgResourceKey key  = {0};
  key.id             = rg_descriptor.m_ResourceId;
  key.temporal_frame = rg_get_temporal_frame(g_FrameId, rg_descriptor.m_TemporalLifetime, rg_descriptor.m_TemporalFrame);

  return hash_table_find(&g_RenderGraph->texture_map, key);
}

template <typename T>
const GpuBuffer*
rg_deref_buffer(T rg_descriptor)
{
  RgResourceKey key  = {0};
  key.id             = rg_descriptor.m_ResourceId;
  key.temporal_frame = rg_get_temporal_frame(g_FrameId, rg_descriptor.m_TemporalLifetime, rg_descriptor.m_TemporalFrame);

  return hash_table_find(&g_RenderGraph->buffer_map, key);
}

RgPassBuilder* add_render_pass(
  AllocHeap heap,
  RgBuilder* builder,
  CmdQueueType queue,
  const char* name,
  void* data,
  RenderHandler* handler,
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

static constexpr u8 kInfiniteLifetime = 0xFF;

RgHandle<GpuTexture> rg_create_texture(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  GpuFormat format
);

RgHandle<GpuTexture> rg_create_texture_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  GpuFormat format,
  u8 temporal_lifetime
);

RgHandle<GpuTexture> rg_create_texture_array(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u16 array_size,
  GpuFormat format
);

RgHandle<GpuTexture> rg_create_texture_array_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u16 array_size,
  GpuFormat format,
  u8 temporal_lifetime
);

RgHandle<GpuBuffer> rg_create_buffer(
  RgBuilder* builder,
  const char* name,
  u32 size,
  u32 stride
);

RgHandle<GpuBuffer> rg_create_upload_buffer(
  RgBuilder* builder,
  const char* name,
  GpuHeapLocation location,
  u32 size,
  u32 stride = 0
);

RgHandle<GpuBuffer> rg_create_buffer_ex(
  RgBuilder* builder,
  const char* name,
  u32 size,
  u32 stride,
  u8 temporal_lifetime
);

struct RgOpaqueDescriptor
{
  u32 pass_id           = 0;
  u32 descriptor_idx    = 0;
  u32 resource_id       = 0;

  u8  temporal_lifetime = 0;
  s8  temporal_frame    = 0;
  u16 __pad__           = 0;
};

RgOpaqueDescriptor rg_read_texture (RgPassBuilder* builder, RgHandle<GpuTexture>  texture, const GpuTextureSrvDesc& desc, s8 temporal_frame = 0);
RgOpaqueDescriptor rg_write_texture(RgPassBuilder* builder, RgHandle<GpuTexture>* texture, const GpuTextureUavDesc& desc);
RgOpaqueDescriptor rg_write_rtv    (RgPassBuilder* builder, RgHandle<GpuTexture>* texture);
RgOpaqueDescriptor rg_write_dsv    (RgPassBuilder* builder, RgHandle<GpuTexture>* texture);

RgOpaqueDescriptor rg_read_buffer  (RgPassBuilder* builder, RgHandle<GpuBuffer>   buffer,  const GpuBufferCbvDesc&  desc, s8 temporal_frame = 0);
RgOpaqueDescriptor rg_read_buffer  (RgPassBuilder* builder, RgHandle<GpuBuffer>   buffer,  const GpuBufferSrvDesc&  desc, s8 temporal_frame = 0);
RgOpaqueDescriptor rg_write_buffer (RgPassBuilder* builder, RgHandle<GpuBuffer>*  buffer,  const GpuBufferUavDesc&  desc);

RgOpaqueDescriptor rg_read_index_buffer (RgPassBuilder* builder, RgHandle<GpuBuffer> buffer);
RgOpaqueDescriptor rg_read_vertex_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer);

struct RgRtv
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgRtv() = default;
  RgRtv(RgPassBuilder* builder, RgHandle<GpuTexture>* texture)
  {
    RgOpaqueDescriptor opaque = rg_write_rtv(builder, texture);

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
    m_Pad              = 0;
  }

  const GpuDescriptor* deref() const
  {
    if (m_ResourceId == kRgBackBufferId)
    {
      return &g_RenderGraph->back_buffer_rtv;
    }

    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

struct RgDsv
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgDsv() = default;
  RgDsv(RgPassBuilder* builder, RgHandle<GpuTexture>* texture)
  {
    RgOpaqueDescriptor opaque = rg_write_dsv(builder, texture);

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  const GpuDescriptor* deref() const
  {
    if (m_ResourceId == kRgBackBufferId)
    {
      return &g_RenderGraph->back_buffer_dsv;
    }

    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

struct RgIndexBuffer
{
  u32 m_PassId           = 0;
  u32 m_Pad0             = 0;
  u32 m_ResourceId       = 0;

  u32 m_Pad1             = 0;

  RgIndexBuffer() = default;
  RgIndexBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer)
  {
    RgOpaqueDescriptor opaque = rg_read_index_buffer(builder, buffer);

    m_PassId     = opaque.pass_id;
    m_ResourceId = opaque.resource_id;
  }
};

struct RgVertexBuffer
{
  u32 m_PassId           = 0;
  u32 m_Pad0             = 0;
  u32 m_ResourceId       = 0;

  u32 m_Pad1             = 0;

  RgVertexBuffer() = default;
  RgVertexBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer)
  {
    RgOpaqueDescriptor opaque = rg_read_vertex_buffer(builder, buffer);

    m_PassId     = opaque.pass_id;
    m_ResourceId = opaque.resource_id;
  }
};

template <typename T>
struct RgBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgBuffer() = default;
  RgBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, s8 temporal_frame = 0, Option<GpuBufferSrvDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      ASSERT(!unwrap(desc).is_raw);
      opaque = rg_read_buffer(builder, buffer, unwrap(desc));
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer.id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferSrvDesc desc = {0};
      desc.first_element    = 0;
      desc.num_elements     = resource_desc->buffer_desc.size / sizeof(T);
      desc.stride           = 0;
      desc.format           = gpu_format_from_type<T>();
      desc.is_raw           = false;

      opaque = rg_read_buffer(builder, buffer, desc);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator BufferPtr<T>() const
  {
    return { deref()->index };
  };

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

struct RgByteAddressBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgByteAddressBuffer() = default;
  RgByteAddressBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, s8 temporal_frame = 0, Option<GpuBufferSrvDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      ASSERT(unwrap(desc).is_raw);
      opaque = rg_read_buffer(builder, buffer, unwrap(desc), temporal_frame);
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer.id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferSrvDesc srv = {0};
      srv.first_element    = 0;
      srv.num_elements     = (u32)(resource_desc->buffer_desc.size / 4);
      srv.stride           = 0;
      srv.format           = kGpuFormatR32Typeless;
      srv.is_raw           = true;

      opaque = rg_read_buffer(builder, buffer, srv, temporal_frame);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator ByteAddressBufferPtr() const 
  {
    return { deref()->index };
  };

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgConstantBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgConstantBuffer() = default;
  RgConstantBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, s8 temporal_frame = 0, Option<GpuBufferCbvDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      opaque = rg_read_buffer(builder, buffer, unwrap(desc), temporal_frame);
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer.id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferCbvDesc cbv = {0};
      cbv.buffer_offset    = 0;
      cbv.size             = resource_desc->buffer_desc.size;

      opaque = rg_read_buffer(builder, buffer, cbv, temporal_frame);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }


  operator ConstantBufferPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgRWBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgRWBuffer() = default;
  RgRWBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, Option<GpuBufferUavDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      ASSERT(!unwrap(desc).is_raw);
      opaque = rg_write_buffer(builder, buffer, unwrap(desc));
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer->id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferUavDesc uav = {0};
      uav.first_element    = 0;
      uav.num_elements     = resource_desc->buffer_desc.size / sizeof(T);
      uav.stride           = 0;
      uav.format           = gpu_format_from_type<T>();
      uav.is_raw           = false;

      opaque = rg_write_buffer(builder, buffer, uav);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }


  operator RWBufferPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

struct RgRWByteAddressBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgRWByteAddressBuffer() = default;
  RgRWByteAddressBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, Option<GpuBufferUavDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      ASSERT(unwrap(desc).is_raw);
      opaque = rg_write_buffer(builder, buffer, unwrap(desc));
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer->id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferUavDesc uav = {0};
      uav.first_element    = 0;
      uav.num_elements     = (u32)(resource_desc->buffer_desc.size / 4);
      uav.stride           = 0;
      uav.format           = kGpuFormatR32Typeless;
      uav.is_raw           = true;

      opaque = rg_write_buffer(builder, buffer, uav);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator RWByteAddressBufferPtr() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgRWStructuredBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgRWStructuredBuffer() = default;
  RgRWStructuredBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, Option<GpuBufferUavDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      ASSERT(!unwrap(desc).is_raw);
      opaque = rg_write_buffer(builder, buffer, unwrap(desc));
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer->id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferUavDesc uav = {0};
      uav.first_element    = 0;
      uav.num_elements     = resource_desc->buffer_desc.size / sizeof(T);
      uav.stride           = sizeof(T);
      uav.format           = kGpuFormatUnknown;
      uav.is_raw           = false;

      opaque = rg_write_buffer(builder, buffer, uav);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator RWStructuredBufferPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgRWTexture2D
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgRWTexture2D() = default;
  RgRWTexture2D(RgPassBuilder* builder, RgHandle<GpuTexture>* texture, Option<GpuTextureUavDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      opaque = rg_write_texture(builder, texture, unwrap(desc));
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, texture->id);
      ASSERT(resource_desc->type == kResourceTypeTexture);
      GpuTextureUavDesc uav = {0};
      uav.array_size        = 1;
      uav.format            = resource_desc->texture_desc.format;

      opaque = rg_write_texture(builder, texture, uav);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator RWTexture2DPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgRWTexture2DArray
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgRWTexture2DArray() = default;
  RgRWTexture2DArray(RgPassBuilder* builder, RgHandle<GpuTexture>* texture, Option<GpuTextureUavDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      opaque = rg_write_texture(builder, texture, unwrap(desc));
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, texture->id);
      ASSERT(resource_desc->type == kResourceTypeTexture);
      GpuTextureUavDesc uav = {0};
      uav.array_size        = resource_desc->texture_desc.array_size;
      uav.format            = resource_desc->texture_desc.format;

      opaque = rg_write_texture(builder, texture, uav);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator RWTexture2DArrayPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgStructuredBuffer
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgStructuredBuffer() = default;
  RgStructuredBuffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, s8 temporal_frame = 0, Option<GpuBufferSrvDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      ASSERT(!unwrap(desc).is_raw);
      opaque = rg_read_buffer(builder, buffer, unwrap(desc), temporal_frame);
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, buffer.id);
      ASSERT(resource_desc->type == kResourceTypeBuffer);
      GpuBufferSrvDesc srv = {0};
      srv.first_element    = 0;
      srv.num_elements     = resource_desc->buffer_desc.size / sizeof(T);
      srv.stride           = sizeof(T);
      srv.format           = kGpuFormatUnknown;
      srv.is_raw           = false;

      opaque = rg_read_buffer(builder, buffer, srv, temporal_frame);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator StructuredBufferPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgTexture2D
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgTexture2D() = default;
  RgTexture2D(RgPassBuilder* builder, RgHandle<GpuTexture> texture, s8 temporal_frame = 0, Option<GpuTextureSrvDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      opaque = rg_read_texture(builder, texture, unwrap(desc), temporal_frame);
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, texture.id);
      ASSERT(resource_desc->type == kResourceTypeTexture);
      GpuTextureSrvDesc srv = {0};
      srv.mip_levels        = 1;
      srv.most_detailed_mip = 1;
      srv.array_size        = 1;
      // TODO(bshihabi): We should support casting within the family with gpu_format_from_type<T>();
      srv.format            = resource_desc->texture_desc.format; 

      opaque = rg_read_texture(builder, texture, srv, temporal_frame);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator Texture2DPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

template <typename T>
struct RgTexture2DArray
{
  u32 m_PassId           = 0;
  u32 m_DescriptorIdx    = 0;
  u32 m_ResourceId       = 0;

  u8  m_TemporalLifetime = 0;
  s8  m_TemporalFrame    = 0;
  u16 m_Pad              = 0;

  RgTexture2DArray() = default;
  RgTexture2DArray(RgPassBuilder* builder, RgHandle<GpuTexture> texture, s8 temporal_frame = 0, Option<GpuTextureSrvDesc> desc = None)
  {
    RgOpaqueDescriptor opaque;
    if (desc)
    {
      opaque = rg_read_texture(builder, texture, unwrap(desc), temporal_frame);
    }
    else
    {
      TransientResourceDesc* resource_desc = hash_table_find(&builder->graph->resource_descs, texture.id);
      ASSERT(resource_desc->type == kResourceTypeTexture);
      GpuTextureSrvDesc srv = {0};
      srv.mip_levels        = 1;
      srv.most_detailed_mip = 1;
      srv.array_size        = resource_desc->texture_desc.array_size;
      srv.format            = resource_desc->texture_desc.format;

      opaque = rg_read_texture(builder, texture, srv, temporal_frame);
    }

    m_PassId           = opaque.pass_id;
    m_DescriptorIdx    = opaque.descriptor_idx;
    m_ResourceId       = opaque.resource_id;
    m_TemporalLifetime = opaque.temporal_lifetime;
    m_TemporalFrame    = opaque.temporal_frame;
  }

  operator Texture2DArrayPtr<T>() const
  {
    return { deref()->index };
  }

  const GpuDescriptor* deref() const
  {
    const RenderPass*    pass = &g_RenderGraph->render_passes[m_PassId];
    const GpuDescriptor* base = &pass->descriptors[m_DescriptorIdx];
    const GpuDescriptor* ptr  = base + rg_get_temporal_frame(g_FrameId, m_TemporalLifetime, m_TemporalFrame);

    return ptr;
  }
};

struct RgCpuUploadBuffer
{
  u32 m_ResourceId       = 0;
  u8  m_TemporalLifetime = 0;
  u8  m_Pad0             = 0;
  u16 m_Pad1             = 0;

  RgCpuUploadBuffer() = default;
  RgCpuUploadBuffer(RgHandle<GpuBuffer> buffer)
  {
    m_ResourceId       = buffer.id;
    m_TemporalLifetime = buffer.temporal_lifetime;
  }

  template <typename T>
  RgCpuUploadBuffer(RgStructuredBuffer<T> buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }

  template <typename T>
  RgCpuUploadBuffer(RgRWStructuredBuffer<T> buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }

  template <typename T>
  RgCpuUploadBuffer(RgBuffer<T> buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }

  template <typename T>
  RgCpuUploadBuffer(RgRWBuffer<T> buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }

  template <typename T>
  RgCpuUploadBuffer(RgConstantBuffer<T> buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }

  RgCpuUploadBuffer(RgByteAddressBuffer buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }

  RgCpuUploadBuffer(RgRWByteAddressBuffer buffer)
  {
    m_ResourceId       = buffer.m_ResourceId;
    m_TemporalLifetime = buffer.m_TemporalLifetime;
  }
};

enum DepthStencilClearFlags
{
  kClearDepth        = 0x1 << 0,
  kClearStencil      = 0x1 << 1,
  kClearDepthStencil = kClearDepth | kClearStencil,
};

struct RenderGraph;
struct RenderContext
{
  CmdList m_CmdBuffer;

  u32     m_Width      = 0;
  u32     m_Height     = 0;

  void clear_depth_stencil_view(
    RgDsv depth_stencil,
    DepthStencilClearFlags flags,
    f32 depth,
    u8 stencil
  );

  void clear_render_target_view(RgRtv render_target_view, const Vec4& rgba);

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

  void ia_set_index_buffer(const GpuBuffer* buffer, u32 stride, u32 size = 0);
  void ia_set_index_buffer(RgIndexBuffer    buffer, u32 stride, u32 size = 0);

  void ia_set_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology);
  
  void ia_set_vertex_buffer(
    u32 start_slot,
    const GpuBuffer* buffer,
    u32 size,
    u32 stride
  );

  void ia_set_vertex_buffer(
    u32 start_slot,
    RgVertexBuffer buffer,
    u32 size,
    u32 stride
  );

  void clear_state();

  void om_set_render_targets(Span<RgRtv> rtvs, Option<RgDsv> dsv);
  void rs_set_scissor_rect(s32 left, s32 top, s32 right, s32 bottom);
  void rs_set_viewport(f32 left, f32 top, f32 width, f32 height);

  template <typename T>
  void set_compute_root_shader_resource_view(u32 root_parameter_index, RgStructuredBuffer<T> buffer)
  {
    set_compute_root_shader_resource_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  template <typename T>
  void set_compute_root_shader_resource_view(u32 root_parameter_index, RgBuffer<T> buffer)
  {
    set_compute_root_shader_resource_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  void set_compute_root_shader_resource_view(u32 root_parameter_index, RgByteAddressBuffer buffer)
  {
    set_compute_root_shader_resource_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  void set_compute_root_shader_resource_view(u32 root_parameter_index,  const GpuBuffer* buffer);

  template <typename T>
  void set_graphics_root_shader_resource_view(u32 root_parameter_index, RgStructuredBuffer<T> buffer)
  {
    set_graphics_root_shader_resource_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  template <typename T>
  void set_graphics_root_shader_resource_view(u32 root_parameter_index, RgBuffer<T> buffer)
  {
    set_graphics_root_shader_resource_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  void set_graphics_root_shader_resource_view(u32 root_parameter_index, RgByteAddressBuffer buffer)
  {
    set_graphics_root_shader_resource_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  void set_graphics_root_shader_resource_view(u32 root_parameter_index, const GpuBuffer* buffer);

  template <typename T>
  void set_compute_root_constant_buffer_view(u32 root_parameter_index, RgConstantBuffer<T> buffer)
  {
    set_compute_root_constant_buffer_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  void set_compute_root_constant_buffer_view(u32 root_parameter_index,  const GpuBuffer* buffer);
  template <typename T>
  void set_graphics_root_constant_buffer_view(u32 root_parameter_index, RgConstantBuffer<T> buffer)
  {
    set_graphics_root_constant_buffer_view(root_parameter_index, rg_deref_buffer(buffer));
  }
  void set_graphics_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer* buffer);

  void set_graphics_root_32bit_constants(u32 root_parameter_index, const u32* src, u32 count, u32 dst_offset);
  void set_compute_root_32bit_constants (u32 root_parameter_index, const u32* src, u32 count, u32 dst_offset);

  void set_descriptor_heaps(Span<const DescriptorLinearAllocator*> heaps);

  template <typename T>
  void graphics_bind_srt(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(u32) == 0, "Invalid resource struct!");
    set_graphics_root_32bit_constants(0, (u32*)&resource, sizeof(T) / sizeof(u32), 0);
  }

  template <typename T>
  void compute_bind_srt(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(u32) == 0, "Invalid resource struct!");
    set_compute_root_32bit_constants(0, (u32*)&resource, sizeof(T) / sizeof(u32), 0);
  }

  template <typename T>
  void ray_tracing_bind_srt(const T& resource)
  {
    static_assert(sizeof(T) % sizeof(u32) == 0, "Invalid resource struct!");
    set_compute_root_32bit_constants(0, (u32*)&resource, sizeof(T) / sizeof(u32), 0);
  }

  void write_cpu_upload_buffer(RgCpuUploadBuffer dst, const void* src, u64 size);
  void write_cpu_upload_buffer(const GpuBuffer* dst,  const void* src, u64 size);
};
