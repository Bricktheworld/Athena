#pragma once
#include "Core/Engine/Render/render_graph.h"

struct BlitParams
{
  RgReadHandle<GpuImage>  src;
  RgWriteHandle<GpuImage> dst;
};

void init_back_buffer_blit(AllocHeap heap, RgBuilder* builder, RgHandle<GpuImage> src);
void render_handler_back_buffer_blit(RenderContext* ctx, const void* data);

