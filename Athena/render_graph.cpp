#include "render_graph.h"
#include "context.h"
#include <windows.h>
#include "vendor/d3dx12.h"

namespace rg
{
	RenderGraph
	init_render_graph(MEMORY_ARENA_PARAM)
	{
		RenderGraph ret = {0};
		ret.render_passes = init_array<RenderPass>(MEMORY_ARENA_FWD, 64);
		ret.imported_resources = init_hash_table<ResourceHandle, ImportedResource>(MEMORY_ARENA_FWD, 32);
		ret.transient_resources = init_hash_table<ResourceHandle, TransientResource>(MEMORY_ARENA_FWD, 32);
		ret.handle_index = 0;

		return ret;
	}

	static u64
	handle_index(RenderGraph* graph)
	{
		return ++graph->handle_index;
	}

	TransientResourceCache
	init_transient_resource_cache(MEMORY_ARENA_PARAM)
	{
		TransientResourceCache ret = {0};
		ret.images = init_hash_table<GpuImageDesc, Array<GpuImage>>(MEMORY_ARENA_FWD, 32);
		ret.buffers = init_hash_table<GpuBufferDesc, Array<GpuBuffer>>(MEMORY_ARENA_FWD, 32);
		return ret;
	}

	enum CmdType : u8
	{
		CMD_CLEAR_RENDER_TARGET,
		CMD_CLEAR_DEPTH_STENCIL,
		CMD_SET_PIPELINE,
		CMD_SET_VIEWPORT,
		CMD_SET_RENDER_TARGET,
		CMD_SET_VERTEX_BUFFER,
		CMD_SET_INDEX_BUFFER,
		CMD_DRAW_INDEXED_INSTANCED,

		CMD_COUNT,
	};


	struct CmdClearRenderTargetView
	{
		Vec4 clear_color;
		WriteHandle<GpuImage> render_target;
	};

	struct CmdClearDepthStencilView
	{
		f32 depth = 0.0f;
		u32 stencil = 0;
		D3D12_CLEAR_FLAGS clear_flags;
		WriteHandle<GpuImage> depth_stencil;
	};

	struct CmdSetPipeline
	{
		Handle<GraphicsPipeline> pipeline;
	};

	struct CmdSetViewport
	{
		Viewport viewport;
	};

	struct CmdSetRenderTarget
	{
		Option<WriteHandle<GpuImage>> render_target;
		Option<WriteHandle<GpuImage>> depth_stencil;
	};

	struct CmdSetVertexBuffer
	{
		ReadHandle<GpuBuffer> vertex_buffer;
	};

	struct CmdSetIndexBuffer
	{
		ReadHandle<GpuBuffer> index_buffer;
	};

	struct CmdDrawIndexedInstanced
	{
		u32 index_count = 0;
		u32 instance_count = 0;
		u32 start_index_location = 0;
		s32 base_vertex_location = 0;
		u32 start_instance_location = 0;
	};

	struct GraphicsCmd
	{
		union
		{
			CmdClearRenderTargetView clear_render_target_view;
			CmdClearDepthStencilView clear_depth_stencil_view;
			CmdSetPipeline set_pipeline;
			CmdSetViewport set_viewport;
			CmdSetRenderTarget set_render_target;
			CmdSetVertexBuffer set_vertex_buffer;
			CmdSetIndexBuffer set_index_buffer;
			CmdDrawIndexedInstanced draw_indexed_instanced;
		};
		CmdType type;
		GraphicsCmd() { zero_memory(this, sizeof(GraphicsCmd)); }
	};

	RenderPass*
	add_render_pass(MEMORY_ARENA_PARAM, RenderGraph* graph, const char* name, u64 max_cmds)
	{
		RenderPass* ret = array_add(&graph->render_passes);

		ret->cmd_buffer = init_array<GraphicsCmd>(MEMORY_ARENA_FWD, max_cmds);
		ret->read_resources = init_array<ResourceHandle>(MEMORY_ARENA_FWD, 16);
		ret->written_resources = init_array<ResourceHandle>(MEMORY_ARENA_FWD, 16);
		ret->pass_id = graph->render_passes.size - 1;

		return ret;
	}


	Handle<GpuImage>
	render_graph_create_image(RenderGraph* graph, const char* name, GpuImageDesc desc)
	{
		ResourceHandle resource_handle = {0};
		resource_handle.id = handle_index(graph);
		resource_handle.type = RESOURCE_TYPE_IMAGE;
		resource_handle.lifetime = RESOURCE_LIFETIME_TRANSIENT;

		TransientResource* resource = hash_table_insert(&graph->transient_resources, resource_handle);
		resource->image_desc = desc;
		resource->type = resource_handle.type;

		Handle<GpuImage> ret;
		ret.id = resource_handle.id;
		ret.lifetime = resource_handle.lifetime;
		return ret;
	}

	Handle<GpuBuffer>
	render_graph_create_buffer(RenderGraph* graph, const char* name, GpuBufferDesc desc)
	{
		ResourceHandle resource_handle = {0};
		resource_handle.id = handle_index(graph);
		resource_handle.type = RESOURCE_TYPE_BUFFER;
		resource_handle.lifetime = RESOURCE_LIFETIME_TRANSIENT;

		TransientResource* resource = hash_table_insert(&graph->transient_resources, resource_handle);
		resource->buffer_desc = desc;
		resource->type = resource_handle.type;

		Handle<GpuBuffer> ret;
		ret.id = resource_handle.id;
		ret.lifetime = resource_handle.lifetime;
		return ret;
	}

	// TODO(Brandon): We need to ensure that you can't import the same resource twice.
	// Probably fix through just storing a HashSet of void*
	Handle<GpuImage>
	render_graph_import_image(RenderGraph* graph, GpuImage* image)
	{
		ResourceHandle resource_handle = {0};
		resource_handle.id = handle_index(graph);
		resource_handle.type = RESOURCE_TYPE_IMAGE;
		resource_handle.lifetime = RESOURCE_LIFETIME_IMPORTED;

		ImportedResource* resource = hash_table_insert(&graph->imported_resources, resource_handle);
		resource->image = image;
		resource->type = resource_handle.type;

		Handle<GpuImage> ret;
		ret.id = resource_handle.id;
		ret.lifetime = resource_handle.lifetime;
		return ret;
	}

	Handle<GpuBuffer>
	render_graph_import_buffer(RenderGraph* graph, GpuBuffer* buffer)
	{
		ResourceHandle resource_handle = {0};
		resource_handle.id = handle_index(graph);
		resource_handle.type = RESOURCE_TYPE_BUFFER;
		resource_handle.lifetime = RESOURCE_LIFETIME_IMPORTED;

		ImportedResource* resource = hash_table_insert(&graph->imported_resources, resource_handle);
		resource->buffer = buffer;
		resource->type = resource_handle.type;

		Handle<GpuBuffer> ret;
		ret.id = resource_handle.id;
		ret.lifetime = resource_handle.lifetime;
		return ret;
	}

	WriteHandle<GpuImage>
	render_pass_write(RenderPass* render_pass, Handle<GpuImage>* img)
	{
		ASSERT(!array_find(&render_pass->written_resources, it->id == img->id));

		ResourceHandle* handle = array_add(&render_pass->written_resources);
		handle->id = img->id;
		handle->type = RESOURCE_TYPE<GpuImage>;
		handle->lifetime = img->lifetime;

		WriteHandle<GpuImage> ret;
		ret.id = img->id;
		ret.pass_id = render_pass->pass_id;

		return ret;
	}

	WriteHandle<GpuBuffer>
	render_pass_write(RenderPass* render_pass, Handle<GpuBuffer>* buf)
	{
		ASSERT(!array_find(&render_pass->written_resources, it->id == buf->id));

		ResourceHandle* handle = array_add(&render_pass->written_resources);
		handle->id = buf->id;
		handle->type = RESOURCE_TYPE<GpuBuffer>;
		handle->lifetime = buf->lifetime;

		WriteHandle<GpuBuffer> ret;
		ret.id = buf->id;
		ret.pass_id = render_pass->pass_id;

		return ret;
	}

	ReadHandle<GpuImage>
	render_pass_read(RenderPass* render_pass, Handle<GpuImage> img)
	{
		ASSERT(img.id != 0);
		ASSERT(!array_find(&render_pass->read_resources, it->id == img.id));

		ResourceHandle* handle = array_add(&render_pass->read_resources);
		handle->id = img.id;
		handle->type = RESOURCE_TYPE<GpuImage>;
		handle->lifetime = img.lifetime;

		ReadHandle<GpuImage> ret;
		ret.id = img.id;
		ret.pass_id = render_pass->pass_id;
		return ret;
	}

	ReadHandle<GpuBuffer>
	render_pass_read(RenderPass* render_pass, Handle<GpuBuffer> buf)
	{
		ASSERT(buf.id != 0);
		ASSERT(!array_find(&render_pass->read_resources, it->id == buf.id));

		ResourceHandle* handle = array_add(&render_pass->read_resources);
		handle->id = buf.id;
		handle->type = RESOURCE_TYPE<GpuBuffer>;
		handle->lifetime = buf.lifetime;

		ReadHandle<GpuBuffer> ret;
		ret.id = buf.id;
		ret.pass_id = render_pass->pass_id;
		return ret;
	}

	void
	cmd_clear_render_target(RenderPass* pass,
	                        WriteHandle<GpuImage> render_target,
	                        Vec4 clear_color)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_CLEAR_RENDER_TARGET;
		cmd.clear_render_target_view.clear_color = clear_color;
		cmd.clear_render_target_view.render_target = render_target;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_clear_depth_stencil(RenderPass* pass,
	                        WriteHandle<GpuImage> depth_stencil,
	                        float depth,
	                        u32 stencil)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_CLEAR_DEPTH_STENCIL;
		cmd.clear_depth_stencil_view.depth = depth;
		cmd.clear_depth_stencil_view.stencil = stencil;
		cmd.clear_depth_stencil_view.clear_flags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
		cmd.clear_depth_stencil_view.depth_stencil = depth_stencil;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_set_pipeline(RenderPass* pass, Handle<GraphicsPipeline> pipeline)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_SET_PIPELINE;
		cmd.set_pipeline.pipeline = pipeline;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_set_viewport(RenderPass* pass, Viewport viewport)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_SET_VIEWPORT;
		cmd.set_viewport.viewport = viewport;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_set_render_target(RenderPass* pass,
	                      Option<WriteHandle<GpuImage>> render,
	                      Option<WriteHandle<GpuImage>> depth_stencil)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_SET_RENDER_TARGET;
		cmd.set_render_target.render_target = render;
		cmd.set_render_target.depth_stencil = depth_stencil;;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_set_vertex_buffer(RenderPass* pass, ReadHandle<GpuBuffer> vertex_buffer)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_SET_VERTEX_BUFFER;
		cmd.set_vertex_buffer.vertex_buffer = vertex_buffer;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_set_index_buffer(RenderPass* pass, ReadHandle<GpuBuffer> index_buffer)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_SET_INDEX_BUFFER;
		cmd.set_index_buffer.index_buffer = index_buffer;

		*array_add(&pass->cmd_buffer) = cmd;
	}

	void
	cmd_draw_indexed_instanced(RenderPass* pass,
	                           u32 index_count,
	                           u32 instance_count,
	                           u32 start_index_location,
	                           s32 base_vertex_location,
	                           u32 start_instance_location)
	{
		GraphicsCmd cmd;
		cmd.type = CMD_DRAW_INDEXED_INSTANCED;
		cmd.draw_indexed_instanced.index_count = index_count;
		cmd.draw_indexed_instanced.instance_count = instance_count;
		cmd.draw_indexed_instanced.start_index_location = start_index_location;
		cmd.draw_indexed_instanced.base_vertex_location = base_vertex_location;
		cmd.draw_indexed_instanced.start_instance_location = start_instance_location;

		*array_add(&pass->cmd_buffer) = cmd;
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
				if (other.pass_id == pass->pass_id)
					continue;

				for (ResourceHandle& read_resource : other.read_resources)
				{
					bool other_depends_on_pass = array_find(&pass->written_resources, it->id == read_resource.id);

					if (!other_depends_on_pass)
						continue;

					*array_add(pass_adjacency_list) = other.pass_id;
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
			for (size_t pass_id = 0; pass_id < graph->render_passes.size; pass_id++)
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

			u64 dependency_level_count = 0;

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
			for (u64 pass_id : topological_list)
			{
				u64 level_index = longest_distances[pass_id];
				DependencyLevel* level = array_at(out_dependency_levels, level_index);
				*array_add(&level->passes) = pass_id;
			}
		}
	}

	CompiledRenderGraph
	compile_render_graph(MEMORY_ARENA_PARAM,
	                     RenderGraph* graph,
	                     TransientResourceCache* cache)
	{
		{
			USE_SCRATCH_ARENA();
			auto dependency_levels = init_array<DependencyLevel>(SCRATCH_ARENA_PASS, graph->render_passes.size);
			for (size_t i = 0; i < graph->render_passes.size; i++)
			{
				DependencyLevel* level = array_add(&dependency_levels); 
				level->passes = init_array<RenderPassId>(SCRATCH_ARENA_PASS, graph->render_passes.size);
			}
	
			build_dependency_list(graph, &dependency_levels);
		}

		CompiledRenderGraph ret = {0};
		return ret;
	}
}