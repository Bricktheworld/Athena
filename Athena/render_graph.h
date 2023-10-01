#pragma once
#include "graphics.h"
#include "memory/memory.h"
#include "hash_table.h"
#include "array.h"
#include "math/math.h"

namespace gfx::render
{
  enum ResourceLifetime : u8 
  {
    kResourceLifetimeImported,
    kResourceLifetimeTransient,

    kResourceLifetimeCount,
  };
  
  enum ResourceType : u8
  {
    kResourceTypeImage,
    kResourceTypeBuffer,
    kResourceTypeShader,
    kResourceTypeGraphicsPSO,
    kResourceTypeSampler,

    kResourceTypeCount,
  };

  struct PhysicalResource
  {
    union
    {
      const GpuImage* image;
      const GpuBuffer* buffer;
      const GpuShader* shader;
      const GraphicsPSO* graphics_pso;
      const GpuBvh* bvh;
    };
    ResourceType type = kResourceTypeImage;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool needs_initialization = false;
  };

  struct TransientResourceDesc
  {
    union
    {
      GpuImageDesc image_desc;
      struct
      {
        GpuBufferDesc gpu_info;
        bool has_upload_data = false;
        u8 upload_data[256];
      } buffer_desc;
    };
    const char* name = nullptr;
    ResourceType type = kResourceTypeImage;
  };

  struct ResourceHandle
  {
    u32 id = 0;
    ResourceType type = kResourceTypeImage;
    ResourceLifetime lifetime = kResourceLifetimeImported;
    u16 __padding__ = 0;

    auto operator<=>(const ResourceHandle& rhs) const = default;
  };
  static_assert(offsetof(ResourceHandle, id) == 0);
  static_assert(offsetof(ResourceHandle, type) == 4);
  static_assert(offsetof(ResourceHandle, lifetime) == 5);
  static_assert(offsetof(ResourceHandle, __padding__) == 6);
  static_assert(sizeof(ResourceHandle) == 8);

  struct Sampler;

  template <typename T>
  inline constexpr ResourceType kResourceType;

#define RESOURCE_TEMPLATE_TYPE(T, enum_type) \
  template <> \
  inline constexpr ResourceType kResourceType<T> = enum_type

  RESOURCE_TEMPLATE_TYPE(GpuImage, kResourceTypeImage);
  RESOURCE_TEMPLATE_TYPE(GpuBuffer, kResourceTypeBuffer);
  RESOURCE_TEMPLATE_TYPE(Sampler, kResourceTypeSampler);
  RESOURCE_TEMPLATE_TYPE(GraphicsPSO, kResourceTypeGraphicsPSO);

  template <typename T>
  struct Handle
  {
    u32 id = 0;
    ResourceLifetime lifetime = kResourceLifetimeImported;

    operator ResourceHandle() const { return { id, kResourceType<T>, lifetime }; }
  };

  struct TransientResourceCache
  {
    CmdListAllocator graphics_cmd_allocator;
    CmdListAllocator compute_cmd_allocator;
    CmdListAllocator copy_cmd_allocator;

    GpuLinearAllocator local_heap;
    GpuLinearAllocator upload_heaps[kFramesInFlight];
    DescriptorLinearAllocator cbv_srv_uav_allocators[kFramesInFlight];
    DescriptorLinearAllocator rtv_allocators[kFramesInFlight];
    DescriptorLinearAllocator dsv_allocators[kFramesInFlight];
    DescriptorLinearAllocator sampler_allocators[kFramesInFlight];
    Array<ID3D12Resource*> last_frame_resources[kFramesInFlight];
    HashTable<u64, GraphicsPSO> pso_cache;

    GpuBuffer debug_vertex_buffer;
    GpuBuffer debug_indirect_draw_args;
//    DescriptorHeap sampler_heaps[kFramesInFlight];
  };

  typedef u32 RenderPassId;

  enum struct RenderGraphCmdType : u8
  {
    kGraphicsBindShaderResources,
    kComputeBindShaderResources,
    kDrawInstanced,
    kDrawIndexedInstanced,
    kDispatch,
//    COPY_BUFFER_REGION,
//    COPY_TEXTURE_REGION,
//    COPY_RESOURCE,
//    COPY_TILES,
//    RESOLVE_SUBRESOURCE,
    kIASetPrimitiveTopology,
    kRSSetViewport,
    kRSSetScissorRects,
    kOMSetBlendFactor,
    kOMSetStencilRef,
    kSetGraphicsPSO,
    kSetComputePSO,
    kSetRayTracingPSO,
//    RESOURCE_BARRIER,
//    EXECUTE_BUNDLE,
//    SET_DESCRIPTOR_HEAPS,
//    SET_COMPUTE_ROOT_SIGNATURE,
//    SET_GRAPHICS_ROOT_SIGNATURE,
//    SET_COMPUTE_ROOT_DESCRIPTOR_TABLE,
//    SET_GRAPHICS_ROOT_DESCRIPTOR_TABLE,
//    SET_COMPUTE_ROOT_32_BIT_CONSTANT,
//    SET_GRAPHICS_ROOT_32_BIT_CONSTANT,
//    SET_COMPUTE_ROOT_32_BIT_CONSTANTS,
//    SET_GRAPHICS_ROOT_32_BIT_CONSTANTS,
//    SET_COMPUTE_ROOT_CONSTANT_BUFFER_VIEW,
//    SET_GRAPHICS_ROOT_CONSTANT_BUFFER_VIEW,
//    SET_COMPUTE_ROOT_SHADER_RESOURCE_VIEW,
//    SET_GRAPHICS_ROOT_SHADER_RESOURCE_VIEW,
//    SET_COMPUTE_ROOT_UNORDERED_ACCESS_VIEW,
//    SET_GRAPHICS_ROOT_UNORDERED_ACCESS_VIEW,
    kIASetIndexBuffer,
//    IA_SET_VERTEX_BUFFERS,
//    SO_SET_TARGETS,
    kOMSetRenderTargets,
    kClearDepthStencilView,
    kClearRenderTargetView,

    kClearUnorderedAccessViewUint,
    kClearUnorderedAccessViewFloat,
//    CLEAR_UNORDERED_ACCESS_VIEW_UINT,
//    CLEAR_UNORDERED_ACCESS_VIEW_FLOAT,
//    DISCARD_RESOURCE,
//    BEGIN_QUERY,
//    END_QUERY,
//    RESOLVE_QUERY_DATA,
//    SET_PREDICATION,
//    SET_MARKER,
//    BEGIN_EVENT,
//    END_EVENT,
//    EXECUTE_INDIRECT,
    kDispatchRays,

    kDrawImGuiOnTop,
  };

  struct ShaderResource
  {
    u32 id = 0;
    ResourceType type = kResourceTypeImage;
    ResourceLifetime lifetime = kResourceLifetimeImported;
    DescriptorType descriptor_type = kDescriptorTypeUav;
    u32 stride = 0;

    operator ResourceHandle() const { return {id, type, lifetime}; }
  };

  struct Sampler
  {
    u32 id = 0;
    const ResourceType type = kResourceTypeSampler;
    ResourceLifetime lifetime = kResourceLifetimeImported;
    const DescriptorType descriptor_type = kDescriptorTypeSampler;
    const u32 __padding__ = 0;

    Sampler(Handle<Sampler> h) : id(h.id), lifetime(h.lifetime) {}
    
    Sampler& operator=(Handle<Sampler> h)
    {
      id = h.id;
      lifetime = h.lifetime;
      return *this;
    }
  };

  namespace priv
  {
    template <typename T>
    struct View
    {
      typedef gfx::GpuBuffer kType;
    };
    
    template <>
    struct View<gfx::GpuImage>
    {
      typedef gfx::GpuImage kType;
    };
  }

  template <typename T>
  struct Uav
  {
    using U = priv::View<T>::kType;

    u32 id = 0;
    const ResourceType type = kResourceType<U>;
    ResourceLifetime lifetime = kResourceLifetimeImported;
    const DescriptorType descriptor_type = kDescriptorTypeUav;
    const u32 stride = sizeof(T);

    Uav(Handle<U> h) : id(h.id), lifetime(h.lifetime) {}

    Uav& operator=(Handle<U> h)
    {
      id = h.id;
      lifetime = h.lifetime;
      return *this;
    }
  };
  static_assert(sizeof(Uav<u32>) == sizeof(ShaderResource));

  template <typename T>
  struct Srv
  {
    using U = priv::View<T>::kType;

    u32 id = 0;
    const ResourceType type = kResourceType<U>;
    ResourceLifetime lifetime = kResourceLifetimeImported;
    const DescriptorType descriptor_type = kDescriptorTypeSrv;
    const u32 stride = sizeof(T);

    Srv(Handle<U> h) : id(h.id), lifetime(h.lifetime) {}

    Srv& operator=(Handle<U> h)
    {
      id = h.id;
      lifetime = h.lifetime;
      return *this;
    }
  };
  static_assert(sizeof(Srv<u32>) == sizeof(ShaderResource));

  template <typename T>
  struct Cbv
  {
    u32 id = 0;
    const ResourceType type = kResourceTypeBuffer;
    ResourceLifetime lifetime = kResourceLifetimeImported;
    const DescriptorType descriptor_type = kDescriptorTypeCbv;
    const u32 __padding__ = 0;

    Cbv(Handle<GpuBuffer> h) : id(h.id), lifetime(h.lifetime) {}

    Cbv& operator=(Handle<GpuBuffer> h) 
    {
      id = h.id;
      lifetime = h.lifetime;
      return *this;
    }
  };
  static_assert(sizeof(Cbv<u32>) == sizeof(ShaderResource));

  struct RenderGraphCmd
  {
    union
    {
      struct GraphicsBindShaderResourcesArgs
      {
        Array<ShaderResource> resources;
      } graphics_bind_shader_resources;

      struct ComputeBindShaderResourcesArgs
      {
        Array<ShaderResource> resources;
      } compute_bind_shader_resources;

      struct DrawInstancedArgs
      {
        u32 vertex_count_per_instance = 0;
        u32 instance_count = 0;
        u32 start_vertex_location = 0;
        u32 start_instance_location = 0;
      } draw_instanced;

      struct DrawIndexedInstancedArgs
      {
        u32 index_count_per_instance = 0;
        u32 instance_count = 0;
        u32 start_index_location = 0;
        s32 base_vertex_location = 0;
        u32 start_instance_location = 0;
      } draw_indexed_instanced;

      struct DispatchArgs
      {
        u32 thread_group_count_x = 0;
        u32 thread_group_count_y = 0;
        u32 thread_group_count_z = 0;
      } dispatch;

      struct IASetPrimitiveTopologyArgs
      {
        D3D12_PRIMITIVE_TOPOLOGY primitive_topology;
      } ia_set_primitive_topology;

      struct RSSetViewportArgs
      {
        D3D12_VIEWPORT viewport;
      } rs_set_viewport;

      struct RSSetScissorRectArgs
      {
        D3D12_RECT rect;
      } rs_set_scissor_rect;

      struct OMSetBlendFactorArgs
      {
        Vec4 blend_factor;
      } om_set_blend_factor;

      struct OMSetStencilRefArgs
      {
        u32 stencil_ref = 0;
      } om_set_stencil_ref;

      struct SetGraphicsPSOArgs
      {
        const GraphicsPSO* graphics_pso = nullptr;
      } set_graphics_pso;

      struct SetComputePSOArgs
      {
        const ComputePSO* compute_pso = nullptr;
      } set_compute_pso;

      struct SetRayTracingPSOArgs
      {
        const RayTracingPSO* ray_tracing_pso = nullptr;
      } set_ray_tracing_pso;

      struct IASetIndexBufferArgs
      {
        const GpuBuffer* index_buffer;
        DXGI_FORMAT format;
      } ia_set_index_buffer;

      struct OMSetRenderTargetsArgs
      {
        Array<Handle<GpuImage>> render_targets;
        Option<Handle<GpuImage>> depth_stencil_target;
      } om_set_render_targets;

      struct ClearRenderTargetViewArgs
      {
        Handle<GpuImage> render_target;
        Vec4 clear_color;
      } clear_render_target_view;

      struct ClearDepthStencilViewArgs
      {
        Handle<GpuImage> depth_stencil;
        D3D12_CLEAR_FLAGS clear_flags;
        f32 depth = 0.0f;
        u8 stencil = 0;
      } clear_depth_stencil_view;

      struct ClearUnorderedAccessViewUintArgs
      {
        ShaderResource uav;
        Array<u32, 4> values;
      } clear_unordered_access_view_uint;

      struct ClearUnorderedAccessViewFloatArgs
      {
        ShaderResource uav;
        Array<f32, 4> values;
      } clear_unordered_access_view_float;

      struct DispatchRaysArgs
      {
        const GpuBvh* bvh;
        const GpuBuffer* index_buffer;
        const GpuBuffer* vertex_buffer;
        ShaderTable shader_table;
        u32 x = 0;
        u32 y = 0;
        u32 z = 0;
      } dispatch_rays;

      struct DrawImGuiOnTopArgs
      {
        const DescriptorLinearAllocator* descriptor_linear_allocator;
      } draw_imgui_on_top;
    };

    RenderGraphCmdType type = RenderGraphCmdType::kDrawInstanced;

    RenderGraphCmd() { zero_memory(this, sizeof(RenderGraphCmd)); }
  };

  struct RenderPass
  {
    RenderPassId pass_id = 0;
    MemoryArena allocator;

    Array<RenderGraphCmd> cmd_buffer;
    Array<ResourceHandle> read_resources;
    Array<ResourceHandle> write_resources;
    HashTable<ResourceHandle, D3D12_RESOURCE_STATES> resource_states;

    CmdQueueType queue = kCmdQueueTypeGraphics;

    Array<RenderPassId> passes_to_sync_with;
    u64 global_execution_index = 0;
    u64 level_execution_index = 0;
    u64 queue_execution_index = 0;

    Array<RenderPassId> synchronization_index;
    const char* name = "Unnamed";
  };

  struct RenderGraph
  {
    Array<RenderPass> render_passes;
    HashTable<ResourceHandle, PhysicalResource> imported_resources;
    HashTable<ResourceHandle, TransientResourceDesc> transient_resources;

    // TODO(Brandon): Only exists because we don't have an iterator for HashTable
    Array<ResourceHandle> resource_list;

    Option<ResourceHandle> back_buffer = None;

    u32 handle_index = 0;
  };

  RenderGraph init_render_graph(MEMORY_ARENA_PARAM);

  TransientResourceCache init_transient_resource_cache(MEMORY_ARENA_PARAM, const GraphicsDevice* device);
  void destroy_transient_resource_cache(TransientResourceCache* cache);

  RenderPass* add_render_pass(MEMORY_ARENA_PARAM,
                              RenderGraph* graph,
                              CmdQueueType queue,
                              const char* name);

  // Invalidates all render passes and submits them.
  void execute_render_graph(MEMORY_ARENA_PARAM,
                            const GraphicsDevice* device,
                            RenderGraph* render_graph,
                            TransientResourceCache* cache,
                            u32 frame_index);

  Handle<GpuImage>  create_image(RenderGraph* graph, const char* name, GpuImageDesc desc);
  Handle<Sampler>   create_sampler(RenderGraph* graph, const char* name);

  // When creating transient buffers, if a src is provided, then it will implicitly
  // be allocated on the upload heap. Thus, we restrict the size of `src` to < 256 bytes.
  Handle<GpuBuffer> create_buffer(RenderGraph* graph,
                                         const char* name,
                                         GpuBufferDesc desc,
                                         Option<const void*> src);

  template <typename T>
  Handle<GpuBuffer> create_buffer(RenderGraph* graph,
                                         const char* name,
                                         const T& src)
  {
    static_assert(sizeof(T) <= 256, "Upload buffers in Render Graph cannot be >256 bytes");

    GpuBufferDesc desc = {0};
    desc.size = sizeof(T);
    desc.flags = D3D12_RESOURCE_FLAG_NONE;

    return create_buffer(graph, name, desc, &src);
  }


  Handle<GpuImage>      import_back_buffer(RenderGraph* graph, const GpuImage* image);
  Handle<GpuImage>      import_image(RenderGraph* graph, const GpuImage* image);
  Handle<GpuBuffer>     import_buffer(RenderGraph* graph, const GpuBuffer* buffer);
  Handle<GpuShader>     import_shader(RenderGraph* graph, const GpuShader* shader);
//  Handle<GraphicsPSO>   import_graphics_pso(RenderGraph* graph, const GraphicsPSO* pipeline);

  void cmd_graphics_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources);

  template <typename T>
  void cmd_graphics_bind_shader_resources(RenderPass* render_pass, const T& resources)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const auto* shader_resources = reinterpret_cast<const ShaderResource*>(&resources);
    u32 num_resources = sizeof(T) / sizeof(ShaderResource);
    cmd_graphics_bind_shader_resources(render_pass, Span(shader_resources, num_resources));
  }

  void cmd_compute_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources);

  template <typename T>
  void cmd_compute_bind_shader_resources(RenderPass* render_pass, const T& resources)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const auto* shader_resources = reinterpret_cast<const ShaderResource*>(&resources);
    u32 num_resources = sizeof(T) / sizeof(ShaderResource);
    cmd_compute_bind_shader_resources(render_pass, Span(shader_resources, num_resources));
  }

  // NOTE(Brandon): These function exactly the same as the compute versions, they are just for name clarity.
  void cmd_ray_tracing_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources);

  template <typename T>
  void cmd_ray_tracing_bind_shader_resources(RenderPass* render_pass, const T& resources)
  {
    static_assert(sizeof(T) % sizeof(ShaderResource) == 0, "Invalid resource struct!");

    const auto* shader_resources = reinterpret_cast<const ShaderResource*>(&resources);
    u32 num_resources = sizeof(T) / sizeof(ShaderResource);
    cmd_ray_tracing_bind_shader_resources(render_pass, Span(shader_resources, num_resources));
  }

  void cmd_draw_instanced(RenderPass* render_pass,
                          u32 vertex_count_per_instance,
                          u32 instance_count,
                          u32 start_vertex_location,
                          u32 start_instance_location);

  void cmd_draw_indexed_instanced(RenderPass* render_pass,
                                  u32 index_count_per_instance,
                                  u32 instance_count,
                                  u32 start_index_location,
                                  s32 base_vertex_location,
                                  u32 start_instance_location);

  void cmd_dispatch(RenderPass* render_pass,
                    u32 thread_group_count_x,
                    u32 thread_group_count_y,
                    u32 thread_group_count_z);

  void cmd_ia_set_primitive_topology(RenderPass* render_pass, D3D12_PRIMITIVE_TOPOLOGY primitive_topology);

  void cmd_rs_set_viewport(RenderPass* render_pass, D3D12_VIEWPORT viewport);

  void cmd_rs_set_scissor_rect(RenderPass* render_pass, D3D12_RECT rect);

  void cmd_om_set_blend_factor(RenderPass* render_pass,  Vec4 blend_factor);

  void cmd_om_set_stencil_ref(RenderPass* render_pass, u32 stencil_ref);

  void cmd_set_graphics_pso(RenderPass* render_pass, const GraphicsPSO* pipeline);

  void cmd_set_compute_pso(RenderPass* render_pass, const ComputePSO* compute_pso);

  void cmd_set_ray_tracing_pso(RenderPass* render_pass, const RayTracingPSO* ray_tracing_pso);

  void cmd_ia_set_index_buffer(RenderPass* render_pass,
                               const GpuBuffer* index_buffer,
                               DXGI_FORMAT format = DXGI_FORMAT_R16_UINT);

  void cmd_om_set_render_targets(RenderPass* render_pass, 
                                 Span<Handle<GpuImage>> render_targets,
                                 Option<Handle<GpuImage>> depth_stencil_target);

  void cmd_clear_render_target_view(RenderPass* render_pass, 
                                    Handle<GpuImage>* render_target,
                                    Vec4 clear_color);

  void cmd_clear_depth_stencil_view(RenderPass* render_pass, 
                                    Handle<GpuImage>* depth_stencil,
                                    D3D12_CLEAR_FLAGS clear_flags,
                                    f32 depth,
                                    u8 stencil);
  
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

  void cmd_dispatch_rays(RenderPass* render_pass,
                         const GpuBvh* bvh,
                         const GpuBuffer* index_buffer,
                         const GpuBuffer* vertex_buffer,
                         ShaderTable shader_table,
                         u32 x,
                         u32 y,
                         u32 z);

  void cmd_draw_imgui_on_top(RenderPass* render_pass, const DescriptorLinearAllocator* descriptor_linear_allocator);
}

