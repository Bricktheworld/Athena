#include "render_graph.h"
#include "context.h"
#include <windows.h>
#include "vendor/d3dx12.h"
#include "vendor/imgui/imgui_impl_dx12.h"
#include "pix3.h"

namespace gfx::render
{
  RenderGraph
  init_render_graph(MEMORY_ARENA_PARAM)
  {
    RenderGraph ret = {0};
    ret.render_passes = init_array<RenderPass>(MEMORY_ARENA_FWD, 64);
    ret.imported_resources = init_hash_table<ResourceHandle, PhysicalResource>(MEMORY_ARENA_FWD, 32);
    ret.transient_resources = init_hash_table<ResourceHandle, TransientResourceDesc>(MEMORY_ARENA_FWD, 32);
    ret.resource_list = init_array<ResourceHandle>(MEMORY_ARENA_FWD, 64);
    ret.handle_index = 0;


    return ret;
  }

  static u32
  handle_index(RenderGraph* graph)
  {
    return ++graph->handle_index;
  }

  TransientResourceCache
  init_transient_resource_cache(MEMORY_ARENA_PARAM, const GraphicsDevice* device)
  {
    TransientResourceCache ret = {0};
    ret.local_heap = init_gpu_linear_allocator(device, MiB(400), kGpuHeapTypeLocal);
    for (u8 i = 0; i < kFramesInFlight; i++)
    {
      ret.upload_heaps[i] = init_gpu_linear_allocator(device, MiB(1), kGpuHeapTypeUpload);
      ret.cbv_srv_uav_allocators[i] = init_descriptor_linear_allocator(device, 1024, kDescriptorHeapTypeCbvSrvUav);
      ret.rtv_allocators[i] = init_descriptor_linear_allocator(device, 256, kDescriptorHeapTypeRtv);
      ret.dsv_allocators[i] = init_descriptor_linear_allocator(device, 256, kDescriptorHeapTypeDsv);
      ret.sampler_allocators[i] = init_descriptor_linear_allocator(device, 256, kDescriptorHeapTypeSampler);
    }

    for (u8 i = 0; i < kFramesInFlight; i++)
    {
      ret.last_frame_resources[i] = init_array<ID3D12Resource*>(MEMORY_ARENA_FWD, 256);
    }

    ret.graphics_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
                                                         device,
                                                         &device->graphics_queue,
                                                         64 * kFramesInFlight);

    ret.compute_cmd_allocator  = init_cmd_list_allocator(MEMORY_ARENA_FWD,
                                                         device,
                                                         &device->compute_queue,
                                                         16 * kFramesInFlight);

    ret.copy_cmd_allocator     = init_cmd_list_allocator(MEMORY_ARENA_FWD,
                                                         device,
                                                         &device->copy_queue,
                                                         8 * kFramesInFlight);
    return ret;
  }

  void
  destroy_transient_resource_cache(TransientResourceCache* cache)
  {
    destroy_cmd_list_allocator(&cache->copy_cmd_allocator);
    destroy_cmd_list_allocator(&cache->compute_cmd_allocator);
    destroy_cmd_list_allocator(&cache->graphics_cmd_allocator);

    for (u8 i = 0; i < kFramesInFlight; i++)
    {
      destroy_descriptor_linear_allocator(&cache->sampler_allocators[i]);
      destroy_descriptor_linear_allocator(&cache->dsv_allocators[i]);
      destroy_descriptor_linear_allocator(&cache->rtv_allocators[i]);
      destroy_descriptor_linear_allocator(&cache->cbv_srv_uav_allocators[i]);
      destroy_gpu_linear_allocator(&cache->upload_heaps[i]);
    }
    destroy_gpu_linear_allocator(&cache->local_heap);
    zero_memory(cache, sizeof(TransientResourceCache));
  }

  RenderPass*
  add_render_pass(MEMORY_ARENA_PARAM, RenderGraph* graph, CmdQueueType queue, const char* name)
  {
    RenderPass* ret = array_add(&graph->render_passes);

    ret->allocator = sub_alloc_memory_arena(MEMORY_ARENA_FWD, KiB(16));

    ret->cmd_buffer = init_array<RenderGraphCmd>(MEMORY_ARENA_FWD, 1024);
    ret->read_resources = init_array<ResourceHandle>(MEMORY_ARENA_FWD, 16);
    ret->write_resources = init_array<ResourceHandle>(MEMORY_ARENA_FWD, 16);
    ret->resource_states = init_hash_table<ResourceHandle, D3D12_RESOURCE_STATES>(MEMORY_ARENA_FWD, 32);

    ret->pass_id = u32(graph->render_passes.size) - 1;
    ret->queue = queue;

    // TODO(Brandon): We should allocate these when we're actually compiling, instead of here
    // to save on memory.
    ret->passes_to_sync_with = init_array<RenderPassId>(MEMORY_ARENA_FWD, 64);
    ret->synchronization_index = init_array<RenderPassId>(MEMORY_ARENA_FWD, 64);
    ret->name = name;

    return ret;
  }


  Handle<GpuImage>
  create_image(RenderGraph* graph, const char* name, GpuImageDesc desc)
  {
    ResourceHandle resource_handle = {0};
    resource_handle.id = handle_index(graph);
    resource_handle.type = kResourceTypeImage;
    resource_handle.lifetime = kResourceLifetimeTransient;

    TransientResourceDesc* resource = hash_table_insert(&graph->transient_resources, resource_handle);
    resource->image_desc = desc;
    resource->type = resource_handle.type;
    resource->name = name;
    *array_add(&graph->resource_list) = resource_handle;

    Handle<GpuImage> ret = {resource_handle.id, resource_handle.lifetime};
    return ret;
  }

  Handle<Sampler>
  create_sampler(RenderGraph* graph, const char* name)
  {
    ResourceHandle resource_handle = {0};
    resource_handle.id = handle_index(graph);
    resource_handle.type = kResourceTypeSampler;
    resource_handle.lifetime = kResourceLifetimeTransient;

    TransientResourceDesc* resource = hash_table_insert(&graph->transient_resources, resource_handle);
    resource->type = resource_handle.type;
    resource->name = name;
    *array_add(&graph->resource_list) = resource_handle;

    Handle<Sampler> ret = {resource_handle.id, resource_handle.lifetime};
    return ret;
  }

  Handle<GpuBuffer>
  create_buffer(RenderGraph* graph, const char* name, GpuBufferDesc desc, Option<const void*> src)
  {
    ResourceHandle resource_handle = {0};
    resource_handle.id = handle_index(graph);
    resource_handle.type = kResourceTypeBuffer;
    resource_handle.lifetime = kResourceLifetimeTransient;

    TransientResourceDesc* resource = hash_table_insert(&graph->transient_resources, resource_handle);
    resource->buffer_desc.gpu_info = desc;
    resource->buffer_desc.has_upload_data = (bool)src;
    resource->name = name;
    *array_add(&graph->resource_list) = resource_handle;

    if (src)
    {
      ASSERT(desc.size <= sizeof(resource->buffer_desc.upload_data));
      ASSERT(desc.size <= 256);
      memcpy(resource->buffer_desc.upload_data, unwrap(src), desc.size);
    }
    resource->type = resource_handle.type;

    Handle<GpuBuffer> ret = {resource_handle.id, resource_handle.lifetime};
    return ret;
  }

  // TODO(Brandon): We need to ensure that you can't import the same resource twice.
  // Probably fix through just storing a HashSet of void*
  Handle<GpuImage>
  import_image(RenderGraph* graph, const GpuImage* image)
  {
    ResourceHandle resource_handle = {0};
    resource_handle.id = handle_index(graph);
    resource_handle.type = kResourceTypeImage;
    resource_handle.lifetime = kResourceLifetimeImported;

    PhysicalResource* resource = hash_table_insert(&graph->imported_resources, resource_handle);
    resource->image = image;
    resource->state = D3D12_RESOURCE_STATE_COMMON;
    resource->type = resource_handle.type;
    resource->needs_initialization = false;
    *array_add(&graph->resource_list) = resource_handle;

    Handle<GpuImage> ret = {resource_handle.id, resource_handle.lifetime};
    return ret;
  }

  Handle<GpuImage>
  import_back_buffer(RenderGraph* graph, const GpuImage* image)
  {
    Handle<GpuImage> resource = import_image(graph, image);
    ASSERT(!graph->back_buffer);

    graph->back_buffer = resource;
    return resource;
  }

  Handle<GpuBuffer>
  import_buffer(RenderGraph* graph, const GpuBuffer* buffer)
  {
    ResourceHandle resource_handle = {0};
    resource_handle.id = handle_index(graph);
    resource_handle.type = kResourceTypeBuffer;
    resource_handle.lifetime = kResourceLifetimeImported;

    PhysicalResource* resource = hash_table_insert(&graph->imported_resources, resource_handle);
    resource->buffer = buffer;
    resource->state = D3D12_RESOURCE_STATE_COMMON;
    resource->type = resource_handle.type;
    resource->needs_initialization = false;
    *array_add(&graph->resource_list) = resource_handle;

    Handle<GpuBuffer> ret = {resource_handle.id, resource_handle.lifetime};
    return ret;
  }

  static void
  render_pass_read(RenderPass* render_pass, ResourceHandle resource, D3D12_RESOURCE_STATES state)
  {
    ASSERT((state & D3D12_RESOURCE_STATE_GENERIC_READ) == state);
    ASSERT(!array_find(&render_pass->write_resources, *it == resource));

    if (!array_find(&render_pass->read_resources, *it == resource))
    {
      *array_add(&render_pass->read_resources) = resource;
    }

    D3D12_RESOURCE_STATES* resource_state = unwrap_or(hash_table_find(&render_pass->resource_states, resource), nullptr);
    if (!resource_state)
    {
      resource_state = hash_table_insert(&render_pass->resource_states, resource);
      *resource_state = D3D12_RESOURCE_STATE_COMMON;
    }
    *resource_state |= state;
  }

  static void
  render_pass_write(RenderPass* render_pass, ResourceHandle resource, D3D12_RESOURCE_STATES state)
  {
    ASSERT((state & ~D3D12_RESOURCE_STATE_GENERIC_READ) == state);

    ASSERT(!array_find(&render_pass->read_resources, *it == resource));
    D3D12_RESOURCE_STATES* resource_state = unwrap_or(hash_table_find(&render_pass->resource_states, resource), nullptr);
    if (!array_find(&render_pass->write_resources, *it == resource))
    {
      *array_add(&render_pass->write_resources) = resource;
    }
    else
    {
      ASSERT(resource_state && (*resource_state & ~state) == 0);
    }

    if (!resource_state)
    {
      resource_state = hash_table_insert(&render_pass->resource_states, resource);
      *resource_state = D3D12_RESOURCE_STATE_COMMON;
    }
    *resource_state |= state;
  }

  static void
  dfs_adjacency_list(RenderPassId pass_id,
                     Array<Array<RenderPassId>> adjacency_list,
                     Array<bool>* visited,
                     Array<bool>* on_stack,
                     bool* is_cyclic,
                     Array<RenderPassId>* out)
  {
    if (*is_cyclic)
      return;

    *array_at(visited, pass_id) = true;
    *array_at(on_stack, pass_id) = true;

    for (RenderPassId neighbour : adjacency_list[pass_id])
    {
      if (*array_at(visited, neighbour) && *array_at(on_stack, neighbour))
      {
        *is_cyclic = true;
        return;
      }

      if (!*array_at(visited, neighbour))
      {
        dfs_adjacency_list(neighbour, adjacency_list, visited, on_stack, is_cyclic, out);
      }
    }

    *array_at(on_stack, pass_id) = false;
    *array_add(out) = pass_id;
  }

  struct DependencyLevel
  {
    Array<RenderPassId> passes;
  };

  static void
  build_dependency_list(RenderGraph* graph, Array<DependencyLevel>* out_dependency_levels)
  {
    USE_SCRATCH_ARENA();

    ASSERT(graph->render_passes.size > 0);
    ASSERT(out_dependency_levels->size == graph->render_passes.size);

    auto topological_list = init_array<RenderPassId>(SCRATCH_ARENA_PASS, graph->render_passes.size);
    auto adjacency_list = init_array<Array<RenderPassId>>(SCRATCH_ARENA_PASS, graph->render_passes.size);
    for (size_t i = 0; i < graph->render_passes.size; i++)
    {
      Array<RenderPassId>* pass_adjacency_list = array_add(&adjacency_list); 
      *pass_adjacency_list = init_array<RenderPassId>(SCRATCH_ARENA_PASS, graph->render_passes.size);

      RenderPass* pass = &graph->render_passes[i];
      for (RenderPass& other : graph->render_passes)
      {
        // TODO(Brandon): This is a hack that makes it so that the order in which you call `add_render_pass`
        // matters so that you can do read -> write -> read sorts of things without a lot of fuss.
        // Eventually we'll want a better system of dealing with this...
        if (other.pass_id < pass->pass_id)
          continue;

        if (other.pass_id == pass->pass_id)
          continue;

        for (ResourceHandle& read_resource : other.read_resources)
        {
          bool other_depends_on_pass = array_find(&pass->write_resources, it->id == read_resource.id);

          if (!other_depends_on_pass)
            continue;

          *array_add(pass_adjacency_list) = other.pass_id;

          if (other.queue != pass->queue)
          {
            *array_add(&other.passes_to_sync_with) = pass->pass_id;
          }

          break;
        }
      }
    }

    {
      USE_SCRATCH_ARENA();
      auto visited = init_array<bool>(SCRATCH_ARENA_PASS, graph->render_passes.size);
      zero_array(&visited, visited.capacity);
      auto on_stack = init_array<bool>(SCRATCH_ARENA_PASS, graph->render_passes.size);
      zero_array(&on_stack, on_stack.capacity);
  
      bool is_cyclic = false;
      for (u32 pass_id = 0; pass_id < graph->render_passes.size; pass_id++)
      {
        if (visited[pass_id])
          continue;
  
        dfs_adjacency_list(pass_id, adjacency_list, &visited, &on_stack, &is_cyclic, &topological_list);
  
        // TODO(Brandon): Ideally we want ASSERT to be able to handle custom messages.
        ASSERT(!is_cyclic);
      }
    }
  
    reverse_array(&topological_list);

    {
      USE_SCRATCH_ARENA();

      auto longest_distances = init_array<u64>(SCRATCH_ARENA_PASS, topological_list.size);
      zero_array(&longest_distances, longest_distances.capacity);

      u64 dependency_level_count = 1;

      for (RenderPassId pass_id : topological_list)
      {
        for (RenderPassId adjacent_pass_id : adjacency_list[pass_id])
        {
          if (longest_distances[adjacent_pass_id] >= longest_distances[pass_id] + 1)
            continue;

          u64 dist = longest_distances[pass_id] + 1;
          longest_distances[adjacent_pass_id] = dist;
          dependency_level_count = MAX(dist + 1, dependency_level_count);
        }
      }

      out_dependency_levels->size = dependency_level_count;
      for (u32 pass_id : topological_list)
      {
        u64 level_index = longest_distances[pass_id];
        DependencyLevel* level = array_at(out_dependency_levels, level_index);
        *array_add(&level->passes) = pass_id;
      }
    }

    {
      USE_SCRATCH_ARENA();
      u64 queue_execution_indices[kCmdQueueTypeCount]{0};
  
      u64 global_execution_index = 0;
      for (DependencyLevel& lvl : *out_dependency_levels)
      {
        u64 level_execution_index = 0;
        for (RenderPassId pass_id : lvl.passes)
        {
          RenderPass* pass = &graph->render_passes[pass_id];
          pass->global_execution_index = global_execution_index;
          pass->level_execution_index = level_execution_index;
          pass->queue_execution_index = queue_execution_indices[pass->queue]++;

          level_execution_index++;
          global_execution_index++;
        }
      }
    }
  }

  struct PhysicalDescriptorKey
  {
    u32 id = 0;
    DescriptorType type = kDescriptorTypeCbv;
    u8 __padding0__ = 0;
    u16 __padding1__ = 0;

    auto operator<=>(const PhysicalDescriptorKey& rhs) const = default;
  };
  static_assert(offsetof(PhysicalDescriptorKey, id) == 0);
  static_assert(offsetof(PhysicalDescriptorKey, type) == 4);
  static_assert(offsetof(PhysicalDescriptorKey, __padding0__) == 5);
  static_assert(offsetof(PhysicalDescriptorKey, __padding1__) == 6);
  static_assert(sizeof(PhysicalDescriptorKey) == 8);

  struct CompiledResourceMap
  {
    HashTable<ResourceHandle, PhysicalResource>  resource_map;
    HashTable<PhysicalDescriptorKey, Descriptor> descriptor_map;
    DescriptorLinearAllocator* cbv_srv_uav_descriptor_allocator = nullptr;
    DescriptorLinearAllocator* rtv_allocator = nullptr;
    DescriptorLinearAllocator* dsv_allocator = nullptr;
    DescriptorLinearAllocator* sampler_allocator = nullptr;
  };

  static PhysicalResource*
  deref_resource(ResourceHandle resource, const CompiledResourceMap* compiled_map)
  {
    return unwrap(hash_table_find(&compiled_map->resource_map, resource));
  }

  static PhysicalResource*
  get_physical(ResourceHandle resource, const CompiledResourceMap* compiled_map)
  {
    return unwrap(hash_table_find(&compiled_map->resource_map, resource));
  }

  static ID3D12Resource*
  get_d3d12_resource(const PhysicalResource* resource)
  {
    switch(resource->type)
    {
      case kResourceTypeImage:            return resource->image->d3d12_image;
      case kResourceTypeBuffer:           return resource->buffer->d3d12_buffer;
      // TODO(Brandon): Implement these resources
      case kResourceTypeShader:           UNREACHABLE;
      case kResourceTypeGraphicsPSO:      UNREACHABLE;  
      default: UNREACHABLE;
    }
  }

  static Descriptor*
  get_descriptor(const GraphicsDevice* device,
                 ResourceHandle resource,
                 DescriptorType descriptor_type,
                 CompiledResourceMap* compiled_map,
                 Option<u32> buffer_stride)
  {
    PhysicalDescriptorKey key = {0};
    key.id   = resource.id;
    key.type = descriptor_type;

    Descriptor* descriptor = unwrap_or(hash_table_find(&compiled_map->descriptor_map, key), nullptr);
    if (!descriptor)
    {
      descriptor = hash_table_insert(&compiled_map->descriptor_map, key);
      PhysicalResource* physical_resource = get_physical(resource, compiled_map);
      ASSERT(physical_resource->type == resource.type);

      if (resource.type == kResourceTypeBuffer)
      {
        const GpuBuffer* buffer = physical_resource->buffer;
  
        u64 size_in_bytes = buffer->desc.size;
  
        *descriptor = alloc_descriptor(compiled_map->cbv_srv_uav_descriptor_allocator);
        switch (descriptor_type)
        {
          case kDescriptorTypeSrv:
          {
            init_buffer_srv(device,
                            descriptor,
                            buffer,
                            0,
                            static_cast<u32>(size_in_bytes / unwrap(buffer_stride)),
                            unwrap(buffer_stride));
          } break;
          case kDescriptorTypeUav:
          {
            init_buffer_uav(device,
                            descriptor,
                            buffer,
                            0,
                            static_cast<u32>(size_in_bytes / unwrap(buffer_stride)),
                            unwrap(buffer_stride));
          } break;
          case kDescriptorTypeCbv:
          {
            init_buffer_cbv(device,
                            descriptor,
                            buffer,
                            0,
                            static_cast<u32>(size_in_bytes));
          } break;
          default: UNREACHABLE;
        }
      }
      else if (resource.type == kResourceTypeImage)
      {
        const GpuImage* image = physical_resource->image;

        switch (descriptor_type)
        {
          case kDescriptorTypeRtv:
          {
            *descriptor = alloc_descriptor(compiled_map->rtv_allocator);
            init_rtv(device, descriptor, image);
          } break;
          case kDescriptorTypeDsv:
          {
            *descriptor = alloc_descriptor(compiled_map->dsv_allocator);
            init_dsv(device, descriptor, image);
          } break;
          case kDescriptorTypeSrv:
          {
            *descriptor = alloc_descriptor(compiled_map->cbv_srv_uav_descriptor_allocator);
            init_image_2D_srv(device, descriptor, image);
          } break;
          case kDescriptorTypeUav:
          {
            *descriptor = alloc_descriptor(compiled_map->cbv_srv_uav_descriptor_allocator);
            init_image_2D_uav(device, descriptor, image);
          } break;
          default: UNREACHABLE;
        }
      }
      else if (resource.type == kResourceTypeSampler)
      {
        ASSERT(descriptor_type == kDescriptorTypeSampler);
        *descriptor = alloc_descriptor(compiled_map->sampler_allocator);
        init_sampler(device, descriptor);
      } else { UNREACHABLE; }
    }

    ASSERT((descriptor->type & descriptor_type) == descriptor_type);
    return descriptor;
  }

  static void
  execute_d3d12_cmd(const GraphicsDevice* device,
                    CmdList* list,
                    const RenderGraphCmd& cmd,
                    CompiledResourceMap* compiled_map)
  {
    switch(cmd.type)
    {
      case RenderGraphCmdType::kGraphicsBindShaderResources:
      case RenderGraphCmdType::kComputeBindShaderResources:
      {
        const auto& args = cmd.graphics_bind_shader_resources;
        USE_SCRATCH_ARENA();
        auto root_consts = init_array<u32>(SCRATCH_ARENA_PASS, args.resources.size);

        for (const ShaderResource& shader_resource : args.resources)
        {
          auto resource = static_cast<ResourceHandle>(shader_resource);
          Descriptor* descriptor = get_descriptor(device,
                                                  resource,
                                                  shader_resource.descriptor_type,
                                                  compiled_map,
                                                  shader_resource.stride);
          *array_add(&root_consts) = descriptor->index;
        }

        if (cmd.type == RenderGraphCmdType::kGraphicsBindShaderResources)
        {
          list->d3d12_list->SetGraphicsRoot32BitConstants(0, u32(root_consts.size), root_consts.memory, 0);
        }
        else
        {
          list->d3d12_list->SetComputeRoot32BitConstants(0, u32(root_consts.size), root_consts.memory, 0);
        }
      } break;
      case RenderGraphCmdType::kDrawInstanced:
      {
        const auto& args = cmd.draw_instanced;
        list->d3d12_list->DrawInstanced(args.vertex_count_per_instance,
                                        args.instance_count,
                                        args.start_vertex_location,
                                        args.start_instance_location);
      } break;
      case RenderGraphCmdType::kDrawIndexedInstanced:
      {
        const auto& args = cmd.draw_indexed_instanced;
        list->d3d12_list->DrawIndexedInstanced(args.index_count_per_instance,
                                               args.instance_count,
                                               args.start_index_location,
                                               args.base_vertex_location,
                                               args.start_instance_location);
      } break;
      case RenderGraphCmdType::kDispatch:
      {
        const auto& args = cmd.dispatch;
        list->d3d12_list->Dispatch(args.thread_group_count_x,
                                   args.thread_group_count_y,
                                   args.thread_group_count_z);
      } break;
      case RenderGraphCmdType::kIASetPrimitiveTopology:
      {
        const auto& args = cmd.ia_set_primitive_topology;
        list->d3d12_list->IASetPrimitiveTopology(args.primitive_topology);

      } break;
      case RenderGraphCmdType::kRSSetViewport:
      {
        const auto& args = cmd.rs_set_viewport;
        list->d3d12_list->RSSetViewports(1, &args.viewport);

      } break;
      case RenderGraphCmdType::kRSSetScissorRects:
      {
        const auto& args = cmd.rs_set_scissor_rect;
        list->d3d12_list->RSSetScissorRects(1, &args.rect);

      } break;
      case RenderGraphCmdType::kOMSetBlendFactor:
      {
        const auto& args = cmd.om_set_blend_factor;
        list->d3d12_list->OMSetBlendFactor((f32*)&args.blend_factor);

      } break;
      case RenderGraphCmdType::kOMSetStencilRef:
      {
        const auto& args = cmd.om_set_stencil_ref;
        list->d3d12_list->OMSetStencilRef(args.stencil_ref);

      } break;
      case RenderGraphCmdType::kSetGraphicsPSO:
      {
        const auto& args = cmd.set_graphics_pso;
        list->d3d12_list->SetPipelineState(args.graphics_pso->d3d12_pso);
      } break;
      case RenderGraphCmdType::kSetComputePSO:
      {
        const auto& args = cmd.set_compute_pso;
        list->d3d12_list->SetPipelineState(args.compute_pso->d3d12_pso);
      } break;
      case RenderGraphCmdType::kSetRayTracingPSO:
      {
        const auto& args = cmd.set_ray_tracing_pso;
        list->d3d12_list->SetPipelineState1(args.ray_tracing_pso->d3d12_pso);
      } break;
      case RenderGraphCmdType::kIASetIndexBuffer:
      {
        const auto& args = cmd.ia_set_index_buffer;
        D3D12_INDEX_BUFFER_VIEW view;
        view.BufferLocation = args.index_buffer->gpu_addr;
        view.SizeInBytes = static_cast<u32>(args.index_buffer->desc.size);
        view.Format = DXGI_FORMAT_R32_UINT;
        list->d3d12_list->IASetIndexBuffer(&view);
      } break;
      case RenderGraphCmdType::kOMSetRenderTargets:
      {
        const auto& args = cmd.om_set_render_targets;
        USE_SCRATCH_ARENA();
        auto rtvs = init_array<D3D12_CPU_DESCRIPTOR_HANDLE>(SCRATCH_ARENA_PASS, args.render_targets.size);
        for (Handle<GpuImage> img : args.render_targets)
        {
          Descriptor* descriptor = get_descriptor(device, img, kDescriptorTypeRtv, compiled_map, None);
          *array_add(&rtvs) = descriptor->cpu_handle;
        }

        Descriptor* dsv_descriptor = args.depth_stencil_target ? get_descriptor(device,
                                                                                unwrap(args.depth_stencil_target),
                                                                                kDescriptorTypeDsv,
                                                                                compiled_map,
                                                                                None) : nullptr;

        list->d3d12_list->OMSetRenderTargets(static_cast<u32>(rtvs.size),
                                             rtvs.memory,
                                             FALSE,
                                             dsv_descriptor ? &dsv_descriptor->cpu_handle : nullptr);
        if (args.render_targets.size > 0)
        {
          PhysicalResource* resource = deref_resource(args.render_targets[0], compiled_map);
          ASSERT(resource->type == kResourceTypeImage);
          f32 width = static_cast<f32>(resource->image->desc.width);
          f32 height = static_cast<f32>(resource->image->desc.height);
          auto viewport = CD3DX12_VIEWPORT(0.0, 0.0, width, height);
          list->d3d12_list->RSSetViewports(1, &viewport);

          auto scissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
          list->d3d12_list->RSSetScissorRects(1, &scissor);
        }

      } break;
      case RenderGraphCmdType::kClearDepthStencilView:
      {
        const auto& args = cmd.clear_depth_stencil_view;
        Descriptor* dsv = get_descriptor(device, args.depth_stencil, kDescriptorTypeDsv, compiled_map, None);

        list->d3d12_list->ClearDepthStencilView(dsv->cpu_handle,
                                                args.clear_flags,
                                                args.depth,
                                                args.stencil,
                                                0, nullptr);

      } break;
      case RenderGraphCmdType::kClearRenderTargetView:
      {
        const auto& args = cmd.clear_render_target_view;

        Descriptor* rtv = get_descriptor(device, args.render_target, kDescriptorTypeRtv, compiled_map, None);

        list->d3d12_list->ClearRenderTargetView(rtv->cpu_handle, (f32*)&args.clear_color, 0, nullptr);
      } break;
      case RenderGraphCmdType::kClearUnorderedAccessViewUint:
      {
        const auto& args = cmd.clear_unordered_access_view_uint;

        auto resource = static_cast<ResourceHandle>(args.uav);
        PhysicalResource* physical = deref_resource(resource, compiled_map);
        Descriptor* descriptor = get_descriptor(device,
                                                resource,
                                                args.uav.descriptor_type,
                                                compiled_map,
                                                args.uav.stride);
        list->d3d12_list->ClearUnorderedAccessViewUint(unwrap(descriptor->gpu_handle),
                                                       descriptor->cpu_handle,
                                                       physical->image->d3d12_image,
                                                       args.values.memory,
                                                       0, nullptr);
      } break;
      case RenderGraphCmdType::kClearUnorderedAccessViewFloat:
      {
        const auto& args = cmd.clear_unordered_access_view_float;

        auto resource = static_cast<ResourceHandle>(args.uav);
        PhysicalResource* physical = deref_resource(resource, compiled_map);
        Descriptor* descriptor = get_descriptor(device,
                                                resource,
                                                args.uav.descriptor_type,
                                                compiled_map,
                                                args.uav.stride);
        list->d3d12_list->ClearUnorderedAccessViewFloat(unwrap(descriptor->gpu_handle),
                                                        descriptor->cpu_handle,
                                                        physical->image->d3d12_image,
                                                        args.values.memory,
                                                        0, nullptr);
      } break;
      case RenderGraphCmdType::kDispatchRays:
      {
        const auto& args = cmd.dispatch_rays;

        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord.StartAddress = args.shader_table.ray_gen_addr;
        desc.RayGenerationShaderRecord.SizeInBytes  = args.shader_table.ray_gen_size;

        desc.MissShaderTable.StartAddress  = args.shader_table.miss_addr;
        desc.MissShaderTable.SizeInBytes   = args.shader_table.miss_size;
        desc.MissShaderTable.StrideInBytes = args.shader_table.record_size;

        desc.HitGroupTable.StartAddress  = args.shader_table.hit_addr;
        desc.HitGroupTable.SizeInBytes   = args.shader_table.hit_size;
        desc.HitGroupTable.StrideInBytes = args.shader_table.record_size;

        desc.Width  = args.x;
        desc.Height = args.y;
        desc.Depth  = args.z;

        list->d3d12_list->SetComputeRootShaderResourceView(1, args.bvh->top_bvh.gpu_addr);
        list->d3d12_list->SetComputeRootShaderResourceView(2, args.index_buffer->gpu_addr);
        list->d3d12_list->SetComputeRootShaderResourceView(3, args.vertex_buffer->gpu_addr);
        list->d3d12_list->DispatchRays(&desc);
      } break;
      case RenderGraphCmdType::kDrawImGuiOnTop:
      {
        const auto& args = cmd.draw_imgui_on_top;
        cmd_set_descriptor_heaps(list, {args.descriptor_linear_allocator});
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list->d3d12_list);

        cmd_set_descriptor_heaps(list, {compiled_map->cbv_srv_uav_descriptor_allocator,
                                        compiled_map->sampler_allocator});
      } break;
      default: UNREACHABLE;
    }
  }

  static void
  execute_d3d12_transition(CmdList* cmd_list,
                           ResourceHandle resource,
                           const CompiledResourceMap* compiled_map,
                           D3D12_RESOURCE_STATES next_state)
  {
    PhysicalResource* physical = deref_resource(resource, compiled_map);
    ID3D12Resource* d3d12_resource = get_d3d12_resource(physical);

    if (physical->state != next_state)
    {
      auto transition = CD3DX12_RESOURCE_BARRIER::Transition(d3d12_resource, physical->state, next_state);
      physical->state = next_state;
      cmd_list->d3d12_list->ResourceBarrier(1, &transition);
    }

    if (physical->needs_initialization)
    {
      cmd_list->d3d12_list->DiscardResource(d3d12_resource, nullptr);
      physical->needs_initialization = false;
    }
  }

  static void
  execute_d3d12_transition(CmdList* cmd_list,
                           ResourceHandle resource,
                           const CompiledResourceMap* compiled_map,
                           const RenderPass& pass)
  {
    D3D12_RESOURCE_STATES next_state = *unwrap(hash_table_find(&pass.resource_states, resource));
    execute_d3d12_transition(cmd_list, resource, compiled_map, next_state);
  }

  static CmdListAllocator*
  get_cmd_list_allocator(TransientResourceCache* cache, CmdQueueType type)
  {
    switch(type)
    {
      case kCmdQueueTypeGraphics: return &cache->graphics_cmd_allocator;
      case kCmdQueueTypeCompute:  return &cache->compute_cmd_allocator;
      case kCmdQueueTypeCopy:     return &cache->copy_cmd_allocator;
      default: UNREACHABLE;
    }
  }

  static D3D12_RESOURCE_FLAGS
  get_additional_resource_flags(ResourceHandle resource, const RenderPass& pass)
  {
    D3D12_RESOURCE_FLAGS ret = D3D12_RESOURCE_FLAG_NONE;

    if (resource.lifetime != kResourceLifetimeTransient)
      return ret;

    D3D12_RESOURCE_STATES states = *unwrap(hash_table_find(&pass.resource_states, resource));
    if ((states & D3D12_RESOURCE_STATE_RENDER_TARGET) != 0)
    {
      ret |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    if ((states & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0)
    {
      ret |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    return ret;
  }

  void
  execute_render_graph(MEMORY_ARENA_PARAM,
                       const GraphicsDevice* device,
                       RenderGraph* graph,
                       TransientResourceCache* cache,
                       u32 frame_index)
  {
    USE_SCRATCH_ARENA();
    auto dependency_levels = init_array<DependencyLevel>(SCRATCH_ARENA_PASS, graph->render_passes.size);
    for (size_t i = 0; i < graph->render_passes.size; i++)
    {
      DependencyLevel* level = array_add(&dependency_levels); 
      level->passes = init_array<RenderPassId>(SCRATCH_ARENA_PASS, graph->render_passes.size);
    }

    build_dependency_list(graph, &dependency_levels);
    
    u64 total_resource_count = graph->transient_resources.used + graph->imported_resources.used;
    CompiledResourceMap compiled_map = {0};
    compiled_map.resource_map   = init_hash_table<ResourceHandle,  PhysicalResource>(SCRATCH_ARENA_PASS, total_resource_count);
    compiled_map.descriptor_map = init_hash_table<PhysicalDescriptorKey, Descriptor>(SCRATCH_ARENA_PASS, total_resource_count * 5 / 4);

    u64 buffer_count = 0;
    u64 image_count = 0;
    for (const ResourceHandle& resource : graph->resource_list)
    {
      if (resource.lifetime != kResourceLifetimeTransient)
        continue;
      if (resource.type == kResourceTypeBuffer)
      {
        buffer_count++;
      }
      else if (resource.type == kResourceTypeImage)
      {
        image_count++;
      }
    }

    auto gpu_buffers = init_array<GpuBuffer>(SCRATCH_ARENA_PASS, buffer_count);
    auto gpu_images = init_array<GpuImage>(SCRATCH_ARENA_PASS, image_count);
    GpuLinearAllocator* local_heap = &cache->local_heap;
    GpuLinearAllocator* upload_heap = &cache->upload_heaps[frame_index];
    compiled_map.cbv_srv_uav_descriptor_allocator = &cache->cbv_srv_uav_allocators[frame_index];
    compiled_map.rtv_allocator = &cache->rtv_allocators[frame_index];
    compiled_map.dsv_allocator = &cache->dsv_allocators[frame_index];
    compiled_map.sampler_allocator = &cache->sampler_allocators[frame_index];
    Array<ID3D12Resource*>* frame_resources = &cache->last_frame_resources[frame_index];

    for (ID3D12Resource* resource : *frame_resources)
    {
      COM_RELEASE(resource);
    }

    clear_array(frame_resources);

    reset_gpu_linear_allocator(local_heap);
    reset_gpu_linear_allocator(upload_heap);
    reset_descriptor_linear_allocator(compiled_map.cbv_srv_uav_descriptor_allocator);
    reset_descriptor_linear_allocator(compiled_map.rtv_allocator);
    reset_descriptor_linear_allocator(compiled_map.dsv_allocator);
    reset_descriptor_linear_allocator(compiled_map.sampler_allocator);

    {
      USE_SCRATCH_ARENA();
      auto additional_flags = init_hash_table<ResourceHandle, D3D12_RESOURCE_FLAGS>(SCRATCH_ARENA_PASS, total_resource_count);
      for (DependencyLevel dependency_level : dependency_levels)
      {
        for (RenderPassId pass_id : dependency_level.passes)
        {
          const RenderPass& pass = graph->render_passes[pass_id];
          for (ResourceHandle resource : pass.read_resources)
          {
            D3D12_RESOURCE_FLAGS* flags = hash_table_insert(&additional_flags, resource);
            *flags |= get_additional_resource_flags(resource, pass);
          }
  
          for (ResourceHandle resource : pass.write_resources)
          {
            D3D12_RESOURCE_FLAGS* flags = hash_table_insert(&additional_flags, resource);
            *flags |= get_additional_resource_flags(resource, pass);
          }
        }
      }

      for (const ResourceHandle& resource : graph->resource_list)
      {
        PhysicalResource physical = { 0 };
        if (resource.lifetime == kResourceLifetimeImported)
        {
          physical = *unwrap(hash_table_find(&graph->imported_resources, resource));
          physical.needs_initialization = false;
          if (resource.type == kResourceTypeImage)
          {
            // TODO(Brandon): I'm pretty sure this isn't really right. Because we never explicitly transition imported resources back to their initial state...
            physical.state = physical.image->desc.initial_state;
          }
        }
        else if (resource.lifetime == kResourceLifetimeTransient)
        {
          TransientResourceDesc desc = *unwrap(hash_table_find(&graph->transient_resources, resource));
          D3D12_RESOURCE_FLAGS default_flags = D3D12_RESOURCE_FLAG_NONE;
          switch (resource.type)
          {
            case kResourceTypeImage:
            {  
              desc.image_desc.flags |= *unwrap_or(hash_table_find(&additional_flags, resource), &default_flags);
              GpuImage image = alloc_gpu_image_2D(device, local_heap, desc.image_desc, desc.name);
              GpuImage* ptr = array_add(&gpu_images);
              *ptr = image;
              physical.image = ptr;
              physical.needs_initialization = true;
            } break;
            case kResourceTypeBuffer:
            {
              desc.buffer_desc.gpu_info.flags |= *unwrap_or(hash_table_find(&additional_flags, resource), &default_flags);
              GpuBuffer buffer = { 0 };
              if (desc.buffer_desc.has_upload_data)
              {
                buffer = alloc_gpu_buffer(device, upload_heap, desc.buffer_desc.gpu_info, desc.name);
                memcpy(unwrap(buffer.mapped), desc.buffer_desc.upload_data, buffer.desc.size);
                physical.needs_initialization = false;
              }
              else
              {
                buffer = alloc_gpu_buffer(device, local_heap, desc.buffer_desc.gpu_info, desc.name);
                physical.needs_initialization = true;
              }
              GpuBuffer* ptr = array_add(&gpu_buffers);
              *ptr = buffer;
              physical.buffer = ptr;
            } break;
            // We don't need to do anything if it's a sampler.
            case kResourceTypeSampler: break;
            default: UNREACHABLE;
          }
        } else { UNREACHABLE; }
        physical.type = resource.type;
        *hash_table_insert(&compiled_map.resource_map, resource) = physical;
      }
    }

    const u32 kPIXTransitionColor = PIX_COLOR(0, 0, 255);
    const u32 kPIXRenderPassColor = PIX_COLOR(255, 0, 0);


    for (DependencyLevel& dependency_level : dependency_levels)
    {
      {
        CmdListAllocator* allocator = get_cmd_list_allocator(cache, kCmdQueueTypeGraphics);
        CmdList cmd_list = alloc_cmd_list(allocator);

        PIXBeginEvent(cmd_list.d3d12_list, kPIXTransitionColor, "Transition Barrier");
        for (RenderPassId pass_id : dependency_level.passes)
        {
          const RenderPass& pass = graph->render_passes[pass_id];
          for (ResourceHandle resource : pass.read_resources)
          {
            execute_d3d12_transition(&cmd_list, resource, &compiled_map, pass);
          }

          for (ResourceHandle resource : pass.write_resources)
          {
            execute_d3d12_transition(&cmd_list, resource, &compiled_map, pass);
          }
        }
        PIXEndEvent(cmd_list.d3d12_list);

        submit_cmd_lists(allocator, { cmd_list });
      }
      {
        USE_SCRATCH_ARENA();
        Array<CmdList> cmd_lists[kCmdQueueTypeCount];
        for (u32 queue_type = 0; queue_type < kCmdQueueTypeCount; queue_type++)
        {
          cmd_lists[queue_type] = init_array<CmdList>(SCRATCH_ARENA_PASS, dependency_level.passes.size);
        }
  
        for (RenderPassId pass_id : dependency_level.passes)
        {
          const RenderPass& pass = graph->render_passes[pass_id];
          CmdListAllocator* allocator = get_cmd_list_allocator(cache, pass.queue);
  
          CmdList list = alloc_cmd_list(allocator);
          PIXBeginEvent(list.d3d12_list, kPIXRenderPassColor, "Render Pass %s", pass.name);
  
          cmd_set_descriptor_heaps(&list, {compiled_map.cbv_srv_uav_descriptor_allocator,
                                           compiled_map.sampler_allocator});
          if (pass.queue == kCmdQueueTypeGraphics)
          {
            cmd_set_primitive_topology(&list);
            cmd_set_graphics_root_signature(&list);
            cmd_set_compute_root_signature(&list);
          }
          else if (pass.queue == kCmdQueueTypeCompute)
          {
            cmd_set_compute_root_signature(&list);
          }
  
          for (const RenderGraphCmd& cmd : pass.cmd_buffer)
          {
            execute_d3d12_cmd(device, &list, cmd, &compiled_map);
          }
          PIXEndEvent(list.d3d12_list);
  
          *array_add(&cmd_lists[pass.queue]) = list;
        }
  
        for (u32 queue_type = 0; queue_type < kCmdQueueTypeCount; queue_type++)
        {
          CmdListAllocator* allocator = get_cmd_list_allocator(cache, CmdQueueType(queue_type));
          submit_cmd_lists(allocator, cmd_lists[queue_type]);
        }
      }
    }


    if (graph->back_buffer)
    {
      PhysicalResource* physical = deref_resource(unwrap(graph->back_buffer), &compiled_map);
      ASSERT(physical->type == kResourceTypeImage);

      const D3D12_RESOURCE_STATES next_state = D3D12_RESOURCE_STATE_PRESENT;
      if (physical->state != next_state)
      {
        CmdListAllocator* allocator = get_cmd_list_allocator(cache, kCmdQueueTypeGraphics);
        CmdList cmd_list = alloc_cmd_list(allocator);
        PIXBeginEvent(cmd_list.d3d12_list, kPIXTransitionColor, "Back Buffer Transition");
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(physical->image->d3d12_image,
                                                            physical->state,
                                                            next_state);
        cmd_list.d3d12_list->ResourceBarrier(1, &barrier);
        PIXEndEvent(cmd_list.d3d12_list);
        submit_cmd_lists(allocator, { cmd_list });
      }

    }


    {
      CmdListAllocator* allocator = get_cmd_list_allocator(cache, kCmdQueueTypeGraphics);
      CmdList cmd_list = alloc_cmd_list(allocator);
      for (const ResourceHandle& resource : graph->resource_list)
      {
        PhysicalResource physical = { 0 };
        if (resource.lifetime != kResourceLifetimeImported)
          continue;
  
        if (graph->back_buffer && resource.id == unwrap(graph->back_buffer).id)
          continue;
  
        physical = *unwrap(hash_table_find(&graph->imported_resources, resource));
        if (resource.type == kResourceTypeImage)
        {
          execute_d3d12_transition(&cmd_list, resource, &compiled_map, physical.image->desc.initial_state);
        }
  
      }
      submit_cmd_lists(allocator, {cmd_list});
    }

    for (const GpuBuffer& buffer : gpu_buffers)
    {
      *array_add(frame_resources) = buffer.d3d12_buffer;
    }

    for (const GpuImage& image : gpu_images)
    {
      *array_add(frame_resources) = image.d3d12_image;
    }
  }

  static void
  push_cmd(RenderPass* render_pass, const RenderGraphCmd& cmd)
  {
    memcpy(array_add(&render_pass->cmd_buffer), &cmd, sizeof(cmd));
  }

  static void
  common_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources, CmdQueueType type)
  {
    RenderGraphCmd cmd;
    if (type == kCmdQueueTypeGraphics)
    {
      cmd.type = RenderGraphCmdType::kGraphicsBindShaderResources;
      cmd.graphics_bind_shader_resources.resources = init_array<ShaderResource>(&render_pass->allocator, resources);
    }
    else if (type == kCmdQueueTypeCompute)
    {
      cmd.type = RenderGraphCmdType::kComputeBindShaderResources;
      cmd.compute_bind_shader_resources.resources = init_array<ShaderResource>(&render_pass->allocator, resources);
    } else { UNREACHABLE; }

    for (const ShaderResource& shader_resource : resources)
    {
      auto resource = static_cast<ResourceHandle>(shader_resource);
      switch (shader_resource.descriptor_type)
      {
        case kDescriptorTypeSrv: render_pass_read(render_pass, resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE); break;
        case kDescriptorTypeCbv: render_pass_read(render_pass, resource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER); break;
        case kDescriptorTypeUav: render_pass_write(render_pass, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); break;
        case kDescriptorTypeSampler: break;
        default: UNREACHABLE;
      }
    }
    push_cmd(render_pass, cmd);
  }

  void
  cmd_graphics_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources)
  {
    common_bind_shader_resources(render_pass, resources, kCmdQueueTypeGraphics);
  }

  void
  cmd_compute_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources)
  {
    common_bind_shader_resources(render_pass, resources, kCmdQueueTypeCompute);
  }

  void
  cmd_ray_tracing_bind_shader_resources(RenderPass* render_pass, Span<ShaderResource> resources)
  {
    common_bind_shader_resources(render_pass, resources, kCmdQueueTypeCompute);
  }

  void
  cmd_draw_instanced(RenderPass* render_pass,
                     u32 vertex_count_per_instance,
                     u32 instance_count,
                     u32 start_vertex_location,
                     u32 start_instance_location)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kDrawInstanced;
    cmd.draw_instanced.vertex_count_per_instance = vertex_count_per_instance;
    cmd.draw_instanced.instance_count = instance_count;
    cmd.draw_instanced.start_vertex_location = start_vertex_location;
    cmd.draw_instanced.start_instance_location = start_instance_location;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_draw_indexed_instanced(RenderPass* render_pass,
                             u32 index_count_per_instance,
                             u32 instance_count,
                             u32 start_index_location,
                             s32 base_vertex_location,
                             u32 start_instance_location)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kDrawIndexedInstanced;
    cmd.draw_indexed_instanced.index_count_per_instance = index_count_per_instance;
    cmd.draw_indexed_instanced.instance_count = instance_count;
    cmd.draw_indexed_instanced.start_index_location = start_index_location;
    cmd.draw_indexed_instanced.base_vertex_location = base_vertex_location;
    cmd.draw_indexed_instanced.start_instance_location = start_instance_location;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_dispatch(RenderPass* render_pass,
               u32 thread_group_count_x,
               u32 thread_group_count_y,
               u32 thread_group_count_z)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kDispatch;
    cmd.dispatch.thread_group_count_x = thread_group_count_x;
    cmd.dispatch.thread_group_count_y = thread_group_count_y;
    cmd.dispatch.thread_group_count_z = thread_group_count_z;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_ia_set_primitive_topology(RenderPass* render_pass,
                                D3D12_PRIMITIVE_TOPOLOGY primitive_topology)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kIASetPrimitiveTopology;
    cmd.ia_set_primitive_topology.primitive_topology = primitive_topology;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_rs_set_viewport(RenderPass* render_pass, 
                      D3D12_VIEWPORT viewport)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kRSSetViewport;
    cmd.rs_set_viewport.viewport = viewport;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_rs_set_scissor_rect(RenderPass* render_pass,
                          D3D12_RECT rect)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kRSSetViewport;
    cmd.rs_set_scissor_rect.rect = rect;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_om_set_blend_factor(RenderPass* render_pass, 
                          Vec4 blend_factor)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kOMSetBlendFactor;
    cmd.om_set_blend_factor.blend_factor = blend_factor;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_om_set_stencil_ref(RenderPass* render_pass, 
                         u32 stencil_ref)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kOMSetBlendFactor;
    cmd.om_set_stencil_ref.stencil_ref = stencil_ref;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_set_graphics_pso(RenderPass* render_pass, const GraphicsPSO* graphics_pso)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kSetGraphicsPSO;
    cmd.set_graphics_pso.graphics_pso = graphics_pso;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_set_compute_pso(RenderPass* render_pass, const ComputePSO* compute_pso)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kSetComputePSO;
    cmd.set_compute_pso.compute_pso = compute_pso;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_set_ray_tracing_pso(RenderPass* render_pass, const RayTracingPSO* ray_tracing_pso)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kSetRayTracingPSO;
    cmd.set_ray_tracing_pso.ray_tracing_pso = ray_tracing_pso;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_ia_set_index_buffer(RenderPass* render_pass, 
                          const GpuBuffer* index_buffer,
                          DXGI_FORMAT format)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kIASetIndexBuffer;
    cmd.ia_set_index_buffer.index_buffer = index_buffer;
    cmd.ia_set_index_buffer.format = format;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_om_set_render_targets(RenderPass* render_pass, 
                            Span<Handle<GpuImage>> render_targets,
                            Option<Handle<GpuImage>> depth_stencil_target)
  {
    for (auto& target : render_targets)
    {
      render_pass_write(render_pass, target, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    if (depth_stencil_target)
    {
      render_pass_write(render_pass, unwrap(depth_stencil_target), D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kOMSetRenderTargets;
    cmd.om_set_render_targets.render_targets = init_array<Handle<GpuImage>>(&render_pass->allocator, render_targets);
    cmd.om_set_render_targets.depth_stencil_target = depth_stencil_target;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_clear_render_target_view(RenderPass* render_pass, 
                               Handle<GpuImage>* render_target,
                               Vec4 clear_color)
  {
    render_pass_write(render_pass, *render_target, D3D12_RESOURCE_STATE_RENDER_TARGET);

    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kClearRenderTargetView;
    cmd.clear_render_target_view.render_target = *render_target;
    cmd.clear_render_target_view.clear_color = clear_color;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_clear_depth_stencil_view(RenderPass* render_pass, 
                               Handle<GpuImage>* depth_stencil,
                               D3D12_CLEAR_FLAGS clear_flags,
                               f32 depth,
                               u8 stencil)
  {
    render_pass_write(render_pass, *depth_stencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kClearDepthStencilView;
    cmd.clear_depth_stencil_view.depth_stencil = *depth_stencil;
    cmd.clear_depth_stencil_view.clear_flags = clear_flags;
    cmd.clear_depth_stencil_view.depth = depth;
    cmd.clear_depth_stencil_view.stencil = stencil;
    push_cmd(render_pass, cmd);
  }

  void cmd_clear_unordered_access_view_uint(RenderPass* render_pass,
                                            Handle<GpuImage>* uav,
                                            Span<u32> values)
  {
    ASSERT(values.size == 4);
    render_pass_write(render_pass, *uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kClearUnorderedAccessViewUint;
    cmd.clear_unordered_access_view_uint.uav.id = uav->id;
    cmd.clear_unordered_access_view_uint.uav.type = kResourceTypeImage;
    cmd.clear_unordered_access_view_uint.uav.lifetime = uav->lifetime;
    cmd.clear_unordered_access_view_uint.uav.descriptor_type = kDescriptorTypeUav;
    cmd.clear_unordered_access_view_uint.uav.stride = 0;
    array_copy(&cmd.clear_unordered_access_view_uint.values, values);
    push_cmd(render_pass, cmd);
  }
  
  void cmd_clear_unordered_access_view_float(RenderPass* render_pass,
                                             Handle<GpuImage>* uav,
                                             Span<f32> values)
  {
    ASSERT(values.size == 4);
    render_pass_write(render_pass, *uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kClearUnorderedAccessViewFloat;
    cmd.clear_unordered_access_view_float.uav.id = uav->id;
    cmd.clear_unordered_access_view_float.uav.type = kResourceTypeImage;
    cmd.clear_unordered_access_view_float.uav.lifetime = uav->lifetime;
    cmd.clear_unordered_access_view_float.uav.descriptor_type = kDescriptorTypeUav;
    cmd.clear_unordered_access_view_float.uav.stride = 0;
    array_copy(&cmd.clear_unordered_access_view_float.values, values);
    push_cmd(render_pass, cmd);
  }

  void
  cmd_dispatch_rays(RenderPass* render_pass,
                    const GpuBvh* bvh,
                    const GpuBuffer* index_buffer,
                    const GpuBuffer* vertex_buffer,
                    ShaderTable shader_table,
                    u32 x,
                    u32 y,
                    u32 z)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kDispatchRays;
    cmd.dispatch_rays.bvh = bvh;
    cmd.dispatch_rays.index_buffer = index_buffer;
    cmd.dispatch_rays.vertex_buffer = vertex_buffer;
    cmd.dispatch_rays.shader_table = shader_table;
    cmd.dispatch_rays.x = x;
    cmd.dispatch_rays.y = y;
    cmd.dispatch_rays.z = z;
    push_cmd(render_pass, cmd);
  }

  void
  cmd_draw_imgui_on_top(RenderPass* render_pass, const DescriptorLinearAllocator* descriptor_linear_allocator)
  {
    RenderGraphCmd cmd;
    cmd.type = RenderGraphCmdType::kDrawImGuiOnTop;
    cmd.draw_imgui_on_top.descriptor_linear_allocator = descriptor_linear_allocator;
    push_cmd(render_pass, cmd);
  }
}