#pragma once
#include "Core/Engine/Render/render_graph.h"

struct GBuffer;

RgHandle<GpuImage> init_hdr_buffer(RgBuilder* builder);

void init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  const GBuffer& gbuffer,
  const Ddgi&    ddgi,
  RgHandle<GpuImage>* hdr_buffer
);

