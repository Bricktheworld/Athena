#include "Core/Foundation/context.h"
#include "Core/Vendor/d3dx12.h"

#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

constant u32 kMaxTemporalLifetime = 3;

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
  resource_handle.type                = kResourceTypeImage;
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
  Array<RenderPassId> dependencies;
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
    dst->dependencies         = init_array<RenderPassId>(heap, builder.render_passes.size);
    for (const RgPassBuilder& other_builder : builder.render_passes)
    {
      // Look through every other render pass (skip our own)
      if (other_builder.pass_id == pass_builder.pass_id)
        continue;

      // Loop through all of our read resources
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
          it->handle.id == write_resource.handle.id && it->handle.version == write_resource.handle.version + 1
        ) || array_find(
          &other_builder.write_resources,
          it->handle.id == write_resource.handle.id && it->handle.version == write_resource.handle.version + 1
        );

        if (other_depends_on_pass)
        {
          *array_add(&dst->dependencies) = other_builder.pass_id;
          break;
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

  Array<RenderPassId> dependencies = list.render_passes[pass_id].dependencies;
  for (RenderPassId dependency : dependencies)
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

  Array<RenderPassId> dependencies = list.render_passes[pass_id].dependencies;
  for (RenderPassId dependency : dependencies)
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
    for (RenderPassId adjacent_pass_id : adjacency_list.render_passes[pass_id].dependencies)
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

struct PhysicalResourceMap
{
  HashTable<RgResourceKey, GpuBuffer> buffers;
  HashTable<RgResourceKey, GpuImage > textures;
};

static PhysicalResourceMap
init_physical_resources(
  AllocHeap heap,
  const RgBuilder& builder,
  const GraphicsDevice* device,
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

      if (handle.type == kResourceTypeImage)
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

      if (handle.type == kResourceTypeImage)
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

  HashTable<RgResourceKey, GpuBuffer> physical_buffers  = init_hash_table<RgResourceKey, GpuBuffer>(heap, buffer_count );
  HashTable<RgResourceKey, GpuImage > physical_textures = init_hash_table<RgResourceKey, GpuImage >(heap, texture_count);
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

      if (resource_desc->temporal_lifetime > 0)
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
    else if (resource.type == kResourceTypeImage)
    {
      GpuImageDesc  desc = {0};
      desc.width         = resource_desc->texture_desc.width;
      desc.height        = resource_desc->texture_desc.height;
      desc.array_size    = 1;
      desc.format        = resource_desc->texture_desc.format;
      desc.initial_state = D3D12_RESOURCE_STATE_COMMON;
      desc.flags         = *unwrap(hash_table_find(&physical_flags, resource.id));

      if (is_depth_format(desc.format))
      {
        desc.depth_clear_value   = resource_desc->texture_desc.depth_clear_value;
        desc.stencil_clear_value = resource_desc->texture_desc.stencil_clear_value;
      }
      else
      {
        desc.color_clear_value   = resource_desc->texture_desc.color_clear_value;
      }

      if (resource_desc->temporal_lifetime > 0)
      {
        ASSERT(resource_desc->temporal_lifetime <= kMaxTemporalLifetime);
        for (u32 iframe = 0; iframe <= resource_desc->temporal_lifetime; iframe++)
        {
          key.temporal_frame = iframe;
          GpuImage* dst      = hash_table_insert(&physical_textures, key);
    
          *dst               = alloc_gpu_image_2D(device, temporal_heaps + iframe, desc, resource_desc->name);
        }
      }
      else
      {
        key.temporal_frame = 0;
        GpuImage* dst      = hash_table_insert(&physical_textures, key);
  
        *dst               = alloc_gpu_image_2D(device, local_heap, desc, resource_desc->name);
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
  const GraphicsDevice* device,
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
      else if (data.handle.type == kResourceTypeImage)
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
      else if (data.handle.type == kResourceTypeImage)
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
      descriptor_count  += handle.temporal_lifetime + 1;
      cbv_srv_uav_count += handle.temporal_lifetime + 1;
    }

    if (type_mask & kDescriptorTypeSrv)
    {
      descriptor_count  += handle.temporal_lifetime + 1;
      cbv_srv_uav_count += handle.temporal_lifetime + 1;
    }

    if (type_mask & kDescriptorTypeUav)
    {
      descriptor_count  += handle.temporal_lifetime + 1;
      cbv_srv_uav_count += handle.temporal_lifetime + 1;
    }

    if (type_mask & kDescriptorTypeRtv)
    {
      descriptor_count  += handle.temporal_lifetime + 1;
      rtv_count         += handle.temporal_lifetime + 1;
    }

    if (type_mask & kDescriptorTypeDsv)
    {
      descriptor_count  += handle.temporal_lifetime + 1;
      dsv_count         += handle.temporal_lifetime + 1;
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

    ASSERT(handle.temporal_lifetime <= kMaxTemporalLifetime);

    for (u32 iframe = 0; iframe <= handle.temporal_lifetime; iframe++)
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
        else if (handle.type == kResourceTypeImage)
        {
          // You can't use the back buffer as an SRV
          ASSERT(handle.id != builder.back_buffer.id);
  
          const GpuImage* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
          init_image_2D_srv(device, dst, texture);
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
        else if (handle.type == kResourceTypeImage)
        {
          // You can't use the back buffer as a UAV
          ASSERT(handle.id != builder.back_buffer.id);
  
          const GpuImage* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
          init_image_2D_uav(device, dst, texture);
        } else { UNREACHABLE; }
      }
  
      if (type_mask & kDescriptorHeapTypeRtv)
      {
        ASSERT(handle.type == kResourceTypeImage);
  
        key.type                = kDescriptorTypeRtv;
        Descriptor*     dst     = hash_table_insert(&descriptor_map, key);
        *dst                    = alloc_descriptor(&descriptor_heap.rtv);
        if (handle.id != builder.back_buffer.id)
        {
          const GpuImage* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
  
          init_rtv(device, dst, texture);
        }
      }
  
      if (type_mask & kDescriptorHeapTypeDsv)
      {
        ASSERT(handle.type == kResourceTypeImage);
  
        key.type                = kDescriptorTypeDsv;
        Descriptor*     dst     = hash_table_insert(&descriptor_map, key);
        *dst                    = alloc_descriptor(&descriptor_heap.dsv);
        if (handle.id != builder.back_buffer.id)
        {
          const GpuImage* texture = unwrap(hash_table_find(&resource_map.textures, resource_key));
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
  else if (data.handle.type == kResourceTypeImage)
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
  *out_exit_barriers = init_array<RgResourceBarrier>(heap, builder.resource_list.size);

  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  struct ResourceStates
  {
    D3D12_RESOURCE_STATES current = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES history = D3D12_RESOURCE_STATE_COMMON;
    bool                  touched = false;
  };

  HashTable<u32, ResourceStates> resource_states = init_hash_table<u32, ResourceStates>(scratch_arena, builder.resource_list.size);
  for (ResourceHandle handle : builder.resource_list)
  {
    ResourceStates* dst = hash_table_insert(&resource_states, handle.id);
    dst->current        = D3D12_RESOURCE_STATE_COMMON;
    dst->history        = D3D12_RESOURCE_STATE_COMMON;
    dst->touched        = false;
  }


  for (u32 ilevel = 0; ilevel < out_dependency_levels.size; ilevel++)
  {
    RgDependencyLevel* level = &out_dependency_levels[ilevel];

    for (RenderPassId pass_id : level->render_passes)
    {
      const RgPassBuilder& pass = builder.render_passes[pass_id];
      for (RgPassBuilder::ResourceAccessData data : pass.read_resources)
      {
        ResourceStates* states = unwrap(hash_table_find(&resource_states, data.handle.id));

        if (!states->touched)
        {
          states->current = D3D12_RESOURCE_STATE_COMMON;
        }

        states->current |= get_d3d12_resource_state(data);
        states->touched  = true;
      }

      for (RgPassBuilder::ResourceAccessData data : pass.write_resources)
      {
        ResourceStates* states = unwrap(hash_table_find(&resource_states, data.handle.id));
        states->current        = get_d3d12_resource_state(data);
        states->touched        = true;
      }
    }

    for (ResourceHandle handle : builder.resource_list)
    {
      ResourceStates* states = unwrap(hash_table_find(&resource_states, handle.id));

      if (states->history != states->current)
      {
        RgResourceBarrier* dst        = array_add(&level->barriers);
        dst->type                     = kResourceBarrierTransition;
        dst->transition.before        = states->history;
        dst->transition.after         = states->current;
        dst->transition.resource_id   = handle.id;
        dst->transition.resource_type = handle.type;
      }

      if (states->current == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
      {
        RgResourceBarrier* dst = array_add(&level->barriers);
        dst->type              = kResourceBarrierUav;
        dst->uav.resource_id   = handle.id;
        dst->uav.resource_type = handle.type;
      }

      states->history        = states->current;
      states->touched        = false;
    }
  }

  for (ResourceHandle handle : builder.resource_list)
  {
    ResourceStates* states = unwrap(hash_table_find(&resource_states, handle.id));
    if (states->history == D3D12_RESOURCE_STATE_COMMON)
      continue;
    
    RgResourceBarrier* dst        = array_add(out_exit_barriers);
    dst->type                     = kResourceBarrierTransition;
    dst->transition.before        = states->history;
    dst->transition.after         = D3D12_RESOURCE_STATE_COMMON;
    dst->transition.resource_id   = handle.id;
    dst->transition.resource_type = handle.type;
  }
}

RenderGraph
compile_render_graph(AllocHeap heap, const RgBuilder& builder, const GraphicsDevice* device)
{
  // You must write to the back buffer exactly once, never more.
  ASSERT(builder.back_buffer.version == 1);

  RenderGraph ret       = {0};
  ret.cmd_allocator     = init_cmd_list_allocator(heap, device, &device->graphics_queue, 4);
  ret.render_passes     = init_array<RenderPass>(heap, builder.render_passes.size);
  ret.dependency_levels = init_dependency_levels(heap, builder);
  ret.back_buffer       = builder.back_buffer;
  ret.frame_id          = 0;
  ret.width             = builder.width;
  ret.height            = builder.height;

  for (const RgPassBuilder& pass_builder : builder.render_passes)
  {
    RenderPass* dst     = array_add(&ret.render_passes);
    dst->handler        = pass_builder.handler;
    dst->data           = pass_builder.data;
    dst->name           = pass_builder.name;
  }

  ret.local_heap        = init_gpu_linear_allocator(device, MiB(700), kGpuHeapTypeLocal);
  for (u32 i = 0; i < kFramesInFlight; i++)
  {
    ret.upload_heaps[i] = init_gpu_linear_allocator(device, MiB(4), kGpuHeapTypeUpload);
  }

  u8  max_temporal_lifetime = 0;
  u32 buffer_count          = 0;
  u32 texture_count         = 0;
  for (ResourceHandle resource : builder.resource_list)
  {
    if (resource.id == builder.back_buffer.id)
      continue;

    TransientResourceDesc* desc = unwrap(hash_table_find(&builder.resource_descs, resource.id));
    max_temporal_lifetime       = MAX(max_temporal_lifetime, desc->temporal_lifetime);

    if      (resource.type == kResourceTypeBuffer) { buffer_count++;  }
    else if (resource.type == kResourceTypeImage)  { texture_count++; }
    else                                           { UNREACHABLE;     }
  }

  ASSERT(max_temporal_lifetime <= kMaxTemporalLifetime);
  ret.temporal_heaps = init_array<GpuLinearAllocator>(heap, max_temporal_lifetime);
  for (u8 i = 0; i < max_temporal_lifetime; i++)
  {
    GpuLinearAllocator* dst = array_add(&ret.temporal_heaps);
    *dst                    = init_gpu_linear_allocator(device, MiB(32), kGpuHeapTypeLocal);
  }

  PhysicalResourceMap physical_resource_map = init_physical_resources(
    heap,
    builder,
    device,
    &ret.local_heap,
    ret.upload_heaps,
    ret.temporal_heaps.memory,
    buffer_count,
    texture_count
  );

  init_dependency_barriers(heap, builder, physical_resource_map, ret.dependency_levels, &ret.exit_barriers);

  PhysicalDescriptorMap physical_descriptor_map = init_physical_descriptors(
    heap,
    builder,
    device,
    physical_resource_map
  );

  ret.descriptor_heap = physical_descriptor_map.descriptor_heap;
  ret.descriptor_map  = physical_descriptor_map.descriptor_map;
  ret.buffer_map      = physical_resource_map.buffers;
  ret.texture_map     = physical_resource_map.textures;

  return ret;
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
      key.temporal_frame = 0;

      ID3D12Resource* d3d12_resource = nullptr;
      if (barrier.transition.resource_type == kResourceTypeImage)
      {
        GpuImage* texture  = unwrap(hash_table_find(&graph->texture_map, key));
        d3d12_resource     = texture->d3d12_image;
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
      if (barrier.uav.resource_type == kResourceTypeImage)
      {
        GpuImage* texture = unwrap(hash_table_find(&graph->texture_map, key));
        d3d12_resource    = texture->d3d12_image;
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
  const GraphicsDevice* device,
  RgHandle<GpuImage> handle,
  const GpuImage* texture,
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
execute_render_graph(RenderGraph* graph, const GraphicsDevice* device, const GpuImage* back_buffer, u32 frame_index)
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
  u32 num_write_resources
) {
  RgPassBuilder* ret   = array_add(&graph->render_passes);

  ret->pass_id         = (u32)graph->render_passes.size - 1;
  ret->queue           = queue;
  ret->name            = name;
  ret->handler         = handler;
  ret->data            = data;

  ret->read_resources  = init_array<RgPassBuilder::ResourceAccessData>(heap, num_read_resources);
  ret->write_resources = init_array<RgPassBuilder::ResourceAccessData>(heap, num_write_resources);

  return ret;
}

RgHandle<GpuImage>
rg_create_texture(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format
) {
  return rg_create_texture_ex(builder, name, width, height, format, 0);
}

RgHandle<GpuImage>
rg_create_texture_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  DXGI_FORMAT format,
  u8 temporal_lifetime
) {
  ASSERT(temporal_lifetime <= kMaxTemporalLifetime);
  ResourceHandle resource_handle      = {0};
  resource_handle.id                  = handle_index(builder);
  resource_handle.version             = 0;
  resource_handle.type                = kResourceTypeImage;
  resource_handle.temporal_lifetime   = temporal_lifetime;
  *array_add(&builder->resource_list) = resource_handle;

  TransientResourceDesc* desc         = hash_table_insert(&builder->resource_descs, resource_handle.id);
  desc->name                          = name;
  desc->type                          = resource_handle.type;
  desc->temporal_lifetime             = resource_handle.temporal_lifetime;
  desc->texture_desc.width            = width;
  desc->texture_desc.height           = height;
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

  RgHandle<GpuImage> ret = {resource_handle.id, resource_handle.version, resource_handle.temporal_lifetime};
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
  ASSERT(temporal_lifetime <= kMaxTemporalLifetime);
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


RgReadHandle<GpuImage>
rg_read_texture(RgPassBuilder* builder, RgHandle<GpuImage> texture, ReadTextureAccessMask access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture.id));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture.id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle   = texture;
  data->access   = (u32)access;
  data->is_write = false;

  return {texture.id, (u32)texture.temporal_lifetime};
}

RgWriteHandle<GpuImage>
rg_write_texture(RgPassBuilder* builder, RgHandle<GpuImage>* texture, WriteTextureAccess access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture->id));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture->id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle   = *texture;
  data->access   = (u32)access;
  data->is_write = true;

  texture->version++;

  return {texture->id, (u32)texture->temporal_lifetime};
}

RgReadHandle<GpuBuffer>
rg_read_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, ReadBufferAccessMask access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == buffer.id));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer.id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle   = buffer;
  data->access   = (u32)access;
  data->is_write = false;

  return {buffer.id, (u32)buffer.temporal_lifetime};
}

RgWriteHandle<GpuBuffer>
rg_write_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, WriteBufferAccess access)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == buffer->id));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer->id));

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle   = *buffer;
  data->access   = (u32)access;
  data->is_write = true;

  buffer->version++;

  return {buffer->id, (u32)buffer->temporal_lifetime};
}

static u32
get_temporal_frame(u32 frame_id, u32 temporal_lifetime)
{
  if (temporal_lifetime == 0)
    return 0;
  
  temporal_lifetime++;
  ASSERT(temporal_lifetime >= 2);

  return frame_id % temporal_lifetime;
}

static const GpuImage*
deref_resource(const RenderGraph* graph, RgWriteHandle<GpuImage> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime);

  return unwrap(hash_table_find(&graph->texture_map, key));
}

static const GpuImage*
deref_resource(const RenderGraph* graph, RgReadHandle<GpuImage> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime);

  return unwrap(hash_table_find(&graph->texture_map, key));
}

static const GpuBuffer*
deref_resource(const RenderGraph* graph, RgWriteHandle<GpuBuffer> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime);

  return unwrap(hash_table_find(&graph->buffer_map, key));
}

static const GpuBuffer*
deref_resource(const RenderGraph* graph, RgReadHandle<GpuBuffer> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime);

  return unwrap(hash_table_find(&graph->buffer_map, key));
}

static const GpuBuffer*
deref_resource(const RenderGraph* graph, RgHandle<GpuBuffer> handle)
{
  RgResourceKey key  = {0};
  key.id             = handle.id;
  key.temporal_frame = get_temporal_frame(graph->frame_id, handle.temporal_lifetime);

  return unwrap(hash_table_find(&graph->buffer_map, key));
}

static const Descriptor*
deref_descriptor(const RenderGraph* graph, u32 handle_id, u32 temporal_lifetime, DescriptorType type)
{
  RgDescriptorKey key = {0};
  key.id              = handle_id;
  key.type            = type;
  key.temporal_frame  = get_temporal_frame(graph->frame_id, temporal_lifetime);
  return unwrap(hash_table_find(&graph->descriptor_map, key));
}

template <typename T>
static const Descriptor*
deref_descriptor(const RenderGraph* graph, T handle, DescriptorType type)
{
  return deref_descriptor(graph, handle.id, handle.temporal_lifetime, type);
}

void
RenderContext::clear_depth_stencil_view(
  RgWriteHandle<GpuImage> depth_stencil,
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
  RgWriteHandle<GpuImage> render_target_view,
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
  Span<RgWriteHandle<GpuImage>> rtvs,
  Option<RgWriteHandle<GpuImage>> dsv
) {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
  u32 rtv_count = 0;
  ASSERT(rtvs.size <= ARRAY_LENGTH(rtv_handles));

  for (RgWriteHandle<GpuImage> handle : rtvs)
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
RenderContext::write_cpu_upload_buffer(RgHandle<GpuBuffer> dst, const void* src, u64 size)
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

#if 0
struct PhysicalDescriptorKey
{
  u32            id           = 0;
  DescriptorType type         = kDescriptorTypeCbv;
  u8             __padding0__ = 0;
  u16            __padding1__ = 0;

  auto operator<=>(const PhysicalDescriptorKey& rhs) const = default;
};
static_assert(offsetof(PhysicalDescriptorKey, id)           == 0);
static_assert(offsetof(PhysicalDescriptorKey, type)         == 4);
static_assert(offsetof(PhysicalDescriptorKey, __padding0__) == 5);
static_assert(offsetof(PhysicalDescriptorKey, __padding1__) == 6);
static_assert(sizeof  (PhysicalDescriptorKey)               == 8);

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
    } else { UNREACHABLE; }
  }

  ASSERT((descriptor->type & descriptor_type) == descriptor_type);
  return descriptor;
}
#endif

#if 0
static void
execute_d3d12_cmd(
  const GraphicsDevice* device,
  CmdList* list,
  const RenderGraphCmd& cmd,
  CompiledResourceMap* compiled_map
) {
  switch(cmd.type)
  {
    case RenderGraphCmdType::kGraphicsBindShaderResources:
    case RenderGraphCmdType::kComputeBindShaderResources:
    {
      ScratchAllocator scratch_arena = alloc_scratch_arena();
      defer { free_scratch_arena(&scratch_arena); };

      const auto& args = cmd.graphics_bind_shader_resources;

      auto root_consts = init_array<u32>(scratch_arena, args.resources.size);

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
      ScratchAllocator scratch_arena = alloc_scratch_arena();
      defer { free_scratch_arena(&scratch_arena); };

      const auto& args = cmd.om_set_render_targets;
      auto rtvs = init_array<D3D12_CPU_DESCRIPTOR_HANDLE>(scratch_arena, args.render_targets.size);
      for (RgHandle<GpuImage> img : args.render_targets)
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
      set_descriptor_heaps(list, {args.descriptor_linear_allocator});
      ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list->d3d12_list);

      set_descriptor_heaps(list, {compiled_map->cbv_srv_uav_descriptor_allocator,
                                      compiled_map->sampler_allocator});
    } break;
    default: UNREACHABLE;
  }
}

static void
execute_d3d12_transition(
  CmdList* cmd_list,
  ResourceHandle resource,
  const CompiledResourceMap* compiled_map,
  D3D12_RESOURCE_STATES next_state
) {
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
#endif

#if 0
static void
execute_d3d12_transition(
  CmdList* cmd_list,
  ResourceHandle resource,
  const CompiledResourceMap* compiled_map,
  const RgPassBuilder& pass
) {
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
get_additional_resource_flags(ResourceHandle resource, const RgPassBuilder& pass)
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
execute_render_graph(
  const GraphicsDevice* device,
  RgBuilder* graph,
  TransientResourceCache* cache,
  u32 frame_index
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  auto dependency_levels = init_array<DependencyLevel>(scratch_arena, graph->render_passes.size);
  for (size_t i = 0; i < graph->render_passes.size; i++)
  {
    DependencyLevel* level = array_add(&dependency_levels); 
    level->passes = init_array<RenderPassId>(scratch_arena, graph->render_passes.size);
  }

  build_dependency_list(graph, &dependency_levels);
  
  u64 total_resource_count = graph->transient_resources.used + graph->imported_resources.used;
  CompiledResourceMap compiled_map = {0};
  compiled_map.resource_map   = init_hash_table<ResourceHandle,  PhysicalResource>(scratch_arena, total_resource_count);
  compiled_map.descriptor_map = init_hash_table<PhysicalDescriptorKey, Descriptor>(scratch_arena, total_resource_count * 5 / 4);

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

  auto gpu_buffers = init_array<GpuBuffer>(scratch_arena, buffer_count);
  auto gpu_images = init_array<GpuImage>(scratch_arena, image_count);
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
    ScratchAllocator scratch_arena = alloc_scratch_arena();
    defer { free_scratch_arena(&scratch_arena); };

    auto additional_flags = init_hash_table<ResourceHandle, D3D12_RESOURCE_FLAGS>(scratch_arena, total_resource_count);
    for (DependencyLevel dependency_level : dependency_levels)
    {
      for (RenderPassId pass_id : dependency_level.passes)
      {
        const RgPassBuilder& pass = graph->render_passes[pass_id];
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

//    const u32 kPIXTransitionColor = PIX_COLOR(0, 0, 255);
//    const u32 kPIXRenderPassColor = PIX_COLOR(255, 0, 0);


  for (DependencyLevel& dependency_level : dependency_levels)
  {
    {
      CmdListAllocator* allocator = get_cmd_list_allocator(cache, kCmdQueueTypeGraphics);
      CmdList cmd_list = alloc_cmd_list(allocator);

//        PIXBeginEvent(cmd_list.d3d12_list, kPIXTransitionColor, "Transition Barrier");
      for (RenderPassId pass_id : dependency_level.passes)
      {
        const RgPassBuilder& pass = graph->render_passes[pass_id];
        for (ResourceHandle resource : pass.read_resources)
        {
          execute_d3d12_transition(&cmd_list, resource, &compiled_map, pass);
        }

        for (ResourceHandle resource : pass.write_resources)
        {
          execute_d3d12_transition(&cmd_list, resource, &compiled_map, pass);
        }
      }
//        PIXEndEvent(cmd_list.d3d12_list);

      submit_cmd_lists(allocator, { cmd_list });
    }
    {
      ScratchAllocator scratch_arena = alloc_scratch_arena();
      defer { free_scratch_arena(&scratch_arena); };

      Array<CmdList> cmd_lists[kCmdQueueTypeCount];
      for (u32 queue_type = 0; queue_type < kCmdQueueTypeCount; queue_type++)
      {
        cmd_lists[queue_type] = init_array<CmdList>(scratch_arena, dependency_level.passes.size);
      }

      for (RenderPassId pass_id : dependency_level.passes)
      {
        const RgPassBuilder& pass = graph->render_passes[pass_id];
        CmdListAllocator* allocator = get_cmd_list_allocator(cache, pass.queue);

        CmdList list = alloc_cmd_list(allocator);
//          PIXBeginEvent(list.d3d12_list, kPIXRenderPassColor, "Render Pass %s", pass.name);

        set_descriptor_heaps(&list, {compiled_map.cbv_srv_uav_descriptor_allocator,
                                        compiled_map.sampler_allocator});
        if (pass.queue == kCmdQueueTypeGraphics)
        {
          set_primitive_topology(&list);
          set_graphics_root_signature(&list);
          set_compute_root_signature(&list);
        }
        else if (pass.queue == kCmdQueueTypeCompute)
        {
          set_compute_root_signature(&list);
        }

#if 0
        for (const RenderGraphCmd& cmd : pass.cmd_buffer)
        {
          execute_d3d12_cmd(device, &list, cmd, &compiled_map);
        }
#endif
//          PIXEndEvent(list.d3d12_list);

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
//        PIXBeginEvent(cmd_list.d3d12_list, kPIXTransitionColor, "Back Buffer Transition");
      auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(physical->image->d3d12_image,
                                                          physical->state,
                                                          next_state);
      cmd_list.d3d12_list->ResourceBarrier(1, &barrier);
//        PIXEndEvent(cmd_list.d3d12_list);
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
#endif

#if 0
static void
push_cmd(RgPassBuilder* render_pass, const RenderGraphCmd& cmd)
{
  memcpy(array_add(&render_pass->cmd_buffer), &cmd, sizeof(cmd));
}
#endif

#if 0
static void
common_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources, CmdQueueType type)
{
  RenderGraphCmd cmd;
  if (type == kCmdQueueTypeGraphics)
  {
    cmd.type = RenderGraphCmdType::kGraphicsBindShaderResources;
    cmd.graphics_bind_shader_resources.resources = init_array<ShaderResource>(
      render_pass->allocator,
      resources
    );
  }
  else if (type == kCmdQueueTypeCompute)
  {
    cmd.type = RenderGraphCmdType::kComputeBindShaderResources;
    cmd.compute_bind_shader_resources.resources = init_array<ShaderResource>(
      render_pass->allocator,
      resources
    );
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
cmd_graphics_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources)
{
  common_bind_shader_resources(render_pass, resources, kCmdQueueTypeGraphics);
}

void
cmd_compute_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources)
{
  common_bind_shader_resources(render_pass, resources, kCmdQueueTypeCompute);
}

void
cmd_ray_tracing_bind_shader_resources(RgPassBuilder* render_pass, Span<ShaderResource> resources)
{
  common_bind_shader_resources(render_pass, resources, kCmdQueueTypeCompute);
}

void
cmd_draw_instanced(RgPassBuilder* render_pass,
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
cmd_draw_indexed_instanced(RgPassBuilder* render_pass,
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
cmd_dispatch(RgPassBuilder* render_pass,
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
cmd_ia_set_primitive_topology(RgPassBuilder* render_pass,
                              D3D12_PRIMITIVE_TOPOLOGY primitive_topology)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kIASetPrimitiveTopology;
  cmd.ia_set_primitive_topology.primitive_topology = primitive_topology;
  push_cmd(render_pass, cmd);
}

void
cmd_rs_set_viewport(RgPassBuilder* render_pass, 
                    D3D12_VIEWPORT viewport)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kRSSetViewport;
  cmd.rs_set_viewport.viewport = viewport;
  push_cmd(render_pass, cmd);
}

void
cmd_rs_set_scissor_rect(RgPassBuilder* render_pass,
                        D3D12_RECT rect)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kRSSetViewport;
  cmd.rs_set_scissor_rect.rect = rect;
  push_cmd(render_pass, cmd);
}

void
cmd_om_set_blend_factor(RgPassBuilder* render_pass, 
                        Vec4 blend_factor)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kOMSetBlendFactor;
  cmd.om_set_blend_factor.blend_factor = blend_factor;
  push_cmd(render_pass, cmd);
}

void
cmd_om_set_stencil_ref(RgPassBuilder* render_pass, 
                      u32 stencil_ref)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kOMSetBlendFactor;
  cmd.om_set_stencil_ref.stencil_ref = stencil_ref;
  push_cmd(render_pass, cmd);
}

void
cmd_set_graphics_pso(RgPassBuilder* render_pass, const GraphicsPSO* graphics_pso)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kSetGraphicsPSO;
  cmd.set_graphics_pso.graphics_pso = graphics_pso;
  push_cmd(render_pass, cmd);
}

void
cmd_set_compute_pso(RgPassBuilder* render_pass, const ComputePSO* compute_pso)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kSetComputePSO;
  cmd.set_compute_pso.compute_pso = compute_pso;
  push_cmd(render_pass, cmd);
}

void
cmd_set_ray_tracing_pso(RgPassBuilder* render_pass, const RayTracingPSO* ray_tracing_pso)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kSetRayTracingPSO;
  cmd.set_ray_tracing_pso.ray_tracing_pso = ray_tracing_pso;
  push_cmd(render_pass, cmd);
}

void
cmd_ia_set_index_buffer(RgPassBuilder* render_pass, 
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
cmd_om_set_render_targets(
  RgPassBuilder* render_pass, 
  Span<RgHandle<GpuImage>> render_targets,
  Option<RgHandle<GpuImage>> depth_stencil_target
) {
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
  cmd.om_set_render_targets.render_targets = init_array<Handle<GpuImage>>(render_pass->allocator, render_targets);
  cmd.om_set_render_targets.depth_stencil_target = depth_stencil_target;
  push_cmd(render_pass, cmd);
}

void
cmd_clear_render_target_view(
  RgPassBuilder* render_pass, 
  Handle<GpuImage>* render_target,
  Vec4 clear_color
) {
  render_pass_write(render_pass, *render_target, D3D12_RESOURCE_STATE_RENDER_TARGET);

  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kClearRenderTargetView;
  cmd.clear_render_target_view.render_target = *render_target;
  cmd.clear_render_target_view.clear_color = clear_color;
  push_cmd(render_pass, cmd);
}

void
cmd_clear_depth_stencil_view(
  RgPassBuilder* render_pass, 
  Handle<GpuImage>* depth_stencil,
  D3D12_CLEAR_FLAGS clear_flags,
  f32 depth,
  u8 stencil
) {
  render_pass_write(render_pass, *depth_stencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kClearDepthStencilView;
  cmd.clear_depth_stencil_view.depth_stencil = *depth_stencil;
  cmd.clear_depth_stencil_view.clear_flags = clear_flags;
  cmd.clear_depth_stencil_view.depth = depth;
  cmd.clear_depth_stencil_view.stencil = stencil;
  push_cmd(render_pass, cmd);
}

void cmd_clear_unordered_access_view_uint(
  RgPassBuilder* render_pass,
  Handle<GpuImage>* uav,
  Span<u32> values
) {
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

void cmd_clear_unordered_access_view_float(
  RgPassBuilder* render_pass,
  Handle<GpuImage>* uav,
  Span<f32> values
) {
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
cmd_dispatch_rays(
  RgPassBuilder* render_pass,
  const GpuBvh* bvh,
  const GpuBuffer* index_buffer,
  const GpuBuffer* vertex_buffer,
  ShaderTable shader_table,
  u32 x,
  u32 y,
  u32 z
) {
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
cmd_draw_imgui_on_top(RgPassBuilder* render_pass, const DescriptorLinearAllocator* descriptor_linear_allocator)
{
  RenderGraphCmd cmd;
  cmd.type = RenderGraphCmdType::kDrawImGuiOnTop;
  cmd.draw_imgui_on_top.descriptor_linear_allocator = descriptor_linear_allocator;
  push_cmd(render_pass, cmd);
}
#endif
