#pragma once
#include "memory/memory.h"
#include "array.h"

// Everything is so small that I just use worst-case arrays
#define MAX_PASS_DEPENDENCIES 32

typedef u64 AttachmentHandle;
typedef u64 BufferHandle;

struct RenderPass
{
};

struct RenderGraph
{
	Array<RenderPass> render_passes;
};

struct CompiledRenderGraph
{
};

RenderGraph init_render_graph(MEMORY_ARENA_PARAM);
RenderPass* add_render_pass(RenderGraph* graph, const char* name);
CompiledRenderGraph compile_render_graph(MEMORY_ARENA_PARAM, RenderGraph* render_graph);

void begin_render_pass(RenderPass* render_pass);
void end_render_pass(RenderPass* render_pass);
