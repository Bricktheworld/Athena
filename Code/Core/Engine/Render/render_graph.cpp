#include "Core/Foundation/context.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/Render/render_graph.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Vendor/D3D12/d3dx12.h"

#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

static constexpr u32 kMaxTemporalLifetime = 3;

#if defined(RENDER_GRAPH_VERBOSE)
#define RG_DBGLN(...) dbgln(__VA_ARGS__)
#else
#define RG_DBGLN(...) do { } while(0)
#endif

RenderGraph* g_RenderGraph = nullptr;
RenderPassId g_HandlerId   = 0;

static u32
handle_index(RgBuilder* graph)
{
  return graph->handle_index++;
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
    level_barrier_counts[level_index] += (u32)(pass.read_resources.size + 2 * pass.write_resources.size);
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

static u8
get_temporal_lifetime(u32 temporal_lifetime)
{
  if (temporal_lifetime == kInfiniteLifetime)
    return 0;
  
  ASSERT(temporal_lifetime < kMaxTemporalLifetime);
  return (u8)temporal_lifetime;
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
  GpuLinearAllocator* sysram_upload_heaps,
  GpuLinearAllocator* vram_upload_heaps,
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

    TransientResourceDesc* resource_desc = hash_table_find(&builder.resource_descs, resource.id);
    if (resource.type == kResourceTypeBuffer)
    {
      GpuBufferDesc desc = {0};
      desc.size          = resource_desc->buffer_desc.size;
      desc.flags         = *hash_table_find(&physical_flags, resource.id);
      desc.initial_state = D3D12_RESOURCE_STATE_COMMON;

      if (resource_desc->temporal_lifetime > 0 && resource_desc->temporal_lifetime < kInfiniteLifetime)
      {
        GpuLinearAllocator* allocator = nullptr;
        switch(resource_desc->buffer_desc.heap_location)
        {
          case kGpuHeapGpuOnly:        allocator = temporal_heaps;      break;
          case kGpuHeapSysRAMCpuToGpu: allocator = sysram_upload_heaps; break;
          case kGpuHeapVRAMCpuToGpu:   allocator = vram_upload_heaps;   break;
          default: UNREACHABLE;
        }

        ASSERT(resource_desc->temporal_lifetime <= kMaxTemporalLifetime);
        for (u32 iframe = 0; iframe <= resource_desc->temporal_lifetime; iframe++)
        {
          key.temporal_frame = iframe;
          GpuBuffer* dst     = hash_table_insert(&physical_buffers, key);
    
          *dst               = alloc_gpu_buffer(device, allocator[iframe], desc, resource_desc->name);
        }
      }
      else
      {
        key.temporal_frame = 0;
        GpuBuffer*    dst  = hash_table_insert(&physical_buffers, key);
        *dst               = alloc_gpu_buffer(device, *local_heap, desc, resource_desc->name);
      }
    }
    else if (resource.type == kResourceTypeTexture)
    {
      GpuTextureDesc desc = {0};
      desc.width          = resource_desc->texture_desc.width;
      desc.height         = resource_desc->texture_desc.height;
      desc.array_size     = resource_desc->texture_desc.array_size;
      desc.format         = resource_desc->texture_desc.format;
      desc.initial_state  = D3D12_RESOURCE_STATE_COMMON;
      desc.flags          = *hash_table_find(&physical_flags, resource.id);

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
    
          *dst               = alloc_gpu_texture(device, temporal_heaps[iframe], desc, resource_desc->name);
        }
      }
      else
      {
        key.temporal_frame = 0;
        GpuTexture* dst    = hash_table_insert(&physical_textures, key);
  
        *dst               = alloc_gpu_texture(device, *local_heap, desc, resource_desc->name);
      }
    } else { UNREACHABLE; }
  }

  PhysicalResourceMap ret = {0};
  ret.buffers  = physical_buffers;
  ret.textures = physical_textures;

  return ret;
}

static void
init_physical_descriptors(
  AllocHeap heap,
  RgDescriptorHeap* descriptor_heap,
  const RgBuilder& builder,
  const PhysicalResourceMap& resource_map,
  RenderPass* render_passes
) {
  for (RenderPassId render_pass_id = 0; render_pass_id < builder.render_passes.size; render_pass_id++)
  {
    const RgPassBuilder& pass_builder = builder.render_passes[render_pass_id];
    RenderPass*          pass         = &render_passes[render_pass_id];

    pass->descriptors                  = init_array_zeroed<GpuDescriptor>(heap, pass_builder.descriptor_idx);

    for (const RgPassBuilder::ResourceAccessData& desc : pass_builder.read_resources)
    {
      RgResourceKey   resource_key = {0};
      resource_key.id              = desc.handle.id;

      for (u32 iframe = 0; iframe <= get_temporal_lifetime(desc.handle.temporal_lifetime); iframe++)
      {
        resource_key.temporal_frame = iframe;

        GpuDescriptor* dst = &pass->descriptors[desc.descriptor_idx + iframe];
        if (desc.handle.type == kResourceTypeBuffer)
        {
          const GpuBuffer* buffer = hash_table_find(&resource_map.buffers, resource_key);
          if (desc.descriptor_type == kDescriptorTypeSrv)
          {
            *dst = alloc_descriptor(g_DescriptorCbvSrvUavPool);
            init_buffer_srv(dst, buffer, desc.buffer_srv);
          }
          else if (desc.descriptor_type == kDescriptorTypeCbv)
          {
            *dst = alloc_descriptor(g_DescriptorCbvSrvUavPool);
            init_buffer_cbv(dst, buffer, desc.buffer_cbv);
          }
          else { /*This means that it's a vertex/index buffer */ }
        }
        else if(desc.handle.type == kResourceTypeTexture)
        {
          const GpuTexture* texture = hash_table_find(&resource_map.textures, resource_key);
          if (desc.descriptor_type == kDescriptorTypeSrv)
          {
            *dst = alloc_descriptor(g_DescriptorCbvSrvUavPool);
            init_texture_srv(dst, texture, desc.texture_srv);
          }
          else { UNREACHABLE; }
        }
        else { UNREACHABLE; }
      }
    }

    for (const RgPassBuilder::ResourceAccessData& desc : pass_builder.write_resources)
    {
      RgResourceKey resource_key = {0};
      resource_key.id            = desc.handle.id;

      // Back buffer descriptors get lazily initialized
      if (resource_key.id == kRgBackBufferId)
      {
        continue;
      }

      for (u32 iframe = 0; iframe <= get_temporal_lifetime(desc.handle.temporal_lifetime); iframe++)
      {
        resource_key.temporal_frame = iframe;

        GpuDescriptor* dst = &pass->descriptors[desc.descriptor_idx + iframe];

        if (desc.handle.type == kResourceTypeBuffer)
        {
          const GpuBuffer* buffer = hash_table_find(&resource_map.buffers, resource_key);
          if (desc.descriptor_type == kDescriptorTypeUav)
          {
            *dst = alloc_descriptor(g_DescriptorCbvSrvUavPool);
            init_buffer_uav(dst, buffer, desc.buffer_uav);
          }
          else { UNREACHABLE; }
        }
        else if(desc.handle.type == kResourceTypeTexture)
        {
          const GpuTexture* texture = hash_table_find(&resource_map.textures, resource_key);
          if (desc.descriptor_type == kDescriptorTypeUav)
          {
            *dst = alloc_descriptor(g_DescriptorCbvSrvUavPool);
            init_texture_uav(dst, texture, desc.texture_uav);
          }
          else if (desc.descriptor_type == kDescriptorTypeRtv)
          {
            *dst = alloc_descriptor(&descriptor_heap->rtv);
            init_rtv(dst, texture);
          }
          else if (desc.descriptor_type == kDescriptorTypeDsv)
          {
            *dst = alloc_descriptor(&descriptor_heap->dsv);
            init_dsv(dst, texture);
          }
          else { UNREACHABLE; }
        }
        else { UNREACHABLE; }
      }

    }
  }
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
      for (const RgPassBuilder::ResourceAccessData& data : pass.read_resources)
      {
        RgTemporalResourceKey key = {0};
        key.id                    = data.handle.id;
        key.temporal_frame        = data.temporal_frame;

        ResourceStates* states    = hash_table_find(&resource_states, key);

        if (!states->touched)
        {
          states->current = D3D12_RESOURCE_STATE_COMMON;
        }

        states->current |= get_d3d12_resource_state(data);
        states->touched  = true;
      }

      for (const RgPassBuilder::ResourceAccessData& data : pass.write_resources)
      {
        RgTemporalResourceKey key = {0};
        key.id                    = data.handle.id;
        key.temporal_frame        = 0;

        ResourceStates* states    = hash_table_find(&resource_states, key);
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

        ResourceStates* states    = hash_table_find(&resource_states, key);
  
        if (states->history != states->current)
        {
          RgResourceBarrier* dst                     = array_add(&level->barriers);
          dst->type                                  = kResourceBarrierTransition;
          dst->transition.before                     = states->history;
          dst->transition.after                      = states->current;
          dst->transition.resource_id                = handle.id;
          dst->transition.resource_type              = handle.type;
          dst->transition.resource_temporal_frame    = (s8)temporal_frame;
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

      ResourceStates* states    = hash_table_find(&resource_states, key);
      if (states->history == D3D12_RESOURCE_STATE_COMMON)
        continue;
      
      RgResourceBarrier* dst                     = array_add(out_exit_barriers);
      dst->type                                  = kResourceBarrierTransition;
      dst->transition.before                     = states->history;
      dst->transition.after                      = D3D12_RESOURCE_STATE_COMMON;
      dst->transition.resource_id                = handle.id;
      dst->transition.resource_type              = handle.type;
      dst->transition.resource_temporal_frame    = (s8)temporal_frame;
      dst->transition.resource_temporal_lifetime = handle.temporal_lifetime;
    }
  }
}

void
compile_render_graph(AllocHeap heap, const RgBuilder& builder, RenderGraphDestroyFlags flags)
{
  // You must write to the back buffer exactly once, never more.
  ASSERT(builder.back_buffer.id      == kRgBackBufferId);
  ASSERT(builder.back_buffer.version == 1);

  if (g_RenderGraph == nullptr)
  {
    g_RenderGraph = HEAP_ALLOC(RenderGraph, g_InitHeap, 1);
  }

  zero_memory(g_RenderGraph, sizeof(RenderGraph));

  if (flags & kRgDestroyCmdListAllocators)
  {
    g_RenderGraph->cmd_allocator     = init_cmd_list_allocator(heap, g_GpuDevice, &g_GpuDevice->graphics_queue, 4);
  }

  g_RenderGraph->render_passes     = init_array<RenderPass>(heap, builder.render_passes.size);
  g_RenderGraph->dependency_levels = init_dependency_levels(heap, builder);
  g_RenderGraph->back_buffer       = builder.back_buffer;
  g_RenderGraph->width             = builder.width;
  g_RenderGraph->height            = builder.height;

  for (const RgPassBuilder& pass_builder : builder.render_passes)
  {
    RenderPass* dst     = array_add(&g_RenderGraph->render_passes);
    dst->handler        = pass_builder.handler;
    dst->data           = pass_builder.data;
    dst->name           = pass_builder.name;
  }

  if (flags & kRgDestroyResourceHeaps)
  {
    g_RenderGraph->local_heap        = init_gpu_linear_allocator(MiB(700), kGpuHeapGpuOnly);
    for (u32 i = 0; i < kBackBufferCount; i++)
    {
      g_RenderGraph->sysram_upload_heaps[i] = init_gpu_linear_allocator(MiB(4), kGpuHeapSysRAMCpuToGpu);
    }

    for (u32 i = 0; i < kBackBufferCount; i++)
    {
      g_RenderGraph->vram_upload_heaps[i] = init_gpu_linear_allocator(MiB(16), kGpuHeapVRAMCpuToGpu);
    }

    g_RenderGraph->temporal_heaps = init_array<GpuLinearAllocator>(heap, kMaxTemporalLifetime);
    for (u8 i = 0; i < kMaxTemporalLifetime; i++)
    {
      GpuLinearAllocator* dst = array_add(&g_RenderGraph->temporal_heaps);
      *dst                    = init_gpu_linear_allocator(MiB(128), kGpuHeapGpuOnly);
    }
  }

  u32 buffer_count          = 0;
  u32 texture_count         = 0;
  for (ResourceHandle resource : builder.resource_list)
  {
    if (resource.id == builder.back_buffer.id)
      continue;

    if      (resource.type == kResourceTypeBuffer)  { buffer_count++;  }
    else if (resource.type == kResourceTypeTexture) { texture_count++; }
    else                                            { UNREACHABLE;     }
  }

  if (flags & kRgFreePhysicalResources)
  {
    RgDescriptorHeap descriptor_heap = {0};
    descriptor_heap.rtv              = init_descriptor_linear_allocator(g_GpuDevice, 128,  kDescriptorHeapTypeRtv);
    descriptor_heap.dsv              = init_descriptor_linear_allocator(g_GpuDevice, 128,  kDescriptorHeapTypeDsv);

    // We will preallocate the backbuffer descriptors and then initialize them at a later point.
    g_RenderGraph->back_buffer_rtv = alloc_descriptor(&descriptor_heap.rtv);
    g_RenderGraph->back_buffer_dsv = alloc_descriptor(&descriptor_heap.dsv);

    PhysicalResourceMap physical_resource_map = init_physical_resources(
      heap,
      builder,
      g_GpuDevice,
      &g_RenderGraph->local_heap,
      g_RenderGraph->sysram_upload_heaps,
      g_RenderGraph->vram_upload_heaps,
      g_RenderGraph->temporal_heaps.memory,
      buffer_count,
      texture_count
    );

    init_dependency_barriers(heap, builder, g_RenderGraph->dependency_levels, &g_RenderGraph->exit_barriers);

    init_physical_descriptors(
      heap,
      &descriptor_heap,
      builder,
      physical_resource_map,
      g_RenderGraph->render_passes.memory
    );

    g_RenderGraph->descriptor_heap = descriptor_heap;
    g_RenderGraph->buffer_map      = physical_resource_map.buffers;
    g_RenderGraph->texture_map     = physical_resource_map.textures;
  }
}

void
destroy_render_graph(RenderGraphDestroyFlags flags)
{
  if (flags & kRgFreePhysicalResources)
  {
    for (auto [_, buffer] : g_RenderGraph->buffer_map)
    {
      free_gpu_buffer(&buffer);
    }

    for (auto [key, texture] : g_RenderGraph->texture_map)
    {
      if (key.id == g_RenderGraph->back_buffer.id)
      {
        continue;
      }
      free_gpu_texture(&texture);
    }
  }

  if (flags & kRgDestroyResourceHeaps)
  {
    destroy_gpu_linear_allocator(&g_RenderGraph->local_heap);
    for (GpuLinearAllocator& upload_heap : g_RenderGraph->sysram_upload_heaps)
    {
      destroy_gpu_linear_allocator(&upload_heap);
    }

    for (GpuLinearAllocator& temporal_heap : g_RenderGraph->temporal_heaps)
    {
      destroy_gpu_linear_allocator(&temporal_heap);
    }
  }
  else
  {
    reset_gpu_linear_allocator(&g_RenderGraph->local_heap);
    for (GpuLinearAllocator& upload_heap : g_RenderGraph->sysram_upload_heaps)
    {
      reset_gpu_linear_allocator(&upload_heap);
    }

    for (GpuLinearAllocator& temporal_heap : g_RenderGraph->temporal_heaps)
    {
      reset_gpu_linear_allocator(&temporal_heap);
    }
  }

  destroy_descriptor_linear_allocator(&g_RenderGraph->descriptor_heap.rtv);
  destroy_descriptor_linear_allocator(&g_RenderGraph->descriptor_heap.dsv);

  if (flags & kRgDestroyCmdListAllocators)
  {
    destroy_cmd_list_allocator(&g_RenderGraph->cmd_allocator);
  }

  if (flags == kRgDestroyAll)
  {
    zero_memory(g_RenderGraph, sizeof(RenderGraph));
  }
}

u8
rg_get_temporal_frame(u32 frame_id, u32 temporal_lifetime, s8 offset)
{
  if (temporal_lifetime == kInfiniteLifetime)
  {
    ASSERT(offset == 0);
    return 0;
  }

  ASSERT(offset <= 0);
  ASSERT(-offset <= (s64)temporal_lifetime);

  if (temporal_lifetime == 0)
    return 0;
  
  temporal_lifetime++;
  ASSERT(temporal_lifetime >= 2);

  s32 signed_frame_id = (u32)frame_id;
  signed_frame_id    += offset;

  s64 ret = modulo(signed_frame_id, temporal_lifetime);
  ASSERT(ret >= 0);
  ASSERT((u32)ret < temporal_lifetime);
  return (u8)ret;
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
      key.temporal_frame = rg_get_temporal_frame(g_FrameId, barrier.transition.resource_temporal_lifetime, barrier.transition.resource_temporal_frame);

      ID3D12Resource* d3d12_resource = nullptr;
      if (barrier.transition.resource_type == kResourceTypeTexture)
      {
        GpuTexture* texture = hash_table_find(&graph->texture_map, key);
        d3d12_resource      = texture->d3d12_texture;
      }
      else if (barrier.transition.resource_type == kResourceTypeBuffer)
      {
        GpuBuffer* buffer = hash_table_find(&graph->buffer_map, key);
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
        GpuTexture* texture = hash_table_find(&graph->texture_map, key);
        d3d12_resource      = texture->d3d12_texture;
      }
      else if (barrier.uav.resource_type == kResourceTypeBuffer)
      {
        GpuBuffer* buffer = hash_table_find(&graph->buffer_map, key);
        d3d12_resource    = buffer->d3d12_buffer;
      } else { UNREACHABLE; }

      *dst = CD3DX12_RESOURCE_BARRIER::UAV(d3d12_resource);
    } break;
    default: UNREACHABLE;
  }
}

void 
execute_render_graph(const GpuTexture* back_buffer)
{
  // NOTE(Brandon): This is honestly kinda dangerous, since we're just straight up copying the back buffer struct instead
  // of the pointer to the GpuImage which is what I really want... maybe there's a nicer way of doing this?
  *hash_table_insert(&g_RenderGraph->texture_map, {g_RenderGraph->back_buffer.id, 0}) = *back_buffer;

  // Initialize all of the back buffer descriptors
  init_rtv(&g_RenderGraph->back_buffer_rtv, back_buffer);
  // init_dsv(&g_RenderGraph->back_buffer_dsv, back_buffer);

  CmdList       cmd_buffer = alloc_cmd_list(&g_RenderGraph->cmd_allocator);
  RenderContext ctx        = {0};
  ctx.m_CmdBuffer          = cmd_buffer;
  ctx.m_Width              = g_RenderGraph->width;
  ctx.m_Height             = g_RenderGraph->height;

  // Main render loop
  for (u32 ilevel = 0; ilevel < g_RenderGraph->dependency_levels.size; ilevel++)
  {
    RG_DBGLN("Level %u", ilevel);
    const RgDependencyLevel& level = g_RenderGraph->dependency_levels[ilevel];
    if (level.barriers.size > 0)
    {
      ScratchAllocator scratch_arena = alloc_scratch_arena();
      defer { free_scratch_arena(&scratch_arena); };

      Array d3d12_barriers = init_array<CD3DX12_RESOURCE_BARRIER>(scratch_arena, level.barriers.size);
  
      for (const RgResourceBarrier& barrier : level.barriers)
      {
        CD3DX12_RESOURCE_BARRIER* dst = array_add(&d3d12_barriers);
        get_d3d12_resource_barrier(g_RenderGraph, barrier, dst);
      }


#if defined(RENDER_GRAPH_VERBOSE)
      for (auto& barrier : d3d12_barriers)
      {
        cmd_buffer.d3d12_list->ResourceBarrier(1, &barrier);
      }
#else
      cmd_buffer.d3d12_list->ResourceBarrier((u32)d3d12_barriers.size, d3d12_barriers.memory);
#endif
    }

    for (RenderPassId pass_id : level.render_passes)
    {
      RG_DBGLN("%s", g_RenderGraph->render_passes[pass_id].name);
      set_descriptor_heaps(&cmd_buffer, {g_DescriptorCbvSrvUavPool});
      set_graphics_root_signature(&cmd_buffer);
      set_compute_root_signature(&cmd_buffer);

      g_HandlerId = pass_id;
      const RenderPass& pass = g_RenderGraph->render_passes[pass_id];
      (*pass.handler)(&ctx, pass.data);

      set_descriptor_heaps(&cmd_buffer, {g_DescriptorCbvSrvUavPool});
    }
  }

  // Execute all of the exit barriers
  if (g_RenderGraph->exit_barriers.size > 0)
  {
    ScratchAllocator scratch_arena = alloc_scratch_arena();
    defer { free_scratch_arena(&scratch_arena); };

    Array d3d12_barriers = init_array<CD3DX12_RESOURCE_BARRIER>(scratch_arena, g_RenderGraph->exit_barriers.size);

    for (const RgResourceBarrier& barrier : g_RenderGraph->exit_barriers)
    {
      CD3DX12_RESOURCE_BARRIER* dst = array_add(&d3d12_barriers);
      get_d3d12_resource_barrier(g_RenderGraph, barrier, dst);
    }

    cmd_buffer.d3d12_list->ResourceBarrier((u32)d3d12_barriers.size, d3d12_barriers.memory);
  }

  submit_cmd_lists(&g_RenderGraph->cmd_allocator, {cmd_buffer});
  g_FrameId++;;
}

RgPassBuilder*
add_render_pass(
  AllocHeap heap,
  RgBuilder* graph,
  CmdQueueType queue,
  const char* name,
  void* data,
  RenderHandler* handler,
  bool is_frame_init
) {
  RgPassBuilder* ret   = array_add(&graph->render_passes);

  ret->pass_id         = (u32)graph->render_passes.size - 1;
  ret->queue           = queue;
  ret->name            = name;
  ret->handler         = handler;
  ret->data            = data;
  ret->graph           = graph;

  ret->read_resources  = init_array<RgPassBuilder::ResourceAccessData>(heap, 32);
  ret->write_resources = init_array<RgPassBuilder::ResourceAccessData>(heap, 32);

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
  GpuFormat format
) {
  return rg_create_texture_ex(builder, name, width, height, format, 0);
}

RgHandle<GpuTexture>
rg_create_texture_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  GpuFormat format,
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
  u16 array_size,
  GpuFormat format
) {
  return rg_create_texture_array_ex(builder, name, width, height, array_size, format, 0);
}

RgHandle<GpuTexture>
rg_create_texture_array_ex(
  RgBuilder* builder,
  const char* name,
  u32 width,
  u32 height,
  u16 array_size,
  GpuFormat format,
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
  u32 size,
  u32 stride
) {
  return rg_create_buffer_ex(builder, name, size, stride, 0);
}

RgHandle<GpuBuffer>
rg_create_upload_buffer(
  RgBuilder* builder,
  const char* name,
  GpuHeapLocation location,
  u32 size,
  u32 stride
) {
  ResourceHandle resource_handle      = {0};
  resource_handle.id                  = handle_index(builder);
  resource_handle.version             = 0;
  resource_handle.type                = kResourceTypeBuffer;
  resource_handle.temporal_lifetime   = kBackBufferCount - 1;
  *array_add(&builder->resource_list) = resource_handle;

  TransientResourceDesc* desc         = hash_table_insert(&builder->resource_descs, resource_handle.id);
  desc->name                          = name;
  desc->type                          = resource_handle.type;
  desc->temporal_lifetime             = resource_handle.temporal_lifetime;
  desc->buffer_desc.size              = size;
  desc->buffer_desc.stride            = stride == 0 ? size : stride;

  ASSERT_MSG_FATAL(location == kGpuHeapSysRAMCpuToGpu || location == kGpuHeapVRAMCpuToGpu, "Attempting to create a render graph upload buffer that isn't actually located in an upload heap. This is probably not what you intended to do. Use either kGpuHeapSysRAMCpuToGpu or kGpuHeapVRAMCpuToGpu for the location.");
  desc->buffer_desc.heap_location     = location;

  RgHandle<GpuBuffer> ret = {resource_handle.id, resource_handle.version, resource_handle.temporal_lifetime};
  return ret;
}

RgHandle<GpuBuffer>
rg_create_buffer_ex(
  RgBuilder* builder,
  const char* name,
  u32 size,
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
  desc->buffer_desc.heap_location     = kGpuHeapGpuOnly;

  RgHandle<GpuBuffer> ret = {resource_handle.id, resource_handle.version, resource_handle.temporal_lifetime};
  return ret;
}

RgOpaqueDescriptor
rg_read_texture(RgPassBuilder* builder, RgHandle<GpuTexture> texture, const GpuTextureSrvDesc& desc, s8 temporal_frame)
{
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture.id && it->temporal_frame == temporal_frame));
  ASSERT(texture.id != kRgBackBufferId);
  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle          = texture;
  data->access          = (u32)kReadTextureSrv;
  data->temporal_frame  = temporal_frame;
  data->is_write        = false;
  data->descriptor_type = kDescriptorTypeSrv;
  data->descriptor_idx  = builder->descriptor_idx;

  data->texture_srv     = desc;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = texture.id;
  ret.temporal_frame       = temporal_frame;
  ret.temporal_lifetime    = texture.temporal_lifetime;

  builder->descriptor_idx += texture.temporal_lifetime + 1;

  return ret;
}

RgOpaqueDescriptor
rg_write_texture(RgPassBuilder* builder, RgHandle<GpuTexture>* texture, const GpuTextureUavDesc& desc)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture->id && it->temporal_frame == 0));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture->id));
  ASSERT(texture->id != kRgBackBufferId);

  defer { texture->version++; };

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle          = *texture;
  data->access          = (u32)kWriteTextureUav;
  data->temporal_frame  = 0;
  data->is_write        = true;
  data->descriptor_type = kDescriptorTypeUav;
  data->descriptor_idx  = builder->descriptor_idx;

  data->texture_uav     = desc;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = texture->id;
  ret.temporal_frame       = 0;
  ret.temporal_lifetime    = texture->temporal_lifetime;

  builder->descriptor_idx += texture->temporal_lifetime + 1;

  return ret;
}

RgOpaqueDescriptor
rg_write_rtv(RgPassBuilder* builder, RgHandle<GpuTexture>* texture)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture->id && it->temporal_frame == 0));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture->id));

  defer
  {
    texture->version++;
  };

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle          = *texture;
  data->access          = (u32)kWriteTextureColorTarget;
  data->temporal_frame  = 0;
  data->is_write        = true;
  data->descriptor_type = kDescriptorTypeRtv;
  data->descriptor_idx  = builder->descriptor_idx;

  if (texture->id == kRgBackBufferId)
  {
    data->descriptor_idx     = U32_MAX;

    RgOpaqueDescriptor ret   = {0};
    ret.pass_id              = U32_MAX;
    ret.descriptor_idx       = U32_MAX;
    ret.resource_id          = texture->id;
    ret.temporal_frame       = 0;
    ret.temporal_lifetime    = kInfiniteLifetime;

    return ret;
  }

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = texture->id;
  ret.temporal_frame       = 0;
  ret.temporal_lifetime    = texture->temporal_lifetime;

  builder->descriptor_idx += texture->temporal_lifetime + 1;

  return ret;
}

RgOpaqueDescriptor
rg_write_dsv(RgPassBuilder* builder, RgHandle<GpuTexture>* texture)
{
  ASSERT(!array_find(&builder->read_resources,  it->handle.id == texture->id && it->temporal_frame == 0));
  ASSERT(!array_find(&builder->write_resources, it->handle.id == texture->id));

  defer
  {
    texture->version++;
  };

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle          = *texture;
  data->access          = (u32)kWriteTextureDepthStencil;
  data->temporal_frame  = 0;
  data->is_write        = true;
  data->descriptor_type = kDescriptorTypeDsv;
  data->descriptor_idx  = builder->descriptor_idx;

  if (texture->id == kRgBackBufferId)
  {
    data->descriptor_idx     = U32_MAX;

    RgOpaqueDescriptor ret   = {0};
    ret.pass_id              = U32_MAX;
    ret.descriptor_idx       = U32_MAX;
    ret.resource_id          = texture->id;
    ret.temporal_frame       = 0;
    ret.temporal_lifetime    = kInfiniteLifetime;

    return ret;
  }

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = texture->id;
  ret.temporal_frame       = 0;
  ret.temporal_lifetime    = texture->temporal_lifetime;

  builder->descriptor_idx += texture->temporal_lifetime + 1;

  return ret;
}

RgOpaqueDescriptor
rg_read_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, const GpuBufferCbvDesc& desc, s8 temporal_frame)
{
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer.id && it->temporal_frame == temporal_frame));
  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle          = buffer;
  data->access          = (u32)kReadBufferCbv;
  data->temporal_frame  = temporal_frame;
  data->is_write        = false;
  data->descriptor_type = kDescriptorTypeCbv;
  data->descriptor_idx  = builder->descriptor_idx;

  data->buffer_cbv      = desc;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = buffer.id;
  ret.temporal_frame       = temporal_frame;
  ret.temporal_lifetime    = buffer.temporal_lifetime;

  builder->descriptor_idx += buffer.temporal_lifetime + 1;

  return ret;
}

RgOpaqueDescriptor
rg_read_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer, const GpuBufferSrvDesc& desc, s8 temporal_frame)
{
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer.id && it->temporal_frame == temporal_frame));
  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle          = buffer;
  data->access          = (u32)kReadBufferSrv;
  data->temporal_frame  = temporal_frame;
  data->is_write        = false;
  data->descriptor_type = kDescriptorTypeSrv;
  data->descriptor_idx  = builder->descriptor_idx;

  data->buffer_srv      = desc;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = buffer.id;
  ret.temporal_frame       = temporal_frame;
  ret.temporal_lifetime    = buffer.temporal_lifetime;

  builder->descriptor_idx += buffer.temporal_lifetime + 1;

  return ret;
}

RgOpaqueDescriptor
rg_write_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer>* buffer, const GpuBufferUavDesc& desc)
{
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer->id && it->temporal_frame == 0));

  defer { buffer->version++; };

  RgPassBuilder::ResourceAccessData* data = array_add(&builder->write_resources);
  data->handle          = *buffer;
  data->access          = (u32)kWriteBufferUav;
  data->temporal_frame  = 0;
  data->is_write        = true;
  data->descriptor_type = kDescriptorTypeUav;
  data->descriptor_idx  = builder->descriptor_idx;

  data->buffer_uav      = desc;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.descriptor_idx       = builder->descriptor_idx;
  ret.resource_id          = buffer->id;
  ret.temporal_frame       = 0;
  ret.temporal_lifetime    = buffer->temporal_lifetime;

  builder->descriptor_idx += buffer->temporal_lifetime + 1;

  return ret;
}


RgOpaqueDescriptor
rg_read_index_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer)
{
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer.id));
  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle          = buffer;
  data->access          = (u32)kReadBufferIndex;
  data->temporal_frame  = 0;
  data->is_write        = false;
  data->descriptor_type = kDescriptorTypeNull;
  data->descriptor_idx  = builder->descriptor_idx;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.resource_id          = buffer.id;

  return ret;
}

RgOpaqueDescriptor
rg_read_vertex_buffer(RgPassBuilder* builder, RgHandle<GpuBuffer> buffer)
{
  ASSERT(!array_find(&builder->write_resources, it->handle.id == buffer.id));
  RgPassBuilder::ResourceAccessData* data = array_add(&builder->read_resources);
  data->handle          = buffer;
  data->access          = (u32)kReadBufferVertex;
  data->temporal_frame  = 0;
  data->is_write        = false;
  data->descriptor_type = kDescriptorTypeNull;
  data->descriptor_idx  = builder->descriptor_idx;

  RgOpaqueDescriptor ret   = {0};
  ret.pass_id              = builder->pass_id;
  ret.resource_id          = buffer.id;

  return ret;
}

template <>
const GpuBuffer*
rg_deref_buffer<RgIndexBuffer>(RgIndexBuffer rg_descriptor)
{
  RgResourceKey key  = {0};
  key.id             = rg_descriptor.m_ResourceId;
  key.temporal_frame = rg_get_temporal_frame(g_FrameId, 0, 0);

  return hash_table_find(&g_RenderGraph->buffer_map, key);
}

template <>
const GpuBuffer*
rg_deref_buffer<RgVertexBuffer>(RgVertexBuffer rg_descriptor)
{
  RgResourceKey key  = {0};
  key.id             = rg_descriptor.m_ResourceId;
  key.temporal_frame = rg_get_temporal_frame(g_FrameId, 0, 0);

  return hash_table_find(&g_RenderGraph->buffer_map, key);
}

template <>
const GpuBuffer*
rg_deref_buffer<RgCpuUploadBuffer>(RgCpuUploadBuffer rg_descriptor)
{
  RgResourceKey key  = {0};
  key.id             = rg_descriptor.m_ResourceId;
  key.temporal_frame = rg_get_temporal_frame(g_FrameId, rg_descriptor.m_TemporalLifetime, 0);

  return hash_table_find(&g_RenderGraph->buffer_map, key);
}

void
RenderContext::clear_depth_stencil_view(
  RgDsv depth_stencil,
  DepthStencilClearFlags flags,
  f32 depth,
  u8 stencil
) {
  const GpuDescriptor* descriptor = depth_stencil.deref();

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
  RgRtv render_target_view,
  const Vec4& rgba
) {
  const GpuDescriptor* descriptor = render_target_view.deref();

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
    ASSERT(buffer->desc.size <= U32_MAX);
    size = (u32)buffer->desc.size;
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
RenderContext::ia_set_index_buffer(RgIndexBuffer buffer, u32 stride, u32 size)
{
  const GpuBuffer* physical = rg_deref_buffer(buffer);
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
  RgVertexBuffer buffer,
  u32 size,
  u32 stride
) {
  const GpuBuffer* physical = rg_deref_buffer(buffer);
  ia_set_vertex_buffer(start_slot, physical, size, stride);
}

void
RenderContext::clear_state()
{
  m_CmdBuffer.d3d12_list->ClearState(nullptr);
}

void
RenderContext::om_set_render_targets(
  Span<RgRtv> rtvs,
  Option<RgDsv> dsv
) {
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
  u32 rtv_count = 0;
  ASSERT(rtvs.size <= ARRAY_LENGTH(rtv_handles));

  for (RgRtv handle : rtvs)
  {
    const GpuDescriptor* descriptor = handle.deref();
    rtv_handles[rtv_count++]     = descriptor->cpu_handle;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
  if (dsv)
  {
    const GpuDescriptor* descriptor = unwrap(dsv).deref();
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
RenderContext::set_graphics_root_shader_resource_view(
  u32 root_parameter_index,
  const GpuBuffer* buffer
) {
  m_CmdBuffer.d3d12_list->SetGraphicsRootShaderResourceView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_compute_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer* buffer)
{
  m_CmdBuffer.d3d12_list->SetComputeRootConstantBufferView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_graphics_root_constant_buffer_view(u32 root_parameter_index, const GpuBuffer* buffer)
{
  m_CmdBuffer.d3d12_list->SetGraphicsRootConstantBufferView(root_parameter_index, buffer->gpu_addr);
}

void
RenderContext::set_graphics_root_32bit_constants(
  u32 root_parameter_index,
  const u32* src,
  u32 count,
  u32 dst_offset
) {
  m_CmdBuffer.d3d12_list->SetGraphicsRoot32BitConstants(root_parameter_index, count, src, dst_offset);
}

void
RenderContext::set_compute_root_32bit_constants(u32 root_parameter_index, const u32* src, u32 count, u32 dst_offset)
{
  m_CmdBuffer.d3d12_list->SetComputeRoot32BitConstants(root_parameter_index, count, src, dst_offset);
}

void
RenderContext::set_descriptor_heaps(Span<const DescriptorLinearAllocator*> heaps)
{
  m_CmdBuffer.d3d12_list->SetDescriptorHeaps(1, &heaps[0]->d3d12_heap);
}

void
RenderContext::write_cpu_upload_buffer(RgCpuUploadBuffer dst, const void* src, u64 size)
{
  const GpuBuffer* physical = rg_deref_buffer(dst);
  write_cpu_upload_buffer(physical, src, size);
}

void
RenderContext::write_cpu_upload_buffer(const GpuBuffer* dst, const void* src, u64 size)
{
  ASSERT_MSG_FATAL(size <= dst->desc.size, "Buffer overwrite detected from CPU to upload GPU buffer. Attempted to write 0x%llx bytes into buffer with only 0x%llx bytes", size, dst->desc.size);

  void* ptr = unwrap(dst->mapped);
  memcpy(ptr, src, size);
}
