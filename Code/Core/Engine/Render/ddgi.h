#pragma once
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Render/ddgi.h"

struct Ddgi
{
  RgHandle<GpuBuffer> desc;
  RgHandle<GpuImage>  distance;
  RgHandle<GpuImage>  irradiance;
};

struct ReadDdgi
{
  RgReadHandle<GpuBuffer> desc;
  RgReadHandle<GpuImage>  distance;
  RgReadHandle<GpuImage>  irradiance;
};

constant u32 kDdgiReadCount = 3;

Ddgi     init_ddgi(AllocHeap heap, RgBuilder* builder);
ReadDdgi read_ddgi(RgPassBuilder* pass_builder, const Ddgi& ddgi, ReadTextureAccessMask access);

