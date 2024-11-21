#pragma once
#include "Core/Engine/Render/render_graph.h"

RgHandle<GpuTexture> init_tonemapped_buffer(RgBuilder* builder);

RgHandle<GpuTexture> init_post_processing(
  AllocHeap heap,
  RgBuilder* builder,
  const GpuDevice* device,
  RgHandle<GpuTexture> hdr_buffer
);
