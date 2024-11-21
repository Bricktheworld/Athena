#include "Core/Foundation/context.h"
#include "Core/Vendor/d3dx12.h"

#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

constant u32 kMaxTemporalLifetime = 3;

#if defined(RENDER_GRAPH_VERBOSE)
#define RG_DBGLN(...) dbgln(__VA_ARGS__)
#else
#define RG_DBGLN(...) do { } while(0)
#endif

static u32
handle_index(RgBuilder* graph)
{
  return ++graph->handle_index;
}

static void
create_back_buffer(RgBuilder* builder)
{
  ResourceHandle resource_handle      = {0};
  resource_handle.id                  = handle_index(builder);
  resource_handle.version             = 0;
  resource_handle.type                = kResourceTypeTexture;
  *array_add(&builder->resource_list) = resource_handle;

  builder->back_buffer                = {resource_handle.id, resource_handle.version};
}

RgBuilder
init_rg_builder(AllocHeap heap, u32 width, u32 height)
{
  RgBuilder ret      = {0};
  ret.render_passes  = init_array<RgPassBuilder>(heap, 64);
  ret.resource_list  = init_array<ResourceHandle>(heap, 64);
  ret.resource_descs = init_hash_table<u32, TransientResourceDesc>(heap, 64);
  ret.handle_index   = 0;
  ret.width          = width;
  ret.height         = height;

  create_back_buffer(&ret);

  return ret;
}

struct AdjacentRenderPasses
{
  Array<RenderPassId> dependent_passes;
};

struct AdjacencyList
{
  Array<AdjacentRenderPasses> render_passes;
};

static AdjacencyList
init_adjacency_list(AllocHeap heap, const RgBuilder& builder)
{
  AdjacencyList ret = {0};
  ret.render_passes = init_array_uninitialized<AdjacentRenderPasses>(heap, builder.render_passes.size);
  for (const RgPassBuilder& pass_builder : builder.render_passes)
  {
    AdjacentRenderPasses* dst = &ret.render_passes[pass_builder.pass_id];
    dst->dependent_passes     = init_array<RenderPassId>(heap, builder.render_passes.size);

    for (const RgPassBuilder& other_builder : builder.render_passes)
    {
      // Look through every other render pass (skip our own)
      if (other_builder.pass_id == pass_builder.pass_id)
        continue;

      if (builder.frame_init && pass_builder.pass_id == unwrap(builder.frame_init))
      {
        *array_add(&dst->dependent_passes) = other_builder.pass_id;
      }
      else
      {
        // Loop through all of our written resources
        for (const RgPassBuilder::ResourceAccessData& write_resource : pass_builder.write_resources)
        {
          // And try to find each write resource in the other pass's read resources
          // We need to check for both the ID and the version that is being written
          // The version should be exactly 1 after our write version (the other pass will write to the resource
          // incrementing the version and this pass will then _immediately_ read the resource). We don't want
          // to consider _all_ passes that have ever written to older versions, just ones that have written
          // to _our_ version (adjacent in the dependency graph).
          bool other_depends_on_pass = array_find(
            &other_builder.read_resources,
            it->handle.id == write_resource.handle.id && it->handle.version == write_resource.handle.version + 1 && it->temporal_frame == 0
          ) || array_find(
            &other_builder.write_resources,
            it->handle.id == write_resource.handle.id && it->handle.version == write_resource.handle.version + 1
          );
  
          if (other_depends_on_pass)
          {
            *array_add(&dst->dependent_passes) = other_builder.pass_id;
            break;
          }
        }
      }

    }
  }

  return ret;
}

struct CycleDetectionState
{
  Array<bool> visited;
  Array<bool> in_path;
  bool        is_cyclic = false;
};

static check_return bool
dfs_has_cycle(RenderPassId pass_id, CycleDetectionState* state, AdjacencyList list)
{
  state->in_path[pass_id] = true;
  state->visited[pass_id] = true;

  Array<RenderPassId> dependent_passes = list.render_passes[pass_id].dependent_passes;
  for (RenderPassId dependency : dependent_passes)
  {
    if (state->visited[dependency])
      continue;

    if (state->in_path[dependency])
      return true;

    if (dfs_has_cycle(dependency, state, list))
      return true;
  }

  state->in_path[pass_id] = false;

  return false;
}

static bool
is_cyclic_adjacency_list(AdjacencyList list)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  CycleDetectionState state = {0};
  state.visited             = init_array_zeroed<bool>(scratch_arena, list.render_passes.size);
  state.in_path             = init_array_zeroed<bool>(scratch_arena, list.render_passes.size);
  state.is_cyclic           = false;

  for (RenderPassId ipass = 0; ipass < list.render_passes.size; ipass++)
  {
    if (state.visited[ipass])
      continue;

    if (dfs_has_cycle(ipass, &state, list))
      return true;
  }

  return false;
}

struct TopologicalSortState
{
  Array<bool>          visited;
  Array<RenderPassId>* out_list;
};

static void
dfs_topological_sort_adjacency_list(RenderPassId pass_id, TopologicalSortState* state, AdjacencyList list)
{
  state->visited[pass_id] = true;

  Array<RenderPassId> dependent_passes = list.render_passes[pass_id].dependent_passes;
  for (RenderPassId dependency : dependent_passes)
  {
    if (state->visited[dependency])
      continue;
    dfs_topological_sort_adjacency_list(dependency, state, list);
  }

  *array_add(state->out_list) = pass_id;
}

static Array<RenderPassId>
topological_sort_adjacency_list(AllocHeap heap, AdjacencyList list)
{
  Array<RenderPassId> ret = init_array<RenderPassId>(heap, list.render_passes.size);

  bool is_cyclic          = is_cyclic_adjacency_list(list);
  ASSERT(!is_cyclic);

  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  TopologicalSortState state = {0};
  state.visited              = init_array_zeroed<bool>(scratch_arena, list.render_passes.size);
  state.out_list             = &ret;

  for (RenderPassId ipass = 0; ipass < list.render_passes.size; ipass++)
  {
    if (state.visited[ipass])
      continue;

    dfs_topological_sort_adjacency_list(ipass, &state, list);
  }

  reverse_array(&ret);

  return ret;
}

static Array<RgDependencyLevel>
init_dependency_levels(AllocHeap heap, const RgBuilder& builder)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  AdjacencyList       adjacency_list         = init_adjacency_list(scratch_arena, builder);
  Array<RenderPassId> topological_list       = topological_sort_adjacency_list(scratch_arena, adjacency_list);
  Array<u64>          longest_distances      = init_array_zeroed<u64>(scratch_arena, topological_list.size);

  u64                 dependency_level_count = 1;

  for (RenderPassId pass_id : topological_list)
  {
    for (RenderPassId adjacent_pass_id : adjacency_list.render_passes[pass_id].dependent_passes)
    {
      u64 dist = longest_distances[pass_id] + 1;
      if (longest_distances[adjacent_pass_id] >= dist)
        continue;

      longest_distances[adjacent_pass_id] = dist;
      dependency_level_count              = MAX(dist + 1, dependency_level_count);
    }
  }

  Array<u32> level_pass_counts    = init_array_zeroed<u32>(scratch_arena, dependency_level_count);
  Array<u32> level_barrier_counts = init_array_zeroed<u32>(scratch_arena, dependency_level_count);

  for (RenderPassId pass_id : topological_list)
  {
    u64                  level_index = longest_distances[pass_id];
    const RgPassBuilder& pass        = builder.render_passes[pass_id]; 

    level_pass_counts[level_index]++;

    // TODO(Brandon): This actually is using a _bit_ more memory because a resource can be read
    // multiple times inside of a single level, so if we want to get some small gains we could fix this...
    level_barrier_counts[level_index] += pass.read_resources.size + 2 * pass.write_resources.size;
  }

  Array<RgDependencyLevel> ret = init_array_zeroed<RgDependencyLevel>(heap, dependency_level_count);
  for (u32 ilevel = 0; ilevel < ret.size; ilevel++)
  {
    ret[ilevel].render_passes  = init_array<RenderPassId>(heap, level_pass_counts[ilevel]);
    ret[ilevel].barriers       = init_array<RgResourceBarrier>(heap, level_barrier_counts[ilevel]);
  }

  for (RenderPassId pass_id : topological_list)
  {
    u64                level_index    = longest_distances[pass_id];
    RgDependencyLevel* level          = &ret[level_index];
    *array_add(&level->render_passes) = pass_id;
  }

  return ret;
}

static u32
get_temporal_lifetime(u32 temporal_lifetime)
{
  if (temporal_lifetime == kInfiniteLifetime)
    return 0;
  
  ASSERT(temporal_lifetime < kMaxTemporalLifetime);
  return temporal_lifetime;
}

struct PhysicalResourceMap
{
  HashTable<RgResourceKey, GpuBuffer > buffers;
  HashTable<RgResourceKey, GpuTexture> textures;
};

static PhysicalResourceMap
init_physical_resources(
  AllocHeap heap,
  const RgBuilder& builder,
  const GpuDevice* device,
  GpuLinearAllocator* local_heap,
  GpuLinearAllocator* upload_heaps,
  GpuLinearAllocator* temporal_heaps,
  u32 buffer_count,
  u32 texture_count
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  // Figure out what all of the necessary flags are for physical resource creation
  // based on how each resource is ever used throughout the graph.
  HashTable<u32, D3D12_RESOURCE_FLAGS> physical_flags = init_hash_table<u32, D3D12_RESOURCE_FLAGS>(
    scratch_arena,
    buffer_count + texture_count
  );

  // Initialize all of the flags to none.
  for (ResourceHandle handle : builder.resource_list)
  {
    D3D12_RESOURCE_FLAGS* flags = hash_table_insert(&physical_flags, handle.id);
    *flags                      = D3D12_RESOURCE_FLAG_NONE;
  }

  // Loop through all of the passes and determine how resources are ever accessed and
  // add the needed resource creation flags.
  for (const RgPassBuilder& pass : builder.render_passes)
  {
    for (const RgPassBuilder::ResourceAccessData& read_data : pass.read_resources)
    {
      ResourceHandle handle = read_data.handle;
      D3D12_RESOURCE_FLAGS* flags = hash_table_insert(&physical_flags, handle.id);

      if (handle.type == kResourceTypeTexture)
      {
        ReadTextureAccessMask access = (ReadTextureAccessMask)read_data.access;
        if (access & ReadTextureAccessMask::kReadTextureDepthStencil)
        {
          *flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
      }
    }

    for (const RgPassBuilder::ResourceAccessData& write_data : pass.write_resources)
    {
      ResourceHandle handle = write_data.handle;
      D3D12_RESOURCE_FLAGS* flags = hash_table_insert(&physical_flags, handle.id);

      if (handle.type == kResourceTypeTexture)
      {
        WriteTextureAccess access = (WriteTextureAccess)write_data.access;
        if (access == WriteTextureAccess::kWriteTextureUav)
        {
          *flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        else if (access == WriteTextureAccess::kWriteTextureColorTarget)
        {
          *flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        else if (access == WriteTextureAccess::kWriteTextureDepthStencil)
        {
          *flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
      }
      else if (handle.type == kResourceTypeBuffer)
      {
        WriteBufferAccess access = (WriteBufferAccess)write_data.access;
        if (access == WriteBufferAccess::kWriteBufferUav)
        {
          *flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
      }
    }
  }

  HashTable physical_buffers  = init_hash_table<RgResourceKey, GpuBuffer >(heap, buffer_count );
  HashTable physical_textures = init_hash_table<RgResourceKey, GpuTexture>(heap, texture_count);
  for (ResourceHandle resource : builder.resource_list)
  {
    if (resource.id == builder.back_buffer.id)
      continue;

    RgResourceKey key  = {0};
    key.id             = resource.id;

    TransientResourceDesc* resource_desc = unwrap(hash_table_find(&builder.resource_descs, resource.id));
    if (resource.type == kResourceTypeBuffer)
    {
      GpuBufferDesc desc = {0};
      desc.size          = resource_desc->buffer_desc.size;
      desc.flags         = *unwrap(hash_table_find(&physical_flags, resource.id));
      desc.initial_state = D3D12_RESOURCE_STATE_COMMON;

      if (resource_desc->temporal_lifetime > 0 && resource_desc->temporal_lifetime < kInfiniteLifetime)
      {
        GpuLinearAllocator* allocator = resource_desc->buffer_desc.heap_type == kGpuHeapTypeLocal ? temporal_heaps : upload_heaps;

        ASSERT(resource_desc->temporal_lifetime <= kMaxTemporalLifetime);
        for (u32 iframe = 0; iframe <= resource_desc->temporal_lifetime; iframe++)
        {
          key.temporal_frame = iframe;
          GpuBuffer* dst     = hash_table_insert(&physical_buffers, key);
    
          *dst               = alloc_gpu_buffer(device, allocator + iframe, desc, resource_desc->name);
        }
      }
      else
      {
        key.temporal_frame = 0;
        GpuBuffer*    dst  = hash_table_insert(&physical_buffers, key);
        *dst               = alloc_gpu_buffer(device, local_heap, desc, resource_desc->name);
      }
    }
    else if (resource.type == kResourceTypeTexture)
    {
      GpuTextureDesc desc = {0};
      desc.width          = resource_desc->texture_desc.width;
      desc.height         = resource_desc->texture_desc.height;
      desc.array_size     = 1;
      desc.format         = resource_desc->texture_desc.format;
      desc.initial_state  = D3D12_RESOURCE_STATE_COMMON;
      desc.flags          = *unwrap(hash_table_find(&physical_flags, resource.id));

      if (is_depth_format(desc.format))
      {
        desc.depth_clear_value   = resource_desc->texture_desc.depth_clear_value;
        desc.stencil_clear_value = resource_desc->texture_desc.stencil_clear_value;
      }
      else
      {
        desc.color_clear_value   = resource_desc->texture_desc.color_clear_value;
      }

      if (resource_desc->temporal_lifetime > 0 && resource_desc->temporal_lifetime < kInfiniteLifetime)
      {
        ASSERT(resource_desc->temporal_lifetime <= kMaxTemporalLifetime);
        for (u32 iframe = 0; iframe <= resource_desc->temporal_lifetime; iframe++)
        {
          key.temporal_frame = iframe;
          GpuTexture* dst    = hash_table_insert(&physical_textures, key);
    
          *dst               = alloc_gpu_texture(device, temporal_heaps + iframe, desc, resource_desc->name);
        }
      }
      else
      {
        key.temporal_frame = 0;
        GpuTexture* dst    = hash_table_insert(&physical_textures, key);
  
        *dst               = alloc_gpu_texture(device, local_heap, desc, resource_desc->name);
      }
    } else { UNREACHABLE; }
  }

  PhysicalResourceMap ret = {0};
  ret.buffers  = physical_buffers;
  ret.textures = physical_textures;

  return ret;
}

struct PhysicalDescriptorMap
{
  HashTable<RgDescriptorKey, Descriptor> descriptor_map;
  RgDescriptorHeap                       descriptor_heap;
};

static PhysicalDescriptorMap
init_physical_descriptors(
  AllocHeap heap,
  const RgBuilder& builder,
  const GpuDevice* device,
  const PhysicalResourceMap& resource_map
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  HashTable<u32, DescriptorType> descriptor_types  = init_hash_table<u32, DescriptorType>(scratch_arena, builder.resource_list.size);

  for (const RgPassBuilder& pass : builder.render_passes)
  {
    for (const RgPassBuilder::ResourceAccessData& data : pass.read_resources)
    {
      u8* type_mask = (u8*)hash_table_insert(&descriptor_types, data.handle.id);
      if (data.handle.type == kResourceTypeBuffer)
      {
        ReadBufferAccessMask access = (ReadBufferAccessMask)data.access;
        if (access & kReadBufferCbv)
        {
          *type_mask |= kDescriptorTypeCbv;
        }

        if (access & kReadBufferSrvPixelShader || access & kReadBufferSrvNonPixelShader)
        {
          *type_mask |= kDescriptorTypeSrv;
        }
      }
      else if (data.handle.type == kResourceTypeTexture)
      {
        *type_mask |= kDescriptorTypeSrv;
      }
    }

    for (const RgPassBuilder::ResourceAccessData& data : pass.write_resources)
    {
      u8* type_mask = (u8*)hash_table_insert(&descriptor_types, data.handle.id);
      if (data.handle.type == kResourceTypeBuffer)
      {
        WriteBufferAccess access = (WriteBufferAccess)data.access;
        if (access == kWriteBufferUav)
        {
          *type_mask |= kDescriptorTypeUav;
        }
      }
      else if (data.handle.type == kResourceTypeTexture)
      {
        WriteTextureAccess access = (WriteTextureAccess)data.access;
        if (access == kWriteTextureDepthStencil)
        {
          *type_mask |= kDescriptorTypeDsv;
        }
        else if (access == kWriteTextureColorTarget)
        {
          *type_mask |= kDescriptorTypeRtv;
        }
        else if (access == kWriteTextureUav)
        {
          *type_mask |= kDescriptorTypeUav;
        }
      }
    }
  }

  u32 descriptor_count  = 0;
  u32 cbv_srv_uav_count = 0;
  u32 rtv_count         = 0;
  u32 dsv_count         = 0;
  for (const ResourceHandle handle : builder.resource_list)
  {
    auto maybe_type_mask = hash_table_find(&descriptor_types, handle.id);
    if (!maybe_type_mask)
      continue;

    DescriptorType type_mask = *unwrap(maybe_type_mask);

    if (type_mask & kDescriptorTypeCbv)
    {
      descriptor_count  += get_temporal_lifetime(handle.temporal_lifetime) + 1;
      cbv_srv_uav_count += get_temporal_lifetime(handle.temporal_lifetime) + 1;
    }

    if (type_mask & kDescriptorTypeSrv)
    {
      descriptor_count  += get_temporal_lifetime(handle.temporal_lifetime) + 1;
      cbv_srv_uav_count += get_temporal_lifetime(handle.temporal_lifetime) + 1;
    }

    if (type_mask & kDescriptorTypeUav)
    {
      descriptor_count  += get_temporal_lifetime(handle.temporal_lifetime) + 1;
      cbv_srv_uav_count += get_temporal_lifetime(handle.temporal_lifetime) + 1;
    }

    if (type_mask & kDescriptorTypeRtv)
    {
      descriptor_count  += get_temporal_lifetime(handle.temporal_lifetime) + 1;
      rtv_count         += get_temporal_lifetime(handle.temporal_lifetime) + 1;
    }

    if (type_mask & kDescriptorTypeDsv)
    {
      descriptor_count  += get_temporal_lifetime(handle.temporal_lifetime) + 1;
      dsv_count         += get_temporal_lifetime(handle.temporal_lifetime) + 1;
    }
  }

  HashTable<RgDescriptorKey, Descriptor> descriptor_map = init_hash_table<RgDescriptorKey, Descriptor>(heap, descriptor_count);

  RgDescriptorHeap descriptor_heap = {0};
  descriptor_heap.cbv_srv_uav      = init_descriptor_linear_allocator(device, MAX(cbv_srv_uav_count, 128), kDescriptorHeapTypeCbvSrvUav);
  descriptor_heap.rtv              = init_descriptor_linear_allocator(device, MAX(rtv_count, 128),         kDescriptorHeapTypeRtv);
  descriptor_heap.dsv              = init_descriptor_linear_allocator(device, MAX(dsv_count, 128),         kDescriptorHeapTypeDsv);

  for (const ResourceHandle handle : builder.resource_list)
  {
    auto maybe_type_mask = hash_table_find(&descriptor_types, handle.id);
    if (!maybe_type_mask)
      continue;

    DescriptorType type_mask = *unwrap(hash_table_find(&descriptor_types, handle.id));

    RgDescriptorKey key          = {0};
    key.id                       = handle.id;
    key.temporal_frame           = 0;

    RgResourceKey   resource_key = {0};
    resource_key.id              = handle.id;

    for (u32 iframe = 0; iframe <= get_temporal_lifetime(handle.temporal_lifetime); iframe++)
    {
      resource_key.temporal_frame = iframe;
      key.temporal_frame          = iframe;

      if (type_mask & kDescriptorTypeCbv)
      {
        ASSERT(handle.type == kResourceTypeBuffer);
        const GpuBuffer* buffer = unwrap(hash_table_find(&resource_map.buffers, resource_key));
        key.type                = kDescriptorTypeCbv;
  
        Descriptor* dst         = hash_table_insert(&descriptor_map, key);
  
        *dst                    = alloc_descriptor(&descriptor_heap.cbv_srv_uav);
        init_buffer_cbv(device, dst, buffer, 0, buffer->desc.size);
      }
  
      if (type_mask & kDescriptorTypeSrv)
      {
        key.type        = kDescriptorTypeSrv;
        Descriptor* dst = hash_table_insert(&descriptor_map, key);
        *dst            = alloc_descriptor(&descriptor_heap.cbv_srv_uav);
  
        if (handle.type == kResourceTypeBuffer)
        {
          const GpuBuffer* buffer     = unwrap(hash_table_find(&resource_map.buffers, resource_key));
          TransientResourceDesc* desc = unwrap(hash_table_find(&builder.resource_descs, handle.id));
          init_buffer_srv(
            device,
            dst,
            buffer,
            0,
            desc->buffer_desc.size / desc->buffer_desc.stride,
            desc->buffer_desc.stride
          );
        }
        else if (handle.type == kResourceTypeTexture)
        {
          // You can't use the back buffer as an SRV
          ASSERT(handle.id != builder.back_buffer.id);
  
          const GpuTexture* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
          init_texture_srv(device, dst, texture);
        } else { UNREACHABLE; }
      }
  
      if (type_mask & kDescriptorTypeUav)
      {
        key.type        = kDescriptorTypeUav;
        Descriptor* dst = hash_table_insert(&descriptor_map, key);
        *dst            = alloc_descriptor(&descriptor_heap.cbv_srv_uav);
  
        if (handle.type == kResourceTypeBuffer)
        {
          const GpuBuffer*       buffer = unwrap(hash_table_find(&resource_map.buffers, resource_key));
          TransientResourceDesc* desc   = unwrap(hash_table_find(&builder.resource_descs, handle.id));
          init_buffer_uav(
            device,
            dst,
            buffer,
            0,
            desc->buffer_desc.size / desc->buffer_desc.stride,
            desc->buffer_desc.stride
          );
        }
        else if (handle.type == kResourceTypeTexture)
        {
          // You can't use the back buffer as a UAV
          ASSERT(handle.id != builder.back_buffer.id);
  
          const GpuTexture* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
          init_texture_uav(device, dst, texture);
        } else { UNREACHABLE; }
      }
  
      if (type_mask & kDescriptorHeapTypeRtv)
      {
        ASSERT(handle.type == kResourceTypeTexture);
  
        key.type                = kDescriptorTypeRtv;
        Descriptor*     dst     = hash_table_insert(&descriptor_map, key);
        *dst                    = alloc_descriptor(&descriptor_heap.rtv);
        if (handle.id != builder.back_buffer.id)
        {
          const GpuTexture* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
  
          init_rtv(device, dst, texture);
        }
      }
  
      if (type_mask & kDescriptorHeapTypeDsv)
      {
        ASSERT(handle.type == kResourceTypeTexture);
  
        key.type                = kDescriptorTypeDsv;
        Descriptor*     dst     = hash_table_insert(&descriptor_map, key);
        *dst                    = alloc_descriptor(&descriptor_heap.dsv);
        if (handle.id != builder.back_buffer.id)
        {
          const GpuTexture* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
          init_dsv(device, dst, texture);
        }
      }
    }
  }

  PhysicalDescriptorMap ret = {0};
  ret.descriptor_map        = descriptor_map;
  ret.descriptor_heap       = descriptor_heap;
  

  return ret;
}

static D3D12_RESOURCE_STATES
get_d3d12_resource_state(RgPassBuilder::ResourceAccessData data)
{
  D3D12_RESOURCE_STATES ret = D3D12_RESOURCE_STATE_COMMON;
  if (data.handle.type == kResourceTypeBuffer)
  {
    if (!data.is_write)
    {
      ReadBufferAccessMask access = (ReadBufferAccessMask)data.access;
      if (access & kReadBufferVertex || access & kReadBufferCbv)
      {
        ret |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
      }

      if (access & kReadBufferIndex)
      {
        ret |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
      }

      if (access & kReadBufferIndirectArgs)
      {
        ret |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
      }

      if (access & kReadBufferSrvPixelShader)
      {
        ret |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      }

      if (access & kReadBufferSrvNonPixelShader)
      {
        ret |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      }

      if (access & kReadBufferCopySrc)
      {
        ret |= D3D12_RESOURCE_STATE_COPY_SOURCE;
      }
    }
    else
    {
      WriteBufferAccess access = (WriteBufferAccess)data.access;
      if (access == kWriteBufferUav)
      {
        ret = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      } else { UNREACHABLE; }
    }
  }
  else if (data.handle.type == kResourceTypeTexture)
  {
    if (!data.is_write)
    {
      ReadTextureAccessMask access = (ReadTextureAccessMask)data.access;
      if (access & kReadTextureDepthStencil)
      {
        ret |= D3D12_RESOURCE_STATE_DEPTH_READ;
      }

      if (access & kReadTextureSrvPixelShader)
      {
        ret |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      }

      if (access & kReadTextureSrvNonPixelShader)
      {
        ret |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      }

      if (access & kReadTextureCopySrc)
      {
        ret |= D3D12_RESOURCE_STATE_COPY_SOURCE;
      }
    }
    else
    {
      WriteTextureAccess access = (WriteTextureAccess)data.access;
      if (access == kWriteTextureDepthStencil)
      {
        ret = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      }
      else if (access == kWriteTextureColorTarget)
      {
        ret = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      else if (access == kWriteTextureUav)
      {
        ret = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      }
      else if (access == kWriteTextureCopyDst)
      {
        ret = D3D12_RESOURCE_STATE_COPY_DEST;
      } else { UNREACHABLE; }
    }
  } else { UNREACHABLE; }

  return ret;
}


static void
init_dependency_barriers(
  AllocHeap heap,
  const RgBuilder& builder,
  PhysicalResourceMap resource_map,
  Array<RgDependencyLevel> out_dependency_levels,
  Array<RgResourceBarrier>* out_exit_barriers
) {
  *out_exit_barriers = init_array<RgResourceBarrier>(heap, builder.resource_list.size * kMaxTemporalLifetime);

  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  struct ResourceStates
  {
    D3D12_RESOURCE_STATES current = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES history = D3D12_RESOURCE_STATE_COMMON;
    bool                  touched = false;
  };

  struct RgTemporalResourceKey
  {
    u32 id             = 0;
    s32 temporal_frame = 0;
    auto operator<=>(const RgTemporalResourceKey& rhs) const = default;
  };

  HashTable resource_states = init_hash_table<RgTemporalResourceKey, ResourceStates>(scratch_arena, builder.resource_list.size * kMaxTemporalLifetime);
  for (ResourceHandle handle : builder.resource_list)
  {
    s32 temporal_frame = 0;
    for (u32 i = 0; i <= get_temporal_lifetime(handle.temporal_lifetime); i++, temporal_frame--)
    {
      RgTemporalResourceKey key = {0};
      key.id                    = handle.id;
      key.temporal_frame        = temporal_frame;

      ResourceStates* dst = hash_table_insert(&resource_states, key);
      dst->current        = D3D12_RESOURCE_STATE_COMMON;
      dst->history        = D3D12_RESOURCE_STATE_COMMON;
      dst->touched        = false;
    }
  }


  for (u32 ilevel = 0; ilevel < out_dependency_levels.size; ilevel++)
  {
    RgDependencyLevel* level = &out_dependency_levels[ilevel];

    for (RenderPassId pass_id : level->render_passes)
    {
      const RgPassBuilder& pass = builder.render_passes[pass_id];
      for (RgPassBuilder::ResourceAccessData data : pass.read_resources)
      {
        RgTemporalResourceKey key = {0};
        key.id                    = data.handle.id;
        key.temporal_frame        = data.temporal_frame;

        ResourceStates* states    = unwrap(hash_table_find(&resource_states, key));

        if (!states->touched)
        {
          states->current = D3D12_RESOURCE_STATE_COMMON;
        }

        states->current |= get_d3d12_resource_state(data);
        states->touched  = true;
      }

      for (RgPassBuilder::ResourceAccessData data : pass.write_resources)
      {
        RgTemporalResourceKey key = {0};
        key.id                    = data.handle.id;
        key.temporal_frame        = 0;

        ResourceStates* states    = unwrap(hash_table_find(&resource_states, key));
        states->current           = get_d3d12_resource_state(data);
        states->touched           = true;
      }
    }

    for (ResourceHandle handle : builder.resource_list)
    {
      s32 temporal_frame = 0;
      for (u32 i = 0; i <= get_temporal_lifetime(handle.temporal_lifetime); i++, temporal_frame--)
      {
        RgTemporalResourceKey key = {0};
        key.id                    = handle.id;
        key.temporal_frame        = temporal_frame;

        ResourceStates* states    = unwrap(hash_table_find(&resource_states, key));
  
        if (states->history != states->current)
        {
          RgResourceBarrier* dst                     = array_add(&level->barriers);
          dst->type                                  = kResourceBarrierTransition;
          dst->transition.before                     = states->history;
          dst->transition.after                      = states->current;
          dst->transition.resource_id                = handle.id;
          dst->transition.resource_type              = handle.type;
          dst->transition.resource_temporal_frame    = temporal_frame;
          dst->transition.resource_temporal_lifetime = handle.temporal_lifetime;
        }
  
        if (states->current == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
          ASSERT(temporal_frame == 0);
          RgResourceBarrier* dst = array_add(&level->barriers);
          dst->type              = kResourceBarrierUav;
          dst->uav.resource_id   = handle.id;
          dst->uav.resource_type = handle.type;
        }
  
        states->history = states->current;
        states->touched = false;
      }
    }
  }

  for (ResourceHandle handle : builder.resource_list)
  {
    s32 temporal_frame = 0;
    for (u32 i = 0; i <= get_temporal_lifetime(handle.temporal_lifetime); i++, temporal_frame--)
    {
      RgTemporalResourceKey key = {0};
      key.id                    = handle.id;
      key.temporal_frame        = temporal_frame;

      ResourceStates* states    = unwrap(hash_table_find(&resource_states, key));
      if (states->history == D3D12_RESOURCE_STATE_COMMON)
        continue;
      
      RgResourceBarrier* dst                     = array_add(out_exit_barriers);
      dst->type                                  = kResourceBarrierTransition;
      dst->transition.before                     = states->history;
      dst->transition.after                      = D3D12_RESOURCE_STATE_COMMON;
      dst->transition.resource_id                = handle.id;
      dst->transition.resource_type              = handle.type;
      dst->transition.resource_temporal_frame    = temporal_frame;
      dst->transition.resource_temporal_lifetime = handle.temporal_lifetime;
    }
  }
}

void
compile_render_graph(AllocHeap heap, const RgBuilder& builder, const GpuDevice* device, RenderGraph* out, RenderGraphDestroyFlags flags)
{
  // You must write to the back buffer exactly once, never more.
  ASSERT(builder.back_buffer.version == 1);

  if (flags & kRgDestroyCmdListAllocators)
  {
    out->cmd_allocator     = init_cmd_list_allocator(heap, device, &device->graphics_queue, 4);
  }

  out->render_passes     = init_array<RenderPass>(heap, builder.render_passes.size);
  out->dependency_levels = init_dependency_levels(heap, builder);
  out->back_buffer       = builder.back_buffer;
  out->frame_id          = 0;
  out->width             = builder.width;
  out->height            = builder.height;

  for (const RgPassBuilder& pass_builder : builder.render_passes)
  {
    RenderPass* dst     = array_add(&out->render_passes);
    dst->handler        = pass_builder.handler;
    dst->data           = pass_builder.data;
    dst->name           = pass_builder.name;
  }

  if (flags & kRgDestroyResourceHeaps)
  {
    out->local_heap        = init_gpu_linear_allocator(device, MiB(700), kGpuHeapTypeLocal);
    for (u32 i = 0; i < kFramesInFlight; i++)
    {
      out->upload_heaps[i] = init_gpu_linear_allocator(device, MiB(4), kGpuHeapTypeUpload);
    }

    out->temporal_heaps = init_array<GpuLinearAllocator>(heap, kMaxTemporalLifetime);
    for (u8 i = 0; i < kMaxTemporalLifetime; i++)
    {
      GpuLinearAllocator* dst = array_add(&out->temporal_heaps);
      *dst                    = init_gpu_linear_allocator(device, MiB(128), kGpuHeapTypeLocal);
    }
  }

  u32 buffer_count          = 0;
  u32 texture_count         = 0;
  for (ResourceHandle resource : builder.resource_list)
  {
    if (resource.id == builder.back_buffer.id)
      continue;

    TransientResourceDesc* desc = unwrap(hash_table_find(&builder.resource_descs, resource.id));

    if      (resource.type == kResourceTypeBuffer)  { buffer_count++;  }
    else if (resource.type == kResourceTypeTexture) { texture_count++; }
    else                                            { UNREACHABLE;     }
  }

  if (flags & kRgFreePhysicalResources)
  {
    PhysicalResourceMap physical_resource_map = init_physical_resources(
      heap,
      builder,
      device,
      &out->local_heap,
      out->upload_heaps,
      out->temporal_heaps.memory,
      buffer_count,
      texture_count
    );

    init_dependency_barriers(heap, builder, physical_resource_map, out->dependency_levels, &out->exit_barriers);

    PhysicalDescriptorMap physical_descriptor_map = init_physical_descriptors(
      heap,
      builder,
      device,
      physical_resource_map
    );

    out->descriptor_heap = physical_descriptor_map.descriptor_heap;
    out->descriptor_map  = physical_descriptor_map.descriptor_map;
    out->buffer_map      = physical_resource_map.buffers;
    out->texture_map     = physical_resource_map.textures;
  }
}

void
destroy_render_graph(RenderGraph* graph, RenderGraphDestroyFlags flags)
{
  if (flags & kRgFreePhysicalResources)
  {
    for (auto [_, buffer] : graph->buffer_map)
    {
      free_gpu_buffer(&buffer);
    }

    for (auto [key, texture] : graph->texture_map)
    {
      if (key.id == graph->back_buffer.id)
      {
        continue;
      }
      free_gpu_texture(&texture);
    }
  }

  if (flags & kRgDestroyResourceHeaps)
  {
    destroy_gpu_linear_allocator(&graph->local_heap);
    for (GpuLinearAllocator& upload_heap : graph->upload_heaps)
    {
      destroy_gpu_linear_allocator(&upload_heap);
    }

    for (GpuLinearAllocator& temporal_heap : graph->temporal_heaps)
    {
      destroy_gpu_linear_allocator(&temporal_heap);
    }
  }
  else
  {
    reset_gpu_linear_allocator(&graph->local_heap);
    for (GpuLinearAllocator& upload_heap : graph->upload_heaps)
    {
      reset_gpu_linear_allocator(&upload_heap);
    }

    for (GpuLinearAllocator& temporal_heap : graph->temporal_heaps)
    {
      reset_gpu_linear_allocator(&temporal_heap);
    }
  }

  destroy_descriptor_linear_allocator(&graph->descriptor_heap.cbv_srv_uav);
  destroy_descriptor_linear_allocator(&graph->descriptor_heap.rtv);
  destroy_descriptor_linear_allocator(&graph->descriptor_heap.dsv);

  if (flags & kRgDestroyCmdListAllocators)
  {
    destroy_cmd_list_allocator(&graph->cmd_allocator);
  }

  if (flags == kRgDestroyAll)
  {
    zero_memory(graph, sizeof(RenderGraph));
  }
}

static u32
get_temporal_frame(u32 frame_id, u32 temporal_lifetime, s8 offset)
{
  if (temporal_lifetime == kInfiniteLifetime)
  {
    ASSERT(offset == 0);
    return 0;
  }

  ASSERT(offset <= 0);
  ASSERT(-offset <= temporal_lifetime);

  if (temporal_lifetime == 0)
    return 0;
  
  temporal_lifetime++;
  ASSERT(temporal_lifetime >= 2);

  s32 signed_frame_id = (u32)frame_id;
  signed_frame_id    += offset;

  s64 ret = modulo(signed_frame_id, temporal_lifetime);
  ASSERT(ret >= 0);
  ASSERT((u32)ret < temporal_lifetime);
  return (u32)ret;
}

static void
get_d3d12_resource_barrier(const RenderGraph* graph, const RgResourceBarrier& barrier, CD3DX12_RESOURCE_BARRIER* dst)
{
  switch(barrier.type)
  {
    case kResourceBarrierTransition:
    {
      RgResourceKey key  = {0};
      key.id             = barrier.transition.resource_id;
      key.temporal_frame = get_temporal_frame(graph->frame_id, barrier.transition.resource_temporal_lifetime, barrier.transition.resource_temporal_frame);

      ID3D12Resource* d3d12_resource = nullptr;
      if (barrier.transition.resource_type == kResourceTypeTexture)
      {
        GpuTexture* texture = unwrap(hash_table_find(&graph->texture_map, key));
        d3d12_resource      = texture->d3d12_texture;
      }
      else if (barrier.transition.resource_type == kResourceTypeBuffer)
      {
        GpuBuffer* buffer = unwrap(hash_table_find(&graph->buffer_map, key));
        d3d12_resource    = buffer->d3d12_buffer;
      } else { UNREACHABLE; }

      *dst = CD3DX12_RESOURCE_BARRIER::Transition(d3d12_resource, barrier.transition.before, barrier.transition.after);
    } break;
    case kResourceBarrierAliasing:
    {
      // TODO(Brandon): Implement resource aliasing
      UNREACHABLE;
    } break;
    case kResourceBarrierUav:
    {
      RgResourceKey key  = {0};
      key.id             = barrier.uav.resource_id;
      key.temporal_frame = 0;

      ID3D12Resource* d3d12_resource = nullptr;
      if (barrier.uav.resource_type == kResourceTypeTexture)
      {
        GpuTexture* texture = unwrap(hash_table_find(&graph->texture_map, key));
        d3d12_resource      = texture->d3d12_texture;
      }
      else if (barrier.uav.resource_type == kResourceTypeBuffer)
      {
        GpuBuffer* buffer = unwrap(hash_table_find(&graph->buffer_map, key));
        d3d12_resource    = buffer->d3d12_buffer;
      } else { UNREACHABLE; }

      *dst = CD3DX12_RESOURCE_BARRIER::UAV(d3d12_resource);
    } break;
    default: UNREACHABLE;
  }
}

static void
lazy_init_back_buffer_descriptors(
  RenderGraph* graph,
  const GpuDevice* device,
  RgHandle<GpuTexture> handle,
  const GpuTexture* texture,
  DescriptorType type
) {
  RgDescriptorKey key = {0};
  key.id   = handle.id;
  key.type = type;

  Descriptor* dst = unwrap_or(hash_table_find(&graph->descriptor_map, key), nullptr);
  if (dst == nullptr)
    return;

  switch (type)
  {
    case kDescriptorTypeRtv: init_rtv(device, dst, texture); break;
    case kDescriptorTypeDsv: init_dsv(device, dst, texture); break;
    default: UNREACHABLE;
  }
}

void 
execute_render_graph(RenderGraph* graph, const GpuDevice* device, const GpuTexture* back_buffer, u32 frame_index)
{
  // NOTE(Brandon): This is honestly kinda dangerous, since we're just straight up copying the back buffer struct instead
  // of the pointer to the GpuImage which is what I really want... maybe there's a nicer way of doing this?
  *hash_table_insert(&graph->texture_map, {graph->back_buffer.id, 0}) = *back_buffer;

  // Initialize all of the back buffer descriptors
  lazy_init_back_buffer_descriptors(graph, device, graph->back_buffer, back_buffer, kDescriptorTypeRtv);
  lazy_init_back_buffer_descriptors(graph, device, graph->back_buffer, back_buffer, kDescriptorTypeDsv);

  CmdList       cmd_buffer = alloc_cmd_list(&graph->cmd_allocator);
  RenderContext ctx        = {0};
  ctx.m_Graph              = graph;
  ctx.m_Device             = device;
  ctx.m_CmdBuffer          = cmd_buffer;
  ctx.m_Width              = graph->width;
  ctx.m_Height             = graph->height;

  // Main render loop
  for (u32 ilevel = 0; ilevel < graph->dependency_levels.size; ilevel++)
  {
    RG_DBGLN("Level %u", ilevel);
    const RgDependencyLevel& level = graph->dependency_levels[ilevel];
    if (level.barriers.size > 0)
    {
      ScratchAllocator scratch_arena = alloc_scratch_arena();
      defer { free_scratch_arena(&scratch_arena); };

      Array d3d12_barriers = init_array<CD3DX12_RESOURCE_BARRIER>(scratch_arena, level.barriers.size);
  
      for (const RgResourceBarrier& barrier : level.barriers)
      {
        CD3DX12_RESOURCE_BARRIER* dst = array_add(&d3d12_barriers);
        get_d3d12_resource_barrier(graph, barrier, dst);
      }

      cmd_buffer.d3d12_list->ResourceBarrier(d3d12_barriers.size, d3d12_barriers.memory);
    }

    for (RenderPassId pass_id : level.render_passes)
    {
      RG_DBGLN("%s", graph->render_passes[pass_id].name);
      set_graphics_root_signature(&cmd_buffer);
      set_compute_root_signature(&cmd_buffer);
      set_descriptor_heaps(&cmd_buffer, {&graph->descriptor_heap.cbv_srv_uav});

      const RenderPass& pass = graph->render_passes[pass_id];
      (*pass.handler)(&ctx, pass.data);

      set_descriptor_heaps(&cmd_buffer, {&graph->descriptor_heap.cbv_srv_uav});
    }
  }

  // Execute all of the exit barriers
  if (graph->exit_barriers.size > 0)
  {
    ScratchAllocator scratch_arena = alloc_scratch_arena();
    defer { free_scratch_arena(&scratch_arena); };

    Array d3d12_barriers = init_array<CD3DX12_RESOURCE_BARRIER>(scratch_arena, graph->exit_barriers.size);

    for (const RgResourceBarrier& barrier : graph->exit_barriers)
    {
      CD3DX12_RESOURCE_BARRIER* dst = array_add(&d3d12_barriers);
      get_d3d12_resource_barrier(graph, barrier, dst);
    }

    cmd_buffer.d3d12_list->ResourceBarrier(d3d12_barriers.size, d3d12_barriers.memory);
  }

  submit_cmd_lists(&graph->cmd_allocator, {cmd_buffer});
  graph->frame_id++;
}

RgPassBuilder*
add_render_pass(
  AllocHeap heap,
  RgBuilder* graph,
  CmdQueueType queue,
  const char* name,
  void* data,
  RenderHandler* handler,
  u32 num_read_resources,
  u32 num_write_resources,
  bool is_frame_init
) {
  RgPassBuilder* ret   = array_add(&graph->render_passes);

  ret->pass_id         = (u32)graph->render_passes.size - 1;
  ret->queue           = queue;
  ret->name            = name;
  ret->handler         = handler;
  ret->data            = data;

  ret->read_resources  = init_array<RgPassBuilder::ResourceAccessData>(heap, num_read_resources);
  ret->write_resources = init_array<RgPassBuilder::ResourceAccessData>(heap, num_write_resources);

  if (is_frame_init)
  {
    ASSERT(!graph->frame_init);
    graph->frame_init = ret->pass_id;
  }

  return ret;
}

RgHandle<GpuTexture>
rg_create_texture(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format
) {
  return rg_create_texture_ex(builder, name, width, height, format, 0);
}

RgHandle<GpuTexture>
rg_create_texture_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format,
  u8 temporal_lifetime
) {
  return rg_create_texture_array_ex(builder, name, width, height, 1, format, temporal_lifetime);
}

RgHandle<GpuTexture>
rg_create_texture_array(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u32 array_size,
  DXGI_FORMAT format
) {
  return rg_create_texture_array_ex(builder, name, width, height, array_size, format, 0);
}

RgHandle<GpuTexture>
rg_create_texture_array_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u32 array_size,
  DXGI_FORMAT format,
  u8 temporal_lifetime
) {
  ASSERT(temporal_lifetime == kInfiniteLifetime || temporal_lifetime <= kMaxTemporalLifetime);
  ResourceHandle resource_handle      = {0};
  resource_handle.id                  = handle_index(builder);
  resource_handle.version             = 0;
  resource_handle.type                = kResourceTypeTexture;
  resource_handle.temporal_lifetime   = temporal_lifetime;
  *array_add(&builder->resource_list) = resource_handle;

  TransientResourceDesc* desc         = hash_table_insert(&builder->resource_descs, resource_handle.id);
  desc->name                          = name;
  desc->type                          = resource_handle.type;
  desc->temporal_lifetime             = resource_handle.temporal_lifetime;
  desc->texture_desc.width            = width;
  desc->texture_desc.height           = height;
  desc->texture_desc.array_size       = array_size;
  desc->texture_desc.format           = format;

  // TODO(Brandon): We don't want to hard-code these values
  if (is_depth_format(format))
  {
    desc->texture_desc.stencil_clear_value = 0;
    desc->texture_desc.depth_clear_value   = 0.0f;
  }
  else
  {
    desc->texture_desc.color_clear_value   = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
  }

  RgHandle<GpuTexture> ret = {resource_handle.id, resource_handle.version, resource_handle.temporal_lifetime};
  return ret;
}

RgHandle<GpuBuffer>
rg_create_buffer(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride
) {
  return rg_create_buffer_ex(builder, name, size, stride, 0);
}

RgHandle<GpuBuffer>
rg_create_upload_buffer(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride
) {
  ResourceHandle resource_handle      = {0};
  resource_handle.id                  = handle_index(builder);
  resource_handle.version             = 0;
  resource_handle.type                = kResourceTypeBuffer;
  resource_handle.temporal_lifetime   = kFramesInFlight - 1;
  *array_add(&builder->resource_list) = resource_handle;

  TransientResourceDesc* desc         = hash_table_insert(&builder->resource_descs, resource_handle.id);
  desc->name                          = name;
  desc->type                          = resource_handle.type;
  desc->temporal_lifetime             = resource_handle.temporal_lifetime;
  desc->buffer_desc.size              = size;
  desc->buffer_desc.stride            = stride == 0 ? size : stride;
  desc->buffer_desc.heap_type         = kGpuHeapTypeUpload;

  RgHandle<GpuBuffer> ret = {resource_handle.id, resource_handle.version, resource_handle.temporal_lifetime};
  return ret;
}

RgHandle<GpuBuffer>
rg_create_buffer_ex(
  RgBuilder* builder,
  const char* name,
  u64 size,
  u32 stride,
  u8 temporal_lifetime
) {
  ASSERT(temporal_lifetime == kInfiniteLifetime || temporal_lifetime <= kMaxTemporalLifetime);
  ResourceHandle resource_handle      = {0};
  resource_handle.id                  = handle_index(builder);
  resource_handle.version             = 0;
  resource_handle.type                = kResourceTypeBuffer;
  resource_handle.temporal_lifetime   = temporal_lifetime;
  *array_add(&builder->resource_list) = resource_handle;

  TransientResourceDesc* desc         = hash_table_insert(&builder->resource_descs, resource_handle.id);
  desc->name                          = name;
  desc->type                          = resource_handle.type;
  desc->temporal_lifetime             = resource_handle.temporal_lifetime;
  desc->buffer_desc.size              = size;
  desc->buffer_desc.stride            = stride;
  desc->buffer_desc.heap_type         = kGpuHeapTypeLocal;

  RgHandle<GpuBuffer> ret = {resource_handle.id, resource_handle.version, resource_handle.temporal_lifetime};
  return ret;
}


RgReadHandle<GpuTexture>
rg_read_texture(RgPassBuilder* builder, RgHandle<GpuTexture> texture, ReadTextureAccessMask access, s8 temporal_frame)
{
  ASSERT(temporal_frame <= 0);
  ASSERT(-temporal_frame <= texture.temporal_lifetime);
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture.id && it->temporal_frame == temporal_frame));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture.id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle         = texture;
  data->access         = (u32)access;
  data->temporal_frame = temporal_frame;
  data->is_write       = false;

  return {texture.id, (u16)texture.temporal_lifetime, data->temporal_frame};
}

RgWriteHandle<GpuTexture>
rg_write_texture(RgPassBuilder* builder, RgHandle<GpuTexture>* texture, WriteTextureAccess access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture->id && it->temporal_frame == 0));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture->id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle         = *texture;
  data->access         = (u32)access;
  data->temporal_frame = 0;
  data->is_write       = true;

  texture->version++;

  return {texture->id, (u16)texture->temporal_lifetime, data->temporal_frame};
}

RgReadHandle<GpuBuffer>
rg_read_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, ReadBufferAccessMask access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == buffer.id));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer.id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle         = buffer;
  data->access         = (u32)access;
  data->temporal_frame = 0;
  data->is_write       = false;

  return {buffer.id, (u16)buffer.temporal_lifetime, data->temporal_frame};
}

RgWriteHandle<GpuBuffer>
rg_write_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, WriteBufferAccess access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == buffer->id));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer->id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle         = *buffer;
  data->access         = (u32)access;
  data->temporal_frame = 0;
  data->is_write       = true;

  buffer->version++;

  return {buffer->id, (u16)buffer->temporal_lifetime, data->temporal_frame};
}

static const GpuTexture*
deref_resource(const RenderGraph* graph, RgWriteHandle<GpuTexture> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime, handle.temporal_frame);

  return unwrap(hash_table_find(&graph->texture_map, key));
}

static const GpuTexture*
deref_resource(const RenderGraph* graph, RgReadHandle<GpuTexture> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime, handle.temporal_frame);

  return unwrap(hash_table_find(&graph->texture_map, key));
}

static const GpuBuffer*
deref_resource(const RenderGraph* graph, RgWriteHandle<GpuBuffer> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime, handle.temporal_frame);

  return unwrap(hash_table_find(&graph->buffer_map, key));
}

static const GpuBuffer*
deref_resource(const RenderGraph* graph, RgReadHandle<GpuBuffer> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime, handle.temporal_frame);

  return unwrap(hash_table_find(&graph->buffer_map, key));
}


static const Descriptor*
deref_descriptor(const RenderGraph* graph, u32 handle_id, u32 temporal_lifetime, s8 temporal_frame, DescriptorType type)
{
  RgDescriptorKey key = {0};
  key.id              = handle_id;
  key.type            = type;
  key.temporal_frame  = get_temporal_frame(graph->frame_id, temporal_lifetime, temporal_frame);
  return unwrap(hash_table_find(&graph->descriptor_map, key));
}

template <typename T>
static const Descriptor*
deref_descriptor(const RenderGraph* graph, T handle, DescriptorType type)
{
  return deref_descriptor(graph, handle.id, handle.temporal_lifetime, handle.temporal_frame, type);
}

void
RenderContext::clear_depth_stencil_view(
  RgWriteHandle<GpuTexture> depth_stencil,
  DepthStencilClearFlags flags,
  f32 depth,
  u8 stencil
) {
  const Descriptor* descriptor = deref_descriptor(m_Graph, depth_stencil, kDescriptorTypeDsv);

  D3D12_CLEAR_FLAGS clear_flags = (D3D12_CLEAR_FLAGS)0;
  if (flags & kClearDepth)
  {
    clear_flags |= D3D12_CLEAR_FLAG_DEPTH;
  }
  if (flags & kClearStencil)
  {
    clear_flags |= D3D12_CLEAR_FLAG_STENCIL;
  }

  m_CmdBuffer.d3d12_list->ClearDepthStencilView(
    descriptor->cpu_handle,
    clear_flags,
    depth,
    stencil,
    0,
    nullptr
  );
}

void
RenderContext::clear_render_target_view(
  RgWriteHandle<GpuTexture> render_target_view,
  const Vec4& rgba
) {
  const Descriptor* descriptor = deref_descriptor(m_Graph, render_target_view, kDescriptorTypeRtv);

  m_CmdBuffer.d3d12_list->ClearRenderTargetView(
    descriptor->cpu_handle,
    &rgba.x,
    0,
    nullptr
  );
}

void
RenderContext::set_graphics_pso(const GraphicsPSO* pso)
{
  m_CmdBuffer.d3d12_list->SetPipelineState(pso->d3d12_pso);
}

void
RenderContext::set_compute_pso(const ComputePSO* pso)
{
  m_CmdBuffer.d3d12_list->SetPipelineState(pso->d3d12_pso);
}

void
RenderContext::set_ray_tracing_pso(const RayTracingPSO* pso)
{
  m_CmdBuffer.d3d12_list->SetPipelineState1(pso->d3d12_pso);
}


void
RenderContext::draw_indexed_instanced(
  u32 index_count_per_instance,
  u32 instance_count,
  u32 start_index_location,
  s32 base_vertex_location,
  u32 start_instance_location
) {
  m_CmdBuffer.d3d12_list->DrawIndexedInstanced(
    index_count_per_instance,
    instance_count,
    start_index_location,
    base_vertex_location,
    start_instance_location
  );
}

void
RenderContext::draw_instanced(
  u32 vertex_count_per_instance,
  u32 instance_count,
  u32 start_vertex_location,
  u32 start_instance_location
) {
  m_CmdBuffer.d3d12_list->DrawInstanced(
    vertex_count_per_instance,
    instance_count,
    start_vertex_location,
    start_instance_location
  );
}

void
RenderContext::dispatch(u32 x, u32 y, u32 z)
{
  m_CmdBuffer.d3d12_list->Dispatch(x, y, z);
}

void 
RenderContext::dispatch_rays(const ShaderTable* shader_table, u32 x, u32 y, u32 z)
{
  D3D12_DISPATCH_RAYS_DESC desc = {};
  desc.RayGenerationShaderRecord.StartAddress = shader_table->ray_gen_addr;
  desc.RayGenerationShaderRecord.SizeInBytes  = shader_table->ray_gen_size;

  desc.MissShaderTable.StartAddress           = shader_table->miss_addr;
  desc.MissShaderTable.SizeInBytes            = shader_table->miss_size;
  desc.MissShaderTable.StrideInBytes          = shader_table->record_size;

  desc.HitGroupTable.StartAddress             = shader_table->hit_addr;
  desc.HitGroupTable.SizeInBytes              = shader_table->hit_size;
  desc.HitGroupTable.StrideInBytes            = shader_table->record_size;

  desc.Width  = x;
  desc.Height = y;
  desc.Depth  = z;

  m_CmdBuffer.d3d12_list->DispatchRays(&desc);
}

void
RenderContext::ia_set_index_buffer(const GpuBuffer* buffer, u32 stride, u32 size)
{
  ASSERT(is_pow2(stride) && stride > 0 && stride <= sizeof(u32));

  if (size == 0)
  {
    size = buffer->desc.size;
  }

  DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
  if (stride == 1)
  {
    format = DXGI_FORMAT_R8_UINT;
  }
  else if (stride == 2)
  {
    format = DXGI_FORMAT_R16_UINT;
  }
  else if (stride == 4)
  {
    format = DXGI_FORMAT_R32_UINT;
  } else { UNREACHABLE; }

  ASSERT(size > 0);

  D3D12_INDEX_BUFFER_VIEW view = {0};
  view.BufferLocation = buffer->gpu_addr;
  view.SizeInBytes    = size;
  view.Format         = format;

  m_CmdBuffer.d3d12_list->IASetIndexBuffer(&view);
}

void
RenderContext::ia_set_index_buffer(RgReadHandle<GpuBuffer> buffer, u32 stride, u32 size)
{
  const GpuBuffer* physical = deref_resource(m_Graph, buffer);
  ia_set_index_buffer(physical, stride, size);
}

void
RenderContext::ia_set_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology)
{
  m_CmdBuffer.d3d12_list->IASetPrimitiveTopology(primitive_topology);
}

void
RenderContext::ia_set_vertex_buffer(
  u32 start_slot,
  const GpuBuffer* buffer,
  u32 size,
  u32 stride
) {
  D3D12_VERTEX_BUFFER_VIEW view = {0};
  view.BufferLocation = buffer->gpu_addr;
  view.SizeInBytes    = size;
  view.StrideInBytes  = stride;
  m_CmdBuffer.d3d12_list->IASetVertexBuffers(start_slot, 1, &view);
}

void
RenderContext::ia_set_vertex_buffer(
  u32 start_slot,
  RgReadHandle<GpuBuffer> buffer,
  u32 size,
  u32 stride
) {
  const GpuBuffer* physical = deref_resource(m_Graph, buffer);
  ia_set_vertex_buffer(start_slot, physical, size, stride);
}

void
RenderContext::clear_state()
{
  m_CmdBuffer.d3d12_list->ClearState(nullptr);
}

void
RenderContext::om_set_render_targets(
  Span<RgWriteHandle<GpuTexture>> rtvs,
  Option<RgWriteHandle<GpuTexture>> dsv
) {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
  u32 rtv_count = 0;
  ASSERT(rtvs.size <= ARRAY_LENGTH(rtv_handles));

  for (RgWriteHandle<GpuTexture> handle : rtvs)
  {
    const Descriptor* descriptor = deref_descriptor(m_Graph, handle, kDescriptorTypeRtv);
    rtv_handles[rtv_count++]     = descriptor->cpu_handle;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
  if (dsv)
  {
    const Descriptor* descriptor = deref_descriptor(m_Graph, unwrap(dsv), kDescriptorTypeDsv);
    dsv_handle                   = descriptor->cpu_handle;
  }

  m_CmdBuffer.d3d12_list->OMSetRenderTargets(rtv_count, rtv_handles, FALSE, (bool)dsv ? &dsv_handle : nullptr);
}

void
RenderContext::rs_set_scissor_rect(s32 left, s32 top, s32 right, s32 bottom)
{
  auto rect = CD3DX12_RECT(left, top, right, bottom);
  m_CmdBuffer.d3d12_list->RSSetScissorRects(1, &rect);
}

void
RenderContext::rs_set_viewport(f32 left, f32 top, f32 width, f32 height)
{
  auto viewport = CD3DX12_VIEWPORT(left, top, width, height);
  m_CmdBuffer.d3d12_list->RSSetViewports(1, &viewport);
}

void
RenderContext::set_compute_root_shader_resource_view(
  u32 root_parameter_index,
  const GpuBuffer* buffer
) {
  m_CmdBuffer.d3d12_list->SetComputeRootShaderResourceView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_compute_root_shader_resource_view(
  u32 root_parameter_index,
  RgReadHandle<GpuBuffer> buffer
) {
  const GpuBuffer* physical = deref_resource(m_Graph, buffer);
  set_compute_root_shader_resource_view(root_parameter_index, physical);
}

void
RenderContext::set_graphics_root_shader_resource_view(
  u32 root_parameter_index,
  const GpuBuffer* buffer
) {
  m_CmdBuffer.d3d12_list->SetGraphicsRootShaderResourceView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_graphics_root_shader_resource_view(
  u32 root_parameter_index,
  RgReadHandle<GpuBuffer> buffer
) {
  const GpuBuffer* physical = deref_resource(m_Graph, buffer);
  set_graphics_root_shader_resource_view(root_parameter_index, physical);
}

void
RenderContext::set_compute_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer* buffer)
{
  m_CmdBuffer.d3d12_list->SetComputeRootConstantBufferView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_compute_root_constant_buffer_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer)
{
  const GpuBuffer* physical = deref_resource(m_Graph, buffer);
  set_compute_root_constant_buffer_view(root_parameter_index, physical);
}

void
RenderContext::set_graphics_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer* buffer)
{
  m_CmdBuffer.d3d12_list->SetGraphicsRootConstantBufferView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_graphics_root_constant_buffer_view(u32 root_parameter_index, RgReadHandle<GpuBuffer> buffer)
{
  const GpuBuffer* physical = deref_resource(m_Graph, buffer);
  set_graphics_root_constant_buffer_view(root_parameter_index, physical);
}

void
RenderContext::set_graphics_root_32bit_constants(
  u32 root_parameter_index,
  Span<u32> src,
  u32 dst_offset
) {
  m_CmdBuffer.d3d12_list->SetGraphicsRoot32BitConstants(root_parameter_index, src.size, src.memory, dst_offset);
}

void
RenderContext::set_compute_root_32bit_constants(u32 root_parameter_index, Span<u32> src, u32 dst_offset)
{
  m_CmdBuffer.d3d12_list->SetComputeRoot32BitConstants(root_parameter_index, src.size, src.memory, dst_offset);
}

void
RenderContext::set_descriptor_heaps(Span<const DescriptorLinearAllocator*> heaps)
{
  // ::set_descriptor_heaps(&m_CmdBuffer, heaps);
  m_CmdBuffer.d3d12_list->SetDescriptorHeaps(1, &heaps[0]->d3d12_heap);
}

void
RenderContext::graphics_bind_shader_resources(Span<ShaderResource> resources)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  Array<u32> root_consts = init_array<u32>(scratch_arena, resources.size);

  for (const ShaderResource& shader_resource : resources)
  {
    const Descriptor* descriptor = deref_descriptor(
      m_Graph,
      shader_resource.id,
      shader_resource.temporal_lifetime,
      shader_resource.temporal_frame,
      shader_resource.descriptor_type
    );

    *array_add(&root_consts) = descriptor->index;
  }

  set_graphics_root_32bit_constants(0, root_consts, 0);
}

// TODO(Brandon): Don't copy pasta this code... combine graphics and compute into single function
void
RenderContext::compute_bind_shader_resources(Span<ShaderResource> resources)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  Array<u32> root_consts = init_array<u32>(scratch_arena, resources.size);

  for (const ShaderResource& shader_resource : resources)
  {
    const Descriptor* descriptor = deref_descriptor(
      m_Graph,
      shader_resource.id,
      shader_resource.temporal_lifetime,
      shader_resource.temporal_frame,
      shader_resource.descriptor_type
    );

    *array_add(&root_consts) = descriptor->index;
  }

  set_compute_root_32bit_constants(0, root_consts, 0);
}

void
RenderContext::ray_tracing_bind_shader_resources(Span<ShaderResource> resources)
{
  compute_bind_shader_resources(resources);
}

void
RenderContext::write_cpu_upload_buffer(RgReadHandle<GpuBuffer> dst, const void* src, u64 size)
{
  const GpuBuffer* physical = deref_resource(m_Graph, dst);
  write_cpu_upload_buffer(physical, src, size);
}

void
RenderContext::write_cpu_upload_buffer(const GpuBuffer* dst, const void* src, u64 size)
{
  ASSERT(size <= dst->desc.size);

  void* ptr = unwrap(dst->mapped);
  memcpy(ptr, src, size);
}
