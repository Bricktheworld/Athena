#pragma once
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Render/ddgi.h"

struct Ddgi
{
  RgHandle<GpuBuffer> desc;
  RgHandle<GpuTexture>  distance;
  RgHandle<GpuTexture>  irradiance;
};

struct ReadDdgi
{
  RgReadHandle<GpuBuffer> desc;
  RgReadHandle<GpuTexture>  distance;
  RgReadHandle<GpuTexture>  irradiance;
};

constant u32 kDdgiReadCount = 3;

Ddgi     init_ddgi(AllocHeap heap, RgBuilder* builder);
ReadDdgi read_ddgi(RgPassBuilder* pass_builder, const Ddgi& ddgi, ReadTextureAccessMask access);

