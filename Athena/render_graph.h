#pragma once
#include "graphics.h"
#include "memory/memory.h"
#include "hash_table.h"
#include "array.h"
#include "math/math.h"

namespace rg
{
	enum ResourceLifetime : u8 
	{
		RESOURCE_LIFETIME_IMPORTED,
		RESOURCE_LIFETIME_TRANSIENT,

		RESOURCE_LIFETIME_COUNT,
	};
	
	enum ResourceType : u8
	{
		RESOURCE_TYPE_IMAGE,
		RESOURCE_TYPE_BUFFER,
		RESOURCE_TYPE_SHADER,
		RESOURCE_TYPE_PIPELINE,

		RESOURCE_TYPE_COUNT,
	};

	struct ImportedResource
	{
		union
		{
			GpuImage* image;
			GpuBuffer* buffer;
			GpuShader* shader;
			GraphicsPipeline* pipeline;
		};
		ResourceType type = RESOURCE_TYPE_IMAGE;
	};

	struct TransientResource
	{
		union
		{
			GpuImageDesc image_desc;
			GpuBufferDesc buffer_desc;
		};
		ResourceType type = RESOURCE_TYPE_IMAGE;
	};

	struct ResourceHandle
	{
		u64 id = 0;
		ResourceType type = RESOURCE_TYPE_IMAGE;
		ResourceLifetime lifetime = RESOURCE_LIFETIME_IMPORTED;
	};

	template <typename T>
	inline constexpr ResourceType RESOURCE_TYPE;

#define RESOURCE_TEMPLATE_TYPE(T, enum_type) \
	template <> \
	inline constexpr ResourceType RESOURCE_TYPE<T> = enum_type

	RESOURCE_TEMPLATE_TYPE(GpuImage, RESOURCE_TYPE_IMAGE);
	RESOURCE_TEMPLATE_TYPE(GpuBuffer, RESOURCE_TYPE_BUFFER);
	RESOURCE_TEMPLATE_TYPE(GpuShader, RESOURCE_TYPE_SHADER);
	RESOURCE_TEMPLATE_TYPE(GraphicsPipeline, RESOURCE_TYPE_PIPELINE);

	template <typename T>
	struct Handle
	{
		u64 id = 0;
		ResourceLifetime lifetime = RESOURCE_LIFETIME_IMPORTED;
	};

	template <typename T>
	struct PassHandle
	{
		u64 id = 0;
		u64 pass_id = 0;
	};

	template <typename T>
	struct WriteHandle
	{
		u64 id = 0;
		u64 pass_id = 0;
	};

	template <typename T>
	struct ReadHandle
	{
		u64 id = 0;
		u64 pass_id = 0;
	};

	struct TransientResourceCache
	{
		HashTable<GpuImageDesc, Array<GpuImage>> images;
		HashTable<GpuBufferDesc, Array<GpuBuffer>> buffers;
	};

	typedef u64 RenderPassId;

	enum RenderPassQueue : u8
	{
		RENDER_PASS_QUEUE_GRAPHICS,
		RENDER_PASS_QUEUE_COMPUTE,
		RENDER_PASS_QUEUE_COPY,

		RENDER_PASS_QUEUE_COUNT,
	};

	struct GraphicsCmd;


	struct RenderPass
	{
		Array<GraphicsCmd> cmd_buffer;
		Array<ResourceHandle> read_resources;
		Array<ResourceHandle> written_resources;
		RenderPassId pass_id = 0;

		RenderPassQueue queue = RENDER_PASS_QUEUE_GRAPHICS;
	};

	struct RenderGraph
	{
		Array<RenderPass> render_passes;
		HashTable<ResourceHandle, ImportedResource> imported_resources;
		HashTable<ResourceHandle, TransientResource> transient_resources;
		u64 handle_index = 0;
	};

	struct CompiledRenderGraph
	{
		bool test;
	};

	RenderGraph init_render_graph(MEMORY_ARENA_PARAM);
	TransientResourceCache init_transient_resource_cache(MEMORY_ARENA_PARAM);
	RenderPass* add_render_pass(MEMORY_ARENA_PARAM, RenderGraph* graph, const char* name, u64 max_cmds = 128);
	CompiledRenderGraph compile_render_graph(MEMORY_ARENA_PARAM, RenderGraph* render_graph, TransientResourceCache* cache);
	void execute_render_graph(CompiledRenderGraph* compiled_render_graph);

	Handle<GpuImage> render_graph_create_image(RenderGraph* graph, const char* name, GpuImageDesc desc);
	Handle<GpuBuffer> render_graph_create_buffer(RenderGraph* graph, const char* name, GpuBufferDesc desc);

	Handle<GpuImage> render_graph_import_image(RenderGraph* graph, GpuImage* image);
	Handle<GpuBuffer> render_graph_import_buffer(RenderGraph* graph, GpuBuffer* buffer);
	Handle<GpuShader> render_graph_import_shader(RenderGraph* graph, GpuShader* shader);
	Handle<GraphicsPipeline> render_graph_import_pipeline(RenderGraph* graph, GraphicsPipeline* pipeline);
	
	WriteHandle<GpuImage> render_pass_write(RenderPass* render_pass, Handle<GpuImage>* img);
	WriteHandle<GpuBuffer> render_pass_write(RenderPass* render_pass, Handle<GpuBuffer>* buf);

	ReadHandle<GpuImage> render_pass_read(RenderPass* render_pass, Handle<GpuImage> img);
	ReadHandle<GpuBuffer> render_pass_read(RenderPass* render_pass, Handle<GpuBuffer> buf);

	void cmd_clear_render_target(RenderPass* pass,
	                             WriteHandle<GpuImage> render_target,
	                             Vec4 clear_color);
	void cmd_clear_depth_stencil(RenderPass* pass,
	                             WriteHandle<GpuImage> depth_stencil,
	                             float depth,
	                             u32 stencil);
	void cmd_set_pipeline(RenderPass* pass, Handle<GraphicsPipeline> pipeline);

	struct Viewport
	{
		f32 top_left_x = 0.0f;
		f32 top_left_y = 0.0f;
		f32 width = 0.0f;
		f32 height = 0.0f;
		f32 min_depth = 0.0f;
		f32 max_depth = 0.0f;
	};

	void cmd_set_viewport(RenderPass* pass, Viewport viewport);
	void cmd_set_render_target(RenderPass* pass,
	                           Option<WriteHandle<GpuImage>> render,
	                           Option<WriteHandle<GpuImage>> depth_stencil);

	void cmd_set_vertex_buffer(RenderPass* pass, ReadHandle<GpuBuffer> vertex_buffer);
	void cmd_set_index_buffer(RenderPass* pass, ReadHandle<GpuBuffer> index_buffer);
	void cmd_draw_indexed_instanced(RenderPass* pass,
	                                u32 index_count,
	                                u32 instance_count = 1,
	                                u32 start_index_location = 0,
	                                s32 base_vertex_location = 0,
	                                u32 start_instance_location = 0);
}

