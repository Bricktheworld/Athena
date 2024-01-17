#pragma once
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/render_graph.h"

struct GBuffer;

struct LightingParams
{
  RgWriteHandle<GpuImage> hdr_buffer;
  ReadGBuffer             gbuffer;
};

RgHandle<GpuImage> init_hdr_buffer(RgBuilder* builder);

void init_lighting(
  AllocHeap heap,
  RgBuilder* builder,
  const GraphicsDevice* device,
  const GBuffer& gbuffer,
  RgHandle<GpuImage>* hdr_buffer
);

void render_handler_lighting(RenderContext* context, const void* data);
