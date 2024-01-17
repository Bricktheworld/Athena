#pragma once
#include "Core/Engine/Render/render_graph.h"

struct PostProcessingParams
{
  RgReadHandle<GpuImage>  hdr_buffer;
  RgWriteHandle<GpuImage> back_buffer;
};

void init_post_processing(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  RgHandle<GpuImage> hdr_buffer
);

void render_handler_post_processing(RenderContext* context, const void* data);
