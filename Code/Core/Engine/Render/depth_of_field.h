#pragma once
#include "Core/Engine/Render/render_graph.h"

RgHandle<GpuTexture> init_depth_of_field(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> depth_buffer,
  RgHandle<GpuTexture> taa_buffer
);
