#pragma once
#include "Core/Engine/Render/render_graph.h"

RgHandle<GpuImage> init_tonemapped_buffer(RgBuilder* builder);

RgHandle<GpuImage> init_post_processing(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  RgHandle<GpuImage> hdr_buffer
);
