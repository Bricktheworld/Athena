#pragma once
#include "Core/Engine/Render/render_graph.h"

RgHandle<GpuTexture> init_tonemapping(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> hdr_buffer
);
