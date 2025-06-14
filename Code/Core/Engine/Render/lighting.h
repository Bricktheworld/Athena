#pragma once
#include "Core/Engine/Render/render_graph.h"

struct GBuffer;

RgHandle<GpuTexture> init_hdr_buffer(RgBuilder* builder);

void init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GBuffer& gbuffer,
  RgHandle<GpuTexture>* hdr_buffer
);

