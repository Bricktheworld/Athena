#pragma once
#include "Core/Engine/Render/render_graph.h"

struct PostProcessingParams
{
  RgReadHandle<GpuImage>  hdr_buffer;
  RgWriteHandle<GpuImage> dst;
};

RgHandle<GpuImage> init_tonemapped_buffer(RgBuilder* builder);

RgHandle<GpuImage> init_post_processing(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  RgHandle<GpuImage> hdr_buffer
);

void render_handler_post_processing(RenderContext* context, const void* data);
